# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec for the Windows build (one-folder).
#
# Run from an MSYS2 MINGW64 shell so PyInstaller sees the mingw Python and the
# GTK4/GStreamer stack. Relies on pyinstaller-hooks-contrib, which ships hooks
# for each gi.repository.* module that collect the matching GObject-
# introspection typelib and shared libraries; GStreamer plugins are collected
# too.

from PyInstaller.utils.hooks import collect_all
from PyInstaller.utils.hooks.gi import get_gi_typelibs

datas, binaries, hiddenimports = [], [], []
for pkg in ("easyamp", "numpy"):
    d, b, h = collect_all(pkg)
    datas += d
    binaries += b
    hiddenimports += h

# Explicitly collect the GObject-introspection namespaces (typelib + the
# shared libraries each declares, e.g. libgtk-4-1.dll). PyInstaller's auto
# hooks miss Gtk 4.0 on MSYS2, so we pin the whole GTK4 stack ourselves.
for ns, ver in [
    ("GLib", "2.0"), ("GObject", "2.0"), ("Gio", "2.0"), ("GModule", "2.0"),
    ("cairo", "1.0"), ("HarfBuzz", "0.0"), ("Graphene", "1.0"),
    ("Pango", "1.0"), ("PangoCairo", "1.0"), ("GdkPixbuf", "2.0"),
    ("Gdk", "4.0"), ("Gsk", "4.0"), ("Gtk", "4.0"),
    ("Gst", "1.0"), ("GstApp", "1.0"),
]:
    try:
        b, d, h = get_gi_typelibs(ns, ver)
        binaries += b
        datas += d
        hiddenimports += h
    except Exception as exc:   # noqa: BLE001 — keep building; log what's missing
        print(f"WARN: could not collect GI namespace {ns}-{ver}: {exc}")

# GI modules EasyAmp imports (each triggers its contrib hook).
hiddenimports += [
    "gi",
    "cairo",
    # The foreign-struct converter that lets GTK pass a cairo.Context to
    # Python draw callbacks. PyInstaller misses it, which makes every Cairo
    # draw raise "Couldn't find foreign struct converter for 'cairo.Context'".
    "gi._gi_cairo",
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
