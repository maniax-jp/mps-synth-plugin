# 制御サーバーのプロトコル

MPS Synth / MPS FX の各インスタンスは `127.0.0.1` の TCP ポート(47800 から空いている番号)で待ち受け、
**1 行 1 JSON** の要求に 1 行 1 JSON で答える。`scripts/mps_ctl.py` はこれを包んだもの(標準ライブラリだけで動く)。

## インスタンスの見つけ方
起動中のインスタンスはレジストリに 1 つずつ JSON を書く。
- macOS / Linux: `~/.mps-plugins/instances/*.json`
- Windows: `%LOCALAPPDATA%\mps-plugins\instances\*.json`

```json
{"plugin": "MPS Synth", "plugin_id": "synth", "port": 47800, "host": "127.0.0.1", "pid": 5201, "track": "Bass", "started": 1792466808}
```
`track` は DAW のトラック名(DAW が VST3 のチャンネル情報を渡す場合。REAPER は渡す)。応答しないファイルは古いので無視してよい
(`mps_ctl.py list` は消す)。

## 要求と応答
```json
{"id": 1, "cmd": "set", "params": {"cutoff": 1200, "filter_type": "lp12"}}
{"id": 1, "ok": true, "result": {"cutoff": 1200.0, "filter_type": "lp12"}}
{"id": 2, "cmd": "set", "params": {"cutof": 1}}
{"id": 2, "ok": false, "error": "unknown param: cutof"}
```
一部のキーだけが間違っている `set` は、正しいキーを反映して `warnings` に間違いを返す。

| cmd | 引数 | 内容 |
|---|---|---|
| `help` | | コマンド一覧 |
| `info` | | プラグイン名・バージョン・ポート・トラック名・サンプルレート・テンポ・再生中か・プリセット名・コマンド数 |
| `list_params` | | 全パラメータ(key, name, unit, scale, min, max, default, value, display, labels, help) |
| `get` | `keys?: [key]`, `changed_only?: bool` | 現在値(プレーン値。enum / bool はラベル) |
| `set` | `params: {key: 値}`, `normalized?: bool` | 値を変えてホストに通知する(DAW の表示・オートメーションの記録・プロジェクトの保存内容が変わる)。プリセット名の末尾に ` *` が付く |
| `list_presets` | | ファクトリープリセット(name, desc) |
| `load_preset` | `name`, `params?: {key: 値}` | プリセットを読み込み、`params` で上書き |
| `get_state` | | 状態 JSON `{"format":"mps-state","version":1,"plugin":"synth","preset":"...","params":{...}}` |
| `set_state` | `state` | 状態を読み込む(書いていないキーは既定値) |
| `reset` | | 全パラメータを既定値に |
| `play` | `note`, `velocity?: 0..1`, `duration?: 秒` | その場で鳴らす(DAW がそのトラックを処理していれば出力に出る) |
| `note_on` / `note_off` | `note`, `velocity?` | 押しっぱなし / 離す |
| `panic` | | 全ノートオフ |
| `render` | 下を参照 | 今の設定で WAV を書き出す(プラグインの中で別に合成する。DAW の再生は止めない) |

### render の引数
| 引数 | 内容 |
|---|---|
| `out` | 書き出す WAV の絶対パス(24bit、`bits: 32` で 32bit float) |
| `notes` | `[[t, dur, note, vel], ...]` か `[{"t":0,"dur":0.5,"note":41,"vel":1}]`。vel は 0〜1(1 より大きければ /127) |
| `unit` | `seconds`(既定)か `beats`(`bpm` で秒に直す) |
| `bpm` / `length` / `sample_rate` | 既定 120 / 最後のノート + 2 秒 / 48000 |
| `preset` / `params` | 書き出しのときだけ使うプリセット / 上書き(インスタンスの設定は変わらない) |
| `automation` | `[[t, {key: 値}], ...]` 途中で値を変える(テープストップは `{"tapestop": "on"}`) |
| `input` | エフェクト(MPS FX)に通す WAV |

## 決まり
- 値は**プレーン値**(Hz・秒・半音・セント・dB)。`normalized: true` なら 0〜1。
- 127.0.0.1 だけで待ち受ける(ほかのマシンからは届かない)。`render` は指定したパスに書き込む。
- 1 インスタンスにつき同時に 1 接続。30 秒何も送らない接続は切られる。
- 環境変数: `MPS_CTL_PORT_BASE`(既定 47800)、`MPS_CTL_DISABLE=1`(サーバーを開かない)、`MPS_CTL_REGISTRY`(レジストリの場所)。
  DAW を起動する環境で設定する。
