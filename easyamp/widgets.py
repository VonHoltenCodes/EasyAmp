"""Shared custom widgets: gold-bar title bars in the classic-player style."""

from __future__ import annotations

import math

import cairo

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk  # noqa: E402

GOLD = (0.85, 0.70, 0.24)


class GoldBars(Gtk.DrawingArea):
    """Two parallel horizontal gold bars (the title-bar flourish)."""

    def __init__(self):
        super().__init__()
        self.set_hexpand(True)
        self.set_content_height(16)
        self.set_draw_func(self._draw)

    def _draw(self, _a, cr, w, h):
        if w < 6:
            return
        cr.set_source_rgb(*GOLD)
        cr.set_line_width(1.5)
        for dy in (-3, 3):
            y = round(h / 2 + dy) + 0.5
            cr.move_to(2, y)
            cr.line_to(w - 2, y)
        cr.stroke()


class TransportIcon(Gtk.DrawingArea):
    """A Cairo-drawn transport glyph (eject/prev/play/pause/stop/next).

    Drawn rather than set as a Unicode label so it renders identically on
    every platform — the U+23xx media symbols are missing from many fonts
    (notably the default Windows fallbacks), which would otherwise show as
    tofu boxes.
    """

    INK = (0.12, 0.12, 0.16)

    def __init__(self, kind: str):
        super().__init__()
        self._kind = kind
        self.set_content_width(14)
        self.set_content_height(14)
        self.set_draw_func(self._draw)

    def set_kind(self, kind: str) -> None:
        if kind != self._kind:
            self._kind = kind
            self.queue_draw()

    def _draw(self, _a, cr, w, h):
        cr.set_source_rgb(*self.INK)
        k = self._kind
        top, bot, mid = h * 0.28, h * 0.72, h * 0.5
        bar = w * 0.085

        def tri_right(x0, x1):
            cr.move_to(x0, top)
            cr.line_to(x0, bot)
            cr.line_to(x1, mid)
            cr.close_path()
            cr.fill()

        def tri_left(x0, x1):
            cr.move_to(x0, top)
            cr.line_to(x0, bot)
            cr.line_to(x1, mid)
            cr.close_path()
            cr.fill()

        if k == "play":
            tri_right(w * 0.36, w * 0.70)
        elif k == "pause":
            cr.rectangle(w * 0.36, top, bar * 1.4, bot - top)
            cr.rectangle(w * 0.55, top, bar * 1.4, bot - top)
            cr.fill()
        elif k == "stop":
            s = bot - top
            cr.rectangle(w * 0.5 - s / 2, top, s, s)
            cr.fill()
        elif k == "prev":
            cr.rectangle(w * 0.28, top, bar, bot - top)   # leading bar
            cr.fill()
            tri_left(w * 0.66, w * 0.42)                  # left-pointing triangle
        elif k == "next":
            tri_right(w * 0.34, w * 0.58)                 # right-pointing triangle
            cr.rectangle(w * 0.64 - bar, top, bar, bot - top)  # trailing bar
            cr.fill()
        elif k == "eject":
            cr.move_to(w * 0.5, top)                      # up triangle
            cr.line_to(w * 0.30, mid)
            cr.line_to(w * 0.70, mid)
            cr.close_path()
            cr.fill()
            cr.rectangle(w * 0.30, bot - h * 0.08, w * 0.40, h * 0.08)  # base bar
            cr.fill()


class StatusIndicator(Gtk.DrawingArea):
    """Stacked green (top) / red (bottom) square LEDs + a play triangle.
    Green lights on play, red lights on stop, triangle glows on play."""

    def __init__(self):
        super().__init__()
        self.state = "stop"
        self.set_content_width(30)
        self.set_content_height(18)
        self.set_draw_func(self._draw)

    def set_state(self, state: str) -> None:
        self.state = state
        self.queue_draw()

    def _draw(self, _a, cr, w, h):
        playing = self.state == "play"
        stopped = self.state == "stop"
        sq = 6
        cr.set_source_rgb(*((0.16, 1.0, 0.20) if playing else (0.05, 0.20, 0.07)))
        cr.rectangle(1, h / 2 - sq - 1, sq, sq)
        cr.fill()
        cr.set_source_rgb(*((1.0, 0.16, 0.12) if stopped else (0.26, 0.05, 0.04)))
        cr.rectangle(1, h / 2 + 1, sq, sq)
        cr.fill()
        tx = 1 + sq + 5
        cr.set_source_rgb(*((0.16, 1.0, 0.20) if playing else (0.10, 0.34, 0.12)))
        cr.move_to(tx, h * 0.22)
        cr.line_to(tx, h * 0.78)
        cr.line_to(tx + h * 0.56, h * 0.5)
        cr.close_path()
        cr.fill()


class SeekBar(Gtk.DrawingArea):
    """Custom position bar: green progress + light-grey grip with three
    short, centred vertical lines. Click/drag to seek."""

    def __init__(self, on_seek=None):
        super().__init__()
        self.on_seek = on_seek
        self.fraction = 0.0
        self._dragging = False
        self.set_content_height(15)
        self.set_hexpand(True)
        self.set_draw_func(self._draw)
        click = Gtk.GestureClick()
        click.connect("pressed", lambda g, n, x, y: self._seek_to(x))
        self.add_controller(click)
        drag = Gtk.GestureDrag()
        drag.connect("drag-begin", self._dbegin)
        drag.connect("drag-update", self._dupdate)
        drag.connect("drag-end", lambda *_: setattr(self, "_dragging", False))
        self.add_controller(drag)

    def set_fraction(self, f: float) -> None:
        if not self._dragging:
            self.fraction = max(0.0, min(1.0, f))
            self.queue_draw()

    def _seek_to(self, x: float) -> None:
        w = self.get_width()
        self.fraction = max(0.0, min(1.0, x / max(w, 1)))
        self.queue_draw()
        if self.on_seek:
            self.on_seek(self.fraction)

    def _dbegin(self, _g, x, _y):
        self._dragging = True
        self._seek_to(x)

    def _dupdate(self, g, dx, _dy):
        ok, sx, _ = g.get_start_point()
        if ok:
            self._seek_to(sx + dx)

    def _draw(self, _a, cr, w, h):
        gy, gh = h / 2, 5
        cr.set_source_rgb(0.02, 0.10, 0.02)            # groove
        cr.rectangle(1, gy - gh / 2, w - 2, gh)
        cr.fill()
        cr.set_source_rgb(0.12, 0.95, 0.14)            # green progress
        cr.rectangle(1, gy - gh / 2, max(0, (w - 2) * self.fraction), gh)
        cr.fill()

        tw, th = 14, h - 2
        tx = max(0, min(w - tw, (w - tw) * self.fraction))
        ty = 1
        cr.set_source_rgb(0.78, 0.78, 0.82)            # grey cap
        cr.rectangle(tx, ty, tw, th)
        cr.fill()
        cr.set_source_rgb(0.96, 0.96, 0.99)            # highlight TL
        cr.set_line_width(1)
        cr.move_to(tx + 0.5, ty + th)
        cr.line_to(tx + 0.5, ty + 0.5)
        cr.line_to(tx + tw - 0.5, ty + 0.5)
        cr.stroke()
        cr.set_source_rgb(0.25, 0.25, 0.30)            # shade BR
        cr.move_to(tx + tw - 0.5, ty + 0.5)
        cr.line_to(tx + tw - 0.5, ty + th)
        cr.line_to(tx + 0.5, ty + th)
        cr.stroke()
        cr.set_source_rgb(0.20, 0.20, 0.24)            # 3 short centred lines
        cr.set_line_width(1)
        cxm = tx + tw / 2
        for off in (-3, 0, 3):
            lx = round(cxm + off) + 0.5
            cr.move_to(lx, ty + th * 0.30)
            cr.line_to(lx, ty + th * 0.70)
            cr.stroke()


def make_button(label: str, toggle: bool = False, led: bool = False):
    """A beveled button, optionally a toggle and/or with a small square LED."""
    btn = Gtk.ToggleButton() if toggle else Gtk.Button()
    btn.add_css_class("eaa-button")
    if led:
        box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=5)
        box.set_halign(Gtk.Align.CENTER)
        ledw = Gtk.Box()
        ledw.add_css_class("eaa-led")
        ledw.set_valign(Gtk.Align.CENTER)
        box.append(ledw)
        box.append(Gtk.Label(label=label))
        btn.set_child(box)
        btn._led = ledw
    else:
        btn.set_label(label)
        btn._led = None
    return btn


def set_led(btn, on: bool) -> None:
    led = getattr(btn, "_led", None)
    if led is not None:
        (led.add_css_class if on else led.remove_css_class)("on")


def panel_bar(text: str) -> Gtk.Box:
    """A blue title bar: gold bars | centered EASYAMP label | gold bars."""
    box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
    box.add_css_class("eaa-panelbar")
    box.append(GoldBars())
    lbl = Gtk.Label(label=text)
    lbl.add_css_class("eaa-panelbar-label")
    box.append(lbl)
    box.append(GoldBars())
    return box


def window_title_bar(text: str) -> Gtk.WindowHandle:
    """Draggable window title bar with gold bars + window controls."""
    box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
    box.add_css_class("eaa-titlebar")
    box.append(GoldBars())
    lbl = Gtk.Label(label=text)
    lbl.add_css_class("eaa-title")
    box.append(lbl)
    box.append(GoldBars())
    box.append(Gtk.WindowControls(side=Gtk.PackType.END))
    handle = Gtk.WindowHandle()
    handle.set_child(box)
    return handle


class Knob(Gtk.DrawingArea):
    """A rotary knob: metallic body with a red grab-dot that turns to set the
    value, and a small green digital readout below. Drag up/down (or scroll)
    to turn; double-click resets to the default."""

    def __init__(self, vmin, vmax, value=0.0, step=0.5, default=None,
                 fmt=None, on_change=None):
        super().__init__()
        self.vmin, self.vmax, self.step = float(vmin), float(vmax), float(step)
        self.value = float(value)
        self.default = float(default if default is not None else value)
        self._fmt = fmt or (lambda v: f"{v:+.1f}")
        self.on_change = on_change
        self._start = self.value
        self.set_content_width(54)
        self.set_content_height(58)
        self.set_draw_func(self._draw)
        drag = Gtk.GestureDrag()
        drag.connect("drag-begin", lambda *_: setattr(self, "_start", self.value))
        drag.connect("drag-update", self._drag)
        self.add_controller(drag)
        scroll = Gtk.EventControllerScroll.new(Gtk.EventControllerScrollFlags.VERTICAL)
        scroll.connect("scroll", self._scroll)
        self.add_controller(scroll)
        click = Gtk.GestureClick()
        click.connect("pressed", self._click)
        self.add_controller(click)

    # ---- value ----
    def _apply(self, v, notify=True):
        v = max(self.vmin, min(self.vmax, v))
        v = round(v / self.step) * self.step
        if v != self.value:
            self.value = v
            self.queue_draw()
            if notify and self.on_change:
                self.on_change(v)
        else:
            self.queue_draw()

    def set_value(self, v):
        self._apply(v, notify=False)

    def get_value(self):
        return self.value

    def _drag(self, g, dx, dy):
        # horizontal: drag right raises, left lowers
        rng = self.vmax - self.vmin
        self._apply(self._start + dx / 150.0 * rng)

    def _scroll(self, _c, _dx, dy):
        self._apply(self.value - dy * self.step)
        return True

    def _click(self, g, n_press, x, y):
        if n_press >= 2:
            self._apply(self.default)

    # ---- drawing ----
    def _draw(self, _a, cr, w, h):
        kr = min(w * 0.42, (h - 16) * 0.5)
        cx, cy = w / 2, kr + 3
        cr.arc(cx, cy, kr + 2, 0, 2 * math.pi)          # bezel
        cr.set_source_rgb(0.05, 0.05, 0.07)
        cr.fill()
        grad = cairo.RadialGradient(cx - kr * 0.35, cy - kr * 0.35, kr * 0.05, cx, cy, kr)
        grad.add_color_stop_rgb(0.0, 0.72, 0.72, 0.78)   # lighter brushed center
        grad.add_color_stop_rgb(0.55, 0.42, 0.42, 0.47)
        grad.add_color_stop_rgb(1.0, 0.16, 0.16, 0.19)   # dark rim
        cr.arc(cx, cy, kr, 0, 2 * math.pi)
        cr.set_source(grad)
        cr.fill()
        cr.arc(cx, cy, kr, 0, 2 * math.pi)
        cr.set_line_width(1)
        cr.set_source_rgb(0.55, 0.55, 0.60)
        cr.stroke()
        # red grab-dot at the value angle (225° sweep over the top to -45°)
        t = (self.value - self.vmin) / (self.vmax - self.vmin) if self.vmax > self.vmin else 0
        ang = math.radians(225 - 270 * t)
        dx, dy = math.cos(ang), -math.sin(ang)
        dotr = max(2.0, kr * 0.16)
        cr.arc(cx + kr * 0.6 * dx, cy + kr * 0.6 * dy, dotr, 0, 2 * math.pi)
        cr.set_source_rgb(0.95, 0.16, 0.12)
        cr.fill()
        cr.arc(cx + kr * 0.6 * dx, cy + kr * 0.6 * dy, dotr, 0, 2 * math.pi)
        cr.set_line_width(0.6)
        cr.set_source_rgb(0.3, 0.0, 0.0)
        cr.stroke()
        # green LCD readout
        txt = self._fmt(self.value)
        cr.select_font_face("monospace")
        cr.set_font_size(9)
        cr.set_source_rgb(0.16, 1.0, 0.16)
        ext = cr.text_extents(txt)
        cr.move_to(cx - ext.width / 2, h - 3)
        cr.show_text(txt)


class LedMeter(Gtk.DrawingArea):
    """Horizontal LED-segment dB meters for left/right (EasyEffects style):
    a row of segments per channel that light green→yellow→red up to the
    current level, with a peak-hold tick and a dB readout. Fed normalized
    0..1 levels (≈ -50..0 dB)."""

    def __init__(self):
        super().__init__()
        self.l = self.r = 0.0
        self.pl = self.pr = 0.0          # peak hold
        self.set_content_height(40)
        self.set_hexpand(True)
        self.set_draw_func(self._draw)

    def set_levels(self, l, r):
        self.l = max(0.0, min(1.0, float(l)))
        self.r = max(0.0, min(1.0, float(r)))
        self.pl = max(self.l, self.pl - 0.012)
        self.pr = max(self.r, self.pr - 0.012)
        self.queue_draw()

    @staticmethod
    def _seg_color(frac):
        g, y, r = (0.12, 0.85, 0.22), (0.92, 0.82, 0.12), (0.94, 0.16, 0.10)
        if frac < 0.6:
            t = frac / 0.6
            return tuple(g[i] + (y[i] - g[i]) * t for i in range(3))
        t = (frac - 0.6) / 0.4
        return tuple(y[i] + (r[i] - y[i]) * t for i in range(3))

    def _bar(self, cr, x0, y, w, h, level, peak):
        seg, gap = 4.0, 2.0
        n = max(1, int(w // (seg + gap)))
        for i in range(n):
            frac = i / (n - 1) if n > 1 else 0.0
            col = self._seg_color(frac)
            if frac > level:
                col = tuple(c * 0.16 for c in col)     # unlit = dim
            cr.set_source_rgb(*col)
            cr.rectangle(x0 + i * (seg + gap), y, seg, h)
            cr.fill()
        if peak > 0.01:                                # peak-hold tick
            pi = min(n - 1, int(peak * (n - 1)))
            cr.set_source_rgb(*self._seg_color(pi / (n - 1) if n > 1 else 0))
            cr.rectangle(x0 + pi * (seg + gap), y - 1, seg, h + 2)
            cr.fill()

    def _draw(self, _a, cr, w, h):
        cr.set_source_rgb(0, 0, 0)
        cr.paint()
        cr.select_font_face("monospace")
        cr.set_font_size(9)
        lbl_w, db_w = 12.0, 30.0
        bar_w = w - lbl_w - db_w - 4
        bh = (h - 9) / 2
        for idx, (lab, lvl, pk) in enumerate((("L", self.l, self.pl),
                                              ("R", self.r, self.pr))):
            y = 2 + idx * (bh + 4)
            cr.set_source_rgb(0.45, 0.6, 0.85)
            cr.move_to(1, y + bh - 1)
            cr.show_text(lab)
            self._bar(cr, lbl_w, y, bar_w, bh, lvl, pk)
            db = lvl * 50.0 - 50.0
            cr.set_source_rgb(0.5, 0.85, 0.55)
            cr.move_to(lbl_w + bar_w + 5, y + bh - 1)
            cr.show_text(f"{db:+.0f}")
