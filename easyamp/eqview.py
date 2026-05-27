"""Full-window equalizer view (the EQUALIZER footer tab).

A compact player strip stays docked at the top so playback is always
controllable; the rest of the window is the expanded N-band parametric EQ
with knob controls (band count, in/out gain, pitch, balance, preamp) and a
presets menu shared with the small player EQ.
"""

from __future__ import annotations

import math

import numpy as np

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, Pango  # noqa: E402

from . import eqpresets  # noqa: E402
from .eqbank import EQBank, _fmt_freq  # noqa: E402
from .widgets import Knob, make_button  # noqa: E402


def _labeled(label_text, widget):
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
    box.add_css_class("eaa-ctl")
    box.set_halign(Gtk.Align.CENTER)
    box.set_margin_start(8)
    box.set_margin_end(8)
    lbl = Gtk.Label(label=label_text)
    lbl.add_css_class("eaa-ctl-lbl")
    box.append(lbl)
    box.append(widget)
    return box


def _bal_fmt(v):
    if abs(v) < 0.03:
        return "C"
    return f"L{int(abs(v) * 100)}" if v < 0 else f"R{int(v * 100)}"


class EQView(Gtk.Box):
    def __init__(self, win):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=3)
        self.win = win
        self.add_css_class("eaa-chassis")
        p = win.player

        # ---- mini player strip ----
        mini = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        mini.add_css_class("eaa-display")
        self.m_play = win._xport("play", win.on_playpause)
        for b in (win._xport("eject", win.on_open), win._xport("prev", win.on_prev),
                  self.m_play, win._xport("stop", win.on_stop),
                  win._xport("next", win.on_next)):
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

        # ---- meters: log-scale spectrum + waveform scope ----
        self._levels = np.zeros(1, dtype=np.float32)
        self._wave = np.zeros(1, dtype=np.float32)
        meters = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=3)
        self.spec = Gtk.DrawingArea()
        self.spec.add_css_class("eaa-viz")
        self.spec.set_content_height(66)
        self.spec.set_hexpand(True)
        self.spec.set_draw_func(self._draw_spectrum)
        meters.append(self.spec)
        self.wave = Gtk.DrawingArea()
        self.wave.add_css_class("eaa-viz")
        self.wave.set_content_height(66)
        self.wave.set_size_request(200, -1)
        self.wave.set_draw_func(self._draw_wave)
        meters.append(self.wave)
        self.append(meters)

        # ---- the expanded EQ bank ----
        self.bank = EQBank(on_change=self._on_band, on_select=self._on_select)
        frame = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        frame.add_css_class("eaa-eqbank")
        frame.set_vexpand(True)
        frame.append(self.bank)
        self.append(frame)

        # ---- knob controls row ----
        ctl = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        ctl.add_css_class("eaa-panel")
        ctl.set_margin_top(4)
        ctl.set_margin_bottom(4)

        self.k_bands = Knob(10, 32, p.band_count(), step=1, default=10,
                            fmt=lambda v: f"{int(v)}", on_change=self._on_bands)
        ctl.append(_labeled("BANDS", self.k_bands))
        self.k_preamp = Knob(-12, 12, 0, step=0.5, default=0,
                             fmt=lambda v: f"{v:+.1f}", on_change=p.set_preamp)
        ctl.append(_labeled("PREAMP", self.k_preamp))
        self.k_in = Knob(-12, 12, 0, step=0.5, default=0,
                         fmt=lambda v: f"{v:+.1f}", on_change=p.set_in_gain)
        ctl.append(_labeled("IN", self.k_in))
        self.k_out = Knob(-12, 12, 0, step=0.5, default=0,
                          fmt=lambda v: f"{v:+.1f}", on_change=p.set_out_gain)
        ctl.append(_labeled("OUT", self.k_out))
        self.k_bal = Knob(-1, 1, 0, step=0.05, default=0,
                          fmt=_bal_fmt, on_change=p.set_balance)
        ctl.append(_labeled("BALANCE", self.k_bal))
        self.k_pitch = Knob(0.90, 1.10, 1.0, step=0.01, default=1.0,
                            fmt=lambda v: f"{v:.2f}x", on_change=p.set_pitch)
        self.k_pitch.set_sensitive(p.has_pitch())
        ctl.append(_labeled("PITCH" if p.has_pitch() else "PITCH n/a", self.k_pitch))

        # selected-band parametric controls (freq is log-scaled)
        self._selected = 0
        self.k_freq = Knob(math.log10(20), math.log10(20000), math.log10(1000),
                           step=0.02, default=math.log10(1000),
                           fmt=lambda v: _fmt_freq(10 ** v), on_change=self._on_freq)
        ctl.append(_labeled("SEL FREQ", self.k_freq))
        self.k_q = Knob(0.3, 12.0, 1.41, step=0.1, default=1.41,
                        fmt=lambda v: f"Q{v:.1f}", on_change=self._on_q)
        ctl.append(_labeled("SEL Q", self.k_q))

        ctl.append(Gtk.Box(hexpand=True))

        self.presets_btn = Gtk.MenuButton(label="PRESETS")
        self.presets_btn.add_css_class("eaa-button")
        self.presets_btn.set_popover(self._build_popover())
        ctl.append(_labeled("EQ PRESETS", self.presets_btn))

        reset = make_button("RESET")
        reset.connect("clicked", self._on_reset)
        ctl.append(_labeled(" ", reset))
        self.append(ctl)

        self.refresh()
        self._on_select(0)   # point the FREQ/Q knobs at band 0 initially

    # ---- presets ----
    def _build_popover(self):
        pop = Gtk.Popover()
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        for m in ("top", "bottom", "start", "end"):
            getattr(box, f"set_margin_{m}")(6)
        scroller = Gtk.ScrolledWindow()
        scroller.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scroller.set_min_content_height(160)
        scroller.set_min_content_width(180)
        self._preset_list = Gtk.ListBox()
        self._preset_list.connect("row-activated", self._on_preset_row)
        scroller.set_child(self._preset_list)
        box.append(scroller)
        self._reload_presets()
        box.append(Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL))
        self._name = Gtk.Entry()
        self._name.set_placeholder_text("save as…")
        box.append(self._name)
        save = make_button("SAVE PRESET")
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
        # presets are 10-band graphic; apply via the small panel so both views
        # and the engine stay consistent, then resync this view.
        self.win.eq_panel.apply_preset(preamp, bands)
        self.k_bands.set_value(self.win.player.band_count())
        self.k_preamp.set_value(preamp)
        self._name.set_text(name)
        self.refresh()

    def _on_save(self, _btn):
        name = self._name.get_text().strip() or "My EQ"
        panel = self.win.eq_panel
        eqpresets.save(name, panel.eq.preamp, list(panel.eq.bands))
        self._reload_presets()

    # ---- band bank ----
    def refresh(self):
        bands = self.win.player.get_bands()
        self.bank.set_bands([b[0] for b in bands], [b[2] for b in bands])

    def _on_band(self, index, gain):
        self.win.player.set_band(index, gain)

    def _on_select(self, index):
        """A band was clicked: point the FREQ/Q knobs at it."""
        self._selected = index
        bands = self.win.player.get_bands()
        if 0 <= index < len(bands):
            f, q, _g, _t = bands[index]
            self.k_freq.set_value(math.log10(max(f, 1.0)))
            self.k_q.set_value(q)

    def _on_freq(self, v):
        if getattr(self, "_selected", -1) >= 0:
            self.win.player.set_band_param(self._selected, freq=10 ** v)
            self.refresh()

    def _on_q(self, v):
        if getattr(self, "_selected", -1) >= 0:
            self.win.player.set_band_param(self._selected, q=v)

    def _on_bands(self, v):
        n = int(v)
        if n != self.win.player.band_count():
            self.win.player.set_band_count(n)
            self.refresh()

    def _on_reset(self, _b):
        self.win.player.reset_eq()
        for k in (self.k_preamp, self.k_in, self.k_out, self.k_bal):
            k.set_value(0.0)
        self.k_pitch.set_value(1.0)
        self.win.player.set_in_gain(0); self.win.player.set_out_gain(0)
        self.win.player.set_balance(0); self.win.player.set_pitch(1.0)
        self.win.player.set_preamp(0)
        self.refresh()

    # ---- meters (fed from the window's capture) ----
    def set_audio(self, levels, wave):
        self._levels = levels
        self._wave = wave
        self.spec.queue_draw()
        self.wave.queue_draw()

    def _draw_spectrum(self, _a, cr, w, h):
        cr.set_source_rgb(0, 0, 0)
        cr.paint()
        n = len(self._levels)
        if n < 2 or w <= 0:
            return
        # log-frequency tick lines (bands are already log-spaced 40Hz..24kHz)
        cr.select_font_face("monospace")
        cr.set_font_size(7)
        lo, hi = 40.0, 24000.0
        span = math.log10(hi / lo)
        for f, lab in ((100, "100"), (1000, "1K"), (10000, "10K")):
            x = (math.log10(f / lo) / span) * w
            cr.set_source_rgba(0.3, 0.45, 0.7, 0.35)
            cr.set_line_width(1)
            cr.move_to(x, 0); cr.line_to(x, h - 9); cr.stroke()
            cr.set_source_rgb(0.4, 0.55, 0.8)
            cr.move_to(x + 2, h - 1); cr.show_text(lab)
        # bars
        bw = w / n
        for i, lv in enumerate(self._levels):
            bh = max(1.0, float(lv) * (h - 10))
            warm = min(1.0, float(lv) * 1.3)
            cr.set_source_rgb(0.12 + 0.8 * warm, 0.9 - 0.5 * warm, 0.12)
            cr.rectangle(i * bw + 0.5, (h - 10) - bh, max(1.0, bw - 1), bh)
            cr.fill()

    def _draw_wave(self, _a, cr, w, h):
        cr.set_source_rgb(0, 0, 0)
        cr.paint()
        wv = self._wave
        n = len(wv)
        if n < 2 or w <= 0:
            return
        mid = h / 2
        cr.set_source_rgb(0.10, 0.95, 0.14)
        cr.set_line_width(1.4)
        for i in range(n):
            x = i / (n - 1) * w
            y = mid - float(wv[i]) * (h * 0.45)
            cr.line_to(x, y) if i else cr.move_to(x, y)
        cr.stroke()

    # ---- mini-player sync (called by the window) ----
    def set_time(self, text):
        self.m_time.set_text(text)

    def set_track(self, text):
        self.m_track.set_text(text)

    def set_state(self, state):
        self.m_play.icon.set_kind("pause" if state == "PLAY" else "play")
