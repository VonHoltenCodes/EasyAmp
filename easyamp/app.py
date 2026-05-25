"""EasyAmp — a classic-player-style shell that remote-controls EasyEffects."""

from __future__ import annotations

import os

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, Gdk, Gio, GLib  # noqa: E402

from .backend import EasyEffects  # noqa: E402

APP_ID = "codes.vonholten.EasyAmp"
STYLE = os.path.join(os.path.dirname(__file__), "style.css")


class EasyAmpWindow(Gtk.ApplicationWindow):
    def __init__(self, app: Gtk.Application, ee: EasyEffects):
        super().__init__(application=app, title="EasyAmp")
        self.ee = ee
        self.add_css_class("easyamp")
        self.set_resizable(False)
        self.set_default_size(420, 230)

        # ---- custom title bar -----------------------------------------
        titlebar = Gtk.CenterBox()
        titlebar.add_css_class("eaa-titlebar")
        title = Gtk.Label(label="E A S Y A M P")
        title.add_css_class("eaa-title")
        titlebar.set_center_widget(title)
        titlebar.set_end_widget(Gtk.WindowControls(side=Gtk.PackType.END))
        self.set_titlebar(titlebar)

        # ---- body -----------------------------------------------------
        root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        root.set_margin_top(10)
        root.set_margin_bottom(10)
        root.set_margin_start(12)
        root.set_margin_end(12)
        self.set_child(root)

        # LCD display: active preset + bypass state
        display = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        display.add_css_class("eaa-display")
        cap = Gtk.Label(label="ACTIVE PRESET", xalign=0)
        cap.add_css_class("eaa-lcd-label")
        self.lcd = Gtk.Label(label="--", xalign=0)
        self.lcd.add_css_class("eaa-lcd")
        display.append(cap)
        display.append(self.lcd)
        root.append(display)

        # spectrum visualizer placeholder (phase 2)
        self.viz = Gtk.DrawingArea()
        self.viz.add_css_class("eaa-viz")
        self.viz.set_content_height(56)
        root.append(self.viz)

        # controls row: preset dropdown + bypass
        controls = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)

        self.preset_model = Gtk.StringList()
        self.dropdown = Gtk.DropDown(model=self.preset_model)
        self.dropdown.add_css_class("eaa-combo")
        self.dropdown.set_hexpand(True)
        self.dropdown.connect("notify::selected", self.on_preset_selected)
        controls.append(self.dropdown)

        self.led = Gtk.Box()
        self.led.add_css_class("eaa-led")
        self.led.set_valign(Gtk.Align.CENTER)
        controls.append(self.led)

        self.bypass_btn = Gtk.Button(label="BYPASS")
        self.bypass_btn.add_css_class("eaa-button")
        self.bypass_btn.connect("clicked", self.on_bypass_clicked)
        controls.append(self.bypass_btn)

        root.append(controls)

        # initial state (deferred so the window paints first)
        GLib.idle_add(self.refresh)

    # ---- state sync ---------------------------------------------------
    def refresh(self) -> bool:
        self.ee.ensure_running()
        presets = self.ee.list_presets()
        self._presets = presets.output
        # repopulate dropdown
        while self.preset_model.get_n_items():
            self.preset_model.remove(0)
        for name in self._presets:
            self.preset_model.append(name)

        active = self.ee.active_preset("output")
        self.lcd.set_text(active or "--")
        if active in self._presets:
            self._suppress = True
            self.dropdown.set_selected(self._presets.index(active))
            self._suppress = False

        self._set_bypass_ui(self.ee.get_bypass())
        return False  # don't repeat

    def _set_bypass_ui(self, bypassed: bool) -> None:
        # LED is ON when effects are ACTIVE (i.e. NOT bypassed)
        if bypassed:
            self.led.remove_css_class("on")
            self.bypass_btn.add_css_class("active")
        else:
            self.led.add_css_class("on")
            self.bypass_btn.remove_css_class("active")

    # ---- handlers -----------------------------------------------------
    def on_preset_selected(self, dropdown, _pspec) -> None:
        if getattr(self, "_suppress", False):
            return
        idx = dropdown.get_selected()
        if 0 <= idx < len(self._presets):
            name = self._presets[idx]
            self.ee.load_preset(name)
            self.lcd.set_text(name)

    def on_bypass_clicked(self, _btn) -> None:
        bypassed = self.ee.toggle_bypass()
        self._set_bypass_ui(bypassed)


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
