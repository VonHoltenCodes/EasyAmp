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
| **EasyEffects** (Wellington Wallace / wwmm) | GPL-3.0 | EasyAmp controls a separately-installed EasyEffects via its CLI / D-Bus / GSettings. **None of its code is included or linked**, so the GPL does not extend to EasyAmp. Credited in the README. |
| **EasyEffects-Presets** (JackHack96) | per that project | EasyAmp does **not** redistribute these presets; they are installed by the user into EasyEffects. EasyAmp's own built-in EQ presets are original. Credited in the README. |
| **GStreamer** | LGPL-2.1+ (plus per-plugin licenses) | System library; provides playback and the `equalizer-10bands` / capture pipeline. Not redistributed by EasyAmp. |
| **GTK 4 / PyGObject** | LGPL-2.1+ | System libraries; the UI toolkit. Not redistributed by EasyAmp. |
| **PipeWire** | MIT | System audio server used for capture. Not redistributed. |
| **NumPy** | BSD-3-Clause | Pure-Python dependency, installed via pip (not vendored). |

> When EasyAmp is packaged as a **Flatpak**, the GNOME runtime supplies GTK4 /
> GStreamer / PyGObject under their own licenses; NumPy is built from its BSD
> source as a manifest module. The bundled OFL fonts are installed into the
> app with their license files intact.

If you believe something here is mis-attributed, please open an issue.
