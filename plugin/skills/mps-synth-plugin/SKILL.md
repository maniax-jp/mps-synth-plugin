---
name: mps-synth-plugin
description: VST3 プラグイン MPS Synth(音源)/ MPS FX(エフェクト)を AI から操作するときに使う。DAW(REAPER など)のトラックに挿したインスタンスをトラック名で選び、プリセットの読み込み、パラメータの変更、試し鳴らし、WAV 書き出し、状態の保存と復元を GUI を触らずに行う。キック、ハット、クラップ、スラップ/主軸ベース、アシッド、スーパーソウ、wub、矩形波プラック、FM ベル、フォルマント声などの音作り、テープストップ・サイドチェイン・Disperser の設定も含む。
---

# MPS Synth / MPS FX を AI から操作する

MPS Synth / MPS FX は、DAW に挿したインスタンスごとに 127.0.0.1 で制御サーバーを開く VST3 プラグイン。
このスキルの `scripts/mps_ctl.py`(Python 3、標準ライブラリだけ)で操作する。

```bash
CTL="python3 ${CLAUDE_SKILL_DIR}/scripts/mps_ctl.py"
```

## 1. まず確かめる
1. `$CTL list` で起動中のインスタンスを見る(`47800  MPS Synth  track='Bass'  name='-'`)。
   - 何も出ない → DAW にプラグインが挿さっていない、DAW が起動していない、プラグインが入っていない。ユーザーに確かめる。
   - トラック名が空 → DAW がトラック名を渡していない。`--port` で選ぶ。
2. 対象は `--name 名前`(完全一致、大文字小文字は区別しない)、`--track トラック名`(部分一致)、`--plugin synth|fx`、
   `--port 番号` で選ぶ。組み合わせて絞り込める(例: `--track Bass --plugin fx`)。候補が複数あるとエラーで一覧と絞り方が出る。
3. **同じトラックに同じプラグインを複数挿したとき**(サイドチェイン用とテープストップ用の MPS FX など)は、
   トラック名とプラグインでは区別できない。`--port` で 1 つずつ選んで名前を付け、以後は `--name` で呼ぶ:
   ```bash
   $CTL --port 47800 rename pump        # 同じ名前が使われていれば断られる(--force で強制)。"" で名前を消す
   $CTL --port 47802 rename tapestop
   $CTL --name pump set pump_depth=0.8
   ```
   名前はプラグインの状態に保存されるので、DAW のプロジェクトを開き直しても残る。どのポートがどれかは、
   プラグインの画面のポート表示と `list` を照らし合わせるか、`play` で鳴らして確かめる。
4. プラグインの画面(表示だけ)には制御ポート・トラック名・名前・プリセット名・最近のコマンド・変えたパラメータが出る。
   ユーザーに「どのトラックか」を聞くときはこの画面を見てもらう。

## 2. 基本の操作
```bash
$CTL --track Bass presets                          # プリセット一覧
$CTL --track Bass load "Slap Bass"                  # 読み込み(続けて key=値 で上書きできる)
$CTL --track Bass set cutoff=900 drive_type=diode drive=5
$CTL --track Bass get --changed                     # 既定値から変えた値だけ
$CTL --track Bass params                            # 全パラメータの範囲・単位・選択肢・説明
$CTL --track Bass play 41 --velocity 1 --duration 0.3   # その場で鳴らしてユーザーに聴いてもらう
$CTL --track Bass render /abs/path/probe.wav --notes '[[0,0.6,41,1]]' --bpm 160   # プラグインの中で書き出す
$CTL --track Bass state > bass.json                 # 状態を保存 / load-state bass.json で戻す
```
- 値は**プレーン値**(Hz・秒・半音・セント・dB)。enum はラベル(`filter_type=lp12`)。範囲外は丸められる。
- `set` / `load` は DAW にも通知されるので、DAW の表示・オートメーション・プロジェクトの保存内容も変わる。
  ユーザーが手で調整している最中に上書きしないよう、変える前に `get --changed` で今の値を見る。
- Python から使うなら `sys.path.insert(0, "${CLAUDE_SKILL_DIR}/scripts")` のあと `from mps_ctl import connect`
  → `bass = connect(track='Bass'); bass.load_preset('Acid 303'); bass.set(resonance=4); bass.play(50)`。
  名前で呼ぶなら `connect(name='pump')`、名前を付けるなら `client.set_name('pump')`。
- 生のプロトコル・全コマンド・`render` の引数: [references/protocol.md](references/protocol.md)

## 3. 音作りの手順
1. 近いプリセットから始める: [references/presets.md](references/presets.md)(どの音程で鳴らす前提かも書いてある)。
2. 必要なパラメータだけ `set` で変える: [references/parameters.md](references/parameters.md)。
3. 音程を持つ音色は、`render` で単音(0.6 秒程度)を書き出して知覚オクターブを測ってから打ち込む
   (倍音の強い音色は 1 オクターブ上に聞こえることがある)。
4. 最後は `play` でユーザーに聴いてもらう。こちらは音を聴けないので、聴感の判断はユーザーに委ねる。

### 要点
- **キック**: `osc_a_wave=sine`、`pitch_env1_hz/time` と `pitch_env2_hz/time`(f = 音程 + env1·e^(−t/time1) + env2·e^(−t/time2))、
  `amp_hold` / `amp_decay`、`amp_sustain=0`、`click_level`、`drive_type=tanh`、`one_shot=on`。鳴らす MIDI ノートが着地の音程。
  ベースに中域を渡すなら `mid_cut`。
- **ベースのアクセント**: `vel_to_filter`(フィルター・エンベロープの量)と `vel_to_amp`。スライドは `voice_mode=legato` + `glide`。
- **wub**: `lfo_shape=wub` + `lfo_sync=1/2`(2 拍)+ `lfo_to_cutoff`。DAW の再生中は曲の拍に同期する。
- **ハット**: `osc_a_wave=noise`(金属的なら `metal`)+ `filter_type=hp12` と `post_hp` を同じ周波数にする(4 次)。
- **クラップ**: `burst_count=4`、`burst_spacing≈0.0087`、`burst_decay≈0.007` + `noise_*` の尾。
- **スーパーソウ**: 1 音で鳴らす前提の音量。和音にするときは `gain` を下げる。
- **Main Bass** プリセットにはマルチバンド圧縮が入っていないので、DAW でマルチバンド・コンプレッサーを後ろに挿すと締まる。

### MPS FX
- サイドチェイン: `pump_depth`(0 でオフ)、`pump_source=tempo`(曲の拍)か `midi`(トラックの MIDI ノートごと)、`pump_rate`、
  `pump_length`、`pump_shape`。
- テープストップ: `tapestop` が **off → on に変わった瞬間**に減速を始める(`tapestop_beats` で止まりきるまでの拍数)。
  もう一度使うときは一度 off に戻す。`tapestop_auto=on` なら止まりきったあと元の音に戻る。
- 他に Phaser、Disperser、歪み、`width` と `bass_mono`、`mix`、`gain`。

## 4. DAW 側の操作(REAPER)
トラックを作る・プラグインを挿す・ノートを打つのは DAW の仕事(REAPER なら ReaScript)。挿すときの名前は
`VST3i: MPS Synth (Maniax)` と `VST3: MPS FX (Maniax)`。挿したらトラック名を付けてから、この制御サーバーで音色を送る。
DAW のオートメーションでも全パラメータを動かせる(表示名は parameters.md の「名前」)。

## 5. うまくいかないとき
- `list` に出ない: プラグインの画面に「制御サーバー: 無効」と出ていないか見てもらう(環境変数 `MPS_CTL_DISABLE=1` か、
  47800〜47899 が全部使われている)。
- `connection refused`: DAW がプラグインを外した・プロジェクトを閉じた。`list` からやり直す。
- 音が出ない(`play`): DAW がそのトラックを処理していない(ミュート、オーディオデバイス停止など)。`render` は DAW と関係なく書き出せる。
