"""EasyAmp — a classic-player-style shell that remote-controls EasyEffects."""

from __future__ import annotations

import math
import os

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, Gdk, Gio, GLib  # noqa: E402
import numpy as np  # noqa: E402

from .backend import EasyEffects  # noqa: E402
from .spectrum import SpectrumCapture  # noqa: E402

APP_ID = "codes.vonholten.EasyAmp"
STYLE = os.path.join(os.path.dirname(__file__), "style.css")
MARQUEE_WIDTH = 24
BANDS = 20


class Marquee:
    """Scrolls long text across a fixed-width label, player-display style."""

    def __init__(self, label: Gtk.Label, width: int = MARQUEE_WIDTH, interval: int = 220):
        self.label = label
        self.width = width
        self.interval = interval
        self._src: int | None = None
        self._scroll = ""
        self._pos = 0

    def set_text(self, text: str) -> None:
        text = text or "--"
        if self._src is not None:
            GLib.source_remove(self._src)
            self._src = None
        self._pos = 0
        if len(text) <= self.width:
            self.label.set_text(text.ljust(self.width))
        else:
            self._scroll = text + "   ***   "
            self._src = GLib.timeout_add(self.interval, self._tick)
            self._tick()

    def _tick(self) -> bool:
        s = self._scroll
        self.label.set_text((s[self._pos:] + s[: self._pos])[: self.width])
        self._pos = (self._pos + 1) % len(s)
        return True


class EasyAmpWindow(Gtk.ApplicationWindow):
    def __init__(self, app: Gtk.Application, ee: EasyEffects):
        super().__init__(application=app, title="EasyAmp")
        self.ee = ee
        self._presets: list[str] = []
        self._idx = -1
        self._suppress = False
        self._seek_src: int | None = None
        self._levels = np.zeros(BANDS, dtype=np.float32)
        self._peaks = np.zeros(BANDS, dtype=np.float32)
        self._vu = (0.0, 0.0)
        self._viz_mode = "spec"  # "spec" or "vu"

        self.add_css_class("easyamp")
        self.set_resizable(True)
        self.set_default_size(640, 360)
        self.set_size_request(500, 300)

        # ---- draggable title bar (WindowHandle makes it a drag region) ----
        bar = Gtk.CenterBox()
        bar.add_css_class("eaa-titlebar")
        title = Gtk.Label(label="E A S Y A M P")
        title.add_css_class("eaa-title")
        bar.set_start_widget(title)
        bar.set_end_widget(Gtk.WindowControls(side=Gtk.PackType.END))
        handle = Gtk.WindowHandle()
        handle.set_child(bar)
        self.set_titlebar(handle)

        root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        root.set_margin_top(8)
        root.set_margin_bottom(8)
        root.set_margin_start(8)
        root.set_margin_end(8)
        self.set_child(root)

        # ---- info strip: big number + indicators + marquee + seek ----
        info = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        info.add_css_class("eaa-display")

        self.lcd_num = Gtk.Label(label="00")
        self.lcd_num.add_css_class("eaa-bignum")
        self.lcd_num.set_valign(Gtk.Align.CENTER)
        info.append(self.lcd_num)

        lcd = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=3)
        lcd.set_hexpand(True)
        lcd.set_valign(Gtk.Align.CENTER)

        inds = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        self.ind_state = Gtk.Label(label="ACTIVE")
        self.ind_state.add_css_class("eaa-ind")
        self.ind_chan = Gtk.Label(label="STEREO")
        self.ind_chan.add_css_class("eaa-ind")
        self.ind_chan.add_css_class("on")
        self.ind_num = Gtk.Label(label="OF --", xalign=1)
        self.ind_num.add_css_class("eaa-ind")
        self.ind_num.set_hexpand(True)
        inds.append(self.ind_state)
        inds.append(self.ind_chan)
        inds.append(self.ind_num)
        lcd.append(inds)

        self.marquee_lbl = Gtk.Label(label="--", xalign=0)
        self.marquee_lbl.add_css_class("eaa-lcd")
        lcd.append(self.marquee_lbl)
        self.marquee = Marquee(self.marquee_lbl)

        self.seek = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 1, 1)
        self.seek.add_css_class("eaa-seek")
        self.seek.set_draw_value(False)
        self.seek.connect("value-changed", self.on_seek)
        lcd.append(self.seek)

        info.append(lcd)
        root.append(info)

        # ---- large expanding visualizer panel ----
        self.viz = Gtk.DrawingArea()
        self.viz.add_css_class("eaa-viz")
        self.viz.set_vexpand(True)
        self.viz.set_hexpand(True)
        self.viz.set_content_height(120)
        self.viz.set_draw_func(self._draw_viz)
        root.append(self.viz)

        # ---- transport row ----
        xport = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=5)
        xport.add_css_class("eaa-transport")

        self.btn_prev = self._xport_btn("⏮", self.on_prev)
        self.btn_bypass = self._xport_btn("⏵", self.on_bypass)
        self.btn_next = self._xport_btn("⏭", self.on_next)
        xport.append(self.btn_prev)
        xport.append(self.btn_bypass)
        xport.append(self.btn_next)

        sep = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        sep.add_css_class("eaa-xsep")
        xport.append(sep)

        self.btn_viz = Gtk.Button(label="VU")
        self.btn_viz.add_css_class("eaa-button")
        self.btn_viz.set_tooltip_text("Toggle spectrum / VU meters")
        self.btn_viz.connect("clicked", self.on_toggle_viz)
        xport.append(self.btn_viz)

        self.preset_model = Gtk.StringList()
        self.dropdown = Gtk.DropDown(model=self.preset_model)
        self.dropdown.add_css_class("eaa-combo")
        self.dropdown.set_hexpand(True)
        self.dropdown.connect("notify::selected", self.on_dropdown)
        xport.append(self.dropdown)

        root.append(xport)

        # spectrum lifecycle
        self.spectrum = SpectrumCapture(bands=BANDS, on_data=self._on_data)
        self.connect("map", lambda *_: self.spectrum.start())
        self.connect("close-request", self._on_close)

        GLib.idle_add(self.refresh)

    def _xport_btn(self, glyph: str, cb) -> Gtk.Button:
        b = Gtk.Button(label=glyph)
        b.add_css_class("eaa-xport")
        b.connect("clicked", cb)
        return b

    # ---- state sync ---------------------------------------------------
    def refresh(self) -> bool:
        self.ee.ensure_running()
        self._presets = self.ee.list_presets().output
        while self.preset_model.get_n_items():
            self.preset_model.remove(0)
        for name in self._presets:
            self.preset_model.append(name)
        self.seek.set_range(0, max(0, len(self._presets) - 1))

        active = self.ee.active_preset("output")
        idx = self._presets.index(active) if active in self._presets else 0
        self._go_to(idx, load=False)
        self._set_bypass_ui(self.ee.get_bypass())
        return False

    def _go_to(self, idx: int, load: bool) -> None:
        if not self._presets:
            return
        self._idx = idx % len(self._presets)
        name = self._presets[self._idx]
        self._suppress = True
        self.dropdown.set_selected(self._idx)
        self.seek.set_value(self._idx)
        self._suppress = False
        self.marquee.set_text(name)
        self.lcd_num.set_text(f"{self._idx + 1:02d}")
        self.ind_num.set_text(f"OF {len(self._presets):02d}")
        if load:
            self.ee.load_preset(name)

    def _set_bypass_ui(self, bypassed: bool) -> None:
        self.btn_bypass.set_label("⏹" if bypassed else "⏵")
        if bypassed:
            self.btn_bypass.remove_css_class("playing")
            self.ind_state.set_text("BYPASS")
            self.ind_state.remove_css_class("on")
        else:
            self.btn_bypass.add_css_class("playing")
            self.ind_state.set_text("ACTIVE")
            self.ind_state.add_css_class("on")

    # ---- handlers -----------------------------------------------------
    def on_prev(self, _b):
        self._go_to((self._idx - 1) if self._idx >= 0 else 0, load=True)

    def on_next(self, _b):
        self._go_to((self._idx + 1) if self._idx >= 0 else 0, load=True)

    def on_bypass(self, _b):
        self._set_bypass_ui(self.ee.toggle_bypass())

    def on_toggle_viz(self, _b):
        self._viz_mode = "vu" if self._viz_mode == "spec" else "spec"
        self.btn_viz.set_label("SPEC" if self._viz_mode == "vu" else "VU")
        self.viz.queue_draw()

    def on_dropdown(self, dropdown, _pspec):
        if self._suppress:
            return
        self._go_to(dropdown.get_selected(), load=True)

    def on_seek(self, scale):
        if self._suppress:
            return
        idx = int(round(scale.get_value()))
        if not self._presets:
            return
        self._idx = idx
        self.marquee.set_text(self._presets[idx])
        self.lcd_num.set_text(f"{idx + 1:02d}")
        self._suppress = True
        self.dropdown.set_selected(idx)
        self._suppress = False
        if self._seek_src is not None:
            GLib.source_remove(self._seek_src)
        self._seek_src = GLib.timeout_add(220, self._commit_seek)

    def _commit_seek(self) -> bool:
        self._seek_src = None
        if 0 <= self._idx < len(self._presets):
            self.ee.load_preset(self._presets[self._idx])
        return False

    # ---- visualizer ---------------------------------------------------
    def _on_data(self, levels, vu) -> bool:
        self._levels = levels
        self._peaks = np.maximum(levels, self._peaks - 0.015)
        self._vu = vu
        self.viz.queue_draw()
        return False

    def _draw_viz(self, _area, cr, w, h) -> None:
        cr.set_source_rgb(0, 0, 0)
        cr.paint()
        if w <= 0 or h <= 0:
            return
        if self._viz_mode == "vu":
            self._draw_vu(cr, w, h)
        else:
            self._draw_spectrum(cr, w, h)

    def _draw_spectrum(self, cr, w, h) -> None:
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
                if frac > 0.85:
                    cr.set_source_rgb(0.90, 0.12, 0.10)
                elif frac > 0.62:
                    cr.set_source_rgb(0.90, 0.82, 0.12)
                else:
                    cr.set_source_rgb(0.12, 0.90, 0.20)
                cr.rectangle(x, y, bw, seg_h)
                cr.fill()
            # falling peak cap
            ps = int(float(peaks[i]) * total)
            if ps > 0:
                y = h - ps * (seg_h + seg_gap)
                cr.set_source_rgb(0.80, 1.0, 0.85)
                cr.rectangle(x, y, bw, seg_h * 0.55)
                cr.fill()

    def _draw_vu(self, cr, w, h) -> None:
        gap = 8.0
        cell = (w - gap * 3) / 2
        self._vu_gauge(cr, gap, gap, cell, h - 2 * gap, self._vu[0], "L")
        self._vu_gauge(cr, gap * 2 + cell, gap, cell, h - 2 * gap, self._vu[1], "R")

    @staticmethod
    def _rrect(cr, x, y, w, h, r) -> None:
        cr.new_sub_path()
        cr.arc(x + w - r, y + r, r, -math.pi / 2, 0)
        cr.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
        cr.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
        cr.arc(x + r, y + r, r, math.pi, 1.5 * math.pi)
        cr.close_path()

    def _vu_gauge(self, cr, x, y, w, h, value, label) -> None:
        # cream face
        cr.set_source_rgb(0.92, 0.89, 0.76)
        self._rrect(cr, x, y, w, h, 4)
        cr.fill()
        cx = x + w / 2
        base_y = y + h * 0.90
        r = min(w * 0.40, h * 0.74)

        def pt(frac, rad):
            a = math.radians(135 - frac * 90)
            return cx + rad * math.cos(a), base_y - rad * math.sin(a)

        # scale arc
        cr.set_line_width(2)
        cr.set_source_rgb(0.10, 0.10, 0.10)
        cr.move_to(*pt(0.0, r))
        for i in range(1, 41):
            cr.line_to(*pt(i / 40, r))
        cr.stroke()
        # red overload zone (top 20%)
        cr.set_line_width(3)
        cr.set_source_rgb(0.82, 0.12, 0.10)
        cr.move_to(*pt(0.8, r))
        for i in range(1, 21):
            cr.line_to(*pt(0.8 + 0.2 * i / 20, r))
        cr.stroke()
        # ticks
        cr.set_line_width(1.5)
        cr.set_source_rgb(0.10, 0.10, 0.10)
        for t in range(11):
            x1, y1 = pt(t / 10, r - 5)
            x2, y2 = pt(t / 10, r)
            cr.move_to(x1, y1)
            cr.line_to(x2, y2)
            cr.stroke()
        # needle
        nx, ny = pt(max(0.0, min(1.0, value)), r)
        cr.set_line_width(2.5)
        cr.set_source_rgb(0.05, 0.05, 0.05)
        cr.move_to(cx, base_y)
        cr.line_to(nx, ny)
        cr.stroke()
        cr.arc(cx, base_y, 3, 0, 2 * math.pi)
        cr.fill()
        # label
        cr.select_font_face("monospace")
        cr.set_font_size(11)
        cr.move_to(x + 6, y + h - 6)
        cr.show_text(label)

    def _on_close(self, *_):
        self.spectrum.stop()
        return False


class EasyAmpApp(Gtk.Application):
    def __init__(self):
        super().__init__(application_id=APP_ID,
                         flags=Gio.ApplicationFlags.DEFAULT_FLAGS)
        self.ee = EasyEffects()

    def do_startup(self):
        Gtk.Application.do_startup(self)
        provider = Gtk.CssProvider()
        provider.load_from_path(STYLE)
        Gtk.StyleContext.add_provider_for_display(
            Gdk.Display.get_default(),
            provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION,
        )

    def do_activate(self):
        win = self.props.active_window or EasyAmpWindow(self, self.ee)
        win.present()


def main() -> int:
    return EasyAmpApp().run(None)
