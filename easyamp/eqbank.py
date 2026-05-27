"""Expanded N-band EQ slider bank for the full-window equalizer view.

Same visual language as the small player EQ (value-coloured columns, light
grey thumbs with a "=" grip, a line graph above), but scales to 10–32 bands
and labels each column with its parametric centre frequency. Dragging a
column sets that band's gain; a band can be selected (click its label area)
so the surrounding view can edit its freq/Q.
"""

from __future__ import annotations

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk  # noqa: E402

from .eqwidget import paint_smoke, _vcolor  # shared look

BAND_MIN, BAND_MAX = -24.0, 12.0
CURVE_H = 34
LABEL_H = 16


def _fmt_freq(f: float) -> str:
    if f >= 1000:
        v = f / 1000.0
        return f"{v:.0f}K" if v >= 10 or v == int(v) else f"{v:.1f}K"
    return f"{f:.0f}"


class EQBank(Gtk.DrawingArea):
    def __init__(self, on_change=None, on_select=None):
        super().__init__()
        self.on_change = on_change          # (index, gain_db)
        self.on_select = on_select          # (index)
        self.freqs: list[float] = []
        self.gains: list[float] = []
        self.selected = -1
        self.set_vexpand(True)
        self.set_hexpand(True)
        self.set_content_height(CURVE_H + 150 + LABEL_H)
        self.set_draw_func(self._draw)
        drag = Gtk.GestureDrag()
        drag.connect("drag-begin", self._begin)
        drag.connect("drag-update", self._update)
        self.add_controller(drag)
        click = Gtk.GestureClick()
        click.connect("pressed", lambda g, n, x, y: self._press(x, y))
        self.add_controller(click)

    def set_bands(self, freqs, gains):
        self.freqs = [float(f) for f in freqs]
        self.gains = [float(g) for g in gains]
        if self.selected >= len(self.freqs):
            self.selected = -1
        self.queue_draw()

    # ---- geometry -----------------------------------------------------
    def _band_w(self, w):
        n = max(1, len(self.freqs))
        return w / n

    def _span(self, h):
        return CURVE_H, h - LABEL_H

    def _index_at(self, x, w):
        n = len(self.freqs)
        if n == 0:
            return -1
        return max(0, min(n - 1, int(x // self._band_w(w))))

    def _set_xy(self, x, y, select=False):
        w, h = self.get_width(), self.get_height()
        if w <= 0 or not self.freqs:
            return
        top, bottom = self._span(h)
        idx = self._index_at(x, w)
        if select:
            self.selected = idx
            if self.on_select:
                self.on_select(idx)
        frac = max(0.0, min(1.0, (y - top) / max(bottom - top, 1)))
        gain = round(BAND_MAX - frac * (BAND_MAX - BAND_MIN), 1)
        self.gains[idx] = gain
        self.queue_draw()
        if self.on_change:
            self.on_change(idx, gain)

    def _press(self, x, y):
        self._set_xy(x, y, select=True)

    def _begin(self, _g, x, y):
        self._set_xy(x, y, select=True)

    def _update(self, g, dx, dy):
        ok, sx, sy = g.get_start_point()
        if ok:
            self._set_xy(sx + dx, sy + dy)

    # ---- drawing ------------------------------------------------------
    def _draw(self, _a, cr, w, h):
        if w <= 0 or h <= 0 or not self.freqs:
            return
        paint_smoke(cr, w, h)
        n = len(self.freqs)
        band_w = self._band_w(w)
        top, bottom = self._span(h)
        span = bottom - top

        def y_of(v):
            return top + (BAND_MAX - v) / (BAND_MAX - BAND_MIN) * span

        # line graph across the columns
        pts = []
        for i in range(n):
            cx = (i + 0.5) * band_w
            cy = 3 + (BAND_MAX - self.gains[i]) / (BAND_MAX - BAND_MIN) * (CURVE_H - 6)
            pts.append((cx, cy, _vcolor(self.gains[i], BAND_MIN, BAND_MAX)))
        cr.set_line_width(2)
        for i in range(len(pts) - 1):
            x1, y1, c1 = pts[i]
            x2, y2, _ = pts[i + 1]
            cr.set_source_rgb(*c1)
            cr.move_to(x1, y1)
            cr.line_to(x2, y2)
            cr.stroke()

        # columns + thumbs
        colw = max(5.0, min(band_w * 0.5, 9.0))
        for i in range(n):
            cx = (i + 0.5) * band_w
            v = self.gains[i]
            color = _vcolor(v, BAND_MIN, BAND_MAX)
            if i == self.selected:                       # highlight selection
                cr.set_source_rgb(0.16, 0.22, 0.45)
                cr.rectangle(cx - band_w / 2 + 1, top - CURVE_H, band_w - 2,
                             span + CURVE_H + 2)
                cr.fill()
            cr.set_source_rgb(*color)
            cr.rectangle(cx - colw / 2, top, colw, span)
            cr.fill()
            cr.set_source_rgb(0, 0, 0)
            cr.set_line_width(1)
            cr.rectangle(cx - colw / 2 + 0.5, top + 0.5, colw - 1, span - 1)
            cr.stroke()

            tw = min(band_w - 4, 20)
            th = max(7, tw * 0.5)
            ty = y_of(v) - th / 2
            tx = cx - tw / 2
            cr.set_source_rgb(0.80, 0.80, 0.84)
            cr.rectangle(tx, ty, tw, th)
            cr.fill()
            cr.set_source_rgb(0.94, 0.94, 0.98)
            cr.set_line_width(1)
            cr.move_to(tx, ty + th); cr.line_to(tx, ty); cr.line_to(tx + tw, ty); cr.stroke()
            cr.set_source_rgb(0.30, 0.30, 0.34)
            cr.move_to(tx + tw, ty); cr.line_to(tx + tw, ty + th); cr.line_to(tx, ty + th); cr.stroke()
            cr.set_source_rgb(0.22, 0.22, 0.26)
            cr.set_line_width(1.2)
            midy = ty + th / 2
            for off in (-1.8, 1.8):
                cr.move_to(cx - 4, midy + off); cr.line_to(cx + 4, midy + off)
            cr.stroke()

        # frequency labels (skip some when crowded)
        cr.select_font_face("monospace")
        cr.set_font_size(8 if n <= 16 else 7)
        step = 1 if band_w > 22 else (2 if band_w > 13 else 3)
        for i in range(n):
            if i % step:
                continue
            cx = (i + 0.5) * band_w
            label = _fmt_freq(self.freqs[i])
            cr.set_source_rgb(0.55, 0.78, 1.0) if i == self.selected else \
                cr.set_source_rgb(0.42, 0.58, 0.85)
            ext = cr.text_extents(label)
            cr.move_to(cx - ext.width / 2, h - 4)
            cr.show_text(label)
