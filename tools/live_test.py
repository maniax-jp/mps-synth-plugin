#!/usr/bin/env python3
"""MPS.vst3 をテスト用ホスト(mps_testhost)で実時間に回し、AI 接続経由の操作を確かめる。標準ライブラリだけで動く。

  python live_test.py BUILD_DIR

確かめること: レジストリにトラック名つきで載る / load_preset・set がホストへ通知される / play で音が出る /
render で WAV が書ける / 状態 JSON に反映される / 終了でレジストリが消える。
同じトラックに MPS FX を 2 つ動かし、名前を付けて呼び分けられること(重複した名前は断る、名前は状態に保存される)も確かめる。
"""
import os, subprocess, sys, tempfile, time, wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'plugin', 'skills', 'mps-synth-plugin', 'scripts'))
import mps_ctl  # noqa: E402


def find(build, names):
    for root, dirs, files in os.walk(build):
        for n in names:
            if n in files or n in dirs:
                return os.path.join(root, n)
    raise SystemExit(f'not found in {build}: {names}')


def peak_after(path, t0):
    with wave.open(path) as w:
        sr, n, sw = w.getframerate(), w.getnframes(), w.getsampwidth()
        raw = w.readframes(n)
    step = sw * 2
    peak = 0
    for i in range(int(t0 * sr) * step, len(raw) - step, step * 16):
        v = int.from_bytes(raw[i:i + sw], 'little', signed=True)
        peak = max(peak, abs(v))
    return peak / float(1 << (8 * sw - 1))


def main():
    build = sys.argv[1]
    host = find(build, ['mps_testhost', 'mps_testhost.exe'])
    vst3 = find(build, ['MPS.vst3'])
    tmp = tempfile.mkdtemp(prefix='mps_live_')
    os.environ['MPS_CTL_REGISTRY'] = os.path.join(tmp, 'reg')
    out = os.path.join(tmp, 'live.wav')
    log = open(os.path.join(tmp, 'host.log'), 'w')
    p = subprocess.Popen([host, vst3, 'MPS Synth', '5', out, '--track', 'Live Test'], stdout=log, stderr=subprocess.STDOUT)
    fails = []

    def check(cond, msg):
        print(('PASS ' if cond else 'FAIL ') + msg)
        if not cond:
            fails.append(msg)

    for _ in range(50):
        if mps_ctl.instances():
            break
        time.sleep(0.1)
    inst = mps_ctl.instances()
    check(len(inst) == 1 and inst[0].get('track') == 'Live Test', f'registry lists the instance with its track name: {inst}')
    c = mps_ctl.connect(track='live')
    r = c.load_preset('Wavetable Bass', cutoff=900)
    check(r.get('cutoff') == 900 and r.get('osc_b_wave') == 'monster', 'load_preset + override')
    c.set(drive=5.0, voice_mode='mono')
    check(c.get('drive', 'voice_mode') == {'drive': 5.0, 'voice_mode': 'mono'}, 'set / get')
    check(c.info().get('preset') == 'Wavetable Bass *', 'preset name marks manual edits')
    c.play(41, 1.0, 0.3)
    wav = os.path.join(tmp, 'render.wav')
    rr = c.render(wav, [[0, 0.25, 41, 1.0]], bpm=160)
    check(os.path.exists(wav) and rr['peak'] > 0.1, f'render writes a wav (peak {rr["peak"]:.2f})')
    try:
        c.set(nope=1)
        check(False, 'unknown key is reported')
    except mps_ctl.MpsError:
        check(True, 'unknown key is reported')
    c.close()
    p.wait(timeout=30)
    log.close()
    text = open(os.path.join(tmp, 'host.log')).read()
    check('host: param' in text, 'host received output parameter changes')
    check('"drive":5.0' in text and '"voice_mode":"mono"' in text, 'state JSON contains the edits')
    check('editor: yes' in text, 'controller creates the status editor')
    check(peak_after(out, 0.0) > 0.1, 'play produced sound in the host output')
    check(not mps_ctl.instances(clean=False), 'registry file removed on exit')

    # ---- 同じトラックに同じプラグインを 2 つ: 名前で呼び分ける
    ctl = [sys.executable, os.path.join(HERE, '..', 'plugin', 'skills', 'mps-synth-plugin', 'scripts', 'mps_ctl.py')]
    procs, logs = [], []
    for k in range(2):
        lp = os.path.join(tmp, f'fx{k}.log')
        logs.append(lp)
        procs.append(subprocess.Popen([host, vst3, 'MPS FX', '4', os.path.join(tmp, f'fx{k}.wav'), '--track', 'Bus'],
                                      stdout=open(lp, 'w'), stderr=subprocess.STDOUT))
    for _ in range(50):
        if len(mps_ctl.instances()) == 2:
            break
        time.sleep(0.1)
    try:
        mps_ctl.connect(track='Bus', plugin='fx')
        check(False, 'two instances on one track are reported as ambiguous')
    except mps_ctl.MpsError as e:
        check('--name' in str(e) and '--port' in str(e), 'ambiguity error suggests --name / --port')
    ports = sorted(i['port'] for i in mps_ctl.instances())
    r1 = subprocess.run(ctl + ['--port', str(ports[0]), 'rename', 'pump'], capture_output=True, text=True)
    r2 = subprocess.run(ctl + ['--port', str(ports[1]), 'rename', 'pump'], capture_output=True, text=True)
    r3 = subprocess.run(ctl + ['--port', str(ports[1]), 'rename', 'tapestop'], capture_output=True, text=True)
    check(r1.returncode == 0 and r2.returncode != 0 and r3.returncode == 0, 'rename refuses a duplicate name')
    names = {i['port']: i.get('name') for i in mps_ctl.instances()}
    check(names == {ports[0]: 'pump', ports[1]: 'tapestop'}, f'registry carries the names: {names}')
    with mps_ctl.connect(name='PUMP') as c1:
        check(c1.port == ports[0] and c1.load_preset('Pump 1/4').get('pump_depth') == 1.0, 'connect by name (case-insensitive)')
    for pr in procs:
        pr.wait(timeout=30)
    states = ''.join(open(lp).read() for lp in logs)
    check('"name":"pump"' in states and '"name":"tapestop"' in states, 'names are saved in the state')
    print('logs:', tmp)
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
