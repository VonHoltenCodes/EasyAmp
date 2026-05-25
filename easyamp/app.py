"""EasyAmp — a classic-player-style media player (player + EQ + playlist)
with EasyEffects system-EQ controls and a system-wide spectrum/VU display."""

from __future__ import annotations

import math
import os

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, Gdk, Gio, GLib  # noqa: E402
import numpy as np  # noqa: E402

from .backend import EasyEffects  # noqa: E402
from .spectrum import SpectrumCapture  # noqa: E402
from .player import Player  # noqa: E402
from .eqpanel import EQPanel  # noqa: E402
from .playlistpanel import PlaylistPanel, AUDIO_PATTERNS  # noqa: E402

APP_ID = "codes.vonholten.EasyAmp"
STYLE = os.path.join(os.path.dirname(__file__), "style.css")
MARQUEE_WIDTH = 24
BANDS = 20


def _fmt(ns: int) -> str:
    s = max(0, int(ns // 1_000_000_000))
    return f"{s // 60:02d}:{s % 60:02d}"


class Marquee:
    def __init__(self, label, width=MARQUEE_WIDTH, interval=220):
        self.label = label
        self.width = width
        self.interval = interval
        self._src = None
        self._scroll = ""
        self._pos = 0

    def set_text(self, text):
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

    def _tick(self):
        s = self._scroll
        self.label.set_text((s[self._pos:] + s[: self._pos])[: self.width])
        self._pos = (self._pos + 1) % len(s)
        return True


class EasyAmpWindow(Gtk.ApplicationWindow):
    def __init__(self, app, ee):
        super().__init__(application=app, title="EasyAmp")
        self.ee = ee
        self.player = Player(on_tags=self._on_tags, on_eos=self._on_eos)
        self.playlist: list[str] = []
        self.track = -1
        self._playing = False
        self._suppress_seek = False
        self._levels = np.zeros(BANDS, dtype=np.float32)
        self._peaks = np.zeros(BANDS, dtype=np.float32)
        self._vu = (0.0, 0.0)
        self._viz_mode = "spec"
        self._presets: list[str] = []
        self._ee_suppress = False

        self.add_css_class("easyamp")
        self.set_resizable(True)
        self.set_default_size(660, 470)
        self.set_size_request(560, 420)

        bar = Gtk.CenterBox()
        bar.add_css_class("eaa-titlebar")
        bar.set_center_widget(self._mk(Gtk.Label(label="E A S Y A M P"), "eaa-title"))
        bar.set_end_widget(Gtk.WindowControls(side=Gtk.PackType.END))
        handle = Gtk.WindowHandle()
        handle.set_child(bar)
        self.set_titlebar(handle)

        outer = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=5)
        outer.add_css_class("eaa-chassis")
        self.set_child(outer)

        left = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=5)
        left.set_size_request(300, -1)
        outer.append(left)

        # ---- display ----
        info = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        info.add_css_class("eaa-display")
        self.lcd_time = self._mk(Gtk.Label(label="00:00"), "eaa-bignum")
        self.lcd_time.set_valign(Gtk.Align.CENTER)
        info.append(self.lcd_time)
        lcd = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=3)
        lcd.set_hexpand(True)
        lcd.set_valign(Gtk.Align.CENTER)
        inds = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        self.ind_state = self._mk(Gtk.Label(label="STOP"), "eaa-ind")
        self.ind_kbps = self._mk(Gtk.Label(label="--K"), "eaa-ind")
        self.ind_khz = self._mk(Gtk.Label(label="--K"), "eaa-ind")
        self.ind_chan = self._mk(Gtk.Label(label="--", xalign=1), "eaa-ind")
        self.ind_chan.set_hexpand(True)
        for w in (self.ind_state, self.ind_kbps, self.ind_khz, self.ind_chan):
            inds.append(w)
        lcd.append(inds)
        self.marquee_lbl = self._mk(Gtk.Label(label="EASYAMP  *  READY", xalign=0), "eaa-lcd")
        lcd.append(self.marquee_lbl)
        self.marquee = Marquee(self.marquee_lbl)
        self.seek = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 1000, 1)
        self.seek.add_css_class("eaa-seek")
        self.seek.set_draw_value(False)
        self.seek.connect("value-changed", self.on_seek)
        lcd.append(self.seek)
        info.append(lcd)
        left.append(info)

        # ---- visualizer ----
        self.viz = Gtk.DrawingArea()
        self.viz.add_css_class("eaa-viz")
        self.viz.set_vexpand(True)
        self.viz.set_content_height(70)
        self.viz.set_draw_func(self._draw_viz)
        left.append(self.viz)

        # ---- player transport ----
        xport = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        xport.add_css_class("eaa-transport")
        self.btn_open = self._xport("⏏", self.on_open)
        self.btn_prev = self._xport("⏮", self.on_prev)
        self.btn_play = self._xport("⏵", self.on_playpause)
        self.btn_stop = self._xport("⏹", self.on_stop)
        self.btn_next = self._xport("⏭", self.on_next)
        for b in (self.btn_open, self.btn_prev, self.btn_play, self.btn_stop, self.btn_next):
            xport.append(b)
        xport.append(self._sep())
        self.btn_eq = self._tbtn("EQ", self.on_toggle_eq, active=True)
        self.btn_pl = self._tbtn("PL", self.on_toggle_pl, active=True)
        self.btn_viz = Gtk.Button(label="VU")
        self.btn_viz.add_css_class("eaa-button")
        self.btn_viz.connect("clicked", self.on_toggle_viz)
        xport.append(self.btn_eq)
        xport.append(self.btn_pl)
        xport.append(self.btn_viz)
        xport.append(Gtk.Box(hexpand=True))
        left.append(xport)

        # ---- EQ panel (docked under player) ----
        self.eq_panel = EQPanel(self.player)
        left.append(self.eq_panel)

        # ---- system EQ (EasyEffects) strip ----
        eerow = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        eerow.add_css_class("eaa-transport")
        eerow.append(self._mk(Gtk.Label(label="SYS"), "eaa-ind", "on"))
        self.preset_model = Gtk.StringList()
        self.dropdown = Gtk.DropDown(model=self.preset_model)
        self.dropdown.add_css_class("eaa-combo")
        self.dropdown.set_hexpand(True)
        self.dropdown.connect("notify::selected", self.on_ee_preset)
        eerow.append(self.dropdown)
        self.btn_bypass = Gtk.Button(label="BYPASS")
        self.btn_bypass.add_css_class("eaa-button")
        self.btn_bypass.connect("clicked", self.on_ee_bypass)
        eerow.append(self.btn_bypass)
        left.append(eerow)

        # ---- playlist (docked right) ----
        self.playlist_panel = PlaylistPanel(
            on_play=self._play_track, on_add=self._pl_add,
            on_replace=self._pl_replace, on_remove=self._pl_remove,
            on_clear=self._pl_clear)
        self.playlist_panel.set_size_request(280, -1)
        self.playlist_panel.set_hexpand(True)
        outer.append(self.playlist_panel)

        self.spectrum = SpectrumCapture(bands=BANDS, on_data=self._on_data)
        self.connect("map", lambda *_: self.spectrum.start())
        self.connect("close-request", self._on_close)
        self._pos_src = GLib.timeout_add(250, self._tick_position)
        GLib.idle_add(self.refresh_ee)

    # ---- tiny builders ------------------------------------------------
    @staticmethod
    def _mk(widget, *classes):
        for c in classes:
            widget.add_css_class(c)
        return widget

    def _xport(self, glyph, cb):
        b = Gtk.Button(label=glyph)
        b.add_css_class("eaa-xport")
        b.connect("clicked", cb)
        return b

    def _tbtn(self, label, cb, active=False):
        b = Gtk.Button(label=label)
        b.add_css_class("eaa-button")
        if active:
            b.add_css_class("on")
        b.connect("clicked", cb)
        return b

    def _sep(self):
        s = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        s.add_css_class("eaa-xsep")
        return s

    # ---- panel toggles ------------------------------------------------
    def on_toggle_eq(self, btn):
        vis = not self.eq_panel.get_visible()
        self.eq_panel.set_visible(vis)
        (btn.add_css_class if vis else btn.remove_css_class)("on")

    def on_toggle_pl(self, btn):
        vis = not self.playlist_panel.get_visible()
        self.playlist_panel.set_visible(vis)
        (btn.add_css_class if vis else btn.remove_css_class)("on")

    # ---- playlist management -----------------------------------------
    def _pl_add(self, paths):
        self.playlist += paths
        self.playlist_panel.set_tracks(self.playlist)
        self.playlist_panel.set_current(self.track)

    def _pl_replace(self, paths):
        self.playlist = list(paths)
        self.playlist_panel.set_tracks(self.playlist)
        if self.playlist:
            self._play_track(0)

    def _pl_remove(self, idx):
        if 0 <= idx < len(self.playlist):
            del self.playlist[idx]
            if idx == self.track:
                self.on_stop(None)
                self.track = -1
            elif idx < self.track:
                self.track -= 1
            self.playlist_panel.set_tracks(self.playlist)
            self.playlist_panel.set_current(self.track)

    def _pl_clear(self):
        self.on_stop(None)
        self.playlist = []
        self.track = -1
        self.playlist_panel.set_tracks([])

    # ---- player -------------------------------------------------------
    def on_open(self, _b):
        dialog = Gtk.FileDialog(title="Open audio")
        flt = Gtk.FileFilter()
        flt.set_name("Audio files")
        for p in AUDIO_PATTERNS:
            flt.add_pattern(p)
        filters = Gio.ListStore.new(Gtk.FileFilter)
        filters.append(flt)
        dialog.set_filters(filters)
        dialog.open_multiple(self, None, self._on_open_done)

    def _on_open_done(self, dialog, result):
        try:
            files = dialog.open_multiple_finish(result)
        except GLib.Error:
            return
        paths = [files.get_item(i).get_path() for i in range(files.get_n_items())]
        paths = [p for p in paths if p]
        if paths:
            self._pl_replace(paths)

    def _play_track(self, idx):
        if not (0 <= idx < len(self.playlist)):
            return
        self.track = idx
        path = self.playlist[idx]
        self.player.load(path)
        self.player.play()
        self._playing = True
        self._set_state("PLAY")
        self.btn_play.set_label("⏸")
        self.marquee.set_text(os.path.splitext(os.path.basename(path))[0])
        self.playlist_panel.set_current(idx)

    def on_playpause(self, _b):
        if self.track < 0:
            if self.playlist:
                self._play_track(0)
            return
        self.player.toggle()
        self._playing = self.player.is_playing()
        self.btn_play.set_label("⏸" if self._playing else "⏵")
        self._set_state("PLAY" if self._playing else "PAUSE")

    def on_stop(self, _b):
        self.player.stop()
        self._playing = False
        self.btn_play.set_label("⏵")
        self._set_state("STOP")
        self.lcd_time.set_text("00:00")
        self._suppress_seek = True
        self.seek.set_value(0)
        self._suppress_seek = False

    def on_prev(self, _b):
        if self.player.position() > 3_000_000_000:
            self.player.seek_fraction(0.0)
        elif self.track > 0:
            self._play_track(self.track - 1)

    def on_next(self, _b):
        if self.track + 1 < len(self.playlist):
            self._play_track(self.track + 1)
        else:
            self.on_stop(None)

    def _on_eos(self):
        self.on_next(None)

    def _on_tags(self, info):
        artist, title = info.get("artist", ""), info.get("title", "")
        if title:
            self.marquee.set_text(f"{artist} - {title}" if artist else title)

    def _set_state(self, state):
        self.ind_state.set_text(state)
        (self.ind_state.add_css_class if state == "PLAY"
         else self.ind_state.remove_css_class)("on")

    def on_seek(self, scale):
        if self._suppress_seek or self.track < 0:
            return
        self.player.seek_fraction(scale.get_value() / 1000.0)

    def _tick_position(self):
        if self.track >= 0:
            dur, pos = self.player.duration(), self.player.position()
            if dur > 0:
                self._suppress_seek = True
                self.seek.set_value(pos / dur * 1000.0)
                self._suppress_seek = False
            self.lcd_time.set_text(_fmt(pos))
            si = self.player.stream_info()
            self.ind_khz.set_text(f"{round(si['rate']/1000)}K" if si["rate"] else "--K")
            self.ind_kbps.set_text(f"{round(si['bitrate']/1000)}K" if si["bitrate"] else "--K")
            self.ind_chan.set_text("STEREO" if si["channels"] == 2
                                   else ("MONO" if si["channels"] == 1 else "--"))
        return True

    # ---- visualizer ---------------------------------------------------
    def on_toggle_viz(self, _b):
        self._viz_mode = "vu" if self._viz_mode == "spec" else "spec"
        self.btn_viz.set_label("SPEC" if self._viz_mode == "vu" else "VU")
        self.viz.queue_draw()

    def _on_data(self, levels, vu):
        self._levels = levels
        self._peaks = np.maximum(levels, self._peaks - 0.015)
        self._vu = vu
        self.viz.queue_draw()
        return False

    def _draw_viz(self, _area, cr, w, h):
        cr.set_source_rgb(0, 0, 0)
        cr.paint()
        if w <= 0 or h <= 0:
            return
        (self._draw_vu if self._viz_mode == "vu" else self._draw_spectrum)(cr, w, h)

    def _draw_spectrum(self, cr, w, h):
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
            ps = int(float(peaks[i]) * total)
            if ps > 0:
                cr.set_source_rgb(0.80, 1.0, 0.85)
                cr.rectangle(x, h - ps * (seg_h + seg_gap), bw, seg_h * 0.55)
                cr.fill()

    def _draw_vu(self, cr, w, h):
        gap = 8.0
        cell = (w - gap * 3) / 2
        self._vu_gauge(cr, gap, gap, cell, h - 2 * gap, self._vu[0], "L")
        self._vu_gauge(cr, gap * 2 + cell, gap, cell, h - 2 * gap, self._vu[1], "R")

    @staticmethod
    def _rrect(cr, x, y, w, h, r):
        cr.new_sub_path()
        cr.arc(x + w - r, y + r, r, -math.pi / 2, 0)
        cr.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
        cr.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
        cr.arc(x + r, y + r, r, math.pi, 1.5 * math.pi)
        cr.close_path()

    def _vu_gauge(self, cr, x, y, w, h, value, label):
        cr.set_source_rgb(0.92, 0.89, 0.76)
        self._rrect(cr, x, y, w, h, 4)
        cr.fill()
        cx = x + w / 2
        base_y = y + h * 0.90
        r = min(w * 0.40, h * 0.74)

        def pt(frac, rad):
            a = math.radians(135 - frac * 90)
            return cx + rad * math.cos(a), base_y - rad * math.sin(a)

        cr.set_line_width(2)
        cr.set_source_rgb(0.10, 0.10, 0.10)
        cr.move_to(*pt(0.0, r))
        for i in range(1, 41):
            cr.line_to(*pt(i / 40, r))
        cr.stroke()
        cr.set_line_width(3)
        cr.set_source_rgb(0.82, 0.12, 0.10)
        cr.move_to(*pt(0.8, r))
        for i in range(1, 21):
            cr.line_to(*pt(0.8 + 0.2 * i / 20, r))
        cr.stroke()
        cr.set_line_width(1.5)
        cr.set_source_rgb(0.10, 0.10, 0.10)
        for t in range(11):
            cr.move_to(*pt(t / 10, r - 5))
            cr.line_to(*pt(t / 10, r))
            cr.stroke()
        nx, ny = pt(max(0.0, min(1.0, value)), r)
        cr.set_line_width(2.5)
        cr.set_source_rgb(0.05, 0.05, 0.05)
        cr.move_to(cx, base_y)
        cr.line_to(nx, ny)
        cr.stroke()
        cr.arc(cx, base_y, 3, 0, 2 * math.pi)
        cr.fill()
        cr.select_font_face("monospace")
        cr.set_font_size(11)
        cr.move_to(x + 6, y + h - 6)
        cr.show_text(label)

    # ---- EasyEffects system controls ---------------------------------
    def refresh_ee(self):
        self.ee.ensure_running()
        self._presets = self.ee.list_presets().output
        while self.preset_model.get_n_items():
            self.preset_model.remove(0)
        for name in self._presets:
            self.preset_model.append(name)
        active = self.ee.active_preset("output")
        if active in self._presets:
            self._ee_suppress = True
            self.dropdown.set_selected(self._presets.index(active))
            self._ee_suppress = False
        self._set_bypass_ui(self.ee.get_bypass())
        return False

    def _set_bypass_ui(self, bypassed):
        (self.btn_bypass.remove_css_class if bypassed
         else self.btn_bypass.add_css_class)("on")

    def on_ee_preset(self, dropdown, _pspec):
        if self._ee_suppress:
            return
        idx = dropdown.get_selected()
        if 0 <= idx < len(self._presets):
            self.ee.load_preset(self._presets[idx])

    def on_ee_bypass(self, _b):
        self._set_bypass_ui(self.ee.toggle_bypass())

    def _on_close(self, *_):
        if self._pos_src:
            GLib.source_remove(self._pos_src)
            self._pos_src = None
        self.spectrum.stop()
        self.player.stop()
        return False


class EasyAmpApp(Gtk.Application):
    def __init__(self):
        super().__init__(application_id=APP_ID, flags=Gio.ApplicationFlags.DEFAULT_FLAGS)
        self.ee = EasyEffects()

    def do_startup(self):
        Gtk.Application.do_startup(self)
        provider = Gtk.CssProvider()
        provider.load_from_path(STYLE)
        Gtk.StyleContext.add_provider_for_display(
            Gdk.Display.get_default(), provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

    def do_activate(self):
        win = self.props.active_window or EasyAmpWindow(self, self.ee)
        win.present()


def main() -> int:
    return EasyAmpApp().run(None)
