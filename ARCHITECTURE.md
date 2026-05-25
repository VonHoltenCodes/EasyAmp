# EasyAmp architecture

EasyAmp is a **companion shell**: a standalone GTK4 application that remote-controls
an unmodified EasyEffects instance. This document records the design and the
EasyEffects control surface it relies on.

## Components

```
easyamp/
├── backend.py   # EasyEffects control: CLI + D-Bus + GSettings wrappers
├── app.py       # GTK4 Application + the player window, wired to backend
├── style.css    # the retro skin (gunmetal chrome + green LCD)
└── __main__.py  # entry point (python -m easyamp)
```

- **backend.py** has no GTK dependency and is independently testable.
- **app.py** is pure presentation + event wiring; all state changes go through `backend`.
- **style.css** is loaded at `STYLE_PROVIDER_PRIORITY_APPLICATION`; because EasyAmp owns
  its window (it is *not* a Flatpak and uses *no* libadwaita), CSS applies with no
  sandbox/theme-shadowing issues.

## EasyEffects control surface (verified on v7.2.5)

### CLI (forwarded to the running instance via GApplication/D-Bus)
| Need                | Command                        | Notes |
|---------------------|--------------------------------|-------|
| List presets        | `easyeffects -p`               | "Output Presets: a,b,…" / "Input Presets: …" |
| Load preset         | `easyeffects -l <name>`        | |
| Active preset       | `easyeffects -s output\|input` | |
| Global bypass set   | `easyeffects -b 1` / `-b 2`    | 1 = enable bypass, 2 = disable |
| Global bypass query | `easyeffects -b 3`             | prints `0`/`1` |
| Quit / reset        | `easyeffects -q` / `-r`        | |

On this machine the binary is the Flatpak, invoked as
`flatpak run --command=easyeffects com.github.wwmm.easyeffects …`. `backend._ee_cmd()`
prefers a native `easyeffects` on PATH and falls back to the Flatpak.

### D-Bus
EasyEffects registers `com.github.wwmm.easyeffects` on the session bus and exposes the
standard GApplication interfaces — notably `org.gtk.Actions` (`Activate`/`SetState`) and
`org.freedesktop.Application.ActivateAction`. Useful for triggering app actions without
spawning a process; the CLI is simpler for presets/bypass and is what v0.1 uses.

### GSettings
Five schemas: `com.github.wwmm.easyeffects`, `.spectrum`, `.libportal`,
`.streaminputs`, `.streamoutputs`. Read inside the Flatpak sandbox via
`flatpak run --command=sh … -c 'gsettings …'`. The spectrum trace color lives at
`…spectrum color` (RGBA floats) — EasyAmp reads it so its own visualizer can match.

## Spectrum visualizer (planned)

EasyEffects' spectrum is drawn internally and **not** exposed as data, and its panel
background is hardcoded black (not stylable). So EasyAmp will render its **own** classic
green spectrum by capturing audio independently:

1. Tap a PipeWire monitor source (e.g. via `pw-cat`/`pipewire` Python bindings or a
   GStreamer `pipewiresrc → spectrum` element).
2. Run an FFT (numpy is already on the system) and draw bars/oscilloscope on the
   `.eaa-viz` `Gtk.DrawingArea` with a Cairo snapshot at ~30–60 fps.
3. Match the bar color to EasyEffects' configured spectrum color for consistency.

This is intentionally decoupled from EasyEffects and is the next milestone after the
control UI is solid.

## Meters: digital vs. analog VU (planned)

The level meters fed by the same capture pipeline will be switchable between two looks:

- **Digital** — segmented LED bar meters (green→amber→red), the default.
- **Analog VU** — a custom Cairo-drawn needle gauge with the classic cream/black VU face,
  ballistics approximating the 300 ms VU integration time and a red overload zone.

Both consume the same RMS/peak values from the capture thread; only the
`Gtk.DrawingArea` renderer differs. A toggle in the UI (and a persisted setting) selects
the active meter style.

## Non-goals

- Forking or patching EasyEffects.
- Reimplementing DSP/EQ (EasyEffects owns all audio processing).
- Shipping any trademarked media-player assets.
