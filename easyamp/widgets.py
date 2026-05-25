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
