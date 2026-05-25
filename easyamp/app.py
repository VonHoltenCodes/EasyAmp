"""EasyAmp — a self-contained classic-player-style media player with a
built-in 10-band EQ, playlist, and a system-wide spectrum/VU display."""

from __future__ import annotations

import math
import os
import sys

import cairo

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, Gdk, Gio, GLib  # noqa: E402
import numpy as np  # noqa: E402

from .spectrum import SpectrumCapture  # noqa: E402
from .player import Player  # noqa: E402
from .eqpanel import EQPanel  # noqa: E402
from .playlistpanel import PlaylistPanel, AUDIO_PATTERNS  # noqa: E402
from .widgets import window_title_bar, make_button, set_led, SeekBar, StatusIndicator  # noqa: E402

APP_ID = "com.vonholtencodes.EasyAmp"
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
    def __init__(self, app):
        super().__init__(application=app, title="EasyAmp")
        self.player = Player(on_tags=self._on_tags, on_eos=self._on_eos)
        self.playlist: list[str] = []
        self.track = -1
        self._playing = False
        self._levels = np.zeros(BANDS, dtype=np.float32)
        self._peaks = np.zeros(BANDS, dtype=np.float32)
        self._vu = (0.0, 0.0)
        self._wave = np.zeros(64, dtype=np.float32)
        self._viz_mode = "spec"

        self.add_css_class("easyamp")
        self.set_resizable(True)
        self.set_default_size(680, 470)
        self.set_size_request(640, 440)

        self.set_titlebar(window_title_bar("EASYAMP"))

        outer = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=3)
        outer.add_css_class("eaa-chassis")
        self.set_child(outer)

        left = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=3)
        left.set_size_request(300, -1)
        outer.append(left)

        # ---- display ----
        info = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        info.add_css_class("eaa-display")
        left_cell = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        # top row: status indicator (left) + clock pushed to the top-right
        top_row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        self.status = StatusIndicator()
        self.status.set_valign(Gtk.Align.START)
        top_row.append(self.status)
        top_row.append(Gtk.Box(hexpand=True))
        self.lcd_time = self._mk(Gtk.Label(label="00:00"), "eaa-bignum")
        self.lcd_time.set_valign(Gtk.Align.START)
        self.lcd_time.set_halign(Gtk.Align.END)
        top_row.append(self.lcd_time)
        left_cell.append(top_row)
        self.scope = Gtk.DrawingArea()       # mini bar-graph scope under the timer
        self.scope.set_content_height(40)
        self.scope.set_content_width(150)
        self.scope.set_hexpand(True)
        self.scope.set_draw_func(self._draw_scope)
        left_cell.append(self.scope)
        info.append(left_cell)
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
        # fixed character width so the proportional font can't resize the
        # left column (and shift the playlist divider) as the text scrolls
        self.marquee_lbl.set_width_chars(MARQUEE_WIDTH)
        self.marquee_lbl.set_max_width_chars(MARQUEE_WIDTH)
        lcd.append(self.marquee_lbl)
        self.marquee = Marquee(self.marquee_lbl)
        self.seek = SeekBar(on_seek=self._do_seek)
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
        self.btn_eq = make_button("EQ", led=True)
        self.btn_eq.connect("clicked", self.on_toggle_eq)
        set_led(self.btn_eq, True)
        self.btn_pl = make_button("PL", led=True)
        self.btn_pl.connect("clicked", self.on_toggle_pl)
        set_led(self.btn_pl, True)
        self.btn_viz = make_button("VU", led=True)
        self.btn_viz.connect("clicked", self.on_toggle_viz)
        set_led(self.btn_viz, False)
        xport.append(self.btn_eq)
        xport.append(self.btn_pl)
        xport.append(self.btn_viz)
        xport.append(Gtk.Box(hexpand=True))
        left.append(xport)

        # ---- EQ panel (docked under player) ----
        self.eq_panel = EQPanel(self.player)
        left.append(self.eq_panel)

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

    def _sep(self):
        s = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        s.add_css_class("eaa-xsep")
        return s

    # ---- panel toggles ------------------------------------------------
    def on_toggle_eq(self, btn):
        vis = not self.eq_panel.get_visible()
        self.eq_panel.set_visible(vis)
        set_led(btn, vis)

    def on_toggle_pl(self, btn):
        vis = not self.playlist_panel.get_visible()
        self.playlist_panel.set_visible(vis)
        set_led(btn, vis)

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
        self.seek.set_fraction(0)

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
        self.status.set_state(state.lower())

    def _do_seek(self, frac):
        if self.track >= 0:
            self.player.seek_fraction(frac)

    def _tick_position(self):
        if self.track >= 0:
            dur, pos = self.player.duration(), self.player.position()
            if dur > 0:
                self.seek.set_fraction(pos / dur)
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
        set_led(self.btn_viz, self._viz_mode == "vu")
        self.viz.queue_draw()

    def _on_data(self, levels, vu, wave):
        self._levels = levels
        self._peaks = np.maximum(levels, self._peaks - 0.015)
        self._vu = vu
        self._wave = wave
        self.viz.queue_draw()
        self.scope.queue_draw()
        return False

    def _draw_scope(self, _a, cr, w, h):
        """White mirrored bar graph with a faint grid and alternating
        light/dark blue dots along the bottom and left axes."""
        wave = self._wave
        n = len(wave)
        if n < 2 or w <= 0 or h <= 0:
            return
        mid = h / 2
        amp = h * 0.44

        # faint grid
        cr.set_source_rgba(0.40, 0.52, 0.85, 0.13)
        cr.set_line_width(1)
        for c in range(1, 8):
            x = round(w * c / 8) + 0.5
            cr.move_to(x, 0)
            cr.line_to(x, h)
        for r in (0.25, 0.5, 0.75):
            y = round(h * r) + 0.5
            cr.move_to(0, y)
            cr.line_to(w, y)
        cr.stroke()

        # white mirrored bars
        nbars = 22
        bw = w / nbars
        cr.set_source_rgb(0.96, 0.97, 1.0)
        for b in range(nbars):
            i = min(int(b / nbars * n), n - 1)
            mag = abs(float(wave[i])) * amp
            cr.rectangle(b * bw + 1, mid - mag, bw - 1.5, mag * 2)
            cr.fill()

        # alternating light/dark blue dots along bottom + left axes
        light, dark = (0.46, 0.66, 1.0), (0.12, 0.22, 0.52)
        k = 0
        x = 1
        while x < w - 1:
            cr.set_source_rgb(*(light if k % 2 == 0 else dark))
            cr.rectangle(x, h - 2, 2, 2)
            cr.fill()
            x += 6
            k += 1
        k = 0
        y = 1
        while y < h - 1:
            cr.set_source_rgb(*(light if k % 2 == 0 else dark))
            cr.rectangle(0, y, 2, 2)
            cr.fill()
            y += 6
            k += 1

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
                if frac > 0.80:
                    cr.set_source_rgb(0.90, 0.13, 0.10)   # red
                elif frac > 0.58:
                    cr.set_source_rgb(0.93, 0.45, 0.10)   # orange
                elif frac > 0.36:
                    cr.set_source_rgb(0.92, 0.82, 0.14)   # yellow
                else:
                    cr.set_source_rgb(0.14, 0.88, 0.20)   # green
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

    # real VU dB scale: (label dB, position fraction along the arc)
    _VU_SCALE = [(-20, 0.0), (-10, 0.22), (-7, 0.36), (-5, 0.48),
                 (-3, 0.62), (0, 0.80), (3, 1.0)]
    _VU_REDFRAC = 0.80  # 0 dB and above = red zone

    def _vu_gauge(self, cr, x, y, w, h, value, label):
        value = max(0.0, min(1.0, value))
        cx = x + w / 2
        base_y = y + h * 0.90
        r = min(w * 0.43, h * 0.82)

        def pt(frac, rad):
            a = math.radians(135 - frac * 90)
            return cx + rad * math.cos(a), base_y - rad * math.sin(a)

        # face: dark, with an atomic-green glow from the pivot that
        # dissipates outward but never fades to full black
        self._rrect(cr, x, y, w, h, 4)
        cr.set_source_rgb(0.02, 0.06, 0.035)
        cr.fill()
        self._rrect(cr, x, y, w, h, 4)
        cr.clip()
        glow = cairo.RadialGradient(cx, base_y, 2, cx, base_y, r * 1.28)
        glow.add_color_stop_rgb(0.0, 0.10, 0.46, 0.22)
        glow.add_color_stop_rgb(0.55, 0.05, 0.22, 0.11)
        glow.add_color_stop_rgb(1.0, 0.03, 0.13, 0.07)
        cr.set_source(glow)
        cr.paint()
        cr.reset_clip()

        # ticks + dB numbers
        cr.select_font_face("monospace")
        for db, frac in self._VU_SCALE:
            major = db in (-20, -10, -5, 0, 3)
            red = frac >= self._VU_REDFRAC
            cr.set_source_rgb(*(0.97, 0.27, 0.18) if red else (0.34, 1.0, 0.62))
            cr.set_line_width(1.4 if major else 1.0)
            cr.move_to(*pt(frac, r - (6 if major else 4)))
            cr.line_to(*pt(frac, r))
            cr.stroke()
            if major:
                cr.set_font_size(7)
                txt = f"+{db}" if db > 0 else str(db)
                ext = cr.text_extents(txt)
                lx, ly = pt(frac, r - 13)
                cr.move_to(lx - ext.width / 2, ly + 3)
                cr.show_text(txt)

        # arc: green up to 0 dB, red beyond
        cr.set_line_width(2)
        cr.set_source_rgb(0.18, 0.95, 0.50)
        cr.move_to(*pt(0.0, r))
        for i in range(1, 41):
            cr.line_to(*pt(i / 40 * self._VU_REDFRAC, r))
        cr.stroke()
        cr.set_source_rgb(0.95, 0.22, 0.16)
        cr.move_to(*pt(self._VU_REDFRAC, r))
        for i in range(1, 13):
            cr.line_to(*pt(self._VU_REDFRAC + (1 - self._VU_REDFRAC) * i / 12, r))
        cr.stroke()

        # needle: subtle glow underlay + bright tapered needle + hub
        tipx, tipy = pt(value, r - 3)
        cr.set_line_cap(cairo.LINE_CAP_ROUND)
        cr.set_source_rgba(0.20, 1.0, 0.55, 0.22)
        cr.set_line_width(5)
        cr.move_to(cx, base_y)
        cr.line_to(tipx, tipy)
        cr.stroke()
        cr.set_source_rgb(0.68, 1.0, 0.82)
        cr.set_line_width(1.8)
        cr.move_to(cx, base_y)
        cr.line_to(tipx, tipy)
        cr.stroke()
        cr.set_line_cap(cairo.LINE_CAP_BUTT)
        cr.set_source_rgb(0.16, 0.92, 0.46)
        cr.arc(cx, base_y, 4, 0, 6.2832)
        cr.fill()
        cr.set_source_rgb(0.82, 1.0, 0.90)
        cr.arc(cx, base_y, 1.6, 0, 6.2832)
        cr.fill()

        # corner labels
        cr.set_source_rgb(0.34, 0.98, 0.58)
        cr.set_font_size(11)
        cr.move_to(x + 6, y + h - 5)
        cr.show_text(label)
        cr.set_font_size(7)
        vu = cr.text_extents("VU")
        cr.move_to(x + w - vu.width - 6, y + h - 5)
        cr.show_text("VU")

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

    def do_startup(self):
        Gtk.Application.do_startup(self)
        from .fontload import ensure_fonts
        ensure_fonts()

    def _install_css(self):
        # Apply our stylesheet once a display exists. This runs from
        # do_activate rather than do_startup because on some platforms
        # (notably Windows) the default display isn't ready during startup,
        # which would silently drop the whole stylesheet.
        if getattr(self, "_css_installed", False):
            return
        display = Gdk.Display.get_default()
        if display is None:
            print("EasyAmp: no default display; stylesheet not applied", file=sys.stderr)
            return
        provider = Gtk.CssProvider()
        provider.connect(
            "parsing-error",
            lambda _p, _sec, err: print(f"EasyAmp CSS error: {err.message}", file=sys.stderr))
        provider.load_from_path(STYLE)
        Gtk.StyleContext.add_provider_for_display(
            display, provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        self._css_installed = True

    def do_activate(self):
        self._install_css()
        win = self.props.active_window or EasyAmpWindow(self)
        win.present()


def main() -> int:
    return EasyAmpApp().run(None)
