# パラメータ一覧

プラグインの定義から自動生成(`tools/gen_skill_refs.py`)。値はプレーン値で送る。
enum はラベルでも番号でもよい。範囲外は丸められる。`scale` は DAW のつまみの曲がり方(cubic = 小さい値を細かく)。

## MPS Synth

| key | 名前 | 範囲 | 既定 | 単位 | 説明 |
|---|---|---|---|---|---|
| `osc_a_wave` | OSC A Wave | saw / square / pulse / sine / triangle / monster / noise / metal | saw |  | saw/square/pulse は polyBLEP、monster は倍音に山のある帯域制限ウェーブテーブル、metal は 808 型の矩形 6 本 |
| `osc_a_level` | OSC A Level | 0 〜 2 | 1 |  |  |
| `osc_a_semi` | OSC A Semi | -48 〜 48 | 0 | st |  |
| `osc_b_wave` | OSC B Wave | saw / square / pulse / sine / triangle / monster / noise / metal | saw |  |  |
| `osc_b_level` | OSC B Level | 0 〜 2 | 0 |  |  |
| `osc_b_semi` | OSC B Semi | -48 〜 48 | 0 | st |  |
| `osc_b_fine` | OSC B Fine | -100 〜 100 | 0 | ct |  |
| `sub_level` | Sub Level | 0 〜 2 | 0 |  | 1 オクターブ下のサイン |
| `unison` | Unison | 1 〜 9 | 1 |  | OSC A/B のユニゾン数(saw/square/pulse/sine/triangle/monster) |
| `unison_osc` | Unison Target | a+b / a / b | a+b |  | ユニゾンを掛ける OSC |
| `detune` | Detune | 0 〜 1 | 0.14 | st | ユニゾンを ±この半音幅に等間隔で並べる |
| `spread` | Stereo Spread | 0 〜 1 | 0 |  | ユニゾンのパンを ±この幅に並べる |
| `pulse_width` | Pulse Width | 0.05 〜 0.95 | 0.5 |  |  |
| `pwm_depth` | PWM Depth | 0 〜 0.45 | 0 |  |  |
| `pwm_rate` | PWM Rate | 0.05 〜 20 | 0.8 | Hz |  |
| `fm_ratio` | FM Ratio | 0.25 〜 16 | 2 |  | sine 波の位相変調(モジュレーター周波数比) |
| `fm_index` | FM Index | 0 〜 10 | 0 |  |  |
| `fm_decay` | FM Decay | 0.001 〜 30 | 0.3 | s | 変調指数の減衰時定数(index * exp(-t/decay)) |
| `pitch_env1_hz` | Pitch Env1 | 0 〜 5000 | 0 | Hz | f = 音程 + env1*exp(-t/time1) + env2*exp(-t/time2)(キックのピッチスイープ) |
| `pitch_env1_time` | Pitch Env1 Time | 0.0002 〜 1 | 0.018 | s |  |
| `pitch_env2_hz` | Pitch Env2 | 0 〜 5000 | 0 | Hz |  |
| `pitch_env2_time` | Pitch Env2 Time | 0.0002 〜 1 | 0.0022 | s |  |
| `voice_mode` | Voice Mode | poly / mono / legato | poly |  |  |
| `glide` | Glide | 0 〜 2 | 0 | s | 前の音からこの時間で半音直線に移動(mono/legato) |
| `vib_depth` | Vibrato Depth | 0 〜 100 | 0 | ct |  |
| `vib_rate` | Vibrato Rate | 0.1 〜 20 | 5.8 | Hz |  |
| `vib_delay` | Vibrato Delay | 0 〜 2 | 0.1 | s | この時間のあと 0.2 秒かけて深くなる |
| `bend_range` | Bend Range | 0 〜 24 | 2 | st |  |
| `pitch_bend` | Pitch Bend | -1 〜 1 | 0 |  | MIDI ピッチベンド(自動化可) |
| `filter_type` | Filter Type | off / lp12 / hp12 / bp / lp24 / formant | off |  |  |
| `cutoff` | Cutoff | 0 〜 20000 | 20000 | Hz | fc = cutoff + key_track*音程 + env_amount*env + lfo_cutoff*lfo |
| `key_track` | Key Track | 0 〜 8 | 0 | x | 音程の周波数の何倍を cutoff に足すか(アシッドは 1.5) |
| `resonance` | Resonance (Q) | 0.3 〜 20 | 0.707 |  |  |
| `fenv_amount` | Filter Env | 0 〜 20000 | 0 | Hz |  |
| `fenv_attack` | Filter Env Attack | 0 〜 10 | 0 | s |  |
| `fenv_decay` | Filter Env Decay | 0.001 〜 10 | 0.05 | s |  |
| `vel_to_filter` | Velocity > Filter | 0 〜 1 | 0 |  | ベロシティでフィルター・エンベロープ量を変える(アクセント) |
| `vowel` | Vowel | 0 〜 4 | 0 |  | formant の母音 0=a 1=o 2=e 3=i 4=u(間は補間) |
| `lfo_shape` | LFO Shape | sine / wub / triangle / square / saw_down | sine |  | 0..1 の単極。wub = sin(pi*min(1, 1.6*phase))^2 |
| `lfo_rate` | LFO Rate | 0.01 〜 30 | 1 | Hz |  |
| `lfo_sync` | LFO Sync | off / 1/16 / 1/8 / 1/4 / 1/2 / 1/1 / 2/1 / 4/1 | off |  | 1/4 = 1 拍周期。再生中は曲の位置に同期 |
| `lfo_to_cutoff` | LFO > Cutoff | 0 〜 20000 | 0 | Hz |  |
| `lfo_to_pitch` | LFO > Pitch | 0 〜 1200 | 0 | ct |  |
| `amp_attack` | Amp Attack | 0 〜 10 | 0.001 | s |  |
| `amp_hold` | Amp Hold | 0 〜 10 | 0 | s |  |
| `amp_decay` | Amp Decay | 0.001 〜 30 | 0.3 | s | サステインへ exp(-t/decay) で近づく時定数 |
| `amp_sustain` | Amp Sustain | 0 〜 1 | 1 |  |  |
| `amp_release` | Amp Release | 0.0005 〜 10 | 0.006 | s | 直線で 0 まで |
| `one_shot` | One Shot | off / on | off |  | ノートオフを無視してエンベロープを最後まで(ドラム) |
| `vel_to_amp` | Velocity > Amp | 0 〜 1 | 0.3 |  |  |
| `burst_count` | Burst Count | 1 〜 8 | 1 |  | 2 以上でクラップの連打エンベロープ(バースト + 0.5*AHDSR) |
| `burst_spacing` | Burst Spacing | 0.001 〜 0.05 | 0.0087 | s |  |
| `burst_decay` | Burst Decay | 0.0005 〜 0.05 | 0.007 | s |  |
| `click_level` | Click Level | 0 〜 1 | 0 |  | HP ノイズのアタック(キック) |
| `click_time` | Click Time | 0.0002 〜 0.05 | 0.0025 | s |  |
| `click_hp` | Click HP | 200 〜 16000 | 2500 | Hz |  |
| `noise_level` | Noise Level | 0 〜 2 | 0 |  | 独立したノイズ層(スネアのノイズ、クラップの尾) |
| `noise_filter` | Noise Filter | white / bp / hp | white |  |  |
| `noise_freq` | Noise Freq | 100 〜 16000 | 4500 | Hz |  |
| `noise_decay` | Noise Decay | 0.001 〜 10 | 0.05 | s |  |
| `phaser_mix` | Phaser Mix | 0 〜 1 | 0 |  |  |
| `phaser_rate` | Phaser Rate | 0.01 〜 10 | 0.7 | Hz |  |
| `phaser_center` | Phaser Center | 50 〜 8000 | 600 | Hz |  |
| `phaser_depth` | Phaser Depth | 0 〜 4000 | 400 | Hz |  |
| `disperser_stages` | Disperser Stages | 0 〜 48 | 0 |  |  |
| `disperser_freq` | Disperser Freq | 30 〜 8000 | 350 | Hz |  |
| `drive_type` | Drive Type | off / tanh / diode / fold / hard | off |  | tanh は tanh(d*x)/tanh(d)、diode は非対称 |
| `drive` | Drive | 0.1 〜 30 | 1 |  |  |
| `post_hp` | Post HP | 10 〜 4000 | 10 | Hz | butter 2 次(10 Hz = 実質オフ) |
| `post_lp` | Post LP | 500 〜 22000 | 22000 | Hz | butter 2 次(22000 = オフ) |
| `mid_cut` | Mid Cut | 0 〜 1 | 0 |  | x - mid_cut*HP300(LP2000(x)):キックやループの中域をベースに渡す |
| `gain` | Gain | -48 〜 12 | 0 | dB |  |

## MPS FX

| key | 名前 | 範囲 | 既定 | 単位 | 説明 |
|---|---|---|---|---|---|
| `pump_depth` | Pump Depth | 0 〜 1 | 0 |  | サイドチェインの深さ(0 = オフ)。周期の頭で下げ、曲線で戻す |
| `pump_source` | Pump Source | tempo / midi | tempo |  | tempo = 曲の拍に同期、midi = ノートオンごと(キックと同じ位置にゴーストノートを置く) |
| `pump_rate` | Pump Rate | 1/16 / 1/8 / 1/4 / 1/2 / 1/1 / 2/1 | 1/4 |  |  |
| `pump_attack` | Pump Attack | 0 〜 0.9 | 0 |  | 周期の割合 |
| `pump_length` | Pump Length | 0.05 〜 1 | 0.75 |  | 戻りきるまでの周期の割合 |
| `pump_shape` | Pump Shape | 0.2 〜 6 | 1.8 |  | 戻りの曲線(大きいほど長く下がる) |
| `phaser_mix` | Phaser Mix | 0 〜 1 | 0 |  |  |
| `phaser_rate` | Phaser Rate | 0.01 〜 10 | 0.7 | Hz |  |
| `phaser_center` | Phaser Center | 50 〜 8000 | 600 | Hz |  |
| `phaser_depth` | Phaser Depth | 0 〜 4000 | 400 | Hz |  |
| `disperser_stages` | Disperser Stages | 0 〜 48 | 0 |  |  |
| `disperser_freq` | Disperser Freq | 30 〜 8000 | 350 | Hz |  |
| `drive_type` | Drive Type | off / tanh / diode / fold / hard | off |  |  |
| `drive` | Drive | 0.1 〜 30 | 1 |  |  |
| `tapestop` | Tape Stop | off / on | off |  | off→on で減速を始める(自動化か制御サーバーで切り替える) |
| `tapestop_beats` | Tape Stop Length | 0.0625 〜 16 | 0.5 | beats | 止まりきるまでの長さ(拍) |
| `tapestop_end_gain` | Tape Stop End Gain | 0 〜 1 | 0.6 |  | 止まるまでに音量を 1 → この値に下げる |
| `tapestop_auto` | Tape Stop Auto Resume | off / on | on |  | on = 止まりきったら元の音に戻る、off = Tape Stop を off にするまで無音 |
| `width` | Width | 0 〜 4 | 1 |  | サイドの倍率(1 = そのまま) |
| `bass_mono` | Bass Mono | 0 〜 500 | 0 | Hz | この周波数より下のサイドを消す(0 = オフ) |
| `mix` | Mix | 0 〜 1 | 1 |  |  |
| `gain` | Gain | -48 〜 12 | 0 | dB |  |
