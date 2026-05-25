"""Docked 10-band graphic EQ panel with response curve and presets menu."""

from __future__ import annotations

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk  # noqa: E402

from . import eqpresets  # noqa: E402
from .player import NBANDS  # noqa: E402

BAND_LABELS = ["60", "170", "310", "600", "1K", "3K", "6K", "12K", "14K", "16K"]
GAIN_MIN, GAIN_MAX = -24.0, 12.0


class EQPanel(Gtk.Box):
    def __init__(self, player):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        self.player = player
        self._suppress = False
        self._on = True
        self._preamp_val = 0.0
        self._band_vals = [0.0] * NBANDS

        # ---- bar: ON | AUTO | PRESETS ----
        bar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        bar.add_css_class("eaa-panelbar")
        title = Gtk.Label(label="EQUALIZER")
        title.add_css_class("eaa-panelbar-label")
        bar.append(title)
        bar.append(Gtk.Box(hexpand=True))

        self.btn_on = Gtk.ToggleButton(label="ON")
        self.btn_on.add_css_class("eaa-button")
        self.btn_on.set_active(True)
        self.btn_on.add_css_class("on")
        self.btn_on.connect("toggled", self._on_toggle)
        bar.append(self.btn_on)

        self.presets_btn = Gtk.MenuButton(label="PRESETS")
        self.presets_btn.add_css_class("eaa-button")
        self.presets_btn.set_popover(self._build_presets_popover())
        bar.append(self.presets_btn)
        self.append(bar)

        # ---- response curve ----
        self.curve = Gtk.DrawingArea()
        self.curve.add_css_class("eaa-eqcurve")
        self.curve.set_content_height(34)
        self.curve.set_draw_func(self._draw_curve)
        self.append(self.curve)

        # ---- sliders: PREAMP | 10 bands ----
        bank = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        bank.add_css_class("eaa-eqbank")
        bank.append(self._slider_col("PRE", -1))
        sep = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        sep.add_css_class("eaa-xsep")
        bank.append(sep)
        self._bands: list[Gtk.Scale] = []
        for i in range(NBANDS):
            bank.append(self._slider_col(BAND_LABELS[i], i))
        self.append(bank)

    def _slider_col(self, label: str, index: int) -> Gtk.Box:
        col = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        col.set_hexpand(True)
        scale = Gtk.Scale.new_with_range(Gtk.Orientation.VERTICAL, GAIN_MIN, GAIN_MAX, 1.0)
        scale.add_css_class("eaa-eq")
        scale.set_inverted(True)
        scale.set_draw_value(False)
        scale.set_vexpand(True)
        scale.set_size_request(-1, 96)
        scale.set_value(0.0)
        if index == -1:
            self._preamp = scale
            scale.connect("value-changed", self._on_preamp)
        else:
            self._bands.append(scale)
            scale.connect("value-changed", self._on_band, index)
        col.append(scale)
        lbl = Gtk.Label(label=label)
        lbl.add_css_class("eaa-eqlabel")
        col.append(lbl)
        return col

    # ---- presets popover ----------------------------------------------
    def _build_presets_popover(self) -> Gtk.Popover:
        pop = Gtk.Popover()
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        box.set_margin_top(6)
        box.set_margin_bottom(6)
        box.set_margin_start(6)
        box.set_margin_end(6)

        scroller = Gtk.ScrolledWindow()
        scroller.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scroller.set_min_content_height(160)
        self._preset_list = Gtk.ListBox()
        self._preset_list.connect("row-activated", self._on_preset_row)
        scroller.set_child(self._preset_list)
        box.append(scroller)
        self._reload_preset_list()

        box.append(Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL))
        self._name_entry = Gtk.Entry()
        self._name_entry.set_placeholder_text("save as…")
        box.append(self._name_entry)
        save = Gtk.Button(label="SAVE PRESET")
        save.add_css_class("eaa-button")
        save.connect("clicked", self._on_save)
        box.append(save)

        pop.set_child(box)
        return pop

    def _reload_preset_list(self) -> None:
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

    def _on_preset_row(self, _lb, row) -> None:
        name = row.get_child().get_text()
        preamp, bands = eqpresets.load(name)
        self.apply_values(preamp, bands)
        self._name_entry.set_text(name)

    def _on_save(self, _btn) -> None:
        name = self._name_entry.get_text().strip() or "My EQ"
        eqpresets.save(name, self._preamp_val, list(self._band_vals))
        self._reload_preset_list()

    # ---- apply / handlers ---------------------------------------------
    def apply_values(self, preamp: float, bands: list[float]) -> None:
        self._suppress = True
        self._preamp.set_value(preamp)
        for i, g in enumerate(bands):
            self._bands[i].set_value(g)
        self._suppress = False
        self._preamp_val = preamp
        self._band_vals = list(bands)
        self._push()

    def _push(self) -> None:
        """Send current values to the player (or flat if EQ is off)."""
        if self._on:
            self.player.set_preamp(self._preamp_val)
            for i, g in enumerate(self._band_vals):
                self.player.set_band(i, g)
        else:
            self.player.set_preamp(0.0)
            self.player.reset_eq()
        self.curve.queue_draw()

    def _on_toggle(self, btn) -> None:
        self._on = btn.get_active()
        if self._on:
            btn.add_css_class("on")
        else:
            btn.remove_css_class("on")
        self._push()

    def _on_band(self, scale, index) -> None:
        if self._suppress:
            return
        self._band_vals[index] = scale.get_value()
        if self._on:
            self.player.set_band(index, self._band_vals[index])
        self.curve.queue_draw()

    def _on_preamp(self, scale) -> None:
        if self._suppress:
            return
        self._preamp_val = scale.get_value()
        if self._on:
            self.player.set_preamp(self._preamp_val)

    # ---- response curve drawing ---------------------------------------
    def _draw_curve(self, _area, cr, w, h) -> None:
        cr.set_source_rgb(0, 0, 0)
        cr.paint()
        # zero line
        cr.set_source_rgb(0.12, 0.30, 0.12)
        cr.set_line_width(1)
        cr.move_to(0, h / 2)
        cr.line_to(w, h / 2)
        cr.stroke()
        vals = self._band_vals if self._on else [0.0] * NBANDS
        n = len(vals)
        if n < 2:
            return
        cr.set_source_rgb(0.10, 0.90, 0.20)
        cr.set_line_width(2)
        for i, g in enumerate(vals):
            x = w * i / (n - 1)
            y = h / 2 - (g / GAIN_MAX) * (h / 2 - 3)
            cr.line_to(x, y) if i else cr.move_to(x, y)
        cr.stroke()
