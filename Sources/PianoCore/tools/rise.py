"""How each partial rises in the recording: level at a few ms against its peak in the first 120 ms, by region."""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, analyze as A
from scipy.signal import butter, sosfiltfilt
times = [0.002, 0.004, 0.007, 0.01, 0.015, 0.02, 0.03, 0.05]
regions = [(0, 500), (500, 1500), (1500, 4000), (4000, 12000)]
def env(x, f, bw):
    t = np.arange(len(x)) / A.SR; z = x * np.exp(-2j * np.pi * f * t)
    sos = butter(2, bw, fs=A.SR, output='sos'); z = sosfiltfilt(sos, z.real) + 1j * sosfiltfilt(sos, z.imag)
    return 2 * np.abs(z)
for dyn in sys.argv[1:] or ['ff', 'mf']:
    tot = {g: {t: [] for t in times} for g in regions}; tpk = {g: [] for g in regions}
    for note in ['C2', 'Gb2', 'C3', 'Gb3', 'C4', 'Gb4', 'C5', 'Gb5', 'C6', 'Gb6', 'C7']:
        try: r = json.load(open(f'analysis/{dyn}.{note}.json'))
        except FileNotFoundError: continue
        x = A.load(f'ref/aiff/Piano.{dyn}.{note}.aiff')[:A.SR // 2]
        s0 = int(np.argmax(np.abs(x) > 0.05 * np.abs(x).max())); x = x[max(0, s0 - 40):]
        bw = min(0.45 * r['f0'], 400)
        for p in r['partials'][:40]:
            e = env(x, p['f'], bw); pk = e[:int(0.12 * A.SR)].max()
            if 20 * np.log10(pk + 1e-9) < -75: continue
            for g in regions:
                if g[0] <= p['f'] < g[1]:
                    for t in times: tot[g][t].append(20 * np.log10(e[int(t * A.SR)] / pk + 1e-9))
                    tpk[g].append(np.argmax(e[:int(0.12 * A.SR)]) / A.SR * 1000)
    print(dyn, 'dB below each partial\'s peak at', ' '.join(f'{t*1000:g}' for t in times), 'ms; median time of peak')
    for g in regions:
        if tot[g][times[0]]:
            print(f'  {g[0]:5d}-{g[1]:5d} Hz', ' '.join(f'{np.median(tot[g][t]):6.1f}' for t in times), f'  peak {np.median(tpk[g]):.0f} ms  (n={len(tpk[g])})')
