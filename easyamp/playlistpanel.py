"""Playlist panel: a green-on-black numbered track list with file actions.

Rows are :class:`~easyamp.sources.base.Track` objects (local files and
remote/source-bound tracks alike); the file dialogs wrap picked paths with
``Track.local`` so every callback traffics in Tracks."""

from __future__ import annotations

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, Gio, GLib  # noqa: E402

from .widgets import panel_bar  # noqa: E402
from .sources.base import Track  # noqa: E402
from . import m3u  # noqa: E402

AUDIO_PATTERNS = ("*.mp3", "*.flac", "*.wav", "*.ogg", "*.opus",
                  "*.m4a", "*.aac", "*.wma", "*.mp4")


class PlaylistPanel(Gtk.Box):
    def __init__(self, on_play, on_add, on_replace, on_remove, on_clear):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        self.on_play = on_play
        self.on_add = on_add
        self.on_replace = on_replace
        self.on_remove = on_remove
        self.on_clear = on_clear
        self._tracks: list[Track] = []
        self._current = -1

        self.append(panel_bar("EASYAMP PLAYLIST"))

        scroller = Gtk.ScrolledWindow()
        scroller.set_vexpand(True)
        scroller.set_hexpand(True)
        scroller.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        scroller.add_css_class("eaa-playlist")
        self.listbox = Gtk.ListBox()
        self.listbox.set_selection_mode(Gtk.SelectionMode.SINGLE)
        # single click only selects (so you can scroll/select freely);
        # double-click (or Enter) plays — avoids hijacking playback on a tap
        self.listbox.set_activate_on_single_click(False)
        self.listbox.connect("row-activated", self._on_row_activated)
        scroller.set_child(self.listbox)
        self.append(scroller)

        actions = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        actions.add_css_class("eaa-panel")
        actions.append(self._stacked("+", "FILE", self._add))
        actions.append(self._stacked("−", "FILE", self._rem))
        for label, cb in (("CLR", self._clear), ("LOAD", self._load), ("SAVE", self._save)):
            b = Gtk.Button(label=label)
            b.add_css_class("eaa-button")
            b.connect("clicked", cb)
            actions.append(b)
        self.append(actions)

    def _stacked(self, symbol: str, text: str, cb) -> Gtk.Button:
        b = Gtk.Button()
        b.add_css_class("eaa-button")
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        box.set_halign(Gtk.Align.CENTER)
        s = Gtk.Label(label=symbol)
        s.add_css_class("eaa-btn-sym")
        t = Gtk.Label(label=text)
        t.add_css_class("eaa-btn-txt")
        box.append(s)
        box.append(t)
        b.set_child(box)
        b.connect("clicked", cb)
        return b

    # ---- display ------------------------------------------------------
    def set_tracks(self, tracks: list[Track]) -> None:
        """Rebuild the row list. Rows are appended in idle-sized chunks so a
        huge playlist (a full-library Plex playlist can be thousands of
        tracks) never freezes the UI thread."""
        self._tracks = list(tracks)
        self._build_gen = getattr(self, "_build_gen", 0) + 1
        gen = self._build_gen
        child = self.listbox.get_first_child()
        while child:
            nxt = child.get_next_sibling()
            self.listbox.remove(child)
            child = nxt
        cur = self._current
        self._current = -1
        it = iter(enumerate(self._tracks))

        def add_chunk():
            if gen != self._build_gen:
                return False        # a newer rebuild superseded this one
            for _ in range(150):
                try:
                    i, track = next(it)
                except StopIteration:
                    self.set_current(cur)
                    return False
                lbl = Gtk.Label(label=f"{i + 1}. {track.display()}", xalign=0)
                lbl.add_css_class("eaa-track")
                lbl.set_ellipsize(3)  # PANGO_ELLIPSIZE_END
                row = Gtk.ListBoxRow()
                row.set_child(lbl)
                self.listbox.append(row)
            return True             # more rows: continue on the next idle

        # build the first screenful synchronously so short lists still
        # appear instantly, then hand off to idle chunks
        if add_chunk():
            GLib.idle_add(add_chunk)

    def set_current(self, idx: int) -> None:
        # touch only the old and new rows — walking every row is O(n) per
        # track change and crawls on thousand-row playlists
        old = self._current
        self._current = idx
        if old >= 0 and old != idx:
            row = self.listbox.get_row_at_index(old)
            if row and row.get_child():
                row.get_child().remove_css_class("current")
        if idx >= 0:
            row = self.listbox.get_row_at_index(idx)
            if row:
                if row.get_child():
                    row.get_child().add_css_class("current")
                self.listbox.select_row(row)

    # ---- handlers -----------------------------------------------------
    def _on_row_activated(self, _lb, row):
        self.on_play(row.get_index())

    def _selected_index(self) -> int:
        row = self.listbox.get_selected_row()
        return row.get_index() if row else -1

    def _add(self, _b):
        self._open_files(self.on_add)

    def _rem(self, _b):
        idx = self._selected_index()
        if idx >= 0:
            self.on_remove(idx)

    def _clear(self, _b):
        self.on_clear()

    def _open_files(self, cb):
        dialog = Gtk.FileDialog(title="Add audio")
        flt = Gtk.FileFilter()
        flt.set_name("Audio files")
        for p in AUDIO_PATTERNS:
            flt.add_pattern(p)
        filters = Gio.ListStore.new(Gtk.FileFilter)
        filters.append(flt)
        dialog.set_filters(filters)

        def done(dlg, res):
            try:
                files = dlg.open_multiple_finish(res)
            except GLib.Error:
                return
            paths = [files.get_item(i).get_path() for i in range(files.get_n_items())]
            cb([Track.local(p) for p in paths if p])

        dialog.open_multiple(self.get_root(), None, done)

    def _load(self, _b):
        dialog = Gtk.FileDialog(title="Load playlist (.m3u)")
        flt = Gtk.FileFilter()
        flt.set_name("Playlists")
        flt.add_pattern("*.m3u")
        flt.add_pattern("*.m3u8")
        filters = Gio.ListStore.new(Gtk.FileFilter)
        filters.append(flt)
        dialog.set_filters(filters)

        def done(dlg, res):
            try:
                f = dlg.open_finish(res)
            except GLib.Error:
                return
            path = f.get_path()
            if not path:
                return
            try:
                tracks = m3u.parse(path)
            except OSError:
                return
            self.on_replace(tracks)

        dialog.open(self.get_root(), None, done)

    def _save(self, _b):
        dialog = Gtk.FileDialog(title="Save playlist (.m3u)")
        dialog.set_initial_name("playlist.m3u")

        def done(dlg, res):
            try:
                f = dlg.save_finish(res)
            except GLib.Error:
                return
            path = f.get_path()
            if path:
                with open(path, "w", encoding="utf-8") as fh:
                    fh.write(m3u.serialize(self._tracks))

        dialog.save(self.get_root(), None, done)
