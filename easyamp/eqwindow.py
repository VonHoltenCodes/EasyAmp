"""WinAmp-style 10-band graphic EQ window."""

from __future__ import annotations

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk  # noqa: E402

from . import eqpresets  # noqa: E402
from .player import EQ_FREQS, NBANDS  # noqa: E402

BAND_LABELS = ["29", "59", "119", "237", "474", "947", "1.9K", "3.8K", "7.5K", "15K"]
GAIN_MIN, GAIN_MAX = -24.0, 12.0


class EQWindow(Gtk.Window):
    def __init__(self, parent: Gtk.Window, player):
        super().__init__(title="EasyAmp Equalizer")
        self.set_transient_for(parent)
        self.player = player
        self._suppress = False
        self.add_css_class("easyamp")
        self.set_resizable(False)

        root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        root.add_css_class("eaa-eqroot")
        root.set_margin_top(10)
        root.set_margin_bottom(10)
        root.set_margin_start(10)
        root.set_margin_end(10)
        self.set_child(root)

        # ---- preset bar ----
        top = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        self.preset_model = Gtk.StringList()
        for name in eqpresets.list_presets():
            self.preset_model.append(name)
        self.preset_dd = Gtk.DropDown(model=self.preset_model)
        self.preset_dd.add_css_class("eaa-combo")
        self.preset_dd.set_hexpand(True)
        self.preset_dd.connect("notify::selected", self.on_preset)
        top.append(self.preset_dd)

        self.name_entry = Gtk.Entry()
        self.name_entry.set_placeholder_text("preset name")
        self.name_entry.add_css_class("eaa-combo")
        top.append(self.name_entry)

        save_btn = Gtk.Button(label="SAVE")
        save_btn.add_css_class("eaa-button")
        save_btn.connect("clicked", self.on_save)
        top.append(save_btn)

        reset_btn = Gtk.Button(label="RESET")
        reset_btn.add_css_class("eaa-button")
        reset_btn.connect("clicked", lambda *_: self.apply_values(0.0, [0.0] * NBANDS))
        top.append(reset_btn)
        root.append(top)

        # ---- sliders ----
        sliders = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        sliders.add_css_class("eaa-eqbank")

        # preamp first
        sliders.append(self._slider_col("PRE", -1))
        sep = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        sep.add_css_class("eaa-xsep")
        sliders.append(sep)

        self._bands: list[Gtk.Scale] = []
        for i in range(NBANDS):
            sliders.append(self._slider_col(BAND_LABELS[i], i))
        root.append(sliders)

        self.apply_values(0.0, [0.0] * NBANDS)

    def _slider_col(self, label: str, index: int) -> Gtk.Box:
        col = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=3)
        scale = Gtk.Scale.new_with_range(Gtk.Orientation.VERTICAL, GAIN_MIN, GAIN_MAX, 1.0)
        scale.add_css_class("eaa-eq")
        scale.set_inverted(True)        # up = boost
        scale.set_draw_value(False)
        scale.set_vexpand(True)
        scale.set_size_request(-1, 150)
        scale.add_mark(0.0, Gtk.PositionType.LEFT, None)  # 0 dB detent
        if index == -1:
            self._preamp = scale
            scale.connect("value-changed", self.on_preamp)
        else:
            self._bands.append(scale)
            scale.connect("value-changed", self.on_band, index)
        col.append(scale)
        lbl = Gtk.Label(label=label)
        lbl.add_css_class("eaa-eqlabel")
        col.append(lbl)
        return col

    # ---- apply / handlers --------------------------------------------
    def apply_values(self, preamp: float, bands: list[float]) -> None:
        self._suppress = True
        self._preamp.set_value(preamp)
        for i, g in enumerate(bands):
            self._bands[i].set_value(g)
        self._suppress = False
        self.player.set_preamp(preamp)
        for i, g in enumerate(bands):
            self.player.set_band(i, g)

    def on_preset(self, dd, _pspec) -> None:
        if self._suppress:
            return
        idx = dd.get_selected()
        names = eqpresets.list_presets()
        if 0 <= idx < len(names):
            preamp, bands = eqpresets.load(names[idx])
            self.name_entry.set_text(names[idx])
            self.apply_values(preamp, bands)

    def on_band(self, scale, index) -> None:
        if not self._suppress:
            self.player.set_band(index, scale.get_value())

    def on_preamp(self, scale) -> None:
        if not self._suppress:
            self.player.set_preamp(scale.get_value())

    def on_save(self, _btn) -> None:
        name = self.name_entry.get_text().strip() or "My EQ"
        bands = [s.get_value() for s in self._bands]
        eqpresets.save(name, self._preamp.get_value(), bands)
        # refresh dropdown
        existing = [self.preset_model.get_string(i)
                    for i in range(self.preset_model.get_n_items())]
        if name not in existing:
            self.preset_model.append(name)
