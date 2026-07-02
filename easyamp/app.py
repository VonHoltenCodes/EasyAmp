"""EasyAmp application entry point: GTK bootstrap, fonts, and the stylesheet.

The actual UI lives in :mod:`easyamp.window`. This module stays the home of
``main()`` — it is the ``easyamp.app:main`` entry point referenced by
pyproject's gui-script and by the Windows/macOS PyInstaller launchers.
"""

from __future__ import annotations

import os
import sys

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, Gdk, Gio  # noqa: E402

from .window import EasyAmpWindow  # noqa: E402

APP_ID = "com.vonholtencodes.EasyAmp"
STYLE = os.path.join(os.path.dirname(__file__), "style.css")


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
