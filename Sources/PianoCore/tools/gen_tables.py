"""Turn the per-note measurements into the engine's tables (C include)."""
import json, glob, sys, os
import numpy as np

AN = sys.argv[1] if len(sys.argv) > 1 else 'analysis'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'piano_tables.inc'
MIDIS = list(range(21, 109, 3))
NAMES = ['C', 'Db', 'D', 'Eb', 'E', 'F', 'Gb', 'G', 'Ab', 'A', 'Bb', 'B']


def name(m):
    return f'{NAMES[m % 12]}{m // 12 - 1}'


def load(dyn, m):
    try:
        return json.load(open(f'{AN}/{dyn}.{name(m)}.json'))
    except FileNotFoundError:
        return None


def runmed(v, w=5):
    v = np.asarray(v, float)
    out = v.copy()
    h = w // 2
    for i in range(len(v)):
        seg = v[max(0, i - h):i + h + 1]
        seg = seg[np.isfinite(seg)]
        if len(seg):
            out[i] = np.median(seg)
    return out


def fill(v):
    """Interpolate NaNs by index; hold the ends."""
    v = np.asarray(v, float)
    ok = np.isfinite(v)
    if not ok.any():
        return v
    i = np.arange(len(v))
    return np.interp(i, i[ok], v[ok])


def robust_poly(x, y, deg, clip):
    x, y = np.asarray(x, float), np.asarray(y, float)
    keep = np.isfinite(y)
    for _ in range(3):
        c = np.polyfit(x[keep], y[keep], deg)
        r = y - np.polyval(c, x)
        keep = np.isfinite(y) & (np.abs(r) < clip)
    return c


CAL = json.load(open('calib.json')) if os.path.exists('calib.json') else {}
ff = {m: load('ff', m) for m in MIDIS}
mf = {m: load('mf', m) for m in MIDIS}
pp = {m: load('pp', m) for m in MIDIS}

# Inharmonicity and tuning as smooth curves over the keyboard: one string's B and the tuner's stretch are smooth,
# and the per-note scatter is mostly measurement noise.
ms = np.array(MIDIS, float)
logB = np.array([np.log10(ff[m]['B']) if ff[m]['partials'] and len(ff[m]['partials']) >= 4 else np.nan for m in MIDIS])
cB = robust_poly(ms, logB, 3, 0.35)
cents = np.array([ff[m]['cents'] if len(ff[m]['partials']) >= 2 else np.nan for m in MIDIS])
cC = robust_poly(ms, cents, 3, 12)

NPMAX = 96
rows = []
for m in MIDIS:
    r = ff[m]
    # A partial counts only when it stands well clear of the recording's noise; weaker "partials" are noise peaks.
    parts = {p['n']: p for p in r['partials'] if p['peak'] - p['floor'] > 12}
    nmax = max(parts) if parts else 1
    n_arr = np.arange(1, nmax + 1)
    lvl1 = np.full(nmax, np.nan); d2 = np.full(nmax, np.nan)
    k1 = np.full(nmax, np.nan); k2 = np.full(nmax, np.nan); beat = np.full(nmax, np.nan)
    peak = np.full(nmax, np.nan)
    for n, p in parts.items():
        i = n - 1
        peak[i] = p['peak']
        if p['rms'] > 6.5 or p['dur'] < 0.1:
            continue
        lvl1[i] = p['a1']
        k1[i] = p['k1']
        # A partial gone within 0.4 s has no measurable second stage: its slow part comes from its neighbours.
        if p['dur'] > 0.4:
            d2[i] = p['a2'] - p['a1']
            k2[i] = p['k2']
        if p['beat'] > 0 and p['dur'] > 1.5:
            beat[i] = p['beat']
    # A partial whose fit failed still has a measured peak: keep its level, take its decays from its neighbours.
    lvl1 = np.where(np.isfinite(lvl1), lvl1, peak)
    lvl1 = fill(lvl1) if np.isfinite(lvl1).any() else np.full(nmax, -120.0)
    for i in range(nmax):
        if not np.isfinite(peak[i]):
            lvl1[i] -= 6        # not found above the floor: quieter than its neighbours
    # Measured values stand as they are; only the gaps take the median of their neighbours.
    k1s, k2s, d2s = (np.where(np.isfinite(v), v, fill(runmed(v, 5))) for v in (k1, k2, d2))
    beats = fill(runmed(beat, 7))
    for a in (k1s, k2s, d2s, beats):
        if not np.isfinite(a).any():
            a[:] = np.nan
    # The closed-loop corrections (calib.py): each partial's level, and its slow part against its fast part.
    cal = CAL.get(name(m), {})
    for nn, k in cal.items():
        i = int(nn) - 1
        if i < nmax:
            lvl1[i] += k[0]
            if np.isfinite(d2s[i]):
                d2s[i] += k[1]
            if len(k) > 2 and np.isfinite(k1s[i]):
                k1s[i] = max(0.05, k1s[i] + k[2])
    rows.append(dict(m=m, n=nmax, lvl=lvl1, k1=k1s, k2=k2s, d2=d2s, beat=beats, B=10 ** np.polyval(cB, m),
                     cents=float(np.polyval(cC, m))))

# Notes with too few good fits borrow decay curves from their neighbours by partial index.
for i, row in enumerate(rows):
    for key in ('k1', 'k2', 'd2', 'beat'):
        if not np.isfinite(row[key]).all():
            for j in sorted(range(len(rows)), key=lambda j: abs(j - i)):
                src = rows[j][key]
                if j != i and np.isfinite(src).all():
                    v = np.interp(np.arange(row['n']), np.arange(len(src)), src)
                    row[key] = np.where(np.isfinite(row[key]), row[key], v)
                    break

# Velocity: how much softer, and how much darker, mf and pp are than ff, per note (dB = c0 + c1 log2 n).
def tilt(other, m):
    o = other[m]
    if o is None:
        return np.nan, np.nan
    a = {p['n']: p['peak'] for p in ff[m]['partials']}
    b = {p['n']: p['peak'] for p in o['partials']}
    # Only partials well clear of the noise in both, and only up to the first gap: past it the softer note's
    # "partials" are mostly noise peaks.
    ns = []
    for n in sorted(a):
        if n in b and b[n] > -58 and a[n] > -58:
            ns.append(n)
        elif ns and n > ns[-1] + 2:
            break
    if len(ns) == 0:
        return np.nan, np.nan
    d = np.array([b[n] - a[n] for n in ns]); x = np.log2(np.array(ns, float))
    if len(ns) < 3 or np.ptp(x) < 1:
        return float(np.median(d)), np.nan
    sl = [(d[j] - d[i]) / (x[j] - x[i]) for i in range(len(x)) for j in range(i + 1, len(x)) if x[j] > x[i]]
    c1 = float(np.median(sl))
    c0 = float(np.median(d - c1 * x))
    return c0, float(np.clip(c1, -14, 0))


tl = {}
for dyn, src in (('mf', mf), ('pp', pp)):
    c0 = np.array([tilt(src, m)[0] for m in MIDIS]); c1 = np.array([tilt(src, m)[1] for m in MIDIS])
    tl[dyn] = (np.polyval(robust_poly(ms, c0, 2, 6), ms), np.polyval(robust_poly(ms, c1, 2, 4), ms))

# Peaks off the harmonic series in the bass: longitudinal modes and phantom partials, as measured at ff.
extras = []
for m in MIDIS:
    ex = []
    for e in ff[m].get('extras', []):
        if e['rms'] > 6.5 or e['dur'] < 0.1 or e['rel'] < -30:
            continue
        k2, d2 = (e['k2'], e['a2'] - e['a1']) if e['dur'] > 0.4 else (e['k1'], -60.0)
        ex.append((e['f'], e['a1'], d2, e['k1'], k2))
    extras.append(ex)

with open(OUT, 'w') as f:
    f.write('/* Generated by gen_tables.py from measurements of a Steinway B (University of Iowa Electronic Music\n'
            ' * Studios, Musical Instrument Samples: pp, mf and ff, every third key). Levels in dB as recorded at ff;\n'
            ' * decays in nepers a second; beat in Hz. */\n')
    f.write(f'#define PN_TABLE_NOTES {len(rows)}\n#define PN_FIRST_MIDI {MIDIS[0]}\n#define PN_STEP 3\n')
    off = 0
    offs = []
    for row in rows:
        offs.append(off); off += row['n']
    f.write(f'#define PN_TABLE_PARTIALS {off}\n')
    xoffs = []
    xo = 0
    for ex in extras:
        xoffs.append(xo); xo += len(ex)
    f.write(f'#define PN_TABLE_EXTRAS {max(xo, 1)}\n')
    f.write('static const pn_note_t pn_notes[PN_TABLE_NOTES] = {\n')
    for row, o, xo_, ex in zip(rows, offs, xoffs, extras):
        i = MIDIS.index(row['m'])
        f.write(f"    {{{o}, {row['n']}, {xo_}, {len(ex)}, {row['B']:.4e}f, {row['cents']:.2f}f, "
                f"{tl['mf'][0][i]:.2f}f, {tl['mf'][1][i]:.2f}f, {tl['pp'][0][i]:.2f}f, {tl['pp'][1][i]:.2f}f}}, /* {name(row['m'])} */\n")
    f.write('};\n')
    f.write('static const pn_extra_t pn_extras[PN_TABLE_EXTRAS] = {\n')
    for m, ex in zip(MIDIS, extras):
        for e in ex:
            f.write(f"    {{{e[0]:.1f}f, {e[1]:.1f}f, {e[2]:.1f}f, {e[3]:.3f}f, {e[4]:.4f}f}}, /* {name(m)} */\n")
    if not any(extras):
        f.write('    {0}\n')
    f.write('};\n')
    f.write('static const pn_partial_t pn_partials[PN_TABLE_PARTIALS] = {\n')
    for row in rows:
        f.write(f"    /* {name(row['m'])} */\n")
        for i in range(row['n']):
            f.write(f"    {{{row['lvl'][i]:.1f}f, {row['d2'][i]:.1f}f, {row['k1'][i]:.3f}f, {row['k2'][i]:.4f}f, {row['beat'][i]:.3f}f}},\n")
    f.write('};\n')
print('B coeffs', cB, 'cents', cC)
for row in rows:
    i = MIDIS.index(row['m'])
    print(name(row['m']), row['n'], f"B={row['B']:.2e} c={row['cents']:+.1f} mf=({tl['mf'][0][i]:.1f},{tl['mf'][1][i]:.1f}) pp=({tl['pp'][0][i]:.1f},{tl['pp'][1][i]:.1f})")
