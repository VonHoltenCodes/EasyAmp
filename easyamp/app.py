"""EasyAmp — a classic-player-style shell that remote-controls EasyEffects."""

from __future__ import annotations

import os

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, Gdk, Gio, GLib  # noqa: E402
import numpy as np  # noqa: E402

from .backend import EasyEffects  # noqa: E402
from .spectrum import SpectrumCapture  # noqa: E402

APP_ID = "codes.vonholten.EasyAmp"
STYLE = os.path.join(os.path.dirname(__file__), "style.css")
MARQUEE_WIDTH = 22
BANDS = 14


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

        self.add_css_class("easyamp")
        self.set_resizable(False)
        self.set_default_size(470, 150)

        # ---- title bar ------------------------------------------------
        titlebar = Gtk.CenterBox()
        titlebar.add_css_class("eaa-titlebar")
        title = Gtk.Label(label="E A S Y A M P")
        title.add_css_class("eaa-title")
        titlebar.set_start_widget(title)
        titlebar.set_end_widget(Gtk.WindowControls(side=Gtk.PackType.END))
        self.set_titlebar(titlebar)

        root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=5)
        root.set_margin_top(7)
        root.set_margin_bottom(7)
        root.set_margin_start(7)
        root.set_margin_end(7)
        self.set_child(root)

        # ---- display row ---------------------------------------------
        display = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        display.add_css_class("eaa-display")

        self.viz = Gtk.DrawingArea()
        self.viz.add_css_class("eaa-viz")
        self.viz.set_content_width(80)
        self.viz.set_content_height(42)
        self.viz.set_draw_func(self._draw_viz)
        display.append(self.viz)

        self.lcd_num = Gtk.Label(label="00")
        self.lcd_num.add_css_class("eaa-bignum")
        self.lcd_num.set_valign(Gtk.Align.CENTER)
        display.append(self.lcd_num)

        lcd = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        lcd.set_hexpand(True)
        lcd.set_valign(Gtk.Align.CENTER)

        inds = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
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

        # position / seek bar (scrubs through presets)
        self.seek = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 1, 1)
        self.seek.add_css_class("eaa-seek")
        self.seek.set_draw_value(False)
        self.seek.connect("value-changed", self.on_seek)
        lcd.append(self.seek)

        display.append(lcd)
        root.append(display)

        # ---- transport row -------------------------------------------
        xport = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
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

        self.preset_model = Gtk.StringList()
        self.dropdown = Gtk.DropDown(model=self.preset_model)
        self.dropdown.add_css_class("eaa-combo")
        self.dropdown.set_hexpand(True)
        self.dropdown.connect("notify::selected", self.on_dropdown)
        xport.append(self.dropdown)

        root.append(xport)

        # spectrum capture lifecycle
        self.spectrum = SpectrumCapture(bands=BANDS, on_data=self._on_spectrum)
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
        # update display live; debounce the actual (slow) preset load
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

    # ---- spectrum -----------------------------------------------------
    def _on_spectrum(self, levels) -> bool:
        self._levels = levels
        self.viz.queue_draw()
        return False

    def _draw_viz(self, _area, cr, w, h) -> None:
        cr.set_source_rgb(0, 0, 0)
        cr.paint()
        levels = self._levels
        n = len(levels)
        if n == 0 or w <= 0 or h <= 0:
            return
        gap = 2.0
        bw = (w - gap * (n + 1)) / n
        seg_h, seg_gap = 3.0, 1.0
        total_segs = max(int(h // (seg_h + seg_gap)), 1)
        for i in range(n):
            x = gap + i * (bw + gap)
            lit = int(float(levels[i]) * total_segs)
            for s in range(lit):
                y = h - (s + 1) * (seg_h + seg_gap)
                frac = s / max(total_segs - 1, 1)
                if frac > 0.85:
                    cr.set_source_rgb(0.88, 0.10, 0.10)
                elif frac > 0.62:
                    cr.set_source_rgb(0.88, 0.82, 0.10)
                else:
                    cr.set_source_rgb(0.10, 0.88, 0.18)
                cr.rectangle(x, y, bw, seg_h)
                cr.fill()

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
