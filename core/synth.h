// MPS Synth: キック、ハット、クラップ、スラップ/主軸ベース、スーパーソウ、アシッド、wub コード、矩形波プラック、
// FM ベル、フォルマント声などを 1 つのボイス構成で鳴らせる音源。
//
// ボイス: OSC A + OSC B(ユニゾン)+ サブ → フィルター(エンベロープ・LFO・キー追従)→ アンプ(AHDSR / バースト)
//         + クリック(HP ノイズ)+ ノイズ層
// 全体:   Phaser → Disperser → Drive → ポスト HP/LP → 中域カット → Gain
#pragma once
#include <vector>

#include "dsp.h"
#include "engine.h"

namespace mps {

enum SynthParam {
    S_OSC_A_WAVE, S_OSC_A_LEVEL, S_OSC_A_SEMI,
    S_OSC_B_WAVE, S_OSC_B_LEVEL, S_OSC_B_SEMI, S_OSC_B_FINE,
    S_SUB_LEVEL, S_UNISON, S_UNISON_OSC, S_DETUNE, S_SPREAD,
    S_PW, S_PWM_DEPTH, S_PWM_RATE,
    S_FM_RATIO, S_FM_INDEX, S_FM_DECAY,
    S_PENV1_HZ, S_PENV1_TIME, S_PENV2_HZ, S_PENV2_TIME,
    S_VOICE_MODE, S_GLIDE, S_VIB_DEPTH, S_VIB_RATE, S_VIB_DELAY, S_BEND_RANGE, S_PITCH_BEND,
    S_FILTER_TYPE, S_CUTOFF, S_KEYTRACK, S_RESO, S_FENV_AMT, S_FENV_ATTACK, S_FENV_DECAY, S_VEL_FILTER, S_VOWEL,
    S_LFO_SHAPE, S_LFO_RATE, S_LFO_SYNC, S_LFO_TO_CUTOFF, S_LFO_TO_PITCH,
    S_AMP_A, S_AMP_H, S_AMP_D, S_AMP_S, S_AMP_R, S_ONE_SHOT, S_VEL_AMP,
    S_BURST_COUNT, S_BURST_SPACING, S_BURST_DECAY,
    S_CLICK_LEVEL, S_CLICK_TIME, S_CLICK_HP,
    S_NOISE_LEVEL, S_NOISE_FILTER, S_NOISE_FREQ, S_NOISE_DECAY,
    S_PHASER_MIX, S_PHASER_RATE, S_PHASER_CENTER, S_PHASER_DEPTH,
    S_DISP_STAGES, S_DISP_FREQ,
    S_DRIVE_TYPE, S_DRIVE, S_POST_HP, S_POST_LP, S_MID_CUT, S_GAIN,
    S_COUNT
};

enum Wave { W_SAW, W_SQUARE, W_PULSE, W_SINE, W_TRIANGLE, W_MONSTER, W_NOISE, W_METAL };
enum FilterType { F_OFF, F_LP12, F_HP12, F_BP, F_LP24, F_FORMANT };
enum VoiceMode { VM_POLY, VM_MONO, VM_LEGATO };

const std::vector<ParamDef>& synthParams();

class Synth : public Engine {
public:
    static constexpr int kVoices = 16;
    static constexpr int kMaxUnison = 9;

    Synth();
    void prepare(double sampleRate, int maxBlock) override;
    void setParam(int id, double plain) override;
    void noteOn(int note, double velocity) override;
    void noteOff(int note) override;
    void allNotesOff() override;
    void process(const float* const* in, float* const* out, int n, const Transport& tr) override;

private:
    struct Voice {
        bool active = false, released = false;
        int note = 0;
        double vel = 1, t = 0;
        unsigned long long age = 0;
        double curNote = 0, fromNote = 0, glideT = 1e9;
        double relStart = 0, relPos = 0, relLen = 1, lastAmp = 0;
        double phA[kMaxUnison], phB[kMaxUnison], phMA[kMaxUnison], phMB[kMaxUnison];
        double phSub = 0, phMetal[6], lfoPh = 0;
        Svf f1[2], f2[2], form[3][2], click, noise;
        Rng rng;
        int ctr = 0;
    };

    void startVoice(Voice& v, int note, double vel, bool retrigger, bool glide);
    void releaseVoice(Voice& v);
    void renderVoice(Voice& v, float* L, float* R, int n, const Transport& tr, int offset);
    double envAmp(const Voice& v) const;
    double osc(int wave, double* ph, double* phm, int u, double f, double pw, double fmIdx, Voice& v);
    void updateDerived();

    double p_[S_COUNT];
    bool dirty_ = true;
    double sr_ = 48000;
    Voice voices_[kVoices];
    unsigned long long ageCounter_ = 0;
    std::vector<int> held_;  // モノ/レガート用の押鍵スタック
    int lastNote_ = -1;

    double detRatio_[kMaxUnison], panL_[kMaxUnison], panR_[kMaxUnison];
    int unison_ = 1;

    Phaser phaser_[2];
    Disperser disp_[2];
    Biquad postHp_[2], postLp_[2], midLp_[2], midHp_[2];
    std::vector<float> bufL_, bufR_;
};

}  // namespace mps
