"""Full-window equalizer view (the EQUALIZER footer tab).

A compact player strip stays docked at the top so playback is always
controllable; the rest of the window is the expanded N-band parametric EQ
with knob controls (band count, in/out gain, pitch, balance, preamp) and a
presets menu shared with the small player EQ.
"""

from __future__ import annotations

import math

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, Pango  # noqa: E402

from . import eqpresets, eqio  # noqa: E402
from .eqbank import EQBank  # noqa: E402
from .eqmodel import fmt_freq  # noqa: E402
from .presetui import PresetPopover  # noqa: E402
from .viz import LogSpectrum, WaveScope  # noqa: E402
from .widgets import Knob, make_button, transport_button, LedMeter  # noqa: E402


def _labeled(label_text, widget):
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
    box.add_css_class("eaa-ctl")
    box.set_halign(Gtk.Align.CENTER)
    box.set_margin_start(4)
    box.set_margin_end(4)
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
        self.m_play = transport_button("play", win.on_playpause)
        for b in (transport_button("eject", win.on_open),
                  transport_button("prev", win.on_prev),
                  self.m_play,
                  transport_button("stop", win.on_stop),
                  transport_button("next", win.on_next)):
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
        meters = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=3)
        self.spec = LogSpectrum()
        meters.append(self.spec)
        self.wave = WaveScope()
        meters.append(self.wave)
        self.append(meters)

        # ---- the expanded EQ bank ----
        self.bank = EQBank(on_change=self._on_band, on_select=self._on_select)
        frame = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        frame.add_css_class("eaa-eqbank")
        frame.set_vexpand(True)
        frame.append(self.bank)
        self.append(frame)

        # ---- controls: two fixed rows (bounded height, fit at default width)
        ctl_wrap = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        ctl_wrap.add_css_class("eaa-panel")
        ctl_wrap.set_margin_top(4)
        ctl_wrap.set_margin_bottom(4)
        row1 = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=2)
        row1.set_halign(Gtk.Align.CENTER)
        row2 = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=2)
        row2.set_halign(Gtk.Align.CENTER)
        # LED-segment L/R dB meters across the top of the control panel
        self.led = LedMeter()
        ctl_wrap.append(self.led)
        ctl_wrap.append(row1)
        ctl_wrap.append(row2)

        # row 1: output knobs
        self.k_bands = Knob(10, 32, p.band_count(), step=1, default=10,
                            fmt=lambda v: f"{int(v)}", on_change=self._on_bands)
        row1.append(_labeled("BANDS", self.k_bands))
        self.k_preamp = Knob(-12, 12, 0, step=0.5, default=0,
                             fmt=lambda v: f"{v:+.1f}", on_change=p.set_preamp)
        row1.append(_labeled("PREAMP", self.k_preamp))
        self.k_in = Knob(-12, 12, 0, step=0.5, default=0,
                         fmt=lambda v: f"{v:+.1f}", on_change=p.set_in_gain)
        row1.append(_labeled("IN", self.k_in))
        self.k_out = Knob(-12, 12, 0, step=0.5, default=0,
                          fmt=lambda v: f"{v:+.1f}", on_change=p.set_out_gain)
        row1.append(_labeled("OUT", self.k_out))
        self.k_bal = Knob(-1, 1, 0, step=0.05, default=0,
                          fmt=_bal_fmt, on_change=p.set_balance)
        row1.append(_labeled("BALANCE", self.k_bal))
        self.k_pitch = Knob(0.90, 1.10, 1.0, step=0.01, default=1.0,
                            fmt=lambda v: f"{v:.2f}x", on_change=p.set_pitch)
        self.k_pitch.set_sensitive(p.has_pitch())
        row1.append(_labeled("PITCH" if p.has_pitch() else "PITCH n/a", self.k_pitch))

        # row 1 (cont.): selected-band parametric knobs
        self._selected = 0
        self.k_freq = Knob(math.log10(20), math.log10(20000), math.log10(1000),
                           step=0.02, default=math.log10(1000),
                           fmt=lambda v: fmt_freq(10 ** v), on_change=self._on_freq)
        row1.append(_labeled("SEL FREQ", self.k_freq))
        self.k_q = Knob(0.3, 12.0, 1.41, step=0.1, default=1.41,
                        fmt=lambda v: f"Q{v:.1f}", on_change=self._on_q)
        row1.append(_labeled("SEL Q", self.k_q))

        # row 2: buttons
        self.presets_btn = Gtk.MenuButton(label="PRESETS")
        self.presets_btn.add_css_class("eaa-button")
        self.presets = PresetPopover(on_apply=self._apply_named,
                                     on_save=self._save_named)
        self.presets_btn.set_popover(self.presets)
        row2.append(_labeled("EQ PRESETS", self.presets_btn))

        imp = make_button("IMPORT")
        imp.connect("clicked", self._on_import)
        row2.append(_labeled("APO / GEQ", imp))
        exp = Gtk.MenuButton(label="EXPORT")
        exp.add_css_class("eaa-button")
        exp.set_popover(self._build_export_popover())
        row2.append(_labeled(" ", exp))

        reset = make_button("RESET")
        reset.connect("clicked", self._on_reset)
        row2.append(_labeled(" ", reset))
        self.append(ctl_wrap)

        self.refresh()
        self._on_select(0)   # point the FREQ/Q knobs at band 0 initially

    # ---- presets ----
    def _apply_named(self, name):
        preamp, bands = eqpresets.load(name)
        # presets are 10-band graphic; apply via the small panel so both views
        # and the engine stay consistent, then resync this view.
        self.win.eq_panel.apply_preset(preamp, bands)
        self.k_bands.set_value(self.win.player.band_count())
        self.k_preamp.set_value(preamp)
        self.refresh()

    def _save_named(self, name):
        panel = self.win.eq_panel
        eqpresets.save(name, panel.eq.preamp, list(panel.eq.bands))
        panel.presets.reload()   # keep the other popover's list in sync

    # ---- import / export (Equalizer APO + AutoEQ GraphicEQ) ----
    def _build_export_popover(self):
        pop = Gtk.Popover()
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        for m in ("top", "bottom", "start", "end"):
            getattr(box, f"set_margin_{m}")(6)
        for label, kind in (("Equalizer APO config", "apo"),
                            ("AutoEQ GraphicEQ", "geq")):
            b = make_button(label)
            b.connect("clicked", lambda _b, k=kind: (pop.popdown(), self._export(k)))
            box.append(b)
        pop.set_child(box)
        return pop

    def _on_import(self, _b):
        dlg = Gtk.FileDialog()
        dlg.set_title("Import EQ — APO config or GraphicEQ")
        dlg.open(self.win, None, self._import_done)

    def _import_done(self, dlg, res):
        try:
            f = dlg.open_finish(res)
        except Exception:
            return
        try:
            with open(f.get_path(), encoding="utf-8", errors="ignore") as fh:
                parsed = eqio.parse(fh.read())
        except OSError:
            return
        if not parsed:
            return
        preamp, bands = parsed
        self.win.player.load_bands(eqio.downsample(bands, 32), preamp)
        self.k_bands.set_value(self.win.player.band_count())
        self.k_preamp.set_value(preamp)
        self.refresh()
        self._on_select(0)

    def _export(self, kind):
        dlg = Gtk.FileDialog()
        dlg.set_title("Export EQ")
        dlg.set_initial_name("easyamp-eq.txt" if kind == "apo"
                             else "easyamp-graphiceq.txt")
        dlg.save(self.win, None, lambda d, r: self._export_done(d, r, kind))

    def _export_done(self, dlg, res, kind):
        try:
            f = dlg.save_finish(res)
        except Exception:
            return
        bands = self.win.player.get_bands()
        text = (eqio.format_apo(self.k_preamp.get_value(), bands) if kind == "apo"
                else eqio.format_graphiceq(bands))
        try:
            with open(f.get_path(), "w", encoding="utf-8") as fh:
                fh.write(text)
        except OSError:
            pass

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
        if self._selected >= 0:
            self.win.player.set_band_param(self._selected, freq=10 ** v)
            self.refresh()

    def _on_q(self, v):
        if self._selected >= 0:
            self.win.player.set_band_param(self._selected, q=v)

    def _on_bands(self, v):
        n = int(v)
        if n != self.win.player.band_count():
            self.win.player.set_band_count(n)
            self.refresh()

    def _on_reset(self, _b):
        self.win.player.reset_bands()          # gains + freq/Q back to defaults
        for k in (self.k_preamp, self.k_in, self.k_out, self.k_bal):
            k.set_value(0.0)
        self.k_pitch.set_value(1.0)
        self.win.player.set_in_gain(0)
        self.win.player.set_out_gain(0)
        self.win.player.set_balance(0)
        self.win.player.set_pitch(1.0)
        self.win.player.set_preamp(0)
        self.refresh()
        self._on_select(self._selected)        # re-sync SEL FREQ/Q knobs

    # ---- meters (fed from the window's capture) ----
    def set_audio(self, levels, vu, wave):
        self.led.set_levels(vu[0], vu[1])
        self.spec.set_levels(levels)
        self.wave.set_wave(wave)

    # ---- mini-player sync (called by the window) ----
    def set_time(self, text):
        self.m_time.set_text(text)

    def set_track(self, text):
        self.m_track.set_text(text)

    def set_state(self, state):
        self.m_play.icon.set_kind("pause" if state == "PLAY" else "play")
