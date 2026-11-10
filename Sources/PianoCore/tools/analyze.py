"""Measure piano notes: inharmonicity, per-partial level and two-stage decay, beat rate, attack residual.

usage: analyze.py <aiff...> -> writes analysis/<dyn>.<note>.json
"""
import sys, json, os, re
import numpy as np, soundfile as sf
from scipy.signal import butter, sosfiltfilt
from scipy.optimize import least_squares

SR = 44100
NAMES = {'C': 0, 'Db': 1, 'D': 2, 'Eb': 3, 'E': 4, 'F': 5, 'Gb': 6, 'G': 7, 'Ab': 8, 'A': 9, 'Bb': 10, 'B': 11}


def midi_of(name):
    m = re.match(r'([A-G]b?)(\d)', name)
    return 12 * (int(m.group(2)) + 1) + NAMES[m.group(1)]


def load(path):
    x, sr = sf.read(path, always_2d=True)
    assert sr == SR
    x = x.mean(axis=1)
    env = np.sqrt(np.convolve(x * x, np.ones(44) / 44, 'same'))
    on = int(np.argmax(env > 0.1 * env.max()))
    on = max(0, on - int(0.004 * SR))
    return x[on:]


def peak_near(spec, freqs, f, half):
    lo, hi = np.searchsorted(freqs, [f - half, f + half])
    if hi <= lo + 2:
        return None, None
    i = lo + int(np.argmax(spec[lo:hi]))
    if i <= 0 or i >= len(spec) - 1:
        return None, None
    a, b, c = np.log(spec[i - 1:i + 2] + 1e-20)
    p = 0.5 * (a - c) / (a - 2 * b + c)
    df = freqs[1] - freqs[0]
    return freqs[i] + p * df, b - 0.25 * (a - c) * p


def find_partials(x, f0n, fmax):
    t0, t1 = int(0.05 * SR), int(min(len(x), (0.05 + (3.0 if f0n < 120 else 1.5)) * SR))
    seg = x[t0:t1] * np.hanning(t1 - t0)
    N = 1 << int(np.ceil(np.log2(len(seg) * 4)))
    spec = np.abs(np.fft.rfft(seg, N))
    freqs = np.fft.rfftfreq(N, 1 / SR)
    floor = np.median(spec) + 1e-12
    midi = 69 + 12 * np.log2(f0n / 440)
    B = 10 ** (-3.9 + max(0.0, midi - 50) * 0.035)
    f0 = f0n
    found = {}
    f1, lv = peak_near(spec, freqs, f0n, 0.04 * f0n)
    if f1 is not None and lv > np.log(floor) + np.log(30):
        f0 = f1 / np.sqrt(1 + B)
    nmax = int(fmax / f0n)
    for n in range(1, nmax + 1):
        pred = n * f0 * np.sqrt(1 + B * n * n)
        if pred > fmax:
            break
        f, lv = peak_near(spec, freqs, pred, (0.04 if n == 1 else 0.15) * f0)
        if f is None or lv < np.log(floor) + np.log(4):
            continue
        found[n] = f
        if len(found) >= 4:
            ns = np.array(sorted(found))
            fs = np.array([found[k] for k in ns])
            # (f_n/n)^2 = f0^2 + f0^2 B n^2
            A = np.vstack([np.ones_like(ns), ns.astype(float) ** 2]).T
            sol, *_ = np.linalg.lstsq(A, (fs / ns) ** 2, rcond=None)
            if sol[0] > 0 and 1e-5 < sol[1] / sol[0] < 5e-2 and abs(np.sqrt(sol[0]) / f0n - 1) < 0.07:
                f0 = np.sqrt(sol[0])
                B = sol[1] / sol[0]
    return f0, B, found


def envelope(x, f, bw):
    t = np.arange(len(x)) / SR
    z = x * np.exp(-2j * np.pi * f * t)
    sos = butter(2, bw, fs=SR, output='sos')
    z = sosfiltfilt(sos, z.real) + 1j * sosfiltfilt(sos, z.imag)
    hop = SR // 200
    return 2 * np.abs(z[::hop]), hop / SR


def fit_decay(env, dt):
    db = 20 * np.log10(env + 1e-12)
    ipk = int(np.argmax(db[:int(0.3 / dt)]))
    pk = db[ipk]
    floor = np.percentile(db[-int(1 / dt):], 50) if len(db) > 2 / dt else pk - 80
    end = len(db)
    above = np.where(db[ipk:] < max(floor + 8, pk - 70))[0]
    if len(above):
        end = ipk + above[0]
    t = np.arange(end - ipk) * dt
    y = db[ipk:end]
    if len(t) < 10:
        return None
    w = 1 + 4 * np.exp(-t / 0.3)  # the attack matters most to the ear; the long tail has many more samples

    def model(p):
        a1, k1, a2, k2 = p
        return 20 * np.log10(np.exp(a1 - k1 * t) + np.exp(a2 - k2 * t) + 1e-12)

    k_early = max(0.05, -(y[min(len(y) - 1, int(0.3 / dt))] - y[0]) / 0.3 / 8.686)
    k_late = max(0.01, -(y[-1] - y[len(y) // 2]) / max(t[-1] - t[len(t) // 2], 0.1) / 8.686)
    l0 = pk / 8.686
    best = None
    for frac in (0.03, 0.1, 0.3):
        p0 = [l0, min(59, max(k_early, k_late * 1.5)), l0 + np.log(frac), min(59, k_late)]
        r = least_squares(lambda p: np.append((model(p) - y) * w, 6 * (model(p)[0] - pk)), p0,
                          bounds=([l0 - 5, 0.0, l0 - 12, 0.0], [l0 + 3, 60, l0 + 3, 60]))
        if best is None or r.cost < best.cost:
            best = r
    a1, k1, a2, k2 = best.x
    if k2 > k1:
        a1, k1, a2, k2 = a2, k2, a1, k1
    resid = y - model(best.x)
    # beat rate: strongest ripple between 0.15 and 6 Hz in the fit residual
    beat = 0.0
    if t[-1] > 1.5:
        r = resid - resid.mean()
        F = np.abs(np.fft.rfft(r * np.hanning(len(r)), 8 * len(r)))
        fr = np.fft.rfftfreq(8 * len(r), dt)
        m = (fr > 0.15) & (fr < 6)
        if m.any():
            beat = float(fr[m][np.argmax(F[m])])
    return dict(a1=a1 * 8.686, k1=k1, a2=a2 * 8.686, k2=k2, peak=pk, dur=float(t[-1]), floor=float(floor),
                rms=float(np.sqrt(np.mean(resid ** 2))), beat=beat, tpk=ipk * dt)


def off_series(x, f0, B, bw, top_db=35, fmax=4000, most=8):
    """Peaks off the note's harmonic series (longitudinal modes, phantom partials), with their decays."""
    from scipy.signal import find_peaks
    harm = np.array([n * f0 * np.sqrt(1 + B * n * n) for n in range(1, 400)])
    seg = x[int(0.05 * SR):int(0.05 * SR) + 2 * SR]
    seg = seg * np.hanning(len(seg))
    N = 1 << 19
    S = 20 * np.log10(np.abs(np.fft.rfft(seg, N)) + 1e-9)
    f = np.fft.rfftfreq(N, 1 / SR)
    m = f < fmax
    top = S[m].max()
    pk, _ = find_peaks(S[m], prominence=8)
    cands = [i for i in pk if S[i] > top - top_db and np.min(np.abs(harm - f[i])) > max(3, 0.12 * f0)]
    cands.sort(key=lambda i: -S[i])
    out = []
    for i in cands[:most]:
        env, dt = envelope(x, f[i], min(bw, 0.1 * f0, 8.0))
        d = fit_decay(env, dt)
        if d is None:
            continue
        d.update(f=float(f[i]), rel=float(S[i] - top))
        out.append(d)
    return out


def residual_bands(x, partial_freqs):
    """Level of what lies between partials in the first 60 ms, per third-octave band, and its decay."""
    out = []
    centres = 1000 * 2 ** (np.arange(-17, 14) / 3)
    for (a, b) in ((0.0, 0.03), (0.03, 0.08), (0.08, 0.2)):
        seg = x[int(a * SR):int(b * SR)]
        N = 8192
        spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), N)) ** 2
        freqs = np.fft.rfftfreq(N, 1 / SR)
        mask = np.ones_like(freqs, bool)
        res = SR / len(seg) * 2.5
        for f in partial_freqs:
            mask &= np.abs(freqs - f) > res
        row = []
        for c in centres:
            m = mask & (freqs >= c * 2 ** -(1 / 6)) & (freqs < c * 2 ** (1 / 6))
            row.append(float(10 * np.log10(np.mean(spec[m]) / len(seg) + 1e-20)) if m.sum() >= 2 else None)
        out.append(row)
    return dict(centres=list(centres), windows=[[0, .03], [.03, .08], [.08, .2]], db=out)


def third_octave(x, t0, t1):
    seg = x[int(t0 * SR):int(t1 * SR)]
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    freqs = np.fft.rfftfreq(len(seg), 1 / SR)
    centres = 1000 * 2 ** (np.arange(-17, 14) / 3)
    return [float(10 * np.log10(spec[(freqs >= c * 2 ** -(1 / 6)) & (freqs < c * 2 ** (1 / 6))].sum() / len(seg) + 1e-20)) for c in centres]


def analyze(path, outdir):
    base = os.path.basename(path)
    _, dyn, note, _ = base.split('.')
    midi = midi_of(note)
    x = load(path)
    f0n = 440 * 2 ** ((midi - 69) / 12)
    fmax = min(16000, 120 * f0n)
    f0, B, found = find_partials(x, f0n, fmax)
    bw = min(0.45 * f0, 40.0)
    parts = []
    for n, f in sorted(found.items()):
        env, dt = envelope(x, f, bw)
        d = fit_decay(env, dt)
        if d is None:
            continue
        d.update(n=n, f=float(f))
        parts.append(d)
    extras = off_series(x, f0, B, bw) if midi < 60 else []
    rec = dict(file=base, extras=extras, dyn=dyn, note=note, midi=midi, f0=float(f0), f0_nominal=f0n, B=float(B),
               cents=float(1200 * np.log2(f0 / f0n)), partials=parts,
               residual=residual_bands(x, [p['f'] for p in parts]),
               spectrum=dict(early=third_octave(x, 0, 0.2), mid=third_octave(x, 0.2, 1.2),
                             late=third_octave(x, 1.2, 4.0)),
               peak=float(np.abs(x[:SR]).max()), noise=third_octave(x, len(x) / SR - 1.5, len(x) / SR))
    json.dump(rec, open(os.path.join(outdir, f'{dyn}.{note}.json'), 'w'), indent=1)
    return rec


if __name__ == '__main__':
    outdir = 'analysis'
    os.makedirs(outdir, exist_ok=True)
    for p in sys.argv[1:]:
        r = analyze(p, outdir)
        ps = r['partials']
        print(f"{r['file']:24s} f0={r['f0']:8.2f} ({r['cents']:+.1f}c) B={r['B']:.2e} n={len(ps)} "
              f"p1: k1={ps[0]['k1']:.2f} k2={ps[0]['k2']:.2f} beat={ps[0]['beat']:.2f}" if ps else r['file'])
