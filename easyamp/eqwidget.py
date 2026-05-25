"""Custom-drawn interactive 10-band EQ (preamp + bands).

Each column's channel is filled with a single solid colour that reflects
its level — yellow at centre, warming through orange to red as it's
raised, cooling to green as it's lowered. The thumb is a light-grey
horizontal cap with a dark centre dot. A line graph above the bands
mirrors each band's colour and reacts as you drag.
"""

from __future__ import annotations

import cairo

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk  # noqa: E402


def paint_smoke(cr, w, h):
    """Smoked dark-blue glow, strongest at the bottom-right, fading to
    black toward the top-left (also lights the top-right / bottom-left)."""
    cr.set_source_rgb(0, 0, 0)
    cr.paint()
    g = cairo.RadialGradient(w, h, 0, w, h, max(w, h) * 1.15)
    g.add_color_stop_rgb(0.0, 0.11, 0.18, 0.40)
    g.add_color_stop_rgb(0.45, 0.05, 0.09, 0.20)
    g.add_color_stop_rgb(1.0, 0.0, 0.0, 0.0)
    cr.set_source(g)
    cr.paint()

NBANDS = 10
BAND_LABELS = ["60", "170", "310", "600", "1K", "3K", "6K", "12K", "14K", "16K"]
BAND_MIN, BAND_MAX = -24.0, 12.0
PRE_MIN, PRE_MAX = -12.0, 12.0

CURVE_H = 30
LABEL_H = 14


def _lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def _vcolor(v, vmin, vmax):
    """Solid colour for a level: green(low) -> yellow(0) -> orange -> red(high)."""
    yellow = (0.92, 0.82, 0.14)
    orange = (0.93, 0.52, 0.10)
    red = (0.90, 0.13, 0.10)
    green = (0.16, 0.82, 0.18)
    if v >= 0:
        f = min(v / vmax, 1.0) if vmax else 0.0
        return _lerp(yellow, orange, f / 0.5) if f < 0.5 else _lerp(orange, red, (f - 0.5) / 0.5)
    f = min(v / vmin, 1.0) if vmin else 0.0
    return _lerp(yellow, green, f)


class EQWidget(Gtk.DrawingArea):
    def __init__(self, on_change=None):
        super().__init__()
        self.on_change = on_change
        self.preamp = 0.0
        self.bands = [0.0] * NBANDS
        self.set_content_height(CURVE_H + 96 + LABEL_H)
        self.set_draw_func(self._draw)
        drag = Gtk.GestureDrag()
        drag.connect("drag-begin", self._begin)
        drag.connect("drag-update", self._update)
        self.add_controller(drag)
        click = Gtk.GestureClick()
        click.connect("pressed", lambda g, n, x, y: self._set_xy(x, y))
        self.add_controller(click)

    # ---- value access -------------------------------------------------
    def set_values(self, preamp, bands):
        self.preamp = float(preamp)
        self.bands = [float(b) for b in bands][:NBANDS]
        self.bands += [0.0] * (NBANDS - len(self.bands))
        self.queue_draw()

    # ---- geometry -----------------------------------------------------
    def _cols(self, w):
        return w / (NBANDS + 1)

    def _slider_span(self, h):
        return CURVE_H, h - LABEL_H

    def _set_xy(self, x, y):
        w = self.get_width()
        h = self.get_height()
        if w <= 0:
            return
        col = int(x // self._cols(w))
        col = max(0, min(NBANDS, col))
        top, bottom = self._slider_span(h)
        frac = max(0.0, min(1.0, (y - top) / max(bottom - top, 1)))
        if col == 0:
            self.preamp = round(PRE_MAX - frac * (PRE_MAX - PRE_MIN))
        else:
            self.bands[col - 1] = round(BAND_MAX - frac * (BAND_MAX - BAND_MIN))
        self.queue_draw()
        if self.on_change:
            self.on_change()

    def _begin(self, _g, x, y):
        self._set_xy(x, y)

    def _update(self, g, dx, dy):
        ok, sx, sy = g.get_start_point()
        if ok:
            self._set_xy(sx + dx, sy + dy)

    # ---- drawing ------------------------------------------------------
    def _draw(self, _a, cr, w, h):
        if w <= 0 or h <= 0:
            return
        paint_smoke(cr, w, h)
        col_w = self._cols(w)
        top, bottom = self._slider_span(h)
        span = bottom - top

        def y_of(v, vmin, vmax):
            return top + (vmax - v) / (vmax - vmin) * span

        # ---- line graph over the band columns ----
        pts = []
        for i in range(NBANDS):
            cx = (i + 1.5) * col_w
            cy = 3 + (BAND_MAX - self.bands[i]) / (BAND_MAX - BAND_MIN) * (CURVE_H - 6)
            pts.append((cx, cy, _vcolor(self.bands[i], BAND_MIN, BAND_MAX)))
        cr.set_line_width(2)
        for i in range(len(pts) - 1):
            x1, y1, c1 = pts[i]
            x2, y2, _ = pts[i + 1]
            cr.set_source_rgb(*c1)
            cr.move_to(x1, y1)
            cr.line_to(x2, y2)
            cr.stroke()
        for x1, y1, c1 in pts:
            cr.set_source_rgb(*c1)
            cr.arc(x1, y1, 1.6, 0, 6.2832)
            cr.fill()

        # ---- columns: channel + thumb ----
        for col in range(NBANDS + 1):
            cx = (col + 0.5) * col_w
            if col == 0:
                v, vmin, vmax, label = self.preamp, PRE_MIN, PRE_MAX, "PRE"
            else:
                v, vmin, vmax, label = self.bands[col - 1], BAND_MIN, BAND_MAX, BAND_LABELS[col - 1]
            color = _vcolor(v, vmin, vmax)

            # channel filled with the level's solid colour
            ch_w = 7
            cr.set_source_rgb(*color)
            cr.rectangle(cx - ch_w / 2, top, ch_w, span)
            cr.fill()
            cr.set_source_rgb(0, 0, 0)
            cr.set_line_width(1)
            cr.rectangle(cx - ch_w / 2 + 0.5, top + 0.5, ch_w - 1, span - 1)
            cr.stroke()

            # light-grey thumb with a dark centre dot (thin horizontal cap)
            tw = min(col_w - 5, 18)
            th = tw * 0.55
            ty = y_of(v, vmin, vmax) - th / 2
            tx = cx - tw / 2
            cr.set_source_rgb(0.80, 0.80, 0.84)
            cr.rectangle(tx, ty, tw, th)
            cr.fill()
            cr.set_source_rgb(0.92, 0.92, 0.96)   # top/left highlight
            cr.set_line_width(1)
            cr.move_to(tx, ty + th)
            cr.line_to(tx, ty)
            cr.line_to(tx + tw, ty)
            cr.stroke()
            cr.set_source_rgb(0.30, 0.30, 0.34)   # bottom/right shade
            cr.move_to(tx + tw, ty)
            cr.line_to(tx + tw, ty + th)
            cr.line_to(tx, ty + th)
            cr.stroke()
            cr.set_source_rgb(0.22, 0.22, 0.26)   # "=" grip lines
            cr.set_line_width(1.2)
            midy = ty + th / 2
            for off in (-1.8, 1.8):
                cr.move_to(cx - 4, midy + off)
                cr.line_to(cx + 4, midy + off)
            cr.stroke()

            # label
            cr.set_source_rgb(0.42, 0.58, 0.85)
            cr.select_font_face("monospace")
            cr.set_font_size(9)
            ext = cr.text_extents(label)
            cr.move_to(cx - ext.width / 2, h - 3)
            cr.show_text(label)
