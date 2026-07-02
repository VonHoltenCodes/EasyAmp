"""Import/export EQ curves in common interchange formats.

Supported:
  * **Equalizer APO** ``config.txt`` — ``Preamp:`` + ``Filter N: ON PK Fc ...
    Gain ... Q ...`` lines (PK peaking, LSC/LS low shelf, HSC/HS high shelf).
  * **AutoEQ GraphicEQ** — a single ``GraphicEQ: f g; f g; …`` line of
    frequency/gain points.

Bands are represented as ``(freq_hz, q, gain_db, type)`` where type is
0=peaking, 1=low-shelf, 2=high-shelf (matching equalizer-nbands).
"""

from __future__ import annotations

import re

from .eqmodel import DEFAULT_Q, PEAK, LOW_SHELF, HIGH_SHELF

_APO_TYPE = {
    "PK": PEAK, "PEQ": PEAK, "MODAL": PEAK,
    "LSC": LOW_SHELF, "LS": LOW_SHELF, "LSQ": LOW_SHELF,
    "HSC": HIGH_SHELF, "HS": HIGH_SHELF, "HSQ": HIGH_SHELF,
}
_TYPE_APO = {PEAK: "PK", LOW_SHELF: "LSC", HIGH_SHELF: "HSC"}


def parse_apo(text: str):
    """Equalizer APO config -> (preamp_db, [(freq, q, gain, type), …])."""
    preamp = 0.0
    bands = []
    for line in text.splitlines():
        s = line.strip()
        if s.lower().startswith("preamp:"):
            m = re.search(r"(-?\d+(?:\.\d+)?)\s*dB", s, re.I)
            if m:
                preamp = float(m.group(1))
        elif s.lower().startswith("filter") and "fc" in s.lower():
            if not re.search(r":\s*ON\b", s, re.I):
                continue
            tm = re.search(r":\s*ON\s+([A-Za-z]+)", s)
            typ = _APO_TYPE.get(tm.group(1).upper(), PEAK) if tm else PEAK
            fc = re.search(r"Fc\s+([\d.]+)\s*Hz", s, re.I)
            gn = re.search(r"Gain\s+(-?[\d.]+)\s*dB", s, re.I)
            q = re.search(r"Q\s+([\d.]+)", s, re.I)
            bands.append((
                float(fc.group(1)) if fc else 1000.0,
                float(q.group(1)) if q else DEFAULT_Q,
                float(gn.group(1)) if gn else 0.0,
                typ,
            ))
    return preamp, bands


def parse_graphiceq(text: str):
    """AutoEQ GraphicEQ line -> (0.0, [(freq, Q, gain, PEAK), …])."""
    m = re.search(r"GraphicEQ:\s*(.+)", text, re.I)
    if not m:
        return None
    pts = []
    for pair in m.group(1).split(";"):
        parts = pair.split()
        if len(parts) >= 2:
            try:
                pts.append((float(parts[0]), DEFAULT_Q, float(parts[1]), PEAK))
            except ValueError:
                pass
    return (0.0, pts) if pts else None


def parse(text: str):
    """Auto-detect format. Returns (preamp, bands) or None."""
    if "graphiceq:" in text.lower():
        return parse_graphiceq(text)
    preamp, bands = parse_apo(text)
    return (preamp, bands) if bands else None


def downsample(bands, n_max: int = 32):
    """Reduce a long band list (e.g. a 127-point GraphicEQ) to <= n_max,
    keeping endpoints and evenly sampling the rest."""
    if len(bands) <= n_max:
        return bands
    idx = [round(i * (len(bands) - 1) / (n_max - 1)) for i in range(n_max)]
    return [bands[i] for i in idx]


def format_apo(preamp: float, bands) -> str:
    lines = [f"Preamp: {preamp:.1f} dB"]
    for i, (f, q, g, t) in enumerate(bands, 1):
        lines.append(f"Filter {i}: ON {_TYPE_APO.get(t, 'PK')} "
                     f"Fc {f:.0f} Hz Gain {g:.1f} dB Q {q:.2f}")
    return "\n".join(lines) + "\n"


def format_graphiceq(bands) -> str:
    return "GraphicEQ: " + "; ".join(f"{f:.0f} {g:.1f}" for f, q, g, t in bands) + "\n"
