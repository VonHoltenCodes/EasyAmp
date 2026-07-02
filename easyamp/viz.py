"""Visualizer display widgets, all Cairo-drawn.

Every widget here is a passive display: the window feeds it data from the
system-audio capture (see ``spectrum.py``) and it draws on the next frame.

* :class:`ScopeArea` — the small white mirrored bar scope under the timer.
* :class:`SpectrumVU` — the main visualizer, switchable between segmented
  spectrum bars and a pair of analog VU gauges.
* :class:`LogSpectrum` — log-frequency spectrum bars with axis labels
  (the equalizer view's meter).
* :class:`WaveScope` — a plain green waveform trace (the equalizer view's
  second meter).
"""

from __future__ import annotations

import math

import cairo

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk  # noqa: E402
import numpy as np  # noqa: E402


class ScopeArea(Gtk.DrawingArea):
    """White mirrored bar graph with a faint grid and alternating
    light/dark blue dots along the bottom and left axes."""

    def __init__(self):
        super().__init__()
        self._wave = np.zeros(64, dtype=np.float32)
        self.set_content_height(40)
        self.set_content_width(150)
        self.set_hexpand(True)
        self.set_draw_func(self._draw)

    def set_wave(self, wave) -> None:
        self._wave = wave
        self.queue_draw()

    def _draw(self, _a, cr, w, h):
        wave = self._wave
        n = len(wave)
        if n < 2 or w <= 0 or h <= 0:
            return
        mid = h / 2
        amp = h * 0.44

        # faint grid
        cr.set_source_rgba(0.40, 0.52, 0.85, 0.13)
        cr.set_line_width(1)
        for c in range(1, 8):
            x = round(w * c / 8) + 0.5
            cr.move_to(x, 0)
            cr.line_to(x, h)
        for r in (0.25, 0.5, 0.75):
            y = round(h * r) + 0.5
            cr.move_to(0, y)
            cr.line_to(w, y)
        cr.stroke()

        # white mirrored bars
        nbars = 22
        bw = w / nbars
        cr.set_source_rgb(0.96, 0.97, 1.0)
        for b in range(nbars):
            i = min(int(b / nbars * n), n - 1)
            mag = abs(float(wave[i])) * amp
            cr.rectangle(b * bw + 1, mid - mag, bw - 1.5, mag * 2)
            cr.fill()

        # alternating light/dark blue dots along bottom + left axes
        light, dark = (0.46, 0.66, 1.0), (0.12, 0.22, 0.52)
        k = 0
        x = 1
        while x < w - 1:
            cr.set_source_rgb(*(light if k % 2 == 0 else dark))
            cr.rectangle(x, h - 2, 2, 2)
            cr.fill()
            x += 6
            k += 1
        k = 0
        y = 1
        while y < h - 1:
            cr.set_source_rgb(*(light if k % 2 == 0 else dark))
            cr.rectangle(0, y, 2, 2)
            cr.fill()
            y += 6
            k += 1


class SpectrumVU(Gtk.DrawingArea):
    """The main visualizer: segmented spectrum bars (green→yellow→orange→red
    with peak-hold ticks) or two analog VU gauges, toggled via ``set_mode``.
    Peak decay is handled internally — just feed levels each frame."""

    def __init__(self, bands: int):
        super().__init__()
        self._levels = np.zeros(bands, dtype=np.float32)
        self._peaks = np.zeros(bands, dtype=np.float32)
        self._vu = (0.0, 0.0)
        self._mode = "spec"
        self.add_css_class("eaa-viz")
        self.set_vexpand(True)
        self.set_content_height(70)
        self.set_draw_func(self._draw)

    def set_data(self, levels, vu) -> None:
        self._levels = levels
        self._peaks = np.maximum(levels, self._peaks - 0.015)
        self._vu = vu
        self.queue_draw()

    def set_mode(self, mode: str) -> None:
        self._mode = mode
        self.queue_draw()

    def get_mode(self) -> str:
        return self._mode

    def _draw(self, _area, cr, w, h):
        cr.set_source_rgb(0, 0, 0)
        cr.paint()
        if w <= 0 or h <= 0:
            return
        (self._draw_vu if self._mode == "vu" else self._draw_spectrum)(cr, w, h)

    def _draw_spectrum(self, cr, w, h):
        levels, peaks = self._levels, self._peaks
        n = len(levels)
        if n == 0:
            return
        gap = 2.0
        bw = (w - gap * (n + 1)) / n
        seg_h, seg_gap = 4.0, 1.5
        total = max(int(h // (seg_h + seg_gap)), 1)
        for i in range(n):
            x = gap + i * (bw + gap)
            lit = int(float(levels[i]) * total)
            for s in range(lit):
                y = h - (s + 1) * (seg_h + seg_gap)
                frac = s / max(total - 1, 1)
                if frac > 0.80:
                    cr.set_source_rgb(0.90, 0.13, 0.10)   # red
                elif frac > 0.58:
                    cr.set_source_rgb(0.93, 0.45, 0.10)   # orange
                elif frac > 0.36:
                    cr.set_source_rgb(0.92, 0.82, 0.14)   # yellow
                else:
                    cr.set_source_rgb(0.14, 0.88, 0.20)   # green
                cr.rectangle(x, y, bw, seg_h)
                cr.fill()
            ps = int(float(peaks[i]) * total)
            if ps > 0:
                cr.set_source_rgb(0.80, 1.0, 0.85)
                cr.rectangle(x, h - ps * (seg_h + seg_gap), bw, seg_h * 0.55)
                cr.fill()

    def _draw_vu(self, cr, w, h):
        gap = 8.0
        cell = (w - gap * 3) / 2
        _vu_gauge(cr, gap, gap, cell, h - 2 * gap, self._vu[0], "L")
        _vu_gauge(cr, gap * 2 + cell, gap, cell, h - 2 * gap, self._vu[1], "R")


class LogSpectrum(Gtk.DrawingArea):
    """Log-frequency spectrum bars with 100 Hz / 1K / 10K axis labels."""

    def __init__(self):
        super().__init__()
        self._levels = np.zeros(1, dtype=np.float32)
        self.add_css_class("eaa-viz")
        self.set_content_height(66)
        self.set_hexpand(True)
        self.set_draw_func(self._draw)

    def set_levels(self, levels) -> None:
        self._levels = levels
        self.queue_draw()

    def _draw(self, _a, cr, w, h):
        cr.set_source_rgb(0, 0, 0)
        cr.paint()
        n = len(self._levels)
        if n < 2 or w <= 0:
            return
        # log-frequency tick lines (bands are already log-spaced 40Hz..24kHz)
        cr.select_font_face("monospace")
        cr.set_font_size(7)
        lo, hi = 40.0, 24000.0
        span = math.log10(hi / lo)
        for f, lab in ((100, "100"), (1000, "1K"), (10000, "10K")):
            x = (math.log10(f / lo) / span) * w
            cr.set_source_rgba(0.3, 0.45, 0.7, 0.35)
            cr.set_line_width(1)
            cr.move_to(x, 0)
            cr.line_to(x, h - 9)
            cr.stroke()
            cr.set_source_rgb(0.4, 0.55, 0.8)
            cr.move_to(x + 2, h - 1)
            cr.show_text(lab)
        # bars
        bw = w / n
        for i, lv in enumerate(self._levels):
            bh = max(1.0, float(lv) * (h - 10))
            warm = min(1.0, float(lv) * 1.3)
            cr.set_source_rgb(0.12 + 0.8 * warm, 0.9 - 0.5 * warm, 0.12)
            cr.rectangle(i * bw + 0.5, (h - 10) - bh, max(1.0, bw - 1), bh)
            cr.fill()


class WaveScope(Gtk.DrawingArea):
    """A single green waveform trace on black."""

    def __init__(self):
        super().__init__()
        self._wave = np.zeros(1, dtype=np.float32)
        self.add_css_class("eaa-viz")
        self.set_content_height(66)
        self.set_size_request(200, -1)
        self.set_draw_func(self._draw)

    def set_wave(self, wave) -> None:
        self._wave = wave
        self.queue_draw()

    def _draw(self, _a, cr, w, h):
        cr.set_source_rgb(0, 0, 0)
        cr.paint()
        wv = self._wave
        n = len(wv)
        if n < 2 or w <= 0:
            return
        mid = h / 2
        cr.set_source_rgb(0.10, 0.95, 0.14)
        cr.set_line_width(1.4)
        for i in range(n):
            x = i / (n - 1) * w
            y = mid - float(wv[i]) * (h * 0.45)
            cr.line_to(x, y) if i else cr.move_to(x, y)
        cr.stroke()


# ---- analog VU gauge ----------------------------------------------------

# real VU dB scale: (label dB, position fraction along the arc)
_VU_SCALE = [(-20, 0.0), (-10, 0.22), (-7, 0.36), (-5, 0.48),
             (-3, 0.62), (0, 0.80), (3, 1.0)]
_VU_REDFRAC = 0.80  # 0 dB and above = red zone


def _rrect(cr, x, y, w, h, r):
    cr.new_sub_path()
    cr.arc(x + w - r, y + r, r, -math.pi / 2, 0)
    cr.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    cr.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
    cr.arc(x + r, y + r, r, math.pi, 1.5 * math.pi)
    cr.close_path()


def _vu_gauge(cr, x, y, w, h, value, label):
    value = max(0.0, min(1.0, value))
    cx = x + w / 2
    base_y = y + h * 0.90
    r = min(w * 0.43, h * 0.82)

    def pt(frac, rad):
        a = math.radians(135 - frac * 90)
        return cx + rad * math.cos(a), base_y - rad * math.sin(a)

    # face: dark, with an atomic-green glow from the pivot that
    # dissipates outward but never fades to full black
    _rrect(cr, x, y, w, h, 4)
    cr.set_source_rgb(0.02, 0.06, 0.035)
    cr.fill()
    _rrect(cr, x, y, w, h, 4)
    cr.clip()
    glow = cairo.RadialGradient(cx, base_y, 2, cx, base_y, r * 1.28)
    glow.add_color_stop_rgb(0.0, 0.10, 0.46, 0.22)
    glow.add_color_stop_rgb(0.55, 0.05, 0.22, 0.11)
    glow.add_color_stop_rgb(1.0, 0.03, 0.13, 0.07)
    cr.set_source(glow)
    cr.paint()
    cr.reset_clip()

    # ticks + dB numbers
    cr.select_font_face("monospace")
    for db, frac in _VU_SCALE:
        major = db in (-20, -10, -5, 0, 3)
        red = frac >= _VU_REDFRAC
        cr.set_source_rgb(*(0.97, 0.27, 0.18) if red else (0.34, 1.0, 0.62))
        cr.set_line_width(1.4 if major else 1.0)
        cr.move_to(*pt(frac, r - (6 if major else 4)))
        cr.line_to(*pt(frac, r))
        cr.stroke()
        if major:
            cr.set_font_size(7)
            txt = f"+{db}" if db > 0 else str(db)
            ext = cr.text_extents(txt)
            lx, ly = pt(frac, r - 13)
            cr.move_to(lx - ext.width / 2, ly + 3)
            cr.show_text(txt)

    # arc: green up to 0 dB, red beyond
    cr.set_line_width(2)
    cr.set_source_rgb(0.18, 0.95, 0.50)
    cr.move_to(*pt(0.0, r))
    for i in range(1, 41):
        cr.line_to(*pt(i / 40 * _VU_REDFRAC, r))
    cr.stroke()
    cr.set_source_rgb(0.95, 0.22, 0.16)
    cr.move_to(*pt(_VU_REDFRAC, r))
    for i in range(1, 13):
        cr.line_to(*pt(_VU_REDFRAC + (1 - _VU_REDFRAC) * i / 12, r))
    cr.stroke()

    # needle: subtle glow underlay + bright tapered needle + hub
    tipx, tipy = pt(value, r - 3)
    cr.set_line_cap(cairo.LINE_CAP_ROUND)
    cr.set_source_rgba(0.20, 1.0, 0.55, 0.22)
    cr.set_line_width(5)
    cr.move_to(cx, base_y)
    cr.line_to(tipx, tipy)
    cr.stroke()
    cr.set_source_rgb(0.68, 1.0, 0.82)
    cr.set_line_width(1.8)
    cr.move_to(cx, base_y)
    cr.line_to(tipx, tipy)
    cr.stroke()
    cr.set_line_cap(cairo.LINE_CAP_BUTT)
    cr.set_source_rgb(0.16, 0.92, 0.46)
    cr.arc(cx, base_y, 4, 0, 6.2832)
    cr.fill()
    cr.set_source_rgb(0.82, 1.0, 0.90)
    cr.arc(cx, base_y, 1.6, 0, 6.2832)
    cr.fill()

    # corner labels
    cr.set_source_rgb(0.34, 0.98, 0.58)
    cr.set_font_size(11)
    cr.move_to(x + 6, y + h - 5)
    cr.show_text(label)
    cr.set_font_size(7)
    vu = cr.text_extents("VU")
    cr.move_to(x + w - vu.width - 6, y + h - 5)
    cr.show_text("VU")
