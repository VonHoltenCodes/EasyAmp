"""Shared EQ band model: constants, band layouts, and small helpers.

A band is ``[freq_hz, q, gain_db, type]`` where type matches GStreamer's
``equalizer-nbands`` band ``type`` property: 0=peaking, 1=low-shelf,
2=high-shelf. This module is pure Python (no GStreamer or GTK imports) so
the parsing/preset code can share it without pulling in the pipeline.
"""

from __future__ import annotations

# band filter types (the equalizer-nbands "type" property values)
PEAK, LOW_SHELF, HIGH_SHELF = 0, 1, 2

DEFAULT_Q = 1.41
MIN_BANDS, MAX_BANDS = 10, 32

# the classic 10-band graphic layout (Hz) used by presets and the player EQ
GRAPHIC_NBANDS = 10
GRAPHIC_FREQS = [29, 59, 119, 237, 474, 947, 1889, 3770, 7523, 15011]

# UI gain ranges (dB) shared by both EQ surfaces
BAND_MIN, BAND_MAX = -24.0, 12.0
PRE_MIN, PRE_MAX = -12.0, 12.0


def band_freqs(n: int) -> list[float]:
    """Centre frequencies for an n-band bank: the classic graphic layout for
    n == 10, otherwise log-spaced 30 Hz – 16 kHz."""
    if n == GRAPHIC_NBANDS:
        return [float(f) for f in GRAPHIC_FREQS]
    lo, hi = 30.0, 16000.0
    return [lo * (hi / lo) ** (i / (n - 1)) for i in range(n)]


def interp(x: float, xs: list[float], ys: list[float]) -> float:
    """Piecewise-linear interpolation of (xs, ys) at x, clamped at the ends."""
    if not xs:
        return 0.0
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        return ys[-1]
    for i in range(1, len(xs)):
        if x <= xs[i]:
            d = xs[i] - xs[i - 1]
            t = (x - xs[i - 1]) / d if d else 0.0
            return ys[i - 1] + (ys[i] - ys[i - 1]) * t
    return ys[-1]


def fmt_freq(f: float) -> str:
    """Compact frequency label: 60 -> '60', 1200 -> '1.2K', 16000 -> '16K'."""
    if f >= 1000:
        v = f / 1000.0
        return f"{v:.0f}K" if v >= 10 or v == int(v) else f"{v:.1f}K"
    return f"{f:.0f}"
