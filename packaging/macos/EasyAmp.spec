# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec for the macOS .app bundle.
#
# Relies on pyinstaller-hooks-contrib, which ships hooks for each
# gi.repository.* module that collect the matching GObject-introspection
# typelib and shared libraries. GStreamer plugins are collected too.

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
    console=False,
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    name="EasyAmp",
)
app = BUNDLE(
    coll,
    name="EasyAmp.app",
    icon="easyamp.icns",
    bundle_identifier="com.vonholtencodes.EasyAmp",
    info_plist={
        "NSHighResolutionCapable": True,
        "LSMinimumSystemVersion": "12.0",
        "CFBundleDisplayName": "EasyAmp",
        "CFBundleShortVersionString": "0.3.2",
    },
)
