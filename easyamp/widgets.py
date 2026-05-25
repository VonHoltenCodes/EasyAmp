"""Shared custom widgets: gold-bar title bars in the classic-player style."""

from __future__ import annotations

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
