"""PyInstaller entry point for the Windows build.

Wire up the bundled GTK runtime *before* importing anything that loads GTK:
  * GSettings schemas + icon themes live in ``_internal/share``;
  * fonts must be made visible to fontconfig (GTK4/MSYS2 uses fontconfig, not
    Windows GDI), which means writing a fonts.conf and pointing
    ``FONTCONFIG_FILE`` at it before Pango initialises.
"""
import os
import sys


def _bootstrap_gtk_runtime() -> None:
    exe_dir = os.path.dirname(sys.executable)
    internal = os.path.join(exe_dir, "_internal")
    base = internal if os.path.isdir(internal) else exe_dir

    os.environ.setdefault(
        "GSETTINGS_SCHEMA_DIR", os.path.join(base, "share", "glib-2.0", "schemas"))
    os.environ["XDG_DATA_DIRS"] = (
        os.path.join(base, "share") + os.pathsep + os.environ.get("XDG_DATA_DIRS", ""))
    if hasattr(os, "add_dll_directory") and os.path.isdir(base):
        try:
            os.add_dll_directory(base)
        except OSError:
            pass

    # Make the bundled fonts discoverable via fontconfig (keep the Windows
    # system fonts too, as fallbacks).
    try:
        font_dir = os.path.join(base, "easyamp", "fonts").replace("\\", "/")
        win_fonts = os.path.join(
            os.environ.get("WINDIR", r"C:\Windows"), "Fonts").replace("\\", "/")
        conf_dir = os.path.join(os.environ.get("LOCALAPPDATA", exe_dir), "EasyAmp")
        os.makedirs(conf_dir, exist_ok=True)
        cache_dir = os.path.join(conf_dir, "fc-cache").replace("\\", "/")
        conf_path = os.path.join(conf_dir, "fonts.conf")
        with open(conf_path, "w", encoding="utf-8") as fh:
            fh.write(
                '<?xml version="1.0"?>\n'
                '<!DOCTYPE fontconfig SYSTEM "fonts.dtd">\n'
                "<fontconfig>\n"
                f"  <dir>{font_dir}</dir>\n"
                f"  <dir>{win_fonts}</dir>\n"
                f"  <cachedir>{cache_dir}</cachedir>\n"
                "</fontconfig>\n"
            )
        os.environ["FONTCONFIG_FILE"] = conf_path
    except OSError:
        pass  # fonts are optional; CSS falls back to a generic family


if getattr(sys, "frozen", False) and sys.platform == "win32":
    _bootstrap_gtk_runtime()

# Update the bootloader splash before the slow GTK/GStreamer imports below,
# so the user sees progress text while the audio stack loads. app.py closes
# the splash when the window maps. Absent in unfrozen/dev runs — harmless.
try:
    import pyi_splash  # noqa: E402  (injected by PyInstaller only in the splash build)
    pyi_splash.update_text("Loading audio engine…")
except Exception:
    pass

from easyamp.app import main  # noqa: E402 — must follow the runtime setup above

sys.exit(main())
