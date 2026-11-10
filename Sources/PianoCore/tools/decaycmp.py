"""Per-partial level at 0.2, 1, 2 and 3 s, engine minus recording, averaged by frequency region."""
import sys, os, json, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, analyze as A
notes = sys.argv[1:] or ['C2', 'Gb2', 'C3', 'Gb3', 'C4', 'Gb4', 'C5', 'Gb5', 'C6']
regions = [(0, 1000), (1000, 2500), (2500, 6000), (6000, 12000)]
times = [0.2, 1.0, 2.0, 3.0]
tot = {r: {t: [] for t in times} for r in regions}
for note in notes:
    m = A.midi_of(note)
    subprocess.run(['pc/render', 'out/d.wav', '4', str(m), '110', '0', '4'], capture_output=True, check=True)
    ref = A.load(f'ref/aiff/Piano.ff.{note}.aiff')[:4 * A.SR]; syn = A.load('out/d.wav')[:4 * A.SR]
    r = json.load(open(f'analysis/ff.{note}.json')); f0 = r['f0']
    floor_r = {}
    row = {reg: {t: [] for t in times} for reg in regions}
    for p in r['partials']:
        f = p['f']
        bw = min(0.45 * f0, 40)
        er, dt = A.envelope(ref, f, bw)
        # the engine's partial sits at its own frequency: find it near f
        es, _ = A.envelope(syn, f * 1.0, bw)
        for reg in regions:
            if reg[0] <= f < reg[1]:
                for t in times:
                    a = max(-100, 20 * np.log10(er[int(t / dt)] + 1e-9)); b = max(-100, 20 * np.log10(es[int(t / dt)] + 1e-9))
                    if a > -90 or b > -90:   # either is above the floor
                        row[reg][t].append(b - a); tot[reg][t].append(b - a)
    print(note, ' | '.join(f'{reg[0]//1000}-{reg[1]//1000}k: ' + ' '.join(f'{np.median(row[reg][t]):+5.1f}' if row[reg][t] else '  n/a' for t in times) for reg in regions))
print('ALL ', ' | '.join(f'{reg[0]//1000}-{reg[1]//1000}k: ' + ' '.join(f'{np.median(tot[reg][t]):+5.1f}' if tot[reg][t] else '  n/a' for t in times) for reg in regions))
