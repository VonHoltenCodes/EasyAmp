"""Playlist panel: a green-on-black numbered track list with file actions."""

from __future__ import annotations

import os

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, Gio, GLib  # noqa: E402

from .widgets import panel_bar  # noqa: E402

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
        self._tracks: list[str] = []
        self._current = -1

        self.append(panel_bar("EASYAMP PLAYLIST"))

        scroller = Gtk.ScrolledWindow()
        scroller.set_vexpand(True)
        scroller.set_hexpand(True)
        scroller.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        scroller.add_css_class("eaa-playlist")
        self.listbox = Gtk.ListBox()
        self.listbox.set_selection_mode(Gtk.SelectionMode.SINGLE)
        self.listbox.connect("row-activated", self._on_row_activated)
        scroller.set_child(self.listbox)
        self.append(scroller)

        actions = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        actions.add_css_class("eaa-panel")
        for label, cb in (("ADD", self._add), ("REM", self._rem),
                          ("CLR", self._clear), ("LOAD", self._load),
                          ("SAVE", self._save)):
            b = Gtk.Button(label=label)
            b.add_css_class("eaa-button")
            b.connect("clicked", cb)
            actions.append(b)
        self.append(actions)

    # ---- display ------------------------------------------------------
    def set_tracks(self, paths: list[str]) -> None:
        self._tracks = list(paths)
        child = self.listbox.get_first_child()
        while child:
            nxt = child.get_next_sibling()
            self.listbox.remove(child)
            child = nxt
        for i, path in enumerate(self._tracks):
            name = os.path.splitext(os.path.basename(path))[0]
            lbl = Gtk.Label(label=f"{i + 1}. {name}", xalign=0)
            lbl.add_css_class("eaa-track")
            lbl.set_ellipsize(3)  # PANGO_ELLIPSIZE_END
            row = Gtk.ListBoxRow()
            row.set_child(lbl)
            self.listbox.append(row)
        self.set_current(self._current)

    def set_current(self, idx: int) -> None:
        self._current = idx
        i = 0
        row = self.listbox.get_first_child()
        while row:
            lbl = row.get_child()
            if lbl:
                if i == idx:
                    lbl.add_css_class("current")
                else:
                    lbl.remove_css_class("current")
            i += 1
            row = row.get_next_sibling()
        if 0 <= idx:
            target = self.listbox.get_row_at_index(idx)
            if target:
                self.listbox.select_row(target)

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
            cb([p for p in paths if p])

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
            base = os.path.dirname(path)
            out = []
            with open(path, encoding="utf-8", errors="ignore") as fh:
                for line in fh:
                    line = line.strip()
                    if not line or line.startswith("#"):
                        continue
                    out.append(line if os.path.isabs(line) else os.path.join(base, line))
            self.on_replace(out)

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
                    fh.write("#EXTM3U\n")
                    for t in self._tracks:
                        fh.write(t + "\n")

        dialog.save(self.get_root(), None, done)
