# EasyAmp retro — Windows 98 SE / XP

A native build of EasyAmp for machines the GTK app can never reach: one ANSI
Win32 executable that runs on Windows 98 SE through XP SP3, on a Pentium II
class CPU (no SSE), in 16 MB of RAM.

It is a sibling of the GTK app, not a port: same look, same EQ model and
preset roster, same playlist format, written from scratch in C.

![player](docs/retro-player.png)
![equalizer](docs/retro-equalizer.png)

## How it is built

| Piece | File | Notes |
|---|---|---|
| Renderer | `src/gfx.c` | Every pixel is drawn in software into a 32-bit buffer and handed to Windows as one picture. No GDI text, pens or theming, so 98 SE, XP and Wine render identically. |
| Fonts | `tools/mkfont.py` → `src/fonts_gen.c` | DSEG7 and Pixelify Sans pre-rasterized with their LCD glow baked in, so nothing is blurred at runtime. |
| Interface | `src/ui.c` | Layout, chrome, widgets, input. Portable C; static chrome is painted once per page and widgets repaint only their own patch. |
| Model | `src/app.c` | Transport, playlist, EQ bands and presets. |
| Signal path | `src/dsp.c` | In-gain → N parametric bands → BASS/LOUD shelves → balance → out-gain → soft limit; FFT analysis for the meters. x87 float, denormal-safe. |
| Playback | `src/engine_win32.c` | Worker thread: minimp3 / PCM WAV → varispeed → DSP → `waveOut`. Meters are analysed at the position the card is actually playing. |
| Shell | `src/main_win32.c` | Borderless window, file dialogs, drag and drop, ID3 titles, m3u. |

## Colour depth

Windows 98 boxes run anything from 16 colours to true colour, and left alone
GDI ruins a subtle skin on all but the last: it truncates to 16-bit (erasing
the 2-6% scanline sheen and banding the gradients), and on a 256-colour
desktop it maps everything onto a default palette with almost no dark blues.
So the client converts its own picture, per desktop depth, re-checked on
`WM_DISPLAYCHANGE`:

| Desktop | Path |
|---|---|
| 24 / 32-bit | as drawn |
| 15 / 16-bit | ordered dither to 5-5-5 / 5-6-5 |
| 256 colours | its own 236-colour palette, tuned to the skin from the app's renders (`make palette`), realized while in front, plus dither |
| 16 colours | dither into the fixed VGA 16 |

The dither matrix is keyed to screen position, so partial redraws tile
without seams and nothing shimmers. `/depth:32|16|15|8|4` forces a path on
any desktop.

![256 colours](docs/retro-player-256-colours.png)
![16 colours](docs/retro-player-16-colours.png)

## Build

```sh
make palette # re-tune the 256-colour palette after a visual change (commit the result)
make shots   # render every UI state to build/shots/*.png, no Windows needed
make test    # known-answer checks for the filters and the analyzer
make win     # cross-compile build/EASYAMP.EXE (needs i686-w64-mingw32-gcc)
```

The subsystem / OS version stamps in the Makefile are what let a real
Windows 98 loader accept the file. `-mno-sse` and `MINIMP3_NO_SIMD` keep it
runnable on a Pentium II or Mendocino Celeron.

`EASYAMP.EXE file.mp3 /shot:out.bmp /shotms:4000` plays a file, saves what is
on screen after four seconds, and exits — how the running program is checked
under Wine.

## Status

Working: both main pages, transport, seek, playlist (add / remove / clear /
m3u load + save / drag and drop), 10–32 band parametric EQ with presets,
BASS / LOUD, balance, varispeed pitch, spectrum / VU / scope / LED meters,
MP3 + WAV.

Not yet: the SOURCES page (Plex / Jellyfin), APO import / export, saving
settings between runs, FLAC / Ogg, and a run on real Windows 98 hardware.

## Third party

`third_party/minimp3.h` — lieff/minimp3, CC0.
Fonts: DSEG7 Classic and Pixelify Sans, SIL OFL (see `easyamp/fonts/`).
