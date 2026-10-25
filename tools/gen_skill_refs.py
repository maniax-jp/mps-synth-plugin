#!/usr/bin/env python3
"""スキルの references/parameters.md と references/presets.md を、プラグインの定義(mps_render の出力)から作り直す。

  python gen_skill_refs.py PATH/TO/mps_render

パラメータやプリセットを足したら実行してコミットする。CI は差分が出ないことを確かめる(--check)。
"""
import json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.path.join(HERE, '..', 'plugin', 'skills', 'mps-synth-plugin', 'references')


def run(exe, *args):
    return json.loads(subprocess.run([exe, *args], capture_output=True, text=True, check=True).stdout)


def fmt(v):
    if isinstance(v, float) and v.is_integer():
        return str(int(v))
    return f'{v:g}' if isinstance(v, float) else str(v)


def params_md(exe):
    out = ['# パラメータ一覧', '',
           'プラグインの定義から自動生成(`tools/gen_skill_refs.py`)。値はプレーン値で送る。',
           'enum はラベルでも番号でもよい。範囲外は丸められる。`scale` は DAW のつまみの曲がり方(cubic = 小さい値を細かく)。', '']
    for plugin, title in (('synth', 'MPS Synth'), ('fx', 'MPS FX')):
        out += [f'## {title}', '', '| key | 名前 | 範囲 | 初期値 | 単位 | 説明 |', '|---|---|---|---|---|---|']
        for p in run(exe, '--list-params', plugin):
            if p.get('labels'):
                rng = ' / '.join(p['labels'])
                default = p['labels'][int(round(p['default'] - p['min']))]
            elif p['scale'] == 'bool':
                rng, default = 'off / on', 'on' if p['default'] >= 0.5 else 'off'
            else:
                rng, default = f"{fmt(p['min'])} 〜 {fmt(p['max'])}", fmt(p['default'])
            help_ = p.get('help', '').replace('|', '\\|')
            out.append(f"| `{p['key']}` | {p['name']} | {rng} | {default} | {p['unit']} | {help_} |")
        out.append('')
    return '\n'.join(out)


def presets_md(exe):
    out = ['# ファクトリープリセット', '',
           'プラグインの定義から自動生成(`tools/gen_skill_refs.py`)。説明に書いた MIDI ノートは、その音色を鳴らす前提の音程。', '']
    for plugin, title in (('synth', 'MPS Synth'), ('fx', 'MPS FX')):
        out += [f'## {title}', '', '| プリセット | 内容 |', '|---|---|']
        for p in run(exe, '--list-presets', plugin):
            out.append(f"| {p['name']} | {p['desc']} |")
        out.append('')
    return '\n'.join(out)


def main():
    exe = sys.argv[1]
    check = '--check' in sys.argv
    stale = []
    for name, text in (('parameters.md', params_md(exe)), ('presets.md', presets_md(exe))):
        path = os.path.join(REF, name)
        old = open(path, encoding='utf-8').read() if os.path.exists(path) else ''
        if old != text:
            stale.append(name)
            if not check:
                with open(path, 'w', encoding='utf-8') as f:
                    f.write(text)
    if check and stale:
        print('references are out of date (run tools/gen_skill_refs.py):', ', '.join(stale))
        sys.exit(1)
    print('updated' if stale else 'up to date', ', '.join(stale))


if __name__ == '__main__':
    main()
