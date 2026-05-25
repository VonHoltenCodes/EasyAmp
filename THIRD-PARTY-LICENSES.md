# Third-party licenses & attributions

EasyAmp itself is MIT licensed (see [LICENSE](LICENSE)). This document records
the third-party components it **bundles** or **depends on**, and how their
licenses are honored.

## Bundled with EasyAmp (redistributed in this repository)

| Component | Where | License | Compliance |
|-----------|-------|---------|------------|
| **DSEG7 Classic** font (© 2020 keshikan, Reserved Font Name "DSEG") | `easyamp/fonts/DSEG7Classic-*.ttf` | SIL OFL 1.1 | Full license + copyright shipped alongside the fonts in [`easyamp/fonts/DSEG-OFL.txt`](easyamp/fonts/DSEG-OFL.txt). Fonts are bundled unmodified; the reserved name is not reused. |
| **Pixelify Sans** font (© 2021 The Pixelify Sans Project Authors) | `easyamp/fonts/PixelifySans.ttf` | SIL OFL 1.1 | Full license + copyright shipped in [`easyamp/fonts/PixelifySans-OFL.txt`](easyamp/fonts/PixelifySans-OFL.txt). Bundled unmodified. |

The SIL OFL permits bundling/redistribution with software provided each copy
includes the copyright notice and the license, and the fonts are not sold by
themselves. EasyAmp meets these terms: the OFL text and copyright travel with
the font files and are installed alongside them.

## Used at runtime, **not** bundled (no code redistributed)

| Component | License | Notes |
|-----------|---------|-------|
| **GStreamer** | LGPL-2.1+ (plus per-plugin licenses) | System library; provides playback, the `equalizer-10bands` EQ, and the capture pipeline. Not redistributed by EasyAmp. |
| **GTK 4 / PyGObject** | LGPL-2.1+ | System libraries; the UI toolkit. Not redistributed by EasyAmp. |
| **PipeWire** | MIT | System audio server used for capture. Not redistributed. |
| **NumPy** | BSD-3-Clause | Pure-Python dependency, installed via pip (not vendored). |

> When EasyAmp is packaged as a **Flatpak**, the GNOME runtime supplies GTK4 /
> GStreamer / PyGObject under their own licenses; NumPy is built from its BSD
> source as a manifest module. The bundled OFL fonts are installed into the
> app with their license files intact.

If you believe something here is mis-attributed, please open an issue.
