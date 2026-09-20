#!/usr/bin/env python3
"""Build the indexed-colour tables for 256-colour and 16-colour desktops.

On a palettized display Windows offers 256 colours for the whole screen and,
left alone, maps the skin onto its default palette - which has almost no dark
blues, so the LCD glow, the EQ smoke and the scanline sheen all collapse.

This tool looks at the app's own renders (`make shots`) and picks the 236
colours that matter to THIS skin (Windows keeps 20 for itself), then bakes a
15-bit RGB -> palette-index lookup for it and another for the fixed VGA 16.
The client dithers into those tables at runtime; no searching on a Celeron.

Usage: make palette     (renders the shots first, then runs this)
"""
from __future__ import annotations

import glob
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SHOTS = os.path.join(HERE, "..", "build", "shots")
OUT = os.path.join(HERE, "..", "src", "palette_gen.c")

NCOLORS = 236
# colours that must exist exactly: flat areas of them must never dither
ANCHORS = [(0, 0, 0), (255, 255, 255), (0xc4, 0xc4, 0xcc), (0xd0, 0xd0, 0xd8), (0xae, 0xae, 0xb6),
           (0x2c, 0x2c, 0x32), (0x8a, 0x8a, 0x92), (0xed, 0xed, 0xf2), (0x1e, 0xff, 0x1e), (0x2b, 0xff, 0x2b),
           (0x1b, 0x3a, 0xa0), (0x0a, 0x0a, 0x10), (0x36, 0x40, 0x6e), (0x24, 0x24, 0x2c), (0x16, 0x16, 0x1c),
           (0x14, 0x14, 0x1a), (0x1e, 0x1e, 0x24)]
VGA16 = [(0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128), (128, 0, 128), (0, 128, 128), (192, 192, 192),
         (128, 128, 128), (255, 0, 0), (0, 255, 0), (255, 255, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255)]


def to_lab(rgb):
    """sRGB (0..255 float array, shape (...,3)) -> CIELAB."""
    c = rgb / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def training_pixels():
    files = [f for f in sorted(glob.glob(os.path.join(SHOTS, "*.png")))
             if not os.path.basename(f).startswith(("z-", "cmp-", "eq-full")) and "-16bit" not in f and "-256" not in f and "-vga16" not in f]
    if not files:
        sys.exit("no renders in build/shots - run `make shots` first")
    px = np.concatenate([np.asarray(Image.open(f).convert("RGB")).reshape(-1, 3) for f in files])
    print(f"{len(files)} renders, {len(px):,} pixels")
    return px


def kmeans_palette(px, n, anchors):
    """k-means in Lab over the unique colours, weighted by sqrt(count) so big
    flat areas do not starve the gradients of entries."""
    uniq, counts = np.unique(px, axis=0, return_counts=True)
    print(f"{len(uniq):,} distinct colours")
    w = np.sqrt(counts.astype(np.float64))
    lab = to_lab(uniq.astype(np.float64))
    rng = np.random.default_rng(1)
    k = n - len(anchors)
    cent = lab[rng.choice(len(lab), k, replace=False, p=w / w.sum())]
    fixed = to_lab(np.array(anchors, dtype=np.float64))
    for it in range(24):
        allc = np.concatenate([fixed, cent])
        d = ((lab[:, None, :] - allc[None, :, :]) ** 2).sum(-1) if len(lab) * len(allc) < 4e7 else None
        if d is None:
            idx = np.empty(len(lab), dtype=np.int64)
            for s in range(0, len(lab), 20000):
                idx[s:s + 20000] = ((lab[s:s + 20000, None, :] - allc[None, :, :]) ** 2).sum(-1).argmin(1)
        else:
            idx = d.argmin(1)
        moved = 0.0
        for j in range(k):
            sel = idx == j + len(anchors)
            if sel.any():
                new = (lab[sel] * w[sel, None]).sum(0) / w[sel].sum()
                moved += np.abs(new - cent[j]).sum()
                cent[j] = new
            else:                                   # dead centroid: re-seed on the worst-served colour
                err = ((lab - allc[idx]) ** 2).sum(-1) * w
                cent[j] = lab[err.argmax()]
        if moved < 0.5:
            break
    # back to RGB: take the nearest real colour to each centroid
    out = list(anchors)
    for c in cent:
        out.append(tuple(int(v) for v in uniq[((lab - c) ** 2).sum(-1).argmin()]))
    return out


def lut15_rgb(palette):
    """nearest palette index by plain RGB distance. Used for the VGA 16, where
    Lab matching is too faithful to lightness: it turns every dark blue into
    grey, and the skin loses its title bars. RGB distance keeps the hue, and
    the dither then mixes navy with black and grey to land the brightness."""
    g = np.arange(32) * 255.0 / 31.0
    grid = np.stack(np.meshgrid(g, g, g, indexing="ij"), -1).reshape(-1, 3)
    pal = np.array(palette, dtype=np.float64)
    out = np.empty(len(grid), dtype=np.uint8)
    for s in range(0, len(grid), 4096):
        out[s:s + 4096] = ((grid[s:s + 4096, None, :] - pal[None, :, :]) ** 2).sum(-1).argmin(1)
    return out


def lut15(palette):
    """nearest palette index (in Lab) for every 5-5-5 RGB cell centre."""
    g = np.arange(32) * 255.0 / 31.0
    grid = np.stack(np.meshgrid(g, g, g, indexing="ij"), -1).reshape(-1, 3)      # index = r<<10 | g<<5 | b
    lab, pal = to_lab(grid), to_lab(np.array(palette, dtype=np.float64))
    out = np.empty(len(grid), dtype=np.uint8)
    for s in range(0, len(grid), 4096):
        out[s:s + 4096] = ((lab[s:s + 4096, None, :] - pal[None, :, :]) ** 2).sum(-1).argmin(1)
    return out


def emit(name, data, per=24):
    rows = [",".join(str(int(v)) for v in data[i:i + per]) + "," for i in range(0, len(data), per)]
    return f"const unsigned char {name}[{len(data)}] = {{\n" + "\n".join(rows) + "\n};\n"


def main():
    pal = kmeans_palette(training_pixels(), NCOLORS, ANCHORS)
    assert len(pal) == NCOLORS
    c = ["/* GENERATED by tools/mkpalette.py - do not edit. */", '#include "gfx.h"', "",
         f"const int EA_PAL256_N = {NCOLORS};",
         emit("EA_PAL256", [v for rgb in pal for v in rgb]),
         emit("EA_LUT256", lut15(pal)),
         emit("EA_PAL16", [v for rgb in VGA16 for v in rgb]),
         emit("EA_LUT16", lut15_rgb(VGA16))]
    open(OUT, "w").write("\n".join(c))
    print(f"wrote {os.path.relpath(OUT)}: {NCOLORS}-colour palette + two 32 KB lookups")


if __name__ == "__main__":
    main()
