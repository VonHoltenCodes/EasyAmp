"""Shared EQ-presets popover (used by both the docked EQ panel and the
full-window equalizer view): a scrolling preset list, a name entry, and a
SAVE button. The owner supplies the apply/save behaviour via callbacks."""

from __future__ import annotations

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk  # noqa: E402

from . import eqpresets  # noqa: E402
from .widgets import make_button  # noqa: E402


class PresetPopover(Gtk.Popover):
    """``on_apply(name)`` is called when a preset row is activated;
    ``on_save(name)`` when SAVE is clicked (the owner writes the preset,
    the list reloads afterwards)."""

    def __init__(self, on_apply, on_save):
        super().__init__()
        self._on_apply = on_apply
        self._on_save = on_save

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        for m in ("top", "bottom", "start", "end"):
            getattr(box, f"set_margin_{m}")(6)
        scroller = Gtk.ScrolledWindow()
        scroller.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scroller.set_min_content_height(160)
        scroller.set_min_content_width(180)
        self._list = Gtk.ListBox()
        self._list.connect("row-activated", self._on_row)
        scroller.set_child(self._list)
        box.append(scroller)
        self.reload()
        box.append(Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL))
        self._name = Gtk.Entry()
        self._name.set_placeholder_text("save as…")
        box.append(self._name)
        save = make_button("SAVE PRESET")
        save.connect("clicked", self._on_save_clicked)
        box.append(save)
        self.set_child(box)

    def reload(self) -> None:
        child = self._list.get_first_child()
        while child:
            nxt = child.get_next_sibling()
            self._list.remove(child)
            child = nxt
        for name in eqpresets.list_presets():
            row = Gtk.ListBoxRow()
            lbl = Gtk.Label(label=name, xalign=0)
            lbl.set_margin_start(6)
            lbl.set_margin_end(6)
            row.set_child(lbl)
            self._list.append(row)

    def set_name(self, text: str) -> None:
        self._name.set_text(text)

    def _on_row(self, _lb, row):
        name = row.get_child().get_text()
        self._name.set_text(name)
        self._on_apply(name)

    def _on_save_clicked(self, _btn):
        self._on_save(self._name.get_text().strip() or "My EQ")
        self.reload()
