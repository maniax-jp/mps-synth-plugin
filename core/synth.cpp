#include "synth.h"

#include <cstring>
#include <mutex>
#include <stdexcept>

namespace mps {

static const std::vector<const char*> kWaves = {"saw", "square", "pulse", "sine", "triangle", "monster", "noise", "metal"};

const std::vector<ParamDef>& synthParams() {
    using S = Scale;
    static const std::vector<ParamDef> defs = {
        {"osc_a_wave", "OSC A Wave", "", S::Enum, 0, 7, W_SAW, kWaves, "saw/square/pulse は polyBLEP、monster は倍音に山のある帯域制限ウェーブテーブル、metal は 808 型の矩形 6 本"},
        {"osc_a_level", "OSC A Level", "", S::Linear, 0, 2, 1, {}},
        {"osc_a_semi", "OSC A Semi", "st", S::Int, -48, 48, 0, {}},
        {"osc_b_wave", "OSC B Wave", "", S::Enum, 0, 7, W_SAW, kWaves},
        {"osc_b_level", "OSC B Level", "", S::Linear, 0, 2, 0, {}},
        {"osc_b_semi", "OSC B Semi", "st", S::Int, -48, 48, 0, {}},
        {"osc_b_fine", "OSC B Fine", "ct", S::Linear, -100, 100, 0, {}},
        {"sub_level", "Sub Level", "", S::Linear, 0, 2, 0, {}, "1 オクターブ下のサイン"},
        {"unison", "Unison", "", S::Int, 1, 9, 1, {}, "OSC A/B のユニゾン数(saw/square/pulse/sine/triangle/monster)"},
        {"unison_osc", "Unison Target", "", S::Enum, 0, 2, 0, {"a+b", "a", "b"}, "ユニゾンを掛ける OSC"},
        {"detune", "Detune", "st", S::Cubic, 0, 1, 0.14, {}, "ユニゾンを ±この半音幅に等間隔で並べる"},
        {"spread", "Stereo Spread", "", S::Linear, 0, 1, 0, {}, "ユニゾンのパンを ±この幅に並べる"},
        {"pulse_width", "Pulse Width", "", S::Linear, 0.05, 0.95, 0.5, {}},
        {"pwm_depth", "PWM Depth", "", S::Linear, 0, 0.45, 0, {}},
        {"pwm_rate", "PWM Rate", "Hz", S::Log, 0.05, 20, 0.8, {}},
        {"fm_ratio", "FM Ratio", "", S::Log, 0.25, 16, 2, {}, "sine 波の位相変調(モジュレーター周波数比)"},
        {"fm_index", "FM Index", "", S::Cubic, 0, 10, 0, {}},
        {"fm_decay", "FM Decay", "s", S::Cubic, 0.001, 30, 0.3, {}, "変調指数の減衰時定数(index * exp(-t/decay))"},
        {"pitch_env1_hz", "Pitch Env1", "Hz", S::Cubic, 0, 5000, 0, {}, "f = 音程 + env1*exp(-t/time1) + env2*exp(-t/time2)(キックのピッチスイープ)"},
        {"pitch_env1_time", "Pitch Env1 Time", "s", S::Cubic, 0.0002, 1, 0.018, {}},
        {"pitch_env2_hz", "Pitch Env2", "Hz", S::Cubic, 0, 5000, 0, {}},
        {"pitch_env2_time", "Pitch Env2 Time", "s", S::Cubic, 0.0002, 1, 0.0022, {}},
        {"voice_mode", "Voice Mode", "", S::Enum, 0, 2, VM_POLY, {"poly", "mono", "legato"}},
        {"glide", "Glide", "s", S::Cubic, 0, 2, 0, {}, "前の音からこの時間で半音直線に移動(mono/legato)"},
        {"vib_depth", "Vibrato Depth", "ct", S::Linear, 0, 100, 0, {}},
        {"vib_rate", "Vibrato Rate", "Hz", S::Log, 0.1, 20, 5.8, {}},
        {"vib_delay", "Vibrato Delay", "s", S::Cubic, 0, 2, 0.1, {}, "この時間のあと 0.2 秒かけて深くなる"},
        {"bend_range", "Bend Range", "st", S::Int, 0, 24, 2, {}},
        {"pitch_bend", "Pitch Bend", "", S::Linear, -1, 1, 0, {}, "MIDI ピッチベンド(自動化可)"},
        {"filter_type", "Filter Type", "", S::Enum, 0, 5, F_OFF, {"off", "lp12", "hp12", "bp", "lp24", "formant"}},
        {"cutoff", "Cutoff", "Hz", S::Cubic, 0, 20000, 20000, {}, "fc = cutoff + key_track*音程 + env_amount*env + lfo_cutoff*lfo"},
        {"key_track", "Key Track", "x", S::Linear, 0, 8, 0, {}, "音程の周波数の何倍を cutoff に足すか(アシッドは 1.5)"},
        {"resonance", "Resonance (Q)", "", S::Log, 0.3, 20, 0.707, {}},
        {"fenv_amount", "Filter Env", "Hz", S::Cubic, 0, 20000, 0, {}},
        {"fenv_attack", "Filter Env Attack", "s", S::Cubic, 0, 10, 0, {}},
        {"fenv_decay", "Filter Env Decay", "s", S::Cubic, 0.001, 10, 0.05, {}},
        {"vel_to_filter", "Velocity > Filter", "", S::Linear, 0, 1, 0, {}, "ベロシティでフィルター・エンベロープ量を変える(アクセント)"},
        {"vowel", "Vowel", "", S::Linear, 0, 4, 0, {}, "formant の母音 0=a 1=o 2=e 3=i 4=u(間は補間)"},
        {"lfo_shape", "LFO Shape", "", S::Enum, 0, 4, 0, {"sine", "wub", "triangle", "square", "saw_down"}, "0..1 の単極。wub = sin(pi*min(1, 1.6*phase))^2"},
        {"lfo_rate", "LFO Rate", "Hz", S::Log, 0.01, 30, 1, {}},
        {"lfo_sync", "LFO Sync", "", S::Enum, 0, 7, 0, {"off", "1/16", "1/8", "1/4", "1/2", "1/1", "2/1", "4/1"}, "1/4 = 1 拍周期。再生中は曲の位置に同期"},
        {"lfo_to_cutoff", "LFO > Cutoff", "Hz", S::Cubic, 0, 20000, 0, {}},
        {"lfo_to_pitch", "LFO > Pitch", "ct", S::Linear, 0, 1200, 0, {}},
        {"amp_attack", "Amp Attack", "s", S::Cubic, 0, 10, 0.001, {}},
        {"amp_hold", "Amp Hold", "s", S::Cubic, 0, 10, 0, {}},
        {"amp_decay", "Amp Decay", "s", S::Cubic, 0.001, 30, 0.3, {}, "サステインへ exp(-t/decay) で近づく時定数"},
        {"amp_sustain", "Amp Sustain", "", S::Linear, 0, 1, 1, {}},
        {"amp_release", "Amp Release", "s", S::Cubic, 0.0005, 10, 0.006, {}, "直線で 0 まで"},
        {"one_shot", "One Shot", "", S::Bool, 0, 1, 0, {}, "ノートオフを無視してエンベロープを最後まで(ドラム)"},
        {"vel_to_amp", "Velocity > Amp", "", S::Linear, 0, 1, 0.3, {}},
        {"burst_count", "Burst Count", "", S::Int, 1, 8, 1, {}, "2 以上でクラップの連打エンベロープ(バースト + 0.5*AHDSR)"},
        {"burst_spacing", "Burst Spacing", "s", S::Cubic, 0.001, 0.05, 0.0087, {}},
        {"burst_decay", "Burst Decay", "s", S::Cubic, 0.0005, 0.05, 0.007, {}},
        {"click_level", "Click Level", "", S::Linear, 0, 1, 0, {}, "HP ノイズのアタック(キック)"},
        {"click_time", "Click Time", "s", S::Cubic, 0.0002, 0.05, 0.0025, {}},
        {"click_hp", "Click HP", "Hz", S::Log, 200, 16000, 2500, {}},
        {"noise_level", "Noise Level", "", S::Linear, 0, 2, 0, {}, "独立したノイズ層(スネアのノイズ、クラップの尾)"},
        {"noise_filter", "Noise Filter", "", S::Enum, 0, 2, 0, {"white", "bp", "hp"}},
        {"noise_freq", "Noise Freq", "Hz", S::Log, 100, 16000, 4500, {}},
        {"noise_decay", "Noise Decay", "s", S::Cubic, 0.001, 10, 0.05, {}},
        {"phaser_mix", "Phaser Mix", "", S::Linear, 0, 1, 0, {}},
        {"phaser_rate", "Phaser Rate", "Hz", S::Log, 0.01, 10, 0.7, {}},
        {"phaser_center", "Phaser Center", "Hz", S::Log, 50, 8000, 600, {}},
        {"phaser_depth", "Phaser Depth", "Hz", S::Cubic, 0, 4000, 400, {}},
        {"disperser_stages", "Disperser Stages", "", S::Int, 0, 48, 0, {}},
        {"disperser_freq", "Disperser Freq", "Hz", S::Log, 30, 8000, 350, {}},
        {"drive_type", "Drive Type", "", S::Enum, 0, 4, kDriveOff, {"off", "tanh", "diode", "fold", "hard"}, "tanh は tanh(d*x)/tanh(d)、diode は非対称"},
        {"drive", "Drive", "", S::Log, 0.1, 30, 1, {}},
        {"post_hp", "Post HP", "Hz", S::Log, 10, 4000, 10, {}, "butter 2 次(10 Hz = 実質オフ)"},
        {"post_lp", "Post LP", "Hz", S::Log, 500, 22000, 22000, {}, "butter 2 次(22000 = オフ)"},
        {"mid_cut", "Mid Cut", "", S::Linear, 0, 1, 0, {}, "x - mid_cut*HP300(LP2000(x)):キックやループの中域をベースに渡す"},
        {"gain", "Gain", "dB", S::Linear, -48, 12, 0, {}},
    };
    return defs;
}

// ---- monster: 倍音に山のある複雑なウェーブテーブル。半音ごとに帯域制限した表を一度だけ作る
namespace {
constexpr int kWtLen = 2048;
struct MonsterBank {
    std::vector<float> tab[128];
    MonsterBank() {
        Rng rng(8);
        double amp[400], ph[400];
        for (int k = 1; k < 400; ++k) {
            amp[k] = (1.0 / std::pow(k, 0.75)) *
                     (1 + 1.6 * std::exp(-std::pow((k - 7) / 2.5, 2)) + 1.1 * std::exp(-std::pow((k - 19) / 4.0, 2)));
            ph[k] = rng.uniform() * kTwoPi;
        }
        for (int m = 0; m < 128; ++m) {
            double f = mtof(m);
            int nh = std::max(1, std::min(399, static_cast<int>(16000 / f)));
            std::vector<double> acc(kWtLen, 0.0);
            for (int k = 1; k <= nh; ++k) {
                // 回転で sin を進める(初期化を速くする)
                double c = std::cos(kTwoPi * k / kWtLen), s = std::sin(kTwoPi * k / kWtLen);
                double re = std::cos(ph[k]), im = std::sin(ph[k]);
                for (int i = 0; i < kWtLen; ++i) {
                    acc[i] += amp[k] * im;
                    double nre = re * c - im * s;
                    im = re * s + im * c;
                    re = nre;
                }
            }
            double mx = 1e-9;
            for (double v : acc) mx = std::max(mx, std::fabs(v));
            tab[m].resize(kWtLen + 1);
            for (int i = 0; i < kWtLen; ++i) tab[m][i] = static_cast<float>(acc[i] / mx);
            tab[m][kWtLen] = tab[m][0];
        }
    }
};
const MonsterBank& monster() {
    static MonsterBank bank;
    return bank;
}
inline double monsterAt(double ph, double f) {
    int m = static_cast<int>(std::round(69 + 12 * std::log2(std::max(f, 8.0) / 440.0)));
    m = std::max(0, std::min(127, m));
    const auto& t = monster().tab[m];
    double x = frac(ph) * kWtLen;
    int i = static_cast<int>(x);
    double fr = x - i;
    return t[i] + (t[i + 1] - t[i]) * fr;
}

// 母音のフォルマント: (fc, bw, gain) x 3
const double kVowels[5][3][3] = {
    {{800, 80, 1.0}, {1150, 90, 0.5}, {2900, 120, 0.25}},   // a
    {{450, 70, 1.0}, {800, 80, 0.45}, {2830, 100, 0.12}},   // o
    {{400, 60, 1.0}, {1700, 90, 0.5}, {2600, 120, 0.3}},    // e
    {{300, 50, 1.0}, {2300, 100, 0.4}, {3000, 120, 0.3}},   // i
    {{330, 50, 1.0}, {700, 70, 0.3}, {2500, 100, 0.08}},    // u
};
const double kMetal[6] = {205.3, 304.4, 369.6, 522.7, 540.0, 800.0};
const double kSyncBeats[8] = {0, 0.25, 0.5, 1, 2, 4, 8, 16};
}  // namespace

Synth::Synth() {
    const auto& d = synthParams();
    if (d.size() != S_COUNT) throw std::logic_error("synthParams() and SynthParam are out of sync");
    for (int i = 0; i < S_COUNT; ++i) p_[i] = d[i].def;
    (void)monster();
}

void Synth::prepare(double sampleRate, int maxBlock) {
    sr_ = sampleRate;
    bufL_.assign(std::max(maxBlock, 64), 0.f);
    bufR_.assign(std::max(maxBlock, 64), 0.f);
    for (int c = 0; c < 2; ++c) {
        phaser_[c].reset();
        midLp_[c].lowpass(2000, 0.7071, sr_);
        midHp_[c].highpass(300, 0.7071, sr_);
    }
    dirty_ = true;
    allNotesOff();
    for (auto& v : voices_) v.active = false;
}

void Synth::setParam(int id, double plain) {
    if (id < 0 || id >= S_COUNT) return;
    if (p_[id] != plain) {
        p_[id] = plain;
        dirty_ = true;
    }
}

void Synth::updateDerived() {
    unison_ = std::max(1, std::min(kMaxUnison, static_cast<int>(p_[S_UNISON])));
    for (int u = 0; u < unison_; ++u) {
        double x = unison_ > 1 ? -1.0 + 2.0 * u / (unison_ - 1) : 0.0;
        detRatio_[u] = std::pow(2.0, x * p_[S_DETUNE] / 12.0);
        double pan = x * p_[S_SPREAD];
        // 等パワーパン(中央で 1)
        panL_[u] = std::cos((pan + 1) * kPi / 4) * std::sqrt(2.0) / unison_;
        panR_[u] = std::sin((pan + 1) * kPi / 4) * std::sqrt(2.0) / unison_;
    }
    for (int c = 0; c < 2; ++c) {
        postHp_[c].highpass(p_[S_POST_HP], 0.7071, sr_);
        postLp_[c].lowpass(std::min(p_[S_POST_LP], sr_ * 0.45), 0.7071, sr_);
        disp_[c].configure(static_cast<int>(p_[S_DISP_STAGES]), p_[S_DISP_FREQ], sr_);
    }
    dirty_ = false;
}

void Synth::startVoice(Voice& v, int note, double vel, bool retrigger, bool glide) {
    double from = (glide && lastNote_ >= 0) ? (v.active ? v.curNote : lastNote_) : note;
    v.note = note;
    v.vel = vel;
    v.fromNote = from;
    v.curNote = from;
    v.glideT = (from != note && p_[S_GLIDE] > 0) ? 0.0 : 1e9;
    if (v.glideT > 1e8) v.curNote = note;
    if (retrigger || !v.active) {
        v.active = true;
        v.released = false;
        v.t = 0;
        v.ctr = 0;
        v.age = ++ageCounter_;
        v.rng = Rng(0x1234567ull + ageCounter_ * 7919ull);
        bool randPhase = true;
        for (int u = 0; u < kMaxUnison; ++u) {
            v.phA[u] = randPhase ? v.rng.uniform() : 0.0;
            v.phB[u] = randPhase ? v.rng.uniform() : 0.0;
            v.phMA[u] = v.phMB[u] = 0.0;
        }
        // サイン・三角は位相 0 から(キックとサブのアタックをそろえる)
        if (p_[S_OSC_A_WAVE] == W_SINE || p_[S_OSC_A_WAVE] == W_TRIANGLE) v.phA[0] = 0;
        if (p_[S_OSC_B_WAVE] == W_SINE || p_[S_OSC_B_WAVE] == W_TRIANGLE) v.phB[0] = 0;
        v.phSub = 0;
        for (double& m : v.phMetal) m = 0;
        v.lfoPh = 0;
        for (int c = 0; c < 2; ++c) {
            v.f1[c].reset();
            v.f2[c].reset();
            for (auto& f : v.form) f[c].reset();
        }
        v.click.reset();
        v.noise.reset();
        v.click.set(p_[S_CLICK_HP], 0.7071, sr_);
    }
    lastNote_ = note;
}

void Synth::releaseVoice(Voice& v) {
    if (!v.active || v.released || p_[S_ONE_SHOT] >= 0.5) return;
    v.released = true;
    v.relStart = v.lastAmp;
    v.relPos = 0;
    v.relLen = std::max(1.0, p_[S_AMP_R] * sr_);
}

void Synth::noteOn(int note, double velocity) {
    int mode = static_cast<int>(p_[S_VOICE_MODE]);
    if (mode == VM_POLY) {
        Voice* best = nullptr;
        for (auto& v : voices_)
            if (!v.active) { best = &v; break; }
        if (!best) {
            best = &voices_[0];
            for (auto& v : voices_)
                if (v.age < best->age) best = &v;
        }
        best->active = false;
        startVoice(*best, note, velocity, true, p_[S_GLIDE] > 0);
        return;
    }
    Voice& v = voices_[0];
    bool overlapping = !held_.empty() && v.active && !v.released;
    held_.erase(std::remove(held_.begin(), held_.end(), note), held_.end());
    held_.push_back(note);
    if (mode == VM_LEGATO && overlapping) startVoice(v, note, velocity, false, true);
    else startVoice(v, note, velocity, true, p_[S_GLIDE] > 0);
}

void Synth::noteOff(int note) {
    int mode = static_cast<int>(p_[S_VOICE_MODE]);
    if (mode == VM_POLY) {
        for (auto& v : voices_)
            if (v.active && !v.released && v.note == note) releaseVoice(v);
        return;
    }
    held_.erase(std::remove(held_.begin(), held_.end(), note), held_.end());
    Voice& v = voices_[0];
    if (!v.active || v.note != note) return;
    if (!held_.empty()) startVoice(v, held_.back(), v.vel, mode == VM_MONO, true);
    else releaseVoice(v);
}

void Synth::allNotesOff() {
    held_.clear();
    for (auto& v : voices_) {
        if (!v.active) continue;
        if (p_[S_ONE_SHOT] >= 0.5) continue;
        releaseVoice(v);
    }
}

double Synth::envAmp(const Voice& v) const {
    double a = p_[S_AMP_A], h = p_[S_AMP_H], d = p_[S_AMP_D], s = p_[S_AMP_S];
    double t = v.t, e;
    if (t < a) e = t / a;
    else if (t < a + h) e = 1.0;
    else e = s + (1.0 - s) * std::exp(-(t - a - h) / d);
    int bc = static_cast<int>(p_[S_BURST_COUNT]);
    if (bc > 1) {
        double b = 0, sp = p_[S_BURST_SPACING], bd = p_[S_BURST_DECAY];
        for (int k = 0; k < bc; ++k) {
            double dt = t - k * sp;
            if (dt >= 0) b += std::exp(-dt / bd);
        }
        e = b + 0.5 * e;
    }
    return e;
}

double Synth::osc(int wave, double* ph, double* phm, int u, double f, double pw, double fmIdx, Voice& v) {
    double dt = std::min(0.5, std::max(1e-7, f / sr_));
    double y;
    switch (wave) {
        case W_SAW: y = sawAt(ph[u], dt); break;
        case W_SQUARE: y = pulseAt(ph[u], dt, 0.5); break;
        case W_PULSE: y = pulseAt(ph[u], dt, pw); break;
        case W_SINE: {
            double m = fmIdx != 0 ? fmIdx * std::sin(kTwoPi * phm[u]) : 0.0;
            y = std::sin(kTwoPi * ph[u] + m);
            phm[u] = frac(phm[u] + dt * p_[S_FM_RATIO]);
            break;
        }
        case W_TRIANGLE: y = triAt(ph[u]); break;
        case W_MONSTER: y = monsterAt(ph[u], f); break;
        default: y = 0; break;
    }
    ph[u] = frac(ph[u] + dt);
    return y;
}

void Synth::renderVoice(Voice& v, float* L, float* R, int n, const Transport& tr, int offset) {
    const double invSr = 1.0 / sr_;
    const int waveA = static_cast<int>(p_[S_OSC_A_WAVE]), waveB = static_cast<int>(p_[S_OSC_B_WAVE]);
    const double lvA = p_[S_OSC_A_LEVEL], lvB = p_[S_OSC_B_LEVEL], lvSub = p_[S_SUB_LEVEL];
    const double ratA = std::pow(2.0, p_[S_OSC_A_SEMI] / 12.0);
    const double ratB = std::pow(2.0, (p_[S_OSC_B_SEMI] + p_[S_OSC_B_FINE] / 100.0) / 12.0);
    const int ftype = static_cast<int>(p_[S_FILTER_TYPE]);
    const double velAmp = 1.0 - p_[S_VEL_AMP] + p_[S_VEL_AMP] * v.vel;
    const double velF = 1.0 - p_[S_VEL_FILTER] + p_[S_VEL_FILTER] * v.vel;
    const int lfoShape = static_cast<int>(p_[S_LFO_SHAPE]);
    const double syncBeats = kSyncBeats[std::max(0, std::min(7, static_cast<int>(p_[S_LFO_SYNC])))];
    const double lfoRate = syncBeats > 0 ? tr.bpm / 60.0 / syncBeats : p_[S_LFO_RATE];
    const bool useLfo = p_[S_LFO_TO_CUTOFF] > 0 || p_[S_LFO_TO_PITCH] > 0;
    const int nf = static_cast<int>(p_[S_NOISE_FILTER]);
    if (nf == 1) v.noise.set(p_[S_NOISE_FREQ], 0.7, sr_);
    else if (nf == 2) v.noise.set(p_[S_NOISE_FREQ], 0.7071, sr_);
    const double bend = p_[S_PITCH_BEND] * p_[S_BEND_RANGE];
    const bool oneShot = p_[S_ONE_SHOT] >= 0.5;

    for (int i = 0; i < n && v.active; ++i) {
        const double t = v.t;
        // ---- 音程
        if (v.glideT < 1e8) {
            v.glideT += invSr;
            double g = std::min(1.0, v.glideT / std::max(1e-4, p_[S_GLIDE]));
            v.curNote = v.fromNote + (v.note - v.fromNote) * g;
            if (g >= 1.0) v.glideT = 1e9;
        }
        double lfo = 0;
        if (useLfo) {
            double ph = (syncBeats > 0 && tr.playing) ? frac((tr.ppq + (offset + i) * invSr * tr.bpm / 60.0) / syncBeats) : v.lfoPh;
            switch (lfoShape) {
                case 0: lfo = 0.5 + 0.5 * std::sin(kTwoPi * ph); break;
                case 1: { double s = std::sin(kPi * std::min(1.0, ph * 1.6)); lfo = s * s; break; }
                case 2: lfo = 1.0 - std::fabs(2.0 * ph - 1.0); break;
                case 3: lfo = ph < 0.5 ? 1.0 : 0.0; break;
                default: lfo = 1.0 - ph; break;
            }
            v.lfoPh = frac(v.lfoPh + lfoRate * invSr);
        }
        double cents = 0;
        if (p_[S_VIB_DEPTH] > 0)
            cents += p_[S_VIB_DEPTH] * std::sin(kTwoPi * p_[S_VIB_RATE] * t) *
                     std::min(1.0, std::max(0.0, (t - p_[S_VIB_DELAY]) / 0.2));
        if (p_[S_LFO_TO_PITCH] > 0) cents += p_[S_LFO_TO_PITCH] * (2 * lfo - 1);
        double base = mtof(v.curNote + bend + cents / 100.0);
        double penv = 0;
        if (p_[S_PENV1_HZ] > 0) penv += p_[S_PENV1_HZ] * std::exp(-t / p_[S_PENV1_TIME]);
        if (p_[S_PENV2_HZ] > 0) penv += p_[S_PENV2_HZ] * std::exp(-t / p_[S_PENV2_TIME]);
        double fA = base * ratA + penv, fB = base * ratB + penv * ratB / ratA;
        double pw = std::min(0.95, std::max(0.05, p_[S_PW] + p_[S_PWM_DEPTH] * std::sin(kTwoPi * p_[S_PWM_RATE] * t)));
        double fmIdx = p_[S_FM_INDEX] > 0 ? p_[S_FM_INDEX] * std::exp(-t / p_[S_FM_DECAY]) : 0.0;

        // ---- オシレーター
        double l = 0, r = 0;
        const int uo = static_cast<int>(p_[S_UNISON_OSC]);
        auto addOsc = [&](int wave, double lv, double f, double* ph, double* phm, bool uni) {
            if (lv <= 0) return;
            if (wave == W_NOISE) {
                double s = lv * v.rng.gauss();
                l += s; r += s;
                return;
            }
            if (wave == W_METAL) {
                double s = 0, k = 1.9 * f / 440.0;
                for (int m = 0; m < 6; ++m) {
                    double fm = kMetal[m] * k, dt = std::min(0.5, fm * invSr);
                    s += pulseAt(v.phMetal[m], dt, 0.5);
                    v.phMetal[m] = frac(v.phMetal[m] + dt);
                }
                s *= lv / 3.0;
                l += s; r += s;
                return;
            }
            if (!uni) {
                double s = lv * osc(wave, ph, phm, 0, f, pw, fmIdx, v);
                l += s; r += s;
                return;
            }
            for (int u = 0; u < unison_; ++u) {
                double s = lv * osc(wave, ph, phm, u, f * detRatio_[u], pw, fmIdx, v);
                l += s * panL_[u];
                r += s * panR_[u];
            }
        };
        addOsc(waveA, lvA, fA, v.phA, v.phMA, uo != 2);
        addOsc(waveB, lvB, fB, v.phB, v.phMB, uo != 1);
        if (lvSub > 0) {
            double s = lvSub * std::sin(kTwoPi * v.phSub);
            v.phSub = frac(v.phSub + 0.5 * base * ratA * invSr);
            l += s; r += s;
        }

        // ---- フィルター(係数は 8 サンプルごと)
        if (ftype != F_OFF) {
            if ((v.ctr & 7) == 0) {
                double fe;
                double fa = p_[S_FENV_ATTACK];
                if (fa > 0 && t < fa) fe = t / fa;
                else fe = std::exp(-(t - fa) / p_[S_FENV_DECAY]);
                double fc = p_[S_CUTOFF] + p_[S_KEYTRACK] * base + p_[S_FENV_AMT] * velF * fe + p_[S_LFO_TO_CUTOFF] * lfo;
                if (ftype == F_FORMANT) {
                    double vw = std::min(4.0, std::max(0.0, p_[S_VOWEL]));
                    int i0 = static_cast<int>(vw), i1 = std::min(4, i0 + 1);
                    double w = vw - i0;
                    for (int k = 0; k < 3; ++k) {
                        double ffc = kVowels[i0][k][0] * (1 - w) + kVowels[i1][k][0] * w;
                        double bw = kVowels[i0][k][1] * (1 - w) + kVowels[i1][k][1] * w;
                        for (int c = 0; c < 2; ++c) v.form[k][c].set(ffc, ffc / bw, sr_);
                    }
                } else {
                    double q = p_[S_RESO];
                    for (int c = 0; c < 2; ++c) {
                        v.f1[c].set(fc, ftype == F_LP24 ? 0.5412 : q, sr_);
                        if (ftype == F_LP24) v.f2[c].set(fc, q * 1.848, sr_);
                    }
                }
            }
            double in[2] = {l, r};
            for (int c = 0; c < 2; ++c) {
                double lp, bp, hp, y = 0;
                if (ftype == F_FORMANT) {
                    double vw = std::min(4.0, std::max(0.0, p_[S_VOWEL]));
                    int i0 = static_cast<int>(vw), i1 = std::min(4, i0 + 1);
                    double w = vw - i0;
                    for (int k = 0; k < 3; ++k) {
                        v.form[k][c].tick(in[c], lp, bp, hp);
                        y += (kVowels[i0][k][2] * (1 - w) + kVowels[i1][k][2] * w) * bp;
                    }
                } else {
                    v.f1[c].tick(in[c], lp, bp, hp);
                    y = ftype == F_HP12 ? hp : ftype == F_BP ? bp : lp;
                    if (ftype == F_LP24) v.f2[c].tick(y, y, bp, hp);
                }
                in[c] = y;
            }
            l = in[0];
            r = in[1];
        }
        v.ctr++;

        // ---- アンプ + クリック + ノイズ層
        double amp = envAmp(v);
        v.lastAmp = amp;
        if (v.released) {
            amp = v.relStart * (1.0 - v.relPos / v.relLen);
            v.relPos += 1;
            if (v.relPos >= v.relLen) v.active = false;
        }
        double extra = 0;
        if (p_[S_CLICK_LEVEL] > 0 && t < 30 * p_[S_CLICK_TIME]) {
            double lp, bp, hp;
            v.click.tick(v.rng.gauss(), lp, bp, hp);
            extra += hp * std::exp(-t / p_[S_CLICK_TIME]) * p_[S_CLICK_LEVEL];
        }
        if (p_[S_NOISE_LEVEL] > 0) {
            double s = v.rng.gauss(), lp, bp, hp;
            if (nf != 0) {
                v.noise.tick(s, lp, bp, hp);
                s = nf == 1 ? bp : hp;
            }
            double ne = std::exp(-t / p_[S_NOISE_DECAY]);
            if (v.released) ne *= (1.0 - v.relPos / v.relLen);
            extra += s * ne * p_[S_NOISE_LEVEL];
        }
        L[i] += static_cast<float>((l * amp + extra) * velAmp);
        R[i] += static_cast<float>((r * amp + extra) * velAmp);

        v.t += invSr;
        // 鳴り終わり(サステイン 0 で減衰しきった / ワンショットの上限)
        double a = p_[S_AMP_A] + p_[S_AMP_H] + 9.2 * p_[S_AMP_D];
        double tailOk = std::max({a, 30 * p_[S_CLICK_TIME], p_[S_NOISE_LEVEL] > 0 ? 9.2 * p_[S_NOISE_DECAY] : 0.0});
        if (!v.released && p_[S_AMP_S] <= 0 && v.t > tailOk) v.active = false;
        if (oneShot && v.t > 30.0) v.active = false;
    }
}

void Synth::process(const float* const*, float* const* out, int n, const Transport& tr) {
    if (dirty_) updateDerived();
    float* L = out[0];
    float* R = out[1];
    std::memset(L, 0, sizeof(float) * n);
    std::memset(R, 0, sizeof(float) * n);
    for (auto& v : voices_)
        if (v.active) renderVoice(v, L, R, n, tr, 0);

    const double phMix = p_[S_PHASER_MIX];
    const int dType = static_cast<int>(p_[S_DRIVE_TYPE]);
    const double dAmt = p_[S_DRIVE];
    const bool hp = p_[S_POST_HP] > 10.5, lp = p_[S_POST_LP] < 21999;
    const double mid = p_[S_MID_CUT];
    const double g = dbToGain(p_[S_GAIN]);
    float* ch[2] = {L, R};
    for (int c = 0; c < 2; ++c) {
        float* x = ch[c];
        for (int i = 0; i < n; ++i) {
            double s = x[i];
            if (phMix > 0) {
                double ph = phaser_[c].tick(s, p_[S_PHASER_CENTER], p_[S_PHASER_DEPTH], p_[S_PHASER_RATE], sr_, true);
                s = (1 - phMix) * s + phMix * ph;
            } else {
                phaser_[c].t += 1.0 / sr_;
            }
            if (disp_[c].n > 0) s = disp_[c].tick(s);
            if (dType != kDriveOff) s = drive(dType, s, dAmt);
            if (hp) s = postHp_[c].tick(s);
            if (lp) s = postLp_[c].tick(s);
            if (mid > 0) s = s - mid * midHp_[c].tick(midLp_[c].tick(s));
            x[i] = static_cast<float>(s * g);
        }
    }
}

}  // namespace mps
