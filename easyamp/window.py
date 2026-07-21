"""The main EasyAmp window: player display, transport, docked EQ + playlist
panels, the full-window equalizer page, and the status/tab footer.

The window owns the playback state (playlist, current track) and wires the
engine (:class:`~easyamp.player.Player`), the system-audio capture
(:class:`~easyamp.spectrum.SpectrumCapture`), and the display widgets
together. All drawing lives in ``viz.py`` / ``widgets.py``.
"""

from __future__ import annotations

import os

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, Gdk, Gio, GLib  # noqa: E402

from .spectrum import SpectrumCapture  # noqa: E402
from .player import Player  # noqa: E402
from .eqpanel import EQPanel  # noqa: E402
from .eqview import EQView  # noqa: E402
from .playlistpanel import PlaylistPanel, AUDIO_PATTERNS  # noqa: E402
from .viz import ScopeArea, SpectrumVU  # noqa: E402
from .widgets import (  # noqa: E402
    Marquee, MARQUEE_WIDTH, window_title_bar, make_button, set_led,
    transport_button, SeekBar, StatusIndicator,
)
from . import update_check  # noqa: E402
from . import __version__  # noqa: E402

VIZ_BANDS = 20      # spectrum bars in the main visualizer


def _fmt(ns: int) -> str:
    s = max(0, int(ns // 1_000_000_000))
    return f"{s // 60:02d}:{s % 60:02d}"


class EasyAmpWindow(Gtk.ApplicationWindow):
    def __init__(self, app):
        super().__init__(application=app, title="EasyAmp")
        self.player = Player(on_tags=self._on_tags, on_eos=self._on_eos,
                             on_error=self._on_play_error)
        self.playlist: list[str] = []
        self.track = -1
        self._playing = False

        self.add_css_class("easyamp")
        self.set_resizable(True)
        self.set_default_size(720, 560)        # ~4:3, taller than wide-ish
        self.set_size_request(540, 430)        # modest floor so it stays fluid

        self.set_titlebar(window_title_bar("EASYAMP"))

        outer = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=3)
        outer.add_css_class("eaa-chassis")
        outer.set_vexpand(True)

        # two views (player / equalizer) swapped by the footer tabs; the
        # equalizer page is added at the end of __init__ once the player exists
        self.stack = Gtk.Stack()
        self.stack.set_vexpand(True)
        self.stack.add_named(outer, "player")

        # window = view stack + a thin status/tab footer at the very bottom
        root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        root.append(self.stack)
        root.append(self._build_footer())
        self.set_child(root)

        # Win/mac: flip the footer to an update notice if a newer build exists
        # (no-op under Flatpak, which auto-updates from the software center).
        update_check.check_async(self._on_update_available)

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
        self.scope = ScopeArea()             # mini bar-graph scope under the timer
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
        self.viz = SpectrumVU(bands=VIZ_BANDS)
        left.append(self.viz)

        # ---- player transport ----
        xport = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        xport.add_css_class("eaa-transport")
        self.btn_open = transport_button("eject", self.on_open)
        self.btn_prev = transport_button("prev", self.on_prev)
        self.btn_play = transport_button("play", self.on_playpause)
        self.btn_stop = transport_button("stop", self.on_stop)
        self.btn_next = transport_button("next", self.on_next)
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

        self.spectrum = SpectrumCapture(bands=VIZ_BANDS, on_data=self._on_data)
        # Prefer tapping the player's own output (works everywhere, incl.
        # macOS); fall back to system-audio capture only if the tap is absent.
        self.connect("map", lambda *_: (
            self.spectrum.attach(self.player.viz_sink) or self.spectrum.start()))
        self.connect("close-request", self._on_close)
        self._pos_src = GLib.timeout_add(250, self._tick_position)

        # full-window equalizer view (second stack page; tabs in the footer)
        self.eqview = EQView(self)
        self.stack.add_named(self.eqview, "equalizer")

    # ---- tiny builders ------------------------------------------------
    @staticmethod
    def _mk(widget, *classes):
        for c in classes:
            widget.add_css_class(c)
        return widget

    def _sep(self):
        s = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        s.add_css_class("eaa-xsep")
        return s

    # ---- status footer ------------------------------------------------
    def _build_footer(self):
        """Bottom strip: PLAYER / EQUALIZER view tabs on the left, and a status
        label on the right showing the running version (dim 'current' green;
        amber + clickable when a newer build is available — Win/mac only)."""
        self._update_url = ""
        bar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=0)
        bar.add_css_class("eaa-footerbar")

        self.tab_player = Gtk.ToggleButton(label="PLAYER")
        self.tab_eq = Gtk.ToggleButton(label="EQUALIZER")
        self.tab_eq.set_group(self.tab_player)
        self.tab_player.set_active(True)
        for t, name in ((self.tab_player, "player"), (self.tab_eq, "equalizer")):
            t.add_css_class("eaa-tab")
            t.set_can_focus(False)
            t.connect("toggled", self._on_tab, name)
            bar.append(t)

        bar.append(Gtk.Box(hexpand=True))   # spacer

        self._footer = Gtk.Button()         # status (version / update notice)
        self._footer.add_css_class("eaa-footer")
        self._footer.set_can_focus(False)
        self._footer_lbl = Gtk.Label(label=f"EASYAMP  v{__version__}", xalign=1)
        self._footer.set_child(self._footer_lbl)
        self._footer.connect("clicked", self._on_footer_clicked)
        bar.append(self._footer)
        return bar

    def _on_tab(self, btn, name):
        if btn.get_active():
            self.stack.set_visible_child_name(name)
            if name == "equalizer" and getattr(self, "eqview", None):
                self.eqview.refresh()

    def _on_footer_clicked(self, _b):
        if not self._update_url:
            return
        # Gtk.show_uri is portal-aware, so the link opens correctly inside the
        # Flatpak sandbox too (falls back to webbrowser if unavailable).
        try:
            Gtk.show_uri(self, self._update_url, Gdk.CURRENT_TIME)
        except Exception:
            import webbrowser
            webbrowser.open(self._update_url)

    def _on_update_available(self, version, url):
        self._update_url = url
        self._footer_lbl.set_text(f"EASYAMP  v{version} AVAILABLE  —  CLICK TO UPDATE")
        self._footer.add_css_class("update")
        return False  # GLib.idle_add one-shot

    # ---- panel toggles ------------------------------------------------
    def on_toggle_eq(self, btn):
        vis = not self.eq_panel.get_visible()
        self.eq_panel.set_visible(vis)
        set_led(btn, vis)

    def on_toggle_pl(self, btn):
        vis = not self.playlist_panel.get_visible()
        self.playlist_panel.set_visible(vis)
        set_led(btn, vis)

    def on_toggle_viz(self, _b):
        mode = "vu" if self.viz.get_mode() == "spec" else "spec"
        self.viz.set_mode(mode)
        set_led(self.btn_viz, mode == "vu")

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
        self.btn_play.icon.set_kind("pause")
        track_name = os.path.splitext(os.path.basename(path))[0]
        self.marquee.set_text(track_name)
        if getattr(self, "eqview", None):
            self.eqview.set_track(track_name)
        self.playlist_panel.set_current(idx)

    def on_playpause(self, _b):
        if self.track < 0:
            if self.playlist:
                self._play_track(0)
            return
        self.player.toggle()
        self._playing = self.player.is_playing()
        self.btn_play.icon.set_kind("pause" if self._playing else "play")
        self._set_state("PLAY" if self._playing else "PAUSE")

    def on_stop(self, _b):
        self.player.stop()
        self._playing = False
        self.btn_play.icon.set_kind("play")
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

    def _on_play_error(self, message):
        """A track failed to load/decode: stop and report, never auto-advance."""
        self._playing = False
        self.btn_play.icon.set_kind("play")
        self._set_state("STOP")
        self.marquee.set_text("LOAD ERROR")
        if getattr(self, "eqview", None):
            self.eqview.set_track("LOAD ERROR")

    def _on_tags(self, info):
        artist, title = info.get("artist", ""), info.get("title", "")
        if title:
            text = f"{artist} - {title}" if artist else title
            self.marquee.set_text(text)
            if getattr(self, "eqview", None):
                self.eqview.set_track(text)

    def _set_state(self, state):
        self.ind_state.set_text(state)
        (self.ind_state.add_css_class if state == "PLAY"
         else self.ind_state.remove_css_class)("on")
        self.status.set_state(state.lower())
        if getattr(self, "eqview", None):
            self.eqview.set_state(state)

    def _do_seek(self, frac):
        if self.track >= 0:
            self.player.seek_fraction(frac)

    def _tick_position(self):
        if self.track >= 0:
            dur, pos = self.player.duration(), self.player.position()
            if dur > 0:
                self.seek.set_fraction(pos / dur)
            self.lcd_time.set_text(_fmt(pos))
            if getattr(self, "eqview", None):
                self.eqview.set_time(_fmt(pos))
            si = self.player.stream_info()
            self.ind_khz.set_text(f"{round(si['rate']/1000)}K" if si["rate"] else "--K")
            self.ind_kbps.set_text(f"{round(si['bitrate']/1000)}K" if si["bitrate"] else "--K")
            self.ind_chan.set_text("STEREO" if si["channels"] == 2
                                   else ("MONO" if si["channels"] == 1 else "--"))
        return True

    # ---- capture -> displays -------------------------------------------
    def _on_data(self, levels, vu, wave):
        self.viz.set_data(levels, vu)
        self.scope.set_wave(wave)
        if getattr(self, "eqview", None):
            self.eqview.set_audio(levels, vu, wave)
        return False

    def _on_close(self, *_):
        if self._pos_src:
            GLib.source_remove(self._pos_src)
            self._pos_src = None
        self.spectrum.stop()
        self.player.stop()
        return False
