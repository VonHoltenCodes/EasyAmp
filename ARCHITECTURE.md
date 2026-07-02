# EasyAmp architecture

EasyAmp is a self-contained GTK4 media player: it decodes and plays audio
itself, applies its own parametric EQ, and renders its own visualizers. It
has no external service dependencies.

## Components

```
easyamp/
├── app.py          # GTK Application bootstrap: fonts, stylesheet, main()
├── window.py       # the main window: layout, transport, playlist state, footer tabs
├── player.py       # GStreamer playbin + the built-in EQ/tone/balance chain
├── spectrum.py     # system-audio capture (GStreamer -> appsink) + numpy FFT analysis
├── viz.py          # visualizer widgets: scope, spectrum/VU, log spectrum, waveform
├── widgets.py      # shared widgets: title bars, buttons, seek bar, knobs, LED meters, marquee
├── paint.py        # shared Cairo helpers: smoke backdrop, value colours, beveled thumbs
├── eqmodel.py      # shared EQ band model: constants, band layouts, interpolation (pure Python)
├── eqpanel.py      # docked EQ panel: ON/BASS/LOUD + the 10-band widget
├── eqwidget.py     # custom-drawn interactive 10-band EQ (the docked panel's surface)
├── eqview.py       # full-window EQUALIZER tab: N-band bank, knobs, import/export
├── eqbank.py       # the 10–32 band parametric slider bank (eqview's surface)
├── eqpresets.py    # portable JSON EQ presets (built-ins + user)
├── presetui.py     # the shared presets popover (list / save-as) used by both EQ surfaces
├── eqio.py         # Equalizer APO / AutoEQ GraphicEQ import + export
├── playlistpanel.py# playlist (track list + ADD/REM/CLR/LOAD/SAVE, .m3u)
├── update_check.py # polls dl.easyampstereo.com/latest.json, lights the footer badge
├── fontload.py     # installs bundled fonts on first run (Linux/macOS)
├── style.css       # the skin
└── fonts/          # bundled DSEG7 + Pixelify Sans (SIL OFL)
```

Two window pages sit in a `Gtk.Stack`, switched by the footer tabs:
**PLAYER** (display, visualizer, transport, docked EQ + playlist panels) and
**EQUALIZER** (a docked mini player strip + the full parametric EQ). The
window owns playback state and forwards time/track/state/audio-data updates
to whichever surfaces display them.

## Playback + EQ

`player.py` builds a GStreamer `playbin` and inserts a processing bin as its
`audio-filter`:

```
playbin → in-gain → pitch (varispeed) → equalizer-nbands → tone → balance → out-gain → sink
```

- `equalizer-nbands` is a single stereo bank of 10–32 fully parametric bands
  (independent centre frequency, Q, gain, and shelf/peak type per band), so
  it can model Equalizer-APO style filter sets. Band layouts, types, and the
  curve-preserving band-count interpolation live in `eqmodel.py`.
- A second `equalizer-3bands` stage provides the **BASS** and **LOUD** tone
  toggles; `audiopanorama` provides balance.
- Pitch prefers the SoundTouch `pitch` element (bundled on Windows) and falls
  back to the dependency-free `speed` resampler (GNOME runtime / Homebrew).
  Both behave as cassette-style varispeed.
- Output goes to the default sink, so it is also picked up by the visualizer
  capture like any other audio source.

**⚠️ The EQ must stay a single stereo bank.** A per-channel L/R split
(deinterleave → EQ-L/EQ-R → interleave) shipped in 0.4.1–0.4.3 and was
reverted: tearing that topology down deadlocks GStreamer state changes
(`set_state(NULL)` hangs the main thread), freezing the app on every track
change. Do not reintroduce a split without a provably deadlock-free design —
and verify any pipeline change by running the real app through track changes,
not just headless tests.

Track load errors stop playback and show "LOAD ERROR"; they never
auto-advance (an error cascade used to race down the whole playlist).
Error handling is deferred to the main loop via `GLib.idle_add` because
calling `set_state()` inside a bus callback deadlocks.

## Visualizer capture

`spectrum.py` captures the system output independently of playback, so the
meters reflect **all** audio on the machine. The capture source is
platform-specific (`_make_capture_source`):

- **Linux/PipeWire**: a sink-monitor source found via `GstDeviceMonitor`
  (no host `pactl` needed, which also makes it work inside Flatpak).
- **Windows**: WASAPI loopback (`wasapi2src`/`wasapisrc`) records the default
  render device directly.
- **macOS/other**: the default input, which carries system audio only if a
  loopback device (e.g. BlackHole) is the default input.

Each FFT frame yields log-spaced spectrum bands, per-channel RMS (VU), and a
decimated waveform — computed with numpy and marshalled to the GTK main loop
via `GLib.idle_add`. The display widgets in `viz.py` are passive: the window
feeds them data and they redraw.

## UI

The chrome, meters, EQ surfaces, knobs, and seek bar are custom Cairo
drawings (`viz.py`, `widgets.py`, `eqwidget.py`, `eqbank.py`, sharing helpers
from `paint.py`); the rest is CSS over standard GTK widgets. Transport
glyphs are Cairo-drawn (`TransportIcon`) because the U+23xx media symbols
render as tofu on common Windows fonts. The stylesheet loads at
`STYLE_PROVIDER_PRIORITY_APPLICATION` and is applied in `do_activate` (not
`do_startup`) because on Windows no default display exists at startup and
the CSS would be silently dropped.

Playlist rows play on double-click/Enter only; single click just selects.

## Updates

`update_check.py` polls `https://dl.easyampstereo.com/latest.json` on launch
(all platforms) and turns the footer badge amber with a link when a newer
version is published. Under Flatpak this requires `--share=network` in the
manifest's finish-args — without it the check dies silently in the sandbox.

## Non-goals

- No external audio daemon control; EasyAmp processes only its own playback.
- No reimplementation of a system-wide effects engine.
