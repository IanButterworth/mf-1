"""Whole-keyboard comparison: third-octave error per window, over bands where the recording is clear of its noise."""
import sys, subprocess, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, analyze as A
cent = 1000 * 2 ** (np.arange(-17, 14) / 3)
notes = sys.argv[2:] if len(sys.argv) > 2 else ['A0','C1','Gb1','C2','Gb2','C3','Gb3','C4','Gb4','C5','Gb5','C6','Gb6','C7','C8']
dyn = sys.argv[1]; vel = {'ff': 110, 'mf': 64, 'pp': 24}[dyn]
allerr = []
for note in notes:
    path = f'ref/aiff/Piano.{dyn}.{note}.aiff'
    if not os.path.exists(path): continue
    m = A.midi_of(note)
    subprocess.run(['pc/render', 'out/s.wav', '7', str(m), str(vel), '0', '7'], capture_output=True, check=True)
    ref = A.load(path); syn = A.load('out/s.wav')
    # the recording's floor: its quietest second
    lv = [np.mean(ref[i:i + A.SR] ** 2) for i in range(0, len(ref) - A.SR, A.SR // 2)]
    i0 = int(np.argmin(lv)) * (A.SR // 2)
    floor = np.array(A.third_octave(ref, i0 / A.SR, i0 / A.SR + 1))
    f0 = 440 * 2 ** ((m - 69) / 12)
    out = []
    for (t0, t1) in ((0, .1), (.1, .5), (.5, 2), (2, 5)):
        r = np.array(A.third_octave(ref, t0, t1)); s = np.array(A.third_octave(syn, t0, t1))
        ok = (r > floor + 10) & (cent > 0.8 * f0) & (cent < 12000)
        e = s[ok] - r[ok]
        # energy-weighted: louder bands matter more
        wgt = 10 ** (r[ok] / 20); wgt /= wgt.sum() if len(wgt) else 1
        out.append((float((e * wgt).sum()) if len(e) else np.nan, float(np.sqrt(np.mean(e ** 2))) if len(e) else np.nan, int(ok.sum())))
        allerr += list(e)
    def lvl(x, t): return 10 * np.log10(np.mean(x[int(t * A.SR):int(t * A.SR) + 2205] ** 2) + 1e-20)
    lv = ' '.join(f'{lvl(syn, t) - lvl(ref, t):+5.1f}' for t in (0.01, 0.1, 0.5, 1, 2, 4))
    print(f'{note:4s} ' + ' | '.join(f'bias {b:+5.1f} rms {r:4.1f} ({n:2d})' for b, r, n in out) + f' | level syn-ref {lv}')
print(f'{dyn}: overall rms {np.sqrt(np.mean(np.array(allerr) ** 2)):.1f} dB over {len(allerr)} band-windows')
