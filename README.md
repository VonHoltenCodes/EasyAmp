# EasyAmp

A **classic-player-style media player with a 10-band EQ** for Linux — a loving tribute to the late-90s desktop audio player, built fresh in GTK4 with original artwork.

EasyAmp plays your local music through its own GStreamer pipeline (with a built-in graphic EQ), shows a live spectrum or analog VU meters, **and** doubles as a retro front-end for [EasyEffects](https://github.com/wwmm/easyeffects) system-wide processing. Because it owns its own window, the chrome is pixel-styled to evoke the era.

> Original artwork and an original name. EasyAmp uses **no** trademarked names, logos, or skin bitmaps from any media player. It's a tribute to an era, not a clone of a product.

![EasyAmp — spectrum view](assets/easyamp-spectrum.png)

![EasyAmp — VU meter view](assets/easyamp-vu.png)

## Features

- **Media player** — open files/playlists (anything GStreamer decodes: MP3, FLAC, WAV, OGG, Opus, M4A…), transport controls, seek, track metadata, `.m3u` load/save.
- **Built-in 10-band graphic EQ** — preamp + 10 bands with a live response curve, value-colored sliders, bypass, and **portable JSON presets** (auto-loads an `EASYAMP DEFAULT` preset if you save one).
- **Visualizer** — switch between a live **green spectrum analyzer** (PipeWire capture + FFT) and dual **analog VU meters** (atomic-green, real dB scale, glowing needle).
- **Mini scope** — a small mirrored bar-graph waveform under the timer.
- **System EQ control** — drives a running EasyEffects instance (preset select + global bypass) for system-wide audio, so it still visualizes/controls anything playing (browser, streaming apps, etc.).
- **Docked single-window UI** — player + EQ + playlist snap together with EQ/PL toggles, in a beveled gunmetal skin with green LCD readouts.

## Requirements

- Python 3 + PyGObject with **GTK 4** (`gir1.2-gtk-4.0`)
- **GStreamer 1.x** with the base/good plugins (playback, `equalizer-10bands`)
- **numpy** (spectrum FFT)
- **PipeWire** with `parec` (PulseAudio compat) for the visualizer capture
- **EasyEffects** (optional) — for the system-EQ controls
- Fonts (OFL, optional but recommended) in `~/.local/share/fonts`: **DSEG7 Classic** and **Pixelify Sans**

## Install

See **[INSTALL.md](INSTALL.md)** for full per-OS instructions. In short: install
the native prerequisites (GTK4 + PyGObject + GStreamer) from your OS package
manager / Homebrew, then:

```bash
pipx install . --system-site-packages    # gives you an `easyamp` launcher
```

Bundled fonts (DSEG7, Pixelify Sans — both SIL OFL) are copied into your user
font directory on first run, so the intended look shows up with no manual setup.

## Run (without installing)

```bash
./run.sh
# or
python3 -m easyamp
```

## Architecture

See [`ARCHITECTURE.md`](ARCHITECTURE.md) for the design and the EasyEffects control surface. In short: a GStreamer `playbin` (with `equalizer-10bands` as its `audio-filter`) handles playback; the spectrum/VU come from an independent `parec` capture so they reflect *all* system audio; EasyEffects is driven via its CLI/D-Bus/GSettings.

## Authors

- **Created and authored by [VonHoltenCodes](https://github.com/VonHoltenCodes)** (Trenton Von Holten) — main author & creator.
- **Co-authored with Claude** (Anthropic's Claude Code).

## Credits & acknowledgements

EasyAmp stands on a lot of open-source work — thank you to:

- **[EasyEffects](https://github.com/wwmm/easyeffects)** by Wellington Wallace (wwmm) — the system-wide audio effects engine EasyAmp front-ends.
- **[EasyEffects-Presets](https://github.com/JackHack96/EasyEffects-Presets)** by JackHack96 — the EasyEffects preset collection used for system-EQ presets.
- **[GStreamer](https://gstreamer.freedesktop.org/)** — playback pipeline and the `equalizer-10bands` graphic EQ.
- **[PipeWire](https://pipewire.org/)** — audio capture for the visualizer.
- **[GTK 4](https://www.gtk.org/) / [PyGObject](https://pygobject.gnome.org/)** — the UI toolkit.
- **[DSEG](https://github.com/keshikan/DSEG)** 7-segment font by keshikan (SIL OFL).
- **[Pixelify Sans](https://fonts.google.com/specimen/Pixelify+Sans)** by Stefie Justprince (SIL OFL).

If you maintain something EasyAmp uses and want different/expanded credit, please open an issue.

## License

[MIT](LICENSE). EasyAmp is completely open source. Bundled fonts are under the SIL Open Font License; EasyEffects and its presets are the property of their respective authors and are used/credited per their own licenses.
