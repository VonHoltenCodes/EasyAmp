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

# --- Drop GStreamer plugins a local audio player can never use ------------
# collect_all pulls in the whole gst-plugins-{base,good,bad} + libav set:
# hundreds of plugin DLLs. GStreamer scans/validates every one of them to
# build its registry at startup, and Windows loads + AV-scans each DLL, which
# is the bulk of the slow launch. None of the families below touch local (or
# http) audio playback, so removing them speeds startup and shrinks the
# bundle without changing what EasyAmp can play. This is a DENYLIST on
# purpose: every audio decoder/parser/demuxer, libav, the container demuxers
# (asf/wma, isomp4, matroska, ogg), the http source, and album-art image
# loaders are all kept — anything not explicitly listed here stays.
_GST_PLUGIN_DENY = (
    # streaming / network transport (local + plain-http playback don't need these)
    "gstrtsp", "gstrtpmanager", "gstrtp", "gstsrtp", "gstdtls", "gstsctp",
    "gstwebrtc", "gstdashdemux", "gstdash", "gsthls", "gstadaptivedemux",
    "gstsrt", "gstrist", "gstrtmp", "gstquic", "gstnetsim", "gstipcpipeline",
    # hardware video capture / GPU video
    "gstdecklink", "gstnvcodec", "gstnvenc", "gstnvdec", "gstd3d11", "gstd3d12",
    "gstwinscreencap", "gstwinks", "gstdshowvideo", "gstvaapi", "gstmsdk",
    # pure video decoders / encoders
    "gstx264", "gstx265", "gstopenh264", "gstde265", "gstkvazaar", "gstaom",
    "gstdav1d", "gstvpx", "gstsvtav1", "gsttheora", "gstschro",
    # video processing / effects / GL
    "gstvideoconvertscale", "gstvideofilter", "gstvideobox", "gstvideocrop",
    "gstvideorate", "gstvideosignal", "gstvideoparsersbad", "gstdeinterlace",
    "gstcompositor", "gstoverlaycomposition", "gstgeometrictransform",
    "gstcoloreffects", "gstgaudieffects", "gstalphacolor", "gstalpha",
    "gstsmpte", "gstframepositioner", "gstopengl", "gstglstereo",
    # visualizers (EasyAmp draws its own from the appsink)
    "gstaudiovisualizers", "gstgoom", "gstgoom2k1", "gstlibvisual",
    "gstspectrascope",
    # AI / cloud / analytics
    "gstanalytics", "gstdeepgram", "gstelevenlabs", "gstdemucs", "gstclaxon",
    "gstonnx", "gsttensor", "gstwhisper", "gstaws", "gsttranscriber",
    # subtitles / captions
    "gstsubparse", "gstsubenc", "gstclosedcaption", "gstcccombiner",
    "gstdvbsub", "gstdvdsub", "gstassrender", "gstkate", "gstttml", "gstsami",
)


def _drop_unused_gst_plugins(toc):
    kept = []
    for name, path, typ in toc:
        base = name.rsplit("/", 1)[-1].rsplit("\\", 1)[-1].lower()
        if base.startswith("libgst") and any(tok in base for tok in _GST_PLUGIN_DENY):
            continue
        kept.append((name, path, typ))
    return kept


a.binaries = _drop_unused_gst_plugins(a.binaries)
a.datas = _drop_unused_gst_plugins(a.datas)

pyz = PYZ(a.pure)

# Splash shown by the bootloader within ~1s of launch, before Python starts
# importing GTK/GStreamer — the ~20s of native init that follows now has a
# logo on screen instead of nothing. easyamp.app closes it via pyi_splash the
# moment the real window maps. (PyInstaller splash is Windows/Linux only,
# which is exactly where we need it; the macOS build has none.)
splash = Splash(
    "splash.png",
    binaries=a.binaries,
    datas=a.datas,
    text_pos=(20, 248),
    text_size=11,
    text_color="white",
    always_on_top=True,
)

exe = EXE(
    pyz,
    a.scripts,
    splash,                 # splash control lives in the EXE
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
    splash.binaries,        # bundled Tcl/Tk the splash renders with
    strip=False,
    upx=False,
    name="EasyAmp",
)
