"""Full-window equalizer view (the EQUALIZER footer tab).

A compact player strip stays docked at the top so playback is always
controllable, and the rest of the window is the expanded N-band parametric
EQ plus its controls (band count, in/out gain, pitch, balance, preamp).
"""

from __future__ import annotations

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, Pango  # noqa: E402

from .eqbank import EQBank


def _labeled(label_text, widget):
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
    box.add_css_class("eaa-ctl")
    lbl = Gtk.Label(label=label_text)
    lbl.add_css_class("eaa-ctl-lbl")
    box.append(lbl)
    box.append(widget)
    return box


class EQView(Gtk.Box):
    def __init__(self, win):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=3)
        self.win = win
        self.add_css_class("eaa-chassis")

        # ---- mini player strip ----
        mini = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        mini.add_css_class("eaa-display")
        self.m_play = win._xport("play", win.on_playpause)
        for b in (win._xport("eject", win.on_open), win._xport("prev", win.on_prev),
                  self.m_play, win._xport("stop", win.on_stop),
                  win._xport("next", win.on_next)):
            b.add_css_class("eaa-xport")
            mini.append(b)
        self.m_time = Gtk.Label(label="00:00")
        self.m_time.add_css_class("eaa-ind")
        mini.append(self.m_time)
        self.m_track = Gtk.Label(label="EASYAMP  *  READY", xalign=0)
        self.m_track.add_css_class("eaa-lcd")
        self.m_track.set_hexpand(True)
        self.m_track.set_ellipsize(Pango.EllipsizeMode.END)
        mini.append(self.m_track)
        self.append(mini)

        # ---- the expanded EQ bank ----
        self.bank = EQBank(on_change=self._on_band, on_select=self._on_select)
        frame = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        frame.add_css_class("eaa-eqbank")
        frame.set_vexpand(True)
        frame.append(self.bank)
        self.append(frame)

        # ---- controls row ----
        ctl = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        ctl.add_css_class("eaa-panel")

        self.bands_scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 10, 32, 1)
        self.bands_scale.set_value(win.player.band_count())
        self.bands_scale.set_size_request(130, -1)
        self.bands_scale.set_draw_value(True)
        self.bands_scale.set_digits(0)
        self.bands_scale.connect("value-changed", self._on_bands)
        ctl.append(_labeled("BANDS", self.bands_scale))

        self.preamp = self._db_scale(self._on_preamp, -12, 12)
        ctl.append(_labeled("PREAMP", self.preamp))
        self.in_gain = self._db_scale(lambda v: win.player.set_in_gain(v), -12, 12)
        ctl.append(_labeled("IN", self.in_gain))
        self.out_gain = self._db_scale(lambda v: win.player.set_out_gain(v), -12, 12)
        ctl.append(_labeled("OUT", self.out_gain))

        self.balance = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, -1.0, 1.0, 0.05)
        self.balance.set_value(0.0)
        self.balance.set_size_request(110, -1)
        self.balance.connect("value-changed", lambda s: win.player.set_balance(s.get_value()))
        ctl.append(_labeled("BALANCE", self.balance))

        self.pitch = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0.90, 1.10, 0.01)
        self.pitch.set_value(1.0)
        self.pitch.set_size_request(110, -1)
        self.pitch.add_mark(1.0, Gtk.PositionType.BOTTOM, None)
        self.pitch.connect("value-changed", lambda s: win.player.set_pitch(s.get_value()))
        self.pitch.set_sensitive(win.player.has_pitch())
        ctl.append(_labeled("PITCH" if win.player.has_pitch() else "PITCH (n/a)", self.pitch))

        ctl.append(Gtk.Box(hexpand=True))
        reset = Gtk.Button(label="RESET")
        reset.add_css_class("eaa-button")
        reset.connect("clicked", self._on_reset)
        ctl.append(reset)
        self.append(ctl)

        self.refresh()

    # ---- helpers ----
    def _db_scale(self, cb, lo, hi):
        s = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, lo, hi, 0.5)
        s.set_value(0.0)
        s.set_size_request(110, -1)
        s.add_mark(0.0, Gtk.PositionType.BOTTOM, None)
        s.connect("value-changed", lambda sc: cb(sc.get_value()))
        return s

    def refresh(self):
        """Re-sync the bank from the engine (freqs + gains)."""
        bands = self.win.player.get_bands()
        self.bank.set_bands([b[0] for b in bands], [b[2] for b in bands])

    # ---- callbacks ----
    def _on_band(self, index, gain):
        self.win.player.set_band(index, gain)

    def _on_select(self, index):
        self._selected = index

    def _on_bands(self, scale):
        n = int(scale.get_value())
        if n != self.win.player.band_count():
            self.win.player.set_band_count(n)
            self.refresh()

    def _on_preamp(self, v):
        self.win.player.set_preamp(v)

    def _on_reset(self, _b):
        self.win.player.reset_eq()
        for s in (self.preamp, self.in_gain, self.out_gain, self.balance):
            s.set_value(0.0)
        self.pitch.set_value(1.0)
        self.refresh()

    # ---- mini-player sync (called by the window) ----
    def set_time(self, text):
        self.m_time.set_text(text)

    def set_track(self, text):
        self.m_track.set_text(text)

    def set_state(self, state):
        self.m_play.icon.set_kind("pause" if state == "PLAY" else "play")
