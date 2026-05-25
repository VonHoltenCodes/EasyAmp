# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec for the Windows build (one-folder).
#
# Run from an MSYS2 MINGW64 shell so PyInstaller sees the mingw Python and the
# GTK4/GStreamer stack. Relies on pyinstaller-hooks-contrib, which ships hooks
# for each gi.repository.* module that collect the matching GObject-
# introspection typelib and shared libraries; GStreamer plugins are collected
# too.

from PyInstaller.utils.hooks import collect_all

datas, binaries, hiddenimports = [], [], []
for pkg in ("easyamp", "numpy"):
    d, b, h = collect_all(pkg)
    datas += d
    binaries += b
    hiddenimports += h

# GI modules EasyAmp imports (each triggers its contrib hook).
hiddenimports += [
    "gi",
    "cairo",
    "gi.repository.GLib",
    "gi.repository.GObject",
    "gi.repository.Gio",
    "gi.repository.Gtk",
    "gi.repository.Gdk",
    "gi.repository.GdkPixbuf",
    "gi.repository.Gst",
]

a = Analysis(
    ["easyamp_launch.py"],
    pathex=[],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="EasyAmp",
    debug=False,
    strip=False,
    upx=False,
    console=False,          # GUI app: no console window
    icon="easyamp.ico",
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    name="EasyAmp",
)
