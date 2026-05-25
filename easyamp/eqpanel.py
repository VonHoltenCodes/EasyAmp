"""Docked EQ panel: gold-bar header, ON/PRESETS, and the custom EQ widget."""

from __future__ import annotations

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk  # noqa: E402

from . import eqpresets  # noqa: E402
from .eqwidget import EQWidget, NBANDS  # noqa: E402
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
        ctl.append(Gtk.Box(hexpand=True))
        self.presets_btn = Gtk.MenuButton(label="PRESETS")
        self.presets_btn.add_css_class("eaa-button")
        self.presets_btn.set_popover(self._build_popover())
        ctl.append(self.presets_btn)
        self.append(ctl)

        self.eq = EQWidget(on_change=self._on_change)
        self.eq.add_css_class("eaa-eqbank")
        self.append(self.eq)
        # load the user's default EQ curve on startup, if saved
        if "EASYAMP DEFAULT" in eqpresets.list_presets():
            preamp, bands = eqpresets.load("EASYAMP DEFAULT")
            self.eq.set_values(preamp, bands)
        self._push()

    # ---- presets popover ---------------------------------------------
    def _build_popover(self):
        pop = Gtk.Popover()
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        for m in ("top", "bottom", "start", "end"):
            getattr(box, f"set_margin_{m}")(6)
        scroller = Gtk.ScrolledWindow()
        scroller.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scroller.set_min_content_height(160)
        self._preset_list = Gtk.ListBox()
        self._preset_list.connect("row-activated", self._on_preset_row)
        scroller.set_child(self._preset_list)
        box.append(scroller)
        self._reload_presets()
        box.append(Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL))
        self._name = Gtk.Entry()
        self._name.set_placeholder_text("save as…")
        box.append(self._name)
        save = Gtk.Button(label="SAVE PRESET")
        save.add_css_class("eaa-button")
        save.connect("clicked", self._on_save)
        box.append(save)
        pop.set_child(box)
        return pop

    def _reload_presets(self):
        child = self._preset_list.get_first_child()
        while child:
            nxt = child.get_next_sibling()
            self._preset_list.remove(child)
            child = nxt
        for name in eqpresets.list_presets():
            row = Gtk.ListBoxRow()
            lbl = Gtk.Label(label=name, xalign=0)
            lbl.set_margin_start(6)
            lbl.set_margin_end(6)
            row.set_child(lbl)
            self._preset_list.append(row)

    def _on_preset_row(self, _lb, row):
        name = row.get_child().get_text()
        preamp, bands = eqpresets.load(name)
        self.eq.set_values(preamp, bands)
        self._name.set_text(name)
        self._push()

    def _on_save(self, _btn):
        name = self._name.get_text().strip() or "My EQ"
        eqpresets.save(name, self.eq.preamp, list(self.eq.bands))
        self._reload_presets()

    # ---- bypass / push ------------------------------------------------
    def _on_toggle(self, btn):
        self._on = btn.get_active()
        set_led(btn, self._on)
        self._push()

    def _on_change(self):
        self._push()

    def _push(self):
        if self._on:
            self.player.set_preamp(self.eq.preamp)
            for i in range(NBANDS):
                self.player.set_band(i, self.eq.bands[i])
        else:
            self.player.set_preamp(0.0)
            self.player.reset_eq()
