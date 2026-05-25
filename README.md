# EasyAmp

A classic-player-style **shell for [EasyEffects](https://github.com/wwmm/easyeffects)** — a loving tribute to the late-90s desktop audio player, built as a retro remote control for your system EQ.

EasyAmp does **not** fork or modify EasyEffects. It's a small, standalone GTK4 app that drives a normally-installed EasyEffects from the outside, wrapping it in beveled gunmetal chrome and a green 7-segment LCD.

> Original artwork and an original name. EasyAmp uses **no** trademarked names, logos, or skin bitmaps from any media player. It's a tribute to an era, not a clone of a product.

## Status

**v0.1 — alpha.** Working today:

- Reads and lists your EasyEffects output presets
- Loads a preset from a dropdown
- Global bypass toggle with a power LED
- LCD readout of the active preset
- Retro skin (gunmetal bevels + DSEG7 green LCD + Pixelify pixel font)

Planned (see `ARCHITECTURE.md`):

- Live spectrum/oscilloscope visualizer (its own PipeWire capture + FFT)
- **Analog VU meters** as a switchable alternative to digital bar meters
- Input-preset support and per-category state
- Optional compact/"shade" mode

## Why a shell instead of a fork?

EasyEffects is GTK4/libadwaita and follows the GNOME HIG closely; a true WinAmp-style *layout* would require forking its C++/Blueprint UI and maintaining a heavy downstream fork that upstream wouldn't merge. Instead, EasyAmp leans on the control surface EasyEffects **already exposes** — its CLI (forwarded over D-Bus to the running instance) and its GSettings — so EasyEffects does all the DSP while EasyAmp is just a retro face. Lightweight, no fork to maintain, and it can be pixel-perfect because it owns its own window.

## Requirements

- EasyEffects (Flatpak `com.github.wwmm.easyeffects`, or a native install)
- Python 3 + PyGObject with GTK 4 (`gir1.2-gtk-4.0`)
- Fonts (bundled-by-reference, OFL): **DSEG7 Classic** and **Pixelify Sans** in `~/.local/share/fonts`

## Run

```bash
./run.sh
# or
python3 -m easyamp
```

## License

MIT (see `LICENSE`). EasyAmp only invokes EasyEffects externally and includes none of its code.
