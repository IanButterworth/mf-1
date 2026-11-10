import sys, subprocess, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, analyze as A
h = 0x9E3779B9; fm = []
for m in range(32):
    h = (h * 1664525 + 1013904223) & 0xffffffff
    j = ((h >> 8) / 16777216.0 - 0.5) * 0.16
    fm.append(55 * 2 ** ((m + j * 3) * 5.45 / 31))
fm = np.array(fm)
db = np.zeros(32)
notes = ['C3', 'Gb3', 'C4', 'Gb4', 'C5', 'Gb5', 'C6', 'C7']
for it in range(int(sys.argv[1]) if len(sys.argv) > 1 else 4):
    np.savetxt('board_db.txt', db)
    env = dict(os.environ, PN_BOARD_DB='board_db.txt')
    errs = {}; allw = []
    for note in notes:
        for dyn, vel in (('ff', 110), ('mf', 64)):
            m = A.midi_of(note)
            subprocess.run(['pc/render', 'out/b.wav', '1', str(m), str(vel), '0', '1'], env=env, capture_output=True, check=True)
            x = A.load('out/b.wav'); ref = json.load(open(f'analysis/{dyn}.{note}.json'))
            res = A.residual_bands(x, [p['f'] for p in ref['partials']])
            for wi in (0, 1, 2):
                for c, s, r in zip(res['centres'], res['db'][wi], ref['residual']['db'][wi]):
                    if s is None or r is None or c < 45 or c > 5000: continue
                    errs.setdefault(c, []).append(s - r); allw.append(s - r)
    cs = np.array(sorted(errs)); e = np.array([np.median(errs[c]) for c in cs])
    print(f'round {it}: median error {np.median(allw):+.1f} dB, rms {np.sqrt(np.mean(np.array(allw)**2)):.1f}; by band', ' '.join(f'{c:.0f}:{v:+.0f}' for c, v in zip(cs, e)))
    corr = np.interp(np.log(fm), np.log(cs), e)
    db -= 0.8 * corr
    db = np.clip(db, -30, 25)
print('final', ', '.join(f'{v:.1f}f' for v in db))
