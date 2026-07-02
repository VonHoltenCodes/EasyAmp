"""Docked EQ panel: gold-bar header, ON/BASS/LOUD toggles, presets, and the
custom-drawn 10-band EQ widget."""

from __future__ import annotations

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk  # noqa: E402

from . import eqpresets  # noqa: E402
from .eqwidget import EQWidget, NBANDS  # noqa: E402
from .presetui import PresetPopover  # noqa: E402
from .widgets import panel_bar, make_button, set_led  # noqa: E402


class EQPanel(Gtk.Box):
    def __init__(self, player):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        self.player = player
        self._on = True

        self.append(panel_bar("EASYAMP EQUALIZER"))

        ctl = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        ctl.add_css_class("eaa-transport")
        self.btn_on = make_button("ON", toggle=True, led=True)
        self.btn_on.set_active(True)
        set_led(self.btn_on, True)
        self.btn_on.connect("toggled", self._on_toggle)
        ctl.append(self.btn_on)

        self.btn_bass = make_button("BASS", toggle=True, led=True)
        self.btn_bass.connect("toggled", self._on_tone)
        ctl.append(self.btn_bass)
        self.btn_loud = make_button("LOUD", toggle=True, led=True)
        self.btn_loud.connect("toggled", self._on_tone)
        ctl.append(self.btn_loud)

        ctl.append(Gtk.Box(hexpand=True))
        self.presets_btn = Gtk.MenuButton(label="PRESETS")
        self.presets_btn.add_css_class("eaa-button")
        self.presets = PresetPopover(on_apply=self._apply_named,
                                     on_save=self._save_named)
        self.presets_btn.set_popover(self.presets)
        ctl.append(self.presets_btn)
        self.append(ctl)

        self.eq = EQWidget(on_change=self._push)
        self.eq.add_css_class("eaa-eqbank")
        self.append(self.eq)
        # load the user's default EQ curve on startup, if saved
        if "EASYAMP DEFAULT" in eqpresets.list_presets():
            preamp, bands = eqpresets.load("EASYAMP DEFAULT")
            self.eq.set_values(preamp, bands)
        self._push()

    # ---- presets ------------------------------------------------------
    def _apply_named(self, name):
        preamp, bands = eqpresets.load(name)
        self.eq.set_values(preamp, bands)
        self._push()

    def _save_named(self, name):
        eqpresets.save(name, self.eq.preamp, list(self.eq.bands))

    def apply_preset(self, preamp, bands):
        """Apply a (preamp, 10-band) preset and push to the engine. Used by the
        full EQ view so both EQ surfaces stay in sync. Presets are 10-band, so
        ensure the engine is in 10-band mode first."""
        self.player.set_band_count(NBANDS)
        self.eq.set_values(preamp, bands)
        self._push()

    # ---- bypass / push ------------------------------------------------
    def _on_toggle(self, btn):
        self._on = btn.get_active()
        set_led(btn, self._on)
        self._push()

    def _on_tone(self, _btn):
        bass = self.btn_bass.get_active()
        loud = self.btn_loud.get_active()
        set_led(self.btn_bass, bass)
        set_led(self.btn_loud, loud)
        self.player.set_tone(bass, loud)

    def _push(self):
        if self._on:
            self.player.set_preamp(self.eq.preamp)
            for i in range(NBANDS):
                self.player.set_band(i, self.eq.bands[i])
        else:
            self.player.set_preamp(0.0)
            self.player.reset_eq()
