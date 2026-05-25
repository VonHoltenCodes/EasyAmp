# EasyAmp architecture

EasyAmp is a self-contained GTK4 media player: it decodes and plays audio
itself, applies its own graphic EQ, and renders its own visualizers. It has no
external service dependencies.

## Components

```
easyamp/
├── app.py          # GTK4 Application + the docked window (player/EQ/playlist)
├── player.py       # GStreamer playbin + the built-in EQ chain
├── spectrum.py     # GStreamer capture (pulsesrc -> appsink) + numpy analysis
├── eqpanel.py      # EQ panel: header, ON/PRESETS/tone toggles
├── eqwidget.py     # custom-drawn interactive 10-band EQ
├── eqpresets.py    # portable JSON EQ presets (built-ins + user)
├── playlistpanel.py# playlist (track list + ADD/REM/LOAD/SAVE)
├── widgets.py      # shared custom widgets (title bars, LED buttons, seek bar)
├── fontload.py     # installs bundled fonts on first run
├── style.css       # the skin
└── fonts/          # bundled DSEG7 + Pixelify Sans (SIL OFL)
```

## Playback + EQ

`player.py` builds a GStreamer `playbin` and inserts an EQ **bin** as its
`audio-filter`:

```
playbin → (audio-filter) → audioconvert → equalizer-10bands → [tone shelf] → out
```

- `equalizer-10bands` is the 10-band graphic EQ driven by `eqwidget.py`.
- A second shelving stage provides the **Bass** and **Loudness** tone toggles.
- Output goes to the default sink, so it's also picked up by the visualizer
  capture like any other audio source.

`playbin` decodes anything GStreamer can read (MP3/FLAC/WAV/OGG/Opus/M4A…) and
provides position/duration, track tags, and stream info (rate/channels/bitrate).

## Visualizer capture

`spectrum.py` captures the system output independently of playback, so the
spectrum/VU/scope reflect **all** audio on the machine:

```
pulsesrc (sink monitor) → audioconvert → caps(F32LE,2ch,48k) → appsink
```

- The monitor source is found with `GstDeviceMonitor` (no host `pactl`/`parec`),
  which also makes it work inside the Flatpak sandbox.
- Each FFT frame yields: log-spaced spectrum bands, per-channel RMS (VU), and a
  decimated waveform (mini scope) — all computed with numpy and marshalled to
  the GTK main loop via `GLib.idle_add`.

## UI

A single resizable window (`app.py`) docks three panels — player + EQ on the
left, playlist on the right — toggled by the EQ/PL buttons (so they "snap"
together without needing window positioning, which Wayland disallows). The
chrome, meters, EQ, and seek bar are custom Cairo drawings; the rest is CSS over
standard GTK widgets. CSS loads at `STYLE_PROVIDER_PRIORITY_APPLICATION`.

## Non-goals

- No external audio daemon control; EasyAmp processes only its own playback.
- No reimplementation of a system-wide effects engine.
