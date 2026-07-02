"""Portable 10-band EQ presets.

Presets are stored as open, interoperable JSON so they can be shared,
hand-edited, or imported/exported:

    {"name": "Rock", "preamp": 0.0, "bands": [g0, g1, ... g9]}

Band gains are in dB for equalizer-10bands' 10 fixed frequencies
(29 Hz .. 15 kHz). User presets live in ~/.config/easyamp/eq/*.json and
are merged over the built-ins of the same name.
"""

from __future__ import annotations

import json
import os

from .eqmodel import GRAPHIC_NBANDS as NBANDS

USER_DIR = os.path.join(
    os.environ.get("XDG_CONFIG_HOME", os.path.expanduser("~/.config")),
    "easyamp", "eq",
)

# name -> 10 band gains (dB). Preamp defaults to 0.
BUILTIN: dict[str, list[float]] = {
    "Flat":      [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    "EasyAmp":   [0, 4, 8, 3, 3, 3, 1, 4, 4, 8],   # the house curve
    "Rock":      [5, 4, 2, 0, -1, 0, 2, 3, 4, 4],
    "Pop":       [-1, 1, 3, 4, 4, 2, 0, -1, -1, -2],
    "Jazz":      [3, 2, 1, 2, -1, -1, 0, 1, 2, 3],
    "Classical": [4, 3, 2, 1, -1, -1, 0, 2, 3, 4],
    "Bass Boost": [7, 6, 5, 3, 1, 0, 0, 0, 0, 0],
    "Treble":    [0, 0, 0, 0, 0, 1, 3, 5, 6, 7],
    "Vocal":     [-2, -1, 0, 2, 4, 4, 3, 1, 0, -1],
}


def _normalize(bands: list[float]) -> list[float]:
    bands = [float(x) for x in bands][:NBANDS]
    bands += [0.0] * (NBANDS - len(bands))
    return bands


def list_presets() -> list[str]:
    names = list(BUILTIN.keys())
    for fn in _user_files():
        name = os.path.splitext(os.path.basename(fn))[0]
        if name not in names:
            names.append(name)
    return names


def _user_files() -> list[str]:
    if not os.path.isdir(USER_DIR):
        return []
    return sorted(
        os.path.join(USER_DIR, f) for f in os.listdir(USER_DIR) if f.endswith(".json")
    )


def load(name: str) -> tuple[float, list[float]]:
    """Return (preamp, bands) for a preset name. User JSON wins over built-in."""
    path = os.path.join(USER_DIR, f"{name}.json")
    if os.path.isfile(path):
        try:
            with open(path, encoding="utf-8") as fh:
                data = json.load(fh)
            return float(data.get("preamp", 0.0)), _normalize(data.get("bands", []))
        except (OSError, ValueError, KeyError):
            pass
    if name in BUILTIN:
        return 0.0, _normalize(BUILTIN[name])
    return 0.0, [0.0] * NBANDS


def save(name: str, preamp: float, bands: list[float]) -> str:
    """Write a preset as portable JSON; returns the file path."""
    os.makedirs(USER_DIR, exist_ok=True)
    path = os.path.join(USER_DIR, f"{name}.json")
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(
            {"name": name, "preamp": float(preamp), "bands": _normalize(bands)},
            fh, indent=2,
        )
    return path
