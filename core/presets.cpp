// ファクトリープリセットとプラグイン仕様。desc には音の説明と、鳴らす前提の MIDI ノートを書く。
#include "fx.h"
#include "synth.h"

namespace mps {

static const std::vector<Preset> kSynthPresets = {
    {"Init", "初期値(のこぎり 1 本)", R"({})"},
    // ---------------------------------------------------------------- ドラム
    {"Short Kick", "短く、音程が一気に落ちるテックコア向けのキック(160 BPM 前後)。G#1(MIDI 32)で鳴らす",
     R"({"osc_a_wave":"sine","pitch_env1_hz":260,"pitch_env1_time":0.018,"pitch_env2_hz":1400,"pitch_env2_time":0.0022,
        "amp_attack":0.0005,"amp_hold":0.13,"amp_decay":0.025,"amp_sustain":0,"one_shot":"on","vel_to_amp":0,
        "click_level":0.35,"click_time":0.0025,"click_hp":2500,"drive_type":"tanh","drive":1.8})"},
    {"Tech Kick", "尾の長いテック・キック。中域を抜いてベースに譲る。C2(MIDI 36)で 66 Hz 付近に着地",
     R"({"osc_a_wave":"sine","pitch_env1_hz":250,"pitch_env1_time":0.035,"pitch_env2_hz":1500,"pitch_env2_time":0.003,
        "amp_attack":0.0005,"amp_decay":0.14,"amp_sustain":0,"one_shot":"on","vel_to_amp":0,
        "click_level":0.3,"click_time":0.002,"click_hp":4000,"drive_type":"tanh","drive":2.2,"mid_cut":0.6})"},
    {"Hardcore Kick", "185 BPM 前後向けの短い歪みキック。E1(MIDI 28)で鳴らす",
     R"({"osc_a_wave":"sine","pitch_env1_hz":180,"pitch_env1_time":0.022,"pitch_env2_hz":1500,"pitch_env2_time":0.003,
        "amp_attack":0.0005,"amp_hold":0.11,"amp_decay":0.02,"amp_sustain":0,"one_shot":"on","vel_to_amp":0,
        "click_level":0.4,"click_time":0.002,"click_hp":3000,"drive_type":"tanh","drive":3.0})"},
    {"Snare", "胴鳴り(190 Hz 付近)+ ノイズのスネア。F#3(MIDI 54)で鳴らす",
     R"({"osc_a_wave":"sine","osc_a_level":0.7,"pitch_env1_hz":114,"pitch_env1_time":0.01,
        "amp_attack":0.0003,"amp_decay":0.04,"amp_sustain":0,"one_shot":"on","vel_to_amp":0.3,
        "noise_level":1,"noise_filter":"bp","noise_freq":4500,"noise_decay":0.05,"drive_type":"tanh","drive":1.5,"gain":-1})"},
    {"Clap", "ノイジーなクラップ(4 連打 + 尾)。どの音程でも同じ音",
     R"({"osc_a_wave":"noise","osc_a_level":1.4,"filter_type":"bp","cutoff":1500,"resonance":0.8,
        "amp_attack":0,"amp_decay":0.08,"amp_sustain":0,"one_shot":"on","vel_to_amp":0.3,
        "burst_count":4,"burst_spacing":0.0087,"burst_decay":0.007,
        "noise_level":0.4,"noise_filter":"hp","noise_freq":5000,"noise_decay":0.05,"drive_type":"tanh","drive":1.5,"gain":-4.7})"},
    {"Hat Closed", "短いクローズド・ハット(ノイズ + 4 次ハイパス)",
     R"({"osc_a_wave":"noise","filter_type":"hp12","cutoff":5000,"post_hp":5000,"amp_attack":0.0003,"amp_decay":0.018,
        "amp_sustain":0,"one_shot":"on","vel_to_amp":0.5,"gain":-7})"},
    {"Hat Metal", "808 型の矩形 6 本で作る金属的なハット。A4(MIDI 69)で基準の音",
     R"({"osc_a_wave":"metal","filter_type":"hp12","cutoff":4500,"post_hp":4500,"amp_attack":0.0003,"amp_decay":0.025,
        "amp_sustain":0,"one_shot":"on","vel_to_amp":0.5,"gain":3.5})"},
    {"Hat Tick", "8 分で刻む硬いチック",
     R"({"osc_a_wave":"noise","filter_type":"hp12","cutoff":10000,"post_hp":10000,"amp_attack":0.0003,"amp_decay":0.006,
        "amp_sustain":0,"one_shot":"on","vel_to_amp":0.5,"gain":-3.4})"},
    {"Hat Open", "オープン・ハット",
     R"({"osc_a_wave":"noise","filter_type":"hp12","cutoff":4500,"post_hp":4500,"amp_attack":0.0003,"amp_decay":0.11,
        "amp_sustain":0,"one_shot":"on","vel_to_amp":0.5,"gain":-8.2})"},
    {"Crash", "ノイズ + 金属音のクラッシュ。A4(MIDI 69)で基準の音",
     R"({"osc_a_wave":"noise","osc_b_wave":"metal","osc_b_level":0.3,"osc_b_semi":8,"post_hp":3500,"amp_attack":0.0003,
        "amp_decay":0.75,"amp_sustain":0,"one_shot":"on","vel_to_amp":0.3,"gain":-10})"},
    {"Zap", "高い音から一気に落ちるザップ。G3(MIDI 55)で 200 Hz 付近に落ちる",
     R"({"osc_a_wave":"sine","pitch_env1_hz":3000,"pitch_env1_time":0.02,"amp_attack":0.0005,"amp_decay":0.04,
        "amp_sustain":0,"one_shot":"on","vel_to_amp":0.3})"},
    // ---------------------------------------------------------------- ベース
    {"Wavetable Bass", "のこぎり + 複雑な倍音のウェーブテーブル x5 → ローパスを速いエンベロープで閉じる → 非対称の歪み。ベロシティでアクセント(フィルターの開き)",
     R"({"osc_a_wave":"saw","osc_a_level":0.5,"osc_b_wave":"monster","osc_b_level":0.8,"unison":5,"unison_osc":"b","detune":0.14,
        "filter_type":"lp12","cutoff":1200,"resonance":1.1,"fenv_amount":12000,"fenv_decay":0.05,"vel_to_filter":0.42,
        "amp_attack":0.0008,"amp_decay":0.08,"amp_sustain":0.55,"amp_release":0.006,"vel_to_amp":0.16,
        "drive_type":"diode","drive":4.2,"post_hp":100})"},
    {"Sub Sine", "サインに 2 倍音をわずかに加えたサブベース",
     R"({"osc_a_wave":"sine","fm_ratio":2,"fm_index":0.15,"fm_decay":30,"amp_attack":0.002,"amp_release":0.008,
        "vel_to_amp":0,"post_lp":120})"},
    {"Main Bass", "ハイテクコアのメインベース。のこぎり → ローパス → Phaser → オールパス(12 段 350 Hz)→ 歪み。DAW 側でマルチバンド・コンプレッサーを足すと締まる",
     R"({"osc_a_wave":"saw","filter_type":"lp12","cutoff":400,"fenv_amount":3000,"fenv_decay":0.03,"resonance":1.2,
        "phaser_mix":0.4,"phaser_rate":0.7,"phaser_center":600,"phaser_depth":400,"allpass_stages":12,"allpass_freq":350,
        "drive_type":"tanh","drive":2.0,"post_hp":40,"post_lp":2500,"voice_mode":"mono",
        "amp_attack":0.0008,"amp_release":0.004,"vel_to_amp":0.2,"gain":-4})"},
    {"Layer Saw", "ベース・チョップ用ののこぎり",
     R"({"osc_a_wave":"saw","filter_type":"lp12","cutoff":600,"fenv_amount":5000,"fenv_decay":0.02,"resonance":1.5,
        "drive_type":"tanh","drive":2.0,"post_hp":90,"amp_attack":0.0008,"amp_release":0.004,"gain":0.6})"},
    {"Layer Hardcore", "のこぎり 2 本を強く歪ませたベース・チョップ",
     R"({"osc_a_wave":"saw","osc_b_wave":"saw","osc_b_level":1,"osc_b_fine":10.4,"drive_type":"tanh","drive":4,
        "post_hp":90,"amp_attack":0.0008,"amp_release":0.004,"gain":1.7})"},
    {"Layer Reso", "のこぎり + 矩形 → 共振ローパスが一瞬開くベース・チョップ",
     R"({"osc_a_wave":"saw","osc_b_wave":"square","osc_b_level":0.25,"filter_type":"lp12","cutoff":300,"fenv_amount":4000,
        "fenv_decay":0.015,"resonance":2.5,"drive_type":"tanh","drive":2.0,"post_hp":90,"amp_attack":0.0008,"amp_release":0.004,"gain":-0.3})"},
    {"Layer Meaty FM", "FM + 歪みの太いベース・チョップ",
     R"({"osc_a_wave":"sine","fm_ratio":1,"fm_index":2.5,"fm_decay":0.04,"drive_type":"tanh","drive":3,"post_hp":90,
        "amp_attack":0.0008,"amp_release":0.004,"gain":1.3})"},
    {"Acid 303", "アシッド・ベース。共振の強いローパス(底は基音の 1.5 倍)、ベロシティでアクセント、レガートでスライド",
     R"({"osc_a_wave":"saw","filter_type":"lp12","cutoff":0,"key_track":1.5,"fenv_amount":4000,"fenv_decay":0.05,
        "resonance":4,"vel_to_filter":0.55,"drive_type":"tanh","drive":2.5,"voice_mode":"legato","glide":0.04,
        "amp_attack":0.001,"amp_release":0.004,"vel_to_amp":0})"},
    {"Gritty Mid", "のこぎり + 矩形 → ローパス → 非対称の歪みの荒々しいミッドベース",
     R"({"osc_a_wave":"saw","osc_a_level":1.5,"osc_b_wave":"square","osc_b_level":0.45,"osc_b_fine":6.9,
        "filter_type":"lp12","cutoff":500,"fenv_amount":1500,"fenv_decay":0.06,"resonance":1.5,
        "drive_type":"diode","drive":3,"post_hp":50,"post_lp":1800,"amp_attack":0.001,"amp_release":0.006,"gain":1.3})"},
    {"Offbeat Bass", "ハードコアのオフビート・ベース",
     R"({"osc_a_wave":"saw","osc_b_wave":"saw","osc_b_level":1,"osc_b_fine":8.6,"filter_type":"lp12","cutoff":600,
        "fenv_amount":3000,"fenv_decay":0.04,"resonance":1.3,"drive_type":"tanh","drive":2.5,
        "amp_attack":0.001,"amp_release":0.006})"},
    // ---------------------------------------------------------------- 上物
    {"Supersaw", "7 声 ±0.28 半音、パン ±0.8、0.18 秒で減衰するスーパーソウ。和音にするときは gain を下げる",
     R"({"osc_a_wave":"saw","unison":7,"detune":0.28,"spread":0.8,"amp_attack":0.001,"amp_decay":0.18,"amp_sustain":0,
        "amp_release":0.01,"post_lp":7000,"post_hp":180})"},
    {"Supersaw Chord", "コード・スタック用のスーパーソウ(減衰 0.45 秒)",
     R"({"osc_a_wave":"saw","unison":7,"detune":0.32,"spread":0.8,"amp_attack":0.001,"amp_decay":0.45,"amp_sustain":0,
        "amp_release":0.01,"post_lp":7000,"post_hp":180})"},
    {"Round Supersaw", "高域を抑えた丸いスーパーソウ",
     R"({"osc_a_wave":"saw","unison":7,"detune":0.2,"spread":0.8,"amp_attack":0.001,"amp_decay":0.6,"amp_sustain":0,
        "amp_release":0.01,"post_lp":2500,"post_hp":180})"},
    {"Chord Stab", "±12 セントの 3 声。ローパスが開いて閉じるコード・スタブ",
     R"({"osc_a_wave":"saw","unison":3,"detune":0.12,"filter_type":"lp12","cutoff":1000,"fenv_amount":7000,"fenv_decay":0.04,
        "resonance":1.0,"amp_attack":0.0005,"amp_decay":0.12,"amp_sustain":0,"amp_release":0.01})"},
    {"Wub Chord", "2 拍ごとにローパスを開き直す柔らかい wub(再生中は曲の拍に同期)",
     R"({"osc_a_wave":"saw","unison":2,"detune":0.09,"filter_type":"lp12","cutoff":300,"resonance":1.4,
        "lfo_shape":"wub","lfo_sync":"1/2","lfo_to_cutoff":3500,"amp_attack":0.02,"amp_release":0.03})"},
    {"Square Pluck", "矩形波のプラック(アルペジオ向け)",
     R"({"osc_a_wave":"square","osc_a_level":0.5,"filter_type":"lp12","cutoff":1800,"fenv_amount":4200,"fenv_decay":0.05,
        "resonance":0.9,"amp_attack":0.001,"amp_decay":0.09,"amp_sustain":0,"amp_release":0.005})"},
    {"PWM Lead", "PWM 矩形 + ビブラートのリード(合いの手向け)",
     R"({"osc_a_wave":"pulse","pulse_width":0.5,"pwm_depth":0.35,"pwm_rate":0.8,"vib_depth":20,"vib_rate":6,"vib_delay":0.08,
        "post_lp":5000,"amp_attack":0.008,"amp_release":0.02})"},
    {"Distorted Lead", "のこぎり + 矩形を強く歪ませたリード",
     R"({"osc_a_wave":"saw","osc_b_wave":"square","osc_b_level":0.25,"vib_depth":18,"vib_rate":5.8,"vib_delay":0.1,
        "drive_type":"tanh","drive":4,"post_lp":7000,"amp_attack":0.002,"amp_release":0.01})"},
    {"Soft Lead", "±12 セントの 3 声 + ビブラートの柔らかいリード",
     R"({"osc_a_wave":"saw","unison":3,"detune":0.12,"vib_depth":12,"vib_rate":5.8,"vib_delay":0.1,"post_lp":5000,
        "amp_attack":0.002,"amp_release":0.01})"},
    {"FM Bell", "比 3.5 の FM ベル。変調が 0.3 秒で減る",
     R"({"osc_a_wave":"sine","fm_ratio":3.5,"fm_index":1.2,"fm_decay":0.3,"amp_attack":0.0005,"amp_decay":0.5,
        "amp_sustain":0,"amp_release":0.02})"},
    {"Pad", "デチューンしたのこぎりの控えめなパッド(ハイパス 400 Hz)",
     R"({"osc_a_wave":"saw","unison":2,"detune":0.08,"post_lp":2500,"post_hp":400,"amp_attack":0.5,"amp_release":0.3,"gain":-8})"},
    {"Formant Vox", "のこぎり + 少しのノイズを 3 フォルマントのフィルターに通した、母音っぽい音。vowel で母音を変える",
     R"({"osc_a_wave":"saw","osc_b_wave":"noise","osc_b_level":0.04,"filter_type":"formant","vowel":0,
        "vib_depth":25,"vib_rate":5.5,"vib_delay":0.15,"amp_attack":0.02,"amp_release":0.06})"},
    // ---------------------------------------------------------------- FX
    {"Noise Riser", "ノイズのライザー(BP を 4 秒かけて開く)。ビルドアップの終わりまで押さえる",
     R"({"osc_a_wave":"noise","filter_type":"bp","cutoff":200,"resonance":1.5,"fenv_amount":9000,"fenv_attack":4,
        "fenv_decay":0.5,"amp_attack":4,"amp_release":0.3,"spread":0,"gain":-6})"},
    {"Impact", "サブが落ちるインパクト + ノイズの尾。C1(MIDI 24)",
     R"({"osc_a_wave":"sine","pitch_env1_hz":220,"pitch_env1_time":0.25,"amp_attack":0.0005,"amp_decay":0.6,"amp_sustain":0,
        "one_shot":"on","noise_level":0.25,"noise_filter":"hp","noise_freq":2000,"noise_decay":0.6,"drive_type":"tanh","drive":1.5})"},
};

static const std::vector<Preset> kFxPresets = {
    {"Init", "初期値(素通し)", R"({})"},
    {"Pump 1/4", "1 拍ごとのサイドチェイン(深さ 1、曲線 1.8、戻り 0.75 拍)",
     R"({"pump_depth":1.0,"pump_rate":"1/4","pump_length":0.75,"pump_shape":1.8})"},
    {"Pump MIDI", "MIDI ノートごとにダッキング(キックと同じ位置にゴーストノートを置く)",
     R"({"pump_depth":0.85,"pump_source":"midi","pump_rate":"1/4","pump_length":0.6,"pump_shape":1.2})"},
    {"Tape Stop 2 steps", "16 分 2 つ(0.5 拍)で止まるテープストップ。Tape Stop を on にした所から止まる",
     R"({"tapestop_beats":0.5,"tapestop_end_gain":0.6,"tapestop_auto":"on"})"},
    {"Hi-Tech Core Bass Chain", "ハイテクコアのベース向けの Phaser + オールパス + 歪み",
     R"({"phaser_mix":0.4,"phaser_rate":0.7,"allpass_stages":12,"allpass_freq":350,"drive_type":"tanh","drive":2.0})"},
    {"Wide + Bass Mono", "バス向けに広げる(幅 260 %、120 Hz 以下はモノラル)",
     R"({"width":2.6,"bass_mono":120})"},
};

static std::unique_ptr<Engine> makeSynth() { return std::make_unique<Synth>(); }
static std::unique_ptr<Engine> makeFx() { return std::make_unique<Fx>(); }

const PluginSpec& synthSpec() {
    static const PluginSpec s{"MPS Synth", "synth", true, &synthParams(), &kSynthPresets, &makeSynth};
    return s;
}
const PluginSpec& fxSpec() {
    static const PluginSpec s{"MPS FX", "fx", false, &fxParams(), &kFxPresets, &makeFx};
    return s;
}
const PluginSpec* specById(const std::string& id) {
    if (id == "synth" || id == "MPS Synth") return &synthSpec();
    if (id == "fx" || id == "MPS FX") return &fxSpec();
    return nullptr;
}

}  // namespace mps
