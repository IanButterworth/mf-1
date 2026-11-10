"""The first 200 ms of each partial at a time resolution of a few ms, engine minus recording, by region."""
import sys, os, json, subprocess, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, analyze as A
from scipy.signal import butter, sosfiltfilt
notes = sys.argv[1:] or ['C3', 'Gb3', 'C4', 'Gb4', 'C5', 'Gb5', 'C6', 'Gb6', 'C7']
times = [0.004, 0.01, 0.02, 0.04, 0.08, 0.15]
regions = [(0, 1500), (1500, 4000), (4000, 12000)]
tot = {r: {t: [] for t in times} for r in regions}
def env(x, f, bw):
    t = np.arange(len(x)) / A.SR; z = x * np.exp(-2j * np.pi * f * t)
    sos = butter(2, bw, fs=A.SR, output='sos'); z = sosfiltfilt(sos, z.real) + 1j * sosfiltfilt(sos, z.imag)
    return 2 * np.abs(z)
for note in notes:
    m = A.midi_of(note)
    subprocess.run(['pc/render', 'out/o.wav', '1', str(m), '110', '0', '1'], capture_output=True, check=True)
    ref = A.load(f'ref/aiff/Piano.ff.{note}.aiff')[:A.SR]; syn = A.load('out/o.wav')[:A.SR]
    # line the two onsets up on their first 1% of peak
    def start(x): return int(np.argmax(np.abs(x) > 0.05 * np.abs(x).max()))
    ref = ref[max(0, start(ref) - 40):]; syn = syn[max(0, start(syn) - 40):]
    r = json.load(open(f'analysis/ff.{note}.json')); f0 = r['f0']
    bw = min(0.45 * f0, 400)
    row = {g: {t: [] for t in times} for g in regions}
    for p in r['partials'][:40]:
        f = p['f']; er = env(ref, f, bw); es = env(syn, f, bw)
        for g in regions:
            if g[0] <= f < g[1]:
                for t in times:
                    a = 20 * np.log10(er[int(t * A.SR)] + 1e-9); b = 20 * np.log10(es[int(t * A.SR)] + 1e-9)
                    row[g][t].append(b - a); tot[g][t].append(b - a)
    print(f'{note:4s}', ' | '.join(f'<{g[1]/1000:g}k ' + ' '.join(f'{np.median(row[g][t]):+5.1f}' if row[g][t] else '  n/a' for t in times) for g in regions))
print('ALL ', ' | '.join(f'<{g[1]/1000:g}k ' + ' '.join(f'{np.median(tot[g][t]):+5.1f}' if tot[g][t] else '  n/a' for t in times) for g in regions))
