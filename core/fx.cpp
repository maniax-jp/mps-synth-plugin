#include "fx.h"

#include <cstring>
#include <stdexcept>

namespace mps {

static const double kRateBeats[6] = {0.25, 0.5, 1, 2, 4, 8};

const std::vector<ParamDef>& fxParams() {
    using S = Scale;
    static const std::vector<ParamDef> defs = {
        {"pump_depth", "Pump Depth", "", S::Linear, 0, 1, 0, {}, "サイドチェインの深さ(0 = オフ)。周期の頭で下げ、曲線で戻す"},
        {"pump_source", "Pump Source", "", S::Enum, 0, 1, 0, {"tempo", "midi"}, "tempo = 曲の拍に同期、midi = ノートオンごと(キックと同じ位置にゴーストノートを置く)"},
        {"pump_rate", "Pump Rate", "", S::Enum, 0, 5, 2, {"1/16", "1/8", "1/4", "1/2", "1/1", "2/1"}},
        {"pump_attack", "Pump Attack", "", S::Linear, 0, 0.9, 0, {}, "周期の割合"},
        {"pump_length", "Pump Length", "", S::Linear, 0.05, 1, 0.75, {}, "戻りきるまでの周期の割合"},
        {"pump_shape", "Pump Shape", "", S::Log, 0.2, 6, 1.8, {}, "戻りの曲線(大きいほど長く下がる)"},
        {"phaser_mix", "Phaser Mix", "", S::Linear, 0, 1, 0, {}},
        {"phaser_rate", "Phaser Rate", "Hz", S::Log, 0.01, 10, 0.7, {}},
        {"phaser_center", "Phaser Center", "Hz", S::Log, 50, 8000, 600, {}},
        {"phaser_depth", "Phaser Depth", "Hz", S::Cubic, 0, 4000, 400, {}},
        {"disperser_stages", "Disperser Stages", "", S::Int, 0, 48, 0, {}},
        {"disperser_freq", "Disperser Freq", "Hz", S::Log, 30, 8000, 350, {}},
        {"drive_type", "Drive Type", "", S::Enum, 0, 4, kDriveOff, {"off", "tanh", "diode", "fold", "hard"}},
        {"drive", "Drive", "", S::Log, 0.1, 30, 1, {}},
        {"tapestop", "Tape Stop", "", S::Bool, 0, 1, 0, {}, "off→on で減速を始める(自動化か制御サーバーで切り替える)"},
        {"tapestop_beats", "Tape Stop Length", "beats", S::Cubic, 0.0625, 16, 0.5, {}, "止まりきるまでの長さ(拍)"},
        {"tapestop_end_gain", "Tape Stop End Gain", "", S::Linear, 0, 1, 0.6, {}, "止まるまでに音量を 1 → この値に下げる"},
        {"tapestop_auto", "Tape Stop Auto Resume", "", S::Bool, 0, 1, 1, {}, "on = 止まりきったら元の音に戻る、off = Tape Stop を off にするまで無音"},
        {"width", "Width", "", S::Linear, 0, 4, 1, {}, "サイドの倍率(1 = そのまま)"},
        {"bass_mono", "Bass Mono", "Hz", S::Cubic, 0, 500, 0, {}, "この周波数より下のサイドを消す(0 = オフ)"},
        {"mix", "Mix", "", S::Linear, 0, 1, 1, {}},
        {"gain", "Gain", "dB", S::Linear, -48, 12, 0, {}},
    };
    return defs;
}

Fx::Fx() {
    const auto& d = fxParams();
    if (d.size() != X_COUNT) throw std::logic_error("fxParams() and FxParam are out of sync");
    for (int i = 0; i < X_COUNT; ++i) p_[i] = d[i].def;
}

void Fx::prepare(double sampleRate, int) {
    sr_ = sampleRate;
    for (auto& r : ring_) r.assign(static_cast<size_t>(sr_ * 24), 0.f);
    writePos_ = 0;
    tsState_ = 0;
    pumpT_ = 1e9;
    for (auto& p : phaser_) p.reset();
    dirty_ = true;
}

void Fx::setParam(int id, double plain) {
    if (id < 0 || id >= X_COUNT) return;
    if (p_[id] != plain) {
        p_[id] = plain;
        dirty_ = true;
    }
}

void Fx::noteOn(int, double) { pumpT_ = 0; }

double Fx::pumpGain(double u) const {
    double a = p_[X_PUMP_ATTACK], len = std::max(a + 1e-3, p_[X_PUMP_LENGTH]);
    double c = std::min(1.0, std::max(0.0, (u - a) / (len - a)));
    c = std::pow(c, p_[X_PUMP_SHAPE]);
    return 1.0 - p_[X_PUMP_DEPTH] * (1.0 - c);
}

void Fx::process(const float* const* in, float* const* out, int n, const Transport& tr) {
    if (dirty_) {
        for (int c = 0; c < 2; ++c) disp_[c].configure(static_cast<int>(p_[X_DISP_STAGES]), p_[X_DISP_FREQ], sr_);
        if (p_[X_BASS_MONO] > 1) monoLp_.lowpass(p_[X_BASS_MONO], 0.7071, sr_);
        dirty_ = false;
    }
    const double invSr = 1.0 / sr_;
    const double rateBeats = kRateBeats[std::max(0, std::min(5, static_cast<int>(p_[X_PUMP_RATE])))];
    const double beatsPerSample = tr.bpm / 60.0 * invSr;
    const int dType = static_cast<int>(p_[X_DRIVE_TYPE]);
    const double mix = p_[X_MIX], g = dbToGain(p_[X_GAIN]);
    const size_t ringN = ring_[0].size();
    const bool trig = p_[X_TAPESTOP] >= 0.5;

    // テープストップの切り替え(ブロック単位)
    if (trig && !prevTrig_) {
        tsState_ = 1;
        tsPos_ = 0;
        tsLen_ = std::max(16.0, std::min(p_[X_TAPESTOP_BEATS] * 60.0 / tr.bpm * sr_, ringN * 0.9));
        tsStartIdx_ = static_cast<double>(writePos_);
    } else if (!trig && prevTrig_ && (tsState_ == 1 || tsState_ == 2)) {
        tsState_ = 3;
        tsFade_ = 0;
    }
    prevTrig_ = trig;

    for (int i = 0; i < n; ++i) {
        double x[2] = {in[0][i], in[1][i]};
        double y[2];
        // Pump
        double pg = 1.0;
        if (p_[X_PUMP_DEPTH] > 0) {
            if (p_[X_PUMP_SOURCE] < 0.5) {
                double ppq = tr.ppq + i * beatsPerSample;
                if (!tr.playing) ppq = pumpT_ < 1e8 ? pumpT_ * tr.bpm / 60.0 : 1e9;
                pg = ppq < 1e8 ? pumpGain(frac(ppq / rateBeats)) : 1.0;
            } else {
                double u = pumpT_ * tr.bpm / 60.0 / rateBeats;
                pg = u < 1.0 ? pumpGain(u) : 1.0;
            }
        }
        pumpT_ += invSr;
        for (int c = 0; c < 2; ++c) {
            double s = x[c] * pg;
            if (p_[X_PHASER_MIX] > 0) {
                double ph = phaser_[c].tick(s, p_[X_PHASER_CENTER], p_[X_PHASER_DEPTH], p_[X_PHASER_RATE], sr_, true);
                s = (1 - p_[X_PHASER_MIX]) * s + p_[X_PHASER_MIX] * ph;
            }
            if (disp_[c].n > 0) s = disp_[c].tick(s);
            if (dType != kDriveOff) s = drive(dType, s, p_[X_DRIVE]);
            y[c] = s;
            ring_[c][static_cast<size_t>(writePos_ % static_cast<long long>(ringN))] = static_cast<float>(s);
        }
        // Tape Stop: 速度 1 → 0 の直線減速(位置 = k - k^2 / 2n)
        if (tsState_ == 1) {
            double k = tsPos_;
            double pos = k - k * k / (2.0 * tsLen_);
            double idx = tsStartIdx_ + pos;
            long long i0 = static_cast<long long>(std::floor(idx));
            double fr = idx - i0;
            double gain = 1.0 + (p_[X_TAPESTOP_END_GAIN] - 1.0) * (k / tsLen_);
            double fadeOut = std::min(1.0, (tsLen_ - k) / (0.004 * sr_));
            for (int c = 0; c < 2; ++c) {
                const auto& r = ring_[c];
                double a = r[static_cast<size_t>(i0 % static_cast<long long>(ringN))];
                double b = r[static_cast<size_t>((i0 + 1) % static_cast<long long>(ringN))];
                y[c] = (a + (b - a) * fr) * gain * fadeOut;
            }
            tsPos_ += 1;
            if (tsPos_ >= tsLen_) {
                if (p_[X_TAPESTOP_AUTO] >= 0.5) { tsState_ = 3; tsFade_ = 0; }
                else tsState_ = 2;
            }
        } else if (tsState_ == 2) {
            y[0] = y[1] = 0;
        } else if (tsState_ == 3) {
            double f = std::min(1.0, tsFade_ / (0.004 * sr_));
            y[0] *= f;
            y[1] *= f;
            tsFade_ += 1;
            if (f >= 1.0) tsState_ = 0;
        }
        writePos_++;
        // Width / Bass Mono
        double m = 0.5 * (y[0] + y[1]), s = 0.5 * (y[0] - y[1]) * p_[X_WIDTH];
        if (p_[X_BASS_MONO] > 1) s -= monoLp_.tick(s);
        double l = m + s, r = m - s;
        out[0][i] = static_cast<float>(((1 - mix) * x[0] + mix * l) * g);
        out[1][i] = static_cast<float>(((1 - mix) * x[1] + mix * r) * g);
    }
}

}  // namespace mps
