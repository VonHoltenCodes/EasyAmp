# EasyAmp — macOS Testing Guide

Thanks for helping test **EasyAmp**, a classic-player-style GTK4 music player with a
built-in 10-band EQ and spectrum / VU visualizers. I don't own a Mac, so I need
a second pair of eyes (and ears) on the macOS build.

There are **two ways** to test. Option A (download the prebuilt app) is easiest.
Option B (build from source) is the fallback if the prebuilt one won't run.

> **Mac type matters.** The prebuilt app is **Apple Silicon only** (M1/M2/M3/M4,
> `arm64`). If you have an **Intel** Mac, skip to **Option B**.
> To check: Apple menu →  About This Mac → look at "Chip" / "Processor".
>
> **macOS 12 (Monterey) or newer** is required.

---

## Option A — Download the prebuilt app (Apple Silicon)

### 1. Download the build

Download the `.dmg` directly (no GitHub account needed):

**https://github.com/VonHoltenCodes/EasyAmp/releases/download/v0.3.3/EasyAmp-macos-arm64.dmg**

(~125 MB. Or browse all downloads at
https://github.com/VonHoltenCodes/EasyAmp/releases/tag/v0.3.3)

### 2. Install

1. Double-click the `.dmg` to mount it.
2. Drag **EasyAmp.app** into your **Applications** folder.

### 3. First launch (important — the app is unsigned)

Because I'm not paying for an Apple Developer signing certificate yet, macOS
Gatekeeper will block the first launch. This is expected. Do **one** of these:

- **Easy way:** Right-click (or Control-click) **EasyAmp.app** → **Open** →
  in the dialog click **Open** again. You only have to do this once.
- **If that's still blocked** (newer macOS), open **Terminal** and run:
  ```bash
  xattr -dr com.apple.quarantine /Applications/EasyAmp.app
  ```
  then open it normally.

If you see *"EasyAmp is damaged and can't be opened"* — that's also just
Gatekeeper on an unsigned app; the `xattr` command above clears it.

---

## Option B — Build & run from source (Intel Macs, or if Option A fails)

Requires [Homebrew](https://brew.sh).

```bash
# 1. Install the GTK + GStreamer stack
brew install pygobject3 gtk4 gstreamer gst-plugins-base gst-plugins-good \
  adwaita-icon-theme librsvg numpy

# 2. Get the code and install it
git clone https://github.com/VonHoltenCodes/EasyAmp.git
cd EasyAmp
pip3 install --break-system-packages .   # or use a venv with --system-site-packages

# 3. Run
easyamp
```

If `easyamp` isn't found on PATH, run it directly: `python3 -m easyamp.app`.

---

## About the visualizer (please read)

The **spectrum bars and VU meters** display *live system audio*. macOS has no
built-in way to capture its own output, so:

- **Playback and the 10-band EQ work fine without anything extra.**
- To make the **visualizer actually move**, you need a loopback audio device.
  Install **[BlackHole (2ch)](https://github.com/ExistentialAudio/BlackHole)**
  (free), then set it (or a Multi-Output device that includes it) as the
  system **output**. Without it, the meters will just sit at zero — that's
  expected, not a bug.

It's fine to test *without* BlackHole — just note in your feedback that the
meters were flat because you didn't set up loopback.

---

## What to test — checklist

**Basic playback**
- [ ] App opens and the window looks reasonable (not garbled / not tiny)
- [ ] Open a music file or folder (drag in, or the **+ FILE** button / playlist)
- [ ] Play / pause / stop / next / previous all work
- [ ] **The playback timer slider advances as the song plays**, and you can
      drag it to seek
- [ ] Volume control works

**Equalizer**
- [ ] Click **EQ** — the 10 sliders show and change the sound
- [ ] EQ presets load (dropdown / preset list)
- [ ] The **ON** button toggles the EQ on/off (audible difference)

**Visualizers** (set up BlackHole first if you want these to move)
- [ ] **Spectrum** bars react to music
- [ ] **VU** button switches to the analog-style VU needles (atomic green)
- [ ] The small scope in the timer area animates
- [ ] **PL** shows the playlist

**Look & feel**
- [ ] The little green LED squares on the EQ / PL / VU / ON buttons render
- [ ] The 7-segment time display font looks correct (not a fallback font)
- [ ] Buttons have the 3D / beveled look, not flat boxes

---

## How to report back

For anything broken or weird, send me:

1. **A screenshot** (Cmd-Shift-4) — especially for layout/font issues.
2. **Which Mac + macOS version** (Apple menu → About This Mac).
3. **Console output**, if it crashes or misbehaves. Launch from Terminal so I
   can see errors:
   - Option A: `/Applications/EasyAmp.app/Contents/MacOS/EasyAmp`
   - Option B: `easyamp`
   Copy whatever it prints.

Even "it just worked, here's a screenshot" is hugely useful. Thanks! 🙏

— Trenton (VonHoltenCodes)
