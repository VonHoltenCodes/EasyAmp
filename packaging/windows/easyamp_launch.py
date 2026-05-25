"""PyInstaller entry point for the Windows build.

Point GTK at the bundled GSettings schemas and icon themes (which live in
``_internal/share`` next to the frozen exe) before importing anything that
loads GTK, otherwise GTK4 fails to find its settings schema at startup.
"""
import os
import sys

if getattr(sys, "frozen", False):
    _exe_dir = os.path.dirname(sys.executable)
    _internal = os.path.join(_exe_dir, "_internal")
    _base = _internal if os.path.isdir(_internal) else _exe_dir
    os.environ.setdefault(
        "GSETTINGS_SCHEMA_DIR", os.path.join(_base, "share", "glib-2.0", "schemas"))
    os.environ["XDG_DATA_DIRS"] = (
        os.path.join(_base, "share") + os.pathsep + os.environ.get("XDG_DATA_DIRS", ""))
    if hasattr(os, "add_dll_directory") and os.path.isdir(_base):
        try:
            os.add_dll_directory(_base)
        except OSError:
            pass

from easyamp.app import main  # noqa: E402 — must follow the env setup above

sys.exit(main())
