#!/usr/bin/env python3
"""MPS Synth / MPS FX を外部(AI・スクリプト)から操作するクライアント。標準ライブラリだけで動く。

DAW に挿した各インスタンスは 127.0.0.1 の TCP ポート(47800〜)で 1 行 1 JSON の制御サーバーを開き、
~/.mps-plugins/instances/*.json(Windows は %LOCALAPPDATA%\\mps-plugins\\instances)に自分のポートとトラック名を書く。

CLI:
  mps_ctl.py list                                   # 起動中のインスタンス(ポート・プラグイン・トラック名)
  mps_ctl.py --track Bass info
  mps_ctl.py --track Bass presets
  mps_ctl.py --track Bass load "Slap Bass" cutoff=900
  mps_ctl.py --track Bass set filter_type=lp12 cutoff=1200 resonance=1.1
  mps_ctl.py --track Bass get cutoff resonance       # キーなしで全部、--changed で既定値から変えたものだけ
  mps_ctl.py --track Bass params                     # 全パラメータの定義(範囲・単位・選択肢・説明)
  mps_ctl.py --track Bass play 41 --velocity 1 --duration 0.3
  mps_ctl.py --track Bass render out.wav --notes '[[0,0.25,41,1]]' --bpm 160
  mps_ctl.py --track Bass state > bass.json ; mps_ctl.py --track Bass load-state bass.json
  mps_ctl.py --port 47801 raw '{"cmd":"help"}'
インスタンスの指定: --port N / --track 名前(部分一致)/ --plugin synth|fx(1 つしかないとき)。

Python:
  from mps_ctl import connect
  bass = connect(track='Bass')
  bass.load_preset('Slap Bass', cutoff=900)
  bass.set(drive=5.0, drive_type='diode')
  bass.play(41, duration=0.3)
"""
import argparse, glob, json, os, socket, sys


def registry_dir():
    if os.environ.get('MPS_CTL_REGISTRY'):
        return os.environ['MPS_CTL_REGISTRY']
    if os.name == 'nt':
        return os.path.join(os.environ.get('LOCALAPPDATA', '.'), 'mps-plugins', 'instances')
    return os.path.join(os.path.expanduser('~'), '.mps-plugins', 'instances')


def _alive(port, host='127.0.0.1'):
    try:
        with socket.create_connection((host, port), timeout=0.5):
            return True
    except OSError:
        return False


def instances(clean=True):
    """起動中のインスタンス一覧(応答しない古いレジストリは消す)。"""
    out = []
    for p in sorted(glob.glob(os.path.join(registry_dir(), '*.json'))):
        try:
            with open(p, encoding='utf-8') as f:
                j = json.load(f)
        except (OSError, ValueError):
            continue
        if _alive(j.get('port', 0)):
            j['registry_file'] = p
            out.append(j)
        elif clean:
            try:
                os.remove(p)
            except OSError:
                pass
    return out


class MpsError(RuntimeError):
    pass


class Client:
    def __init__(self, port, host='127.0.0.1', timeout=30.0):
        self.port, self.host = port, host
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.buf = b''
        self.next_id = 1

    def close(self):
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def request(self, cmd, **args):
        req = dict(args, cmd=cmd, id=self.next_id)
        self.next_id += 1
        self.sock.sendall((json.dumps(req, ensure_ascii=False) + '\n').encode('utf-8'))
        while b'\n' not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise MpsError('connection closed')
            self.buf += chunk
        line, self.buf = self.buf.split(b'\n', 1)
        resp = json.loads(line.decode('utf-8'))
        if not resp.get('ok'):
            raise MpsError(resp.get('error', 'error'))
        for w in resp.get('warnings', []):
            print(f'warning: {w}', file=sys.stderr)
        return resp.get('result')

    # ---- よく使う操作
    def info(self):
        return self.request('info')

    def params(self):
        return self.request('list_params')

    def get(self, *keys, changed_only=False):
        args = {'changed_only': changed_only}
        if keys:
            args['keys'] = list(keys)
        return self.request('get', **args)

    def set(self, **params):
        return self.request('set', params=params)

    def presets(self):
        return self.request('list_presets')

    def load_preset(self, name, **params):
        return self.request('load_preset', name=name, params=params) if params else self.request('load_preset', name=name)

    def state(self):
        return self.request('get_state')

    def load_state(self, state):
        return self.request('set_state', state=state)

    def reset(self):
        return self.request('reset')

    def play(self, note, velocity=1.0, duration=0.5):
        return self.request('play', note=note, velocity=velocity, duration=duration)

    def note_on(self, note, velocity=1.0):
        return self.request('note_on', note=note, velocity=velocity)

    def note_off(self, note):
        return self.request('note_off', note=note)

    def panic(self):
        return self.request('panic')

    def render(self, out, notes, **kw):
        """今の設定で WAV を書き出す(DAW の出力とは別に、プラグインの中で合成する)。"""
        return self.request('render', out=os.path.abspath(out), notes=notes, **kw)


def connect(port=None, track=None, plugin=None, host='127.0.0.1'):
    if port:
        return Client(int(port), host)
    found = instances()
    if track is not None:
        found = [i for i in found if track.lower() in str(i.get('track', '')).lower()]
    if plugin is not None:
        found = [i for i in found if i.get('plugin_id') == plugin or i.get('plugin') == plugin]
    if not found:
        raise MpsError('インスタンスが見つかりません(mps_ctl.py list で確認。DAW にプラグインを挿しているか)')
    if len(found) > 1:
        desc = ', '.join(f"{i['port']}:{i.get('plugin_id')}:{i.get('track', '')}" for i in found)
        raise MpsError(f'候補が複数あります({desc})。--port か --track で絞ってください')
    return Client(found[0]['port'], host)


def _parse_kv(items):
    out = {}
    for it in items:
        if '=' not in it:
            raise SystemExit(f'key=value の形で指定してください: {it}')
        k, v = it.split('=', 1)
        try:
            out[k] = float(v) if any(c in v for c in '.eE') else int(v)
        except ValueError:
            out[k] = v
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', type=int)
    ap.add_argument('--track')
    ap.add_argument('--plugin')
    ap.add_argument('command')
    ap.add_argument('args', nargs='*')
    ap.add_argument('--velocity', type=float, default=1.0)
    ap.add_argument('--duration', type=float, default=0.5)
    ap.add_argument('--changed', action='store_true')
    ap.add_argument('--notes')
    ap.add_argument('--bpm', type=float)
    ap.add_argument('--length', type=float)
    ap.add_argument('--unit', choices=['seconds', 'beats'])
    ap.add_argument('--input')
    a = ap.parse_args(argv)

    if a.command == 'list':
        for i in instances():
            print(f"{i['port']:6d}  {i.get('plugin', i.get('plugin_id')):10s}  track={i.get('track', '')!r}  pid={i.get('pid')}")
        return 0
    c = connect(a.port, a.track, a.plugin)
    cmd, args = a.command, a.args
    if cmd == 'info':
        r = c.info()
    elif cmd == 'params':
        r = c.params()
    elif cmd == 'presets':
        r = c.presets()
    elif cmd == 'get':
        r = c.get(*args, changed_only=a.changed)
    elif cmd == 'set':
        r = c.set(**_parse_kv(args))
    elif cmd == 'load':
        r = c.load_preset(args[0], **_parse_kv(args[1:]))
    elif cmd == 'state':
        r = c.state()
    elif cmd == 'load-state':
        with open(args[0], encoding='utf-8') as f:
            r = c.load_state(json.load(f))
    elif cmd == 'reset':
        r = c.reset()
    elif cmd == 'play':
        r = c.play(int(args[0]), a.velocity, a.duration)
    elif cmd == 'note-on':
        r = c.note_on(int(args[0]), a.velocity)
    elif cmd == 'note-off':
        r = c.note_off(int(args[0]))
    elif cmd == 'panic':
        r = c.panic()
    elif cmd == 'render':
        kw = {k: v for k, v in (('bpm', a.bpm), ('length', a.length), ('unit', a.unit), ('input', a.input)) if v is not None}
        r = c.render(args[0], json.loads(a.notes or '[]'), **kw)
    elif cmd == 'raw':
        req = json.loads(args[0])
        r = c.request(req.pop('cmd'), **req)
    else:
        raise SystemExit(f'unknown command: {cmd}')
    print(json.dumps(r, ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except MpsError as e:
        print(f'error: {e}', file=sys.stderr)
        sys.exit(1)
