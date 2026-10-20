# MPS プラグイン 開発者向け情報

使う人向けの説明は [README.md](README.md)。ここにはビルド、構成、テスト、リリース、制御サーバーの実装の決まりをまとめる。

## ビルド
必要なもの: CMake 3.25+、C++17 コンパイラ(Xcode 14+ / Visual Studio 2022 / GCC 11+)、git(VST3 SDK を GitHub から取得する)。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release     # Windows は -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
- できるもの: `build/VST3/Release/MPS.vst3`(プラグイン)、`build/mps_render`(DAW のない環境でのデバッグ用 CLI。配布物には入れない)、
  `build/bin/Release/mps_testhost`(テスト用ホスト)。
- ビルドの最後に VST3 SDK の validator が自動で走る(94 項目。`SMTG_RUN_VST_VALIDATOR`)。
- **macOS**: 既定で `arm64;x86_64` のユニバーサル(`CMAKE_OSX_ARCHITECTURES`、デプロイ先 10.15)。Ninja か Makefile で生成する
  (Xcode ジェネレーターは SDK が arm64e を足して静的ライブラリと合わなくなる)。`lipo -archs build/VST3/Release/MPS.vst3/Contents/MacOS/MPS` で確認し、
  `codesign --force --deep --sign - build/VST3/Release/MPS.vst3` でアドホック署名する。
  VSTGUI が Objective-C++ を使うため、最上位の CMakeLists で OBJC/OBJCXX を有効にしている。
- **Linux**: VSTGUI の依存(`libx11-xcb-dev libxcb-util-dev libxcb-cursor-dev libxcb-xkb-dev libxcb-keysyms1-dev libxkbcommon-dev
  libxkbcommon-x11-dev libfontconfig1-dev libcairo2-dev libpango1.0-dev libgtkmm-3.0-dev libsqlite3-dev`)。GUI なしなら `-DMPS_BUILD_GUI=OFF`。
- SDK を手元に持っているなら `-DVST3_SDK_ROOT=/path/to/vst3sdk`(submodule の base / cmake / pluginterfaces / public.sdk / vstgui4 が要る)。
- 手元の VST3 フォルダへリンクを張るなら `-DSMTG_CREATE_PLUGIN_LINK=ON`(既定は OFF)。
- VST3 SDK は Steinberg の MIT ライセンス版(v3.8.0_build_66)、JSON は nlohmann/json 3.11.3(MIT)を CMake が取得する。

## 構成
```
core/       DSP(synth / fx)、パラメータ定義、プリセット、状態・制御コマンド・書き出し、制御サーバー(VST3 SDK に依存しない)
  params.*        ParamDef(key・表示名・単位・スケール・範囲・既定値・ラベル)とプレーン値 ⇔ 0〜1 の変換
  synth.* fx.*    エンジン(Engine インタフェース)。パラメータの並び = VST3 のパラメータ ID。並びを変えると保存済みの自動化がずれる
  presets.cpp     ファクトリープリセット(状態と同じ JSON)と PluginSpec
  control.*       ParamStore(スレッド間で共有するプレーン値)、状態 JSON、制御コマンド、オフライン書き出し、WAV 読み書き
  control_server.* 127.0.0.1 の TCP サーバーとインスタンスのレジストリ
vst3/       プロセッサー・コントローラー・ステータス GUI(VSTGUI、表示だけ)・ファクトリー(1 つの MPS.vst3 に 2 クラス)
tools/      mps_render(CLI)、testhost(テスト用ホスト)、gui_snapshot(GUI を PNG に)、live_test.py、gen_skill_refs.py
docs/       README の画像
plugin/     Claude Code のプラグイン「mps-synth」(.claude-plugin/plugin.json)と AI 用スキル skills/mps-synth-plugin/
            (SKILL.md、scripts/mps_ctl.py、references/)
.claude-plugin/marketplace.json   マーケットプレイス「maniax-jp-mps-synth-plugin」(plugin/ を公開する)
```
操作のクライアント `mps_ctl.py` はスキルの中(`plugin/skills/mps-synth-plugin/scripts/`)にあり、スキルと一緒に配る。

### スレッドとデータの流れ
- パラメータの正本は `ParamStore`(atomic のプレーン値)。ホストの自動化(`inputParameterChanges`)も制御サーバーの `set` もここに書く。
  オーディオスレッドはブロックの頭で変わった値だけエンジンへ渡す。
- 制御サーバーで変えた値には dirty 印が付き、次の `process()` で `outputParameterChanges` に載せてホストへ知らせる
  (DAW の表示と自動化の記録が追従する)。値が実際に変わったときだけ印を付ける。
- `play` / `note_off` はミューテックス付きのキューに積み、オーディオスレッドは `try_lock` で取り出す(待たない)。
- 状態(`getState` / `setState`)は JSON の文字列 `{"format":"mps-state","version":1,"plugin":"synth","preset":...,"params":{...}}`。
  知らないキーは無視、書いていないキーは既定値。
- プロセッサーとコントローラーは IMessage でやり取りする: トラック名(`mps.track`、IInfoListener から)、状態の問い合わせ
  (`mps.status?` → `mps.status`、GUI が 300 ms ごと)。256 文字の制限がある TextMessage は使わず、バイナリ属性 `data` に UTF-8 を入れる。
- 制御サーバーは 1 インスタンス 1 スレッド、1 クライアントずつ処理する(30 秒黙っている接続は切る)。`render` はサーバーのスレッドで
  別のエンジンを作って合成するので、オーディオスレッドは止めない。

### 制御サーバーのプロトコル
1 行 1 JSON の要求 `{"id": 任意, "cmd": "...", ...}` に、1 行 1 JSON `{"id": 同じ, "ok": true, "result": ...}`
(失敗は `{"ok": false, "error": "..."}`、一部だけ失敗した `set` は `warnings`)で答える。コマンドの一覧はスキルの
`references/protocol.md`(使う人・AI 向け)。環境変数 `MPS_CTL_PORT_BASE`(既定 47800、100 個の範囲)、`MPS_CTL_DISABLE=1`、
`MPS_CTL_REGISTRY`。レジストリのファイル名は `<plugin>-<pid>-<port>.json`、終了時に消す。

### パラメータ・プリセットを足すとき
- `SynthParam` / `FxParam` の enum と `synthParams()` / `fxParams()` の並びを一致させる(コンストラクタで個数を検査する)。
  新しいパラメータは**末尾に足す**(ID がずれると保存済みのプロジェクトの自動化が別のパラメータに付く)。
- プリセットは `presets.cpp` に状態と同じ JSON で書く。説明には音の内容と鳴らす前提の MIDI ノートを書く。
- スキルの `references/presets.md` と `references/parameters.md` を更新する。

## テスト
```bash
python3 tools/live_test.py build                       # テスト用ホストで実時間に回し、制御サーバー経由の操作を確かめる(標準ライブラリだけ)
python3 tools/gen_skill_refs.py build/mps_render      # スキルの references/parameters.md・presets.md を作り直す(--check で検査だけ)
ninja -C build mps_gui_snapshot && build/bin/Release/mps_gui_snapshot gui.png   # Linux: ステータス GUI を PNG に
```
- `live_test.py`: レジストリにトラック名つきで載る / `load_preset`・`set` がホストへ通知される / `play` で音が出る / `render` / 状態 JSON /
  知らないキーのエラー / 終了でレジストリが消える。
- `mps_render`: プラグインと同じ DSP で WAV を書き出す。DAW のない環境で音を確かめるときに使う。
  ```bash
  build/mps_render --list-presets synth
  echo '{"plugin":"synth","preset":"Psy Kick","notes":[[0,0.25,32,1]],"out":"kick.wav"}' | build/mps_render -
  echo '{"plugin":"fx","preset":"Tape Stop 2 steps","input":"bass.wav","automation":[[1.5,{"tapestop":"on"}]],"bpm":160,"out":"stop.wav"}' | build/mps_render -
  ```
  SPEC は 1 件のオブジェクトか配列。`notes` は `[t, dur, note, vel]`(`"unit":"beats"` で拍単位)、`params` で上書き、`automation` で途中の値を変える。
- スペクトル重心などを比べるときは書き出しを 32bit float(`"bits": 32`)にする。24bit の量子化ノイズで値が大きく振れる。

## CI とリリース
- `.github/workflows/build.yml`: macOS(Apple Silicon ランナー、ユニバーサル)/ Windows x64 / Linux x64 でビルド → validator →
  アーキテクチャ確認とアドホック署名(macOS)→ `live_test.py` → スキルの references が最新か(Linux)→ 成果物 `MPS-<os>.zip`
  (MPS.vst3、README、`mps-synth-plugin-skill.zip`。mps_render はデバッグ用なので入れない)。別のジョブで `claude plugin validate` を通す。
- `v*` のタグを push するか、Actions の画面で release ワークフローを手で実行して tag(例: `v0.1.0`)を入れると、同じビルドの zip で GitHub Release を作る(`.github/workflows/release.yml`。tag を空にすると zip の確認だけ。replace をオンにすると、同じタグの Release とタグを消して今の main で作り直す)。
  バージョンは `core/engine.h` の `kVersion`、`CMakeLists.txt` の `project(VERSION)`、`plugin/.claude-plugin/plugin.json` の `version` の 3 か所を
  そろえる(`tools/check_versions.py` が CI で検査する)。Claude Code のプラグインは `version` が上がったときに更新が配られる。
