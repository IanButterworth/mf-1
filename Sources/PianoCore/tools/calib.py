"""Closed-loop fit of each partial's two levels: render each measured key at ff, compare every partial's envelope with
the recording's, early and late, where the recording is clear of its noise; adjust; repeat. Writes calib.json, which
gen_tables.py applies."""
import sys, os, json, subprocess, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, analyze as A
MIDIS = list(range(21, 109, 3)); NAMES = ['C','Db','D','Eb','E','F','Gb','G','Ab','A','Bb','B']
name = lambda m: f'{NAMES[m % 12]}{m // 12 - 1}'
rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 3
cal = json.load(open('calib.json')) if os.path.exists('calib.json') else {}
def table_notes():
    rows = re.findall(r'\{(\d+), (\d+), (\d+), (\d+), ([-\d.e+]+)f, ([-\d.]+)f, .*?/\* (\w+) \*/', open('pc/piano_tables.inc').read())
    return {r[6]: (float(r[4]), float(r[5])) for r in rows}
for rnd in range(rounds):
    G = 0.6 * 0.65 ** rnd
    subprocess.run([sys.executable, '-I', 'gen_tables.py'], check=True, capture_output=True)
    subprocess.run(['cp', 'piano_tables.inc', 'pc/'], check=True)
    subprocess.run(['cc', '-O2', '-ffp-contract=off', '-o', 'pc/render', 'pc/render.c', 'pc/piano.c'], check=True)
    tn = table_notes(); E = []; L = []; M = []
    for m in MIDIS:
        nm = name(m); B, cents = tn[nm]
        subprocess.run(['pc/render', 'out/c.wav', '4', str(m), '110', '0', '4'], capture_output=True, check=True)
        ref = A.load(f'ref/aiff/Piano.ff.{nm}.aiff')[:4 * A.SR]; syn = A.load('out/c.wav')[:4 * A.SR]
        r = json.load(open(f'analysis/ff.{nm}.json'))
        f0s = 440 * 2 ** ((m - 69) / 12 + cents / 1200)
        c = cal.setdefault(nm, {})
        for p in r['partials']:
            n = p['n']; fr = p['f']; fs = n * f0s * np.sqrt(1 + B * n * n)
            if fs > 16000: continue
            bw = min(0.45 * r['f0'], 40); bwa = min(0.45 * r['f0'], 150)
            er, dt = A.envelope(ref, fr, bw); es, _ = A.envelope(syn, fs, bw)
            nr, _ = A.envelope(ref, fr + 0.5 * r['f0'], bw)   # between partials: the recording's floor there
            era, dta = A.envelope(ref, fr, bwa); esa, _ = A.envelope(syn, fs, bwa)
            nra, _ = A.envelope(ref, fr + 0.5 * r['f0'], bwa)
            def db(e, t0, t1, d=dt): return 20 * np.log10(np.median(e[int(t0 / d):int(t1 / d)]) + 1e-9)
            win_a, win_m, win_l = (0.008, 0.04), (0.15, 0.5), (1.2, 3.0)
            ea = db(esa, *win_a, d=dta) - db(era, *win_a, d=dta) if db(era, *win_a, d=dta) > db(nra, *win_a, d=dta) + 10 else None
            em = db(es, *win_m) - db(er, *win_m) if db(er, *win_m) > db(nr, *win_m) + 10 else None
            el = db(es, *win_l) - db(er, *win_l) if db(er, *win_l) > db(nr, *win_l) + 10 else None
            k = c.setdefault(str(n), [0.0, 0.0, 0.0])
            while len(k) < 3: k.append(0.0)
            d0 = 0.0
            if ea is not None:
                old = k[0]
                k[0] = float(np.clip(k[0] - G * ea, -20, 15)); E.append(ea)
                d0 = k[0] - old
                if em is not None:
                    # the fast stage's rate: too loud by 0.3 s against the attack means it decays too slowly
                    k[2] = float(np.clip(k[2] + G * (em - ea) / (8.686 * 0.3), -30, 60)); M.append(em)
            if el is not None:
                # late on only the slow part sounds, at level + d2: move it by the error, net of the level's own move
                k[1] = float(np.clip(k[1] - G * el - d0, -25, 25))
                L.append(el)
    q = lambda v: f'{np.median(v):+.1f} median, {np.sqrt(np.mean(np.square(v))):.1f} rms ({len(v)})'
    print(f'round {rnd}: attack {q(E)}; 0.15-0.5 s {q(M)}; 1.2-3 s {q(L)}', flush=True)
    json.dump(cal, open('calib.json', 'w'))
