"""Custom-drawn interactive 10-band EQ (preamp + dB scale + bands).

Each column's channel is filled with a single solid colour that reflects
its level — yellow at centre, warming through orange to red as raised,
cooling to green as lowered. The thumb is a light-grey cap with a dark
"=" grip. A colour-matched line graph above the bands reacts to drags.
A small dB scale (+20 / +0 / -20) sits between the preamp and the bands.
"""

from __future__ import annotations

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk  # noqa: E402

from .eqmodel import GRAPHIC_NBANDS as NBANDS  # noqa: E402
from .eqmodel import BAND_MIN, BAND_MAX, PRE_MIN, PRE_MAX  # noqa: E402
from .paint import paint_smoke, paint_thumb, value_color  # noqa: E402

BAND_LABELS = ["60", "170", "310", "600", "1K", "3K", "6K", "12K", "14K", "16K"]

CURVE_H = 30
LABEL_H = 14


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

    def set_values(self, preamp, bands):
        self.preamp = float(preamp)
        self.bands = ([float(b) for b in bands] + [0.0] * NBANDS)[:NBANDS]
        self.queue_draw()

    # ---- geometry: [preamp] [dB labels] [10 bands] -------------------
    def _geom(self, w):
        pre_w = w * 0.09
        lbl_w = w * 0.115
        band_w = (w - pre_w - lbl_w) / NBANDS
        return pre_w, lbl_w, band_w

    def _span(self, h):
        return CURVE_H, h - LABEL_H

    def _set_xy(self, x, y):
        w, h = self.get_width(), self.get_height()
        if w <= 0:
            return
        pre_w, lbl_w, band_w = self._geom(w)
        top, bottom = self._span(h)
        frac = max(0.0, min(1.0, (y - top) / max(bottom - top, 1)))
        if x < pre_w:
            self.preamp = round(PRE_MAX - frac * (PRE_MAX - PRE_MIN))
        elif x < pre_w + lbl_w:
            return
        else:
            idx = int((x - pre_w - lbl_w) // band_w)
            idx = max(0, min(NBANDS - 1, idx))
            self.bands[idx] = round(BAND_MAX - frac * (BAND_MAX - BAND_MIN))
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
        pre_w, lbl_w, band_w = self._geom(w)
        top, bottom = self._span(h)
        span = bottom - top
        band_x0 = pre_w + lbl_w

        def y_of(v, vmin, vmax):
            return top + (vmax - v) / (vmax - vmin) * span

        # dB scale labels between preamp and bands
        cr.set_source_rgb(0.42, 0.58, 0.85)
        cr.select_font_face("monospace")
        cr.set_font_size(8)
        lx = pre_w + 2
        for text, yy in (("+20db", top + 6), ("+0db", (top + bottom) / 2 + 2),
                         ("-20db", bottom - 1)):
            cr.move_to(lx, yy)
            cr.show_text(text)

        # line graph over the band columns
        pts = []
        for i in range(NBANDS):
            cx = band_x0 + (i + 0.5) * band_w
            cy = 3 + (BAND_MAX - self.bands[i]) / (BAND_MAX - BAND_MIN) * (CURVE_H - 6)
            pts.append((cx, cy, value_color(self.bands[i], BAND_MIN, BAND_MAX)))
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

        # columns
        cols = [(pre_w / 2, self.preamp, PRE_MIN, PRE_MAX, "PRE")]
        for i in range(NBANDS):
            cols.append((band_x0 + (i + 0.5) * band_w, self.bands[i],
                         BAND_MIN, BAND_MAX, BAND_LABELS[i]))
        for cx, v, vmin, vmax, label in cols:
            color = value_color(v, vmin, vmax)
            cr.set_source_rgb(*color)
            cr.rectangle(cx - 3.5, top, 7, span)
            cr.fill()
            cr.set_source_rgb(0, 0, 0)
            cr.set_line_width(1)
            cr.rectangle(cx - 3, top + 0.5, 6, span - 1)
            cr.stroke()

            tw = min(band_w - 5, 18)
            paint_thumb(cr, cx, y_of(v, vmin, vmax), tw, tw * 0.55)

            cr.set_source_rgb(0.42, 0.58, 0.85)
            cr.set_font_size(9)
            ext = cr.text_extents(label)
            cr.move_to(cx - ext.width / 2, h - 3)
            cr.show_text(label)
