// MPS FX: ミックス用の処理(テープストップ、曲線で書くサイドチェイン、Phaser、オールパス、
// 歪み、ステレオ幅 + Bass Mono)をトラック/バスに挿せるようにしたエフェクト。
// 順番: Pump → Phaser → オールパス → Drive → Tape Stop → Width/Bass Mono → Mix → Gain
#pragma once
#include <vector>

#include "dsp.h"
#include "engine.h"

namespace mps {

enum FxParam {
    X_PUMP_DEPTH, X_PUMP_SOURCE, X_PUMP_RATE, X_PUMP_ATTACK, X_PUMP_LENGTH, X_PUMP_SHAPE,
    X_PHASER_MIX, X_PHASER_RATE, X_PHASER_CENTER, X_PHASER_DEPTH,
    X_DISP_STAGES, X_DISP_FREQ,
    X_DRIVE_TYPE, X_DRIVE,
    X_TAPESTOP, X_TAPESTOP_BEATS, X_TAPESTOP_END_GAIN, X_TAPESTOP_AUTO,
    X_WIDTH, X_BASS_MONO,
    X_MIX, X_GAIN,
    X_COUNT
};

const std::vector<ParamDef>& fxParams();

class Fx : public Engine {
public:
    Fx();
    void prepare(double sampleRate, int maxBlock) override;
    void setParam(int id, double plain) override;
    void noteOn(int note, double velocity) override;
    void noteOff(int) override {}
    void allNotesOff() override {}
    void process(const float* const* in, float* const* out, int n, const Transport& tr) override;

private:
    double pumpGain(double u) const;
    double p_[X_COUNT];
    double sr_ = 48000;
    bool dirty_ = true;
    // Pump(MIDI ノートで起動するときの経過)
    double pumpT_ = 1e9;
    // Tape Stop
    std::vector<float> ring_[2];
    long long writePos_ = 0;
    int tsState_ = 0;  // 0 通常, 1 減速中, 2 停止(無音), 3 復帰フェード
    double tsPos_ = 0, tsLen_ = 1, tsStartIdx_ = 0, tsFade_ = 0;
    bool prevTrig_ = false;
    Phaser phaser_[2];
    AllpassChain disp_[2];
    Biquad monoLp_;
};

}  // namespace mps
