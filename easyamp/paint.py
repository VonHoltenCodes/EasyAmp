"""Shared Cairo painting helpers for the EQ surfaces.

Both EQ widgets (the small 10-band player EQ and the expanded N-band bank)
share the same visual language: a smoky blue-black backdrop, value-coloured
columns (yellow at centre, warming to red when raised, cooling to green when
lowered), and light-grey beveled thumbs with a dark "=" grip.
"""

from __future__ import annotations

import cairo


def paint_smoke(cr, w, h) -> None:
    """Black backdrop with a soft blue glow rising from the bottom-right."""
    cr.set_source_rgb(0, 0, 0)
    cr.paint()
    g = cairo.RadialGradient(w, h, 0, w, h, max(w, h) * 1.15)
    g.add_color_stop_rgb(0.0, 0.11, 0.18, 0.40)
    g.add_color_stop_rgb(0.45, 0.05, 0.09, 0.20)
    g.add_color_stop_rgb(1.0, 0.0, 0.0, 0.0)
    cr.set_source(g)
    cr.paint()


def _lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def value_color(v: float, vmin: float, vmax: float):
    """Colour for a gain value: yellow at 0, through orange to red as raised,
    towards green as lowered."""
    yellow, orange, red, green = ((0.92, 0.82, 0.14), (0.93, 0.52, 0.10),
                                  (0.90, 0.13, 0.10), (0.16, 0.82, 0.18))
    if v >= 0:
        f = min(v / vmax, 1.0) if vmax else 0.0
        return _lerp(yellow, orange, f / 0.5) if f < 0.5 else _lerp(orange, red, (f - 0.5) / 0.5)
    f = min(v / vmin, 1.0) if vmin else 0.0
    return _lerp(yellow, green, f)


def paint_thumb(cr, cx: float, cy: float, tw: float, th: float) -> None:
    """Light-grey beveled slider thumb centred at (cx, cy) with a dark "="
    grip: highlight on the top-left edges, shade on the bottom-right."""
    tx, ty = cx - tw / 2, cy - th / 2
    cr.set_source_rgb(0.80, 0.80, 0.84)
    cr.rectangle(tx, ty, tw, th)
    cr.fill()
    cr.set_source_rgb(0.94, 0.94, 0.98)
    cr.set_line_width(1)
    cr.move_to(tx, ty + th)
    cr.line_to(tx, ty)
    cr.line_to(tx + tw, ty)
    cr.stroke()
    cr.set_source_rgb(0.30, 0.30, 0.34)
    cr.move_to(tx + tw, ty)
    cr.line_to(tx + tw, ty + th)
    cr.line_to(tx, ty + th)
    cr.stroke()
    cr.set_source_rgb(0.22, 0.22, 0.26)
    cr.set_line_width(1.2)
    midy = ty + th / 2
    for off in (-1.8, 1.8):
        cr.move_to(cx - 4, midy + off)
        cr.line_to(cx + 4, midy + off)
    cr.stroke()
