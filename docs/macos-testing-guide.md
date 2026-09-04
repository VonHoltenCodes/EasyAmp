# EasyAmp — macOS Testing Guide

Thanks for helping test **EasyAmp**, a classic-player-style GTK4 music player with a
built-in 10-band EQ and spectrum / VU visualizers. I don't own a Mac, so I need
a second pair of eyes (and ears) on the macOS build.

There are **two ways** to test. Option A (download the prebuilt app) is easiest.
Option B (build from source) is the fallback if the prebuilt one won't run.

> **Mac type matters.** The prebuilt app is **Apple Silicon** (M1/M2/M3/M4,
> `arm64`). If you have an **Intel** Mac, skip to **Option B** (build from
> source) — GitHub no longer offers Intel build machines for the prebuilt one.
> To check: Apple menu → About This Mac → "Chip"/"Processor".
>
> **macOS 12 (Monterey) or newer** is required.

---

## Option A — Download the prebuilt app (Apple Silicon)

### 1. Download the build

Download the `.dmg` directly (no GitHub account needed):

**https://github.com/VonHoltenCodes/EasyAmp/releases/latest/download/EasyAmp-macos-arm64.dmg**

(~125 MB. Or browse all downloads at
https://github.com/VonHoltenCodes/EasyAmp/releases/latest)

### 2. Install

1. Double-click the `.dmg` to mount it.
2. Drag **EasyAmp.app** into your **Applications** folder.

### 3. First launch

The app is **signed and notarized** (Developer ID: Trenton Von Holten), so it
opens like any other Mac app — no Gatekeeper warnings, no right-click dance.
If macOS *does* complain about the publisher or say the app is damaged, that's
a bug in the release pipeline: please stop and report it (screenshot of the
dialog + your macOS version).

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

## About the visualizer

Since v0.5.2 the **spectrum bars and VU meters** feed from EasyAmp's own
playback audio — **no loopback device (BlackHole) is needed**. The meters
should move whenever a track is playing. If they sit at zero during playback,
that's a bug — please report it.

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

**Visualizers**
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

## In-fleet local signing on Macbase1 (VonHolten fleet)

The VonHolten fleet signs + notarizes EasyAmp macOS builds under the owner's own
Developer ID on **Macbase1** (M1 Mac mini, `ssh macbase1`) — no CI and no
borrowed Mac required. Proven 2026-09-04 (.app + .dmg notarized + stapled,
`spctl` = "Notarized Developer ID").

Full runbook: **`devbase1:~/CLAUDE.md`** → "Local macOS build → sign → notarize on
Macbase1", and `/Users/macbase1/CLAUDE.md` on the box. In brief: run this repo's
`.github/workflows/macos.yml` build steps locally (brew `gtk4 pygobject3 gstreamer
adwaita-icon-theme librsvg create-dmg`, venv off brew `python3`, `pyinstaller …
EasyAmp.spec`), then Developer-ID sign inside-out and notarize with the shared
`fleet-signing.keychain-db` identity.

⚠ **EasyAmp-specific trap:** GStreamer writes `Contents/Frameworks/registry.bin`
into the bundle on first launch, which breaks the code seal and makes Apple reject
notarization ("the signature of the binary is invalid"). **Never launch the app
between signing and notarizing** — sign last, notarize a freshly-signed bundle. A
durable fix worth landing in the app: point `GST_REGISTRY_1_0` at a per-user cache
path so the registry never lands inside the `.app` (otherwise an end user's first
launch also breaks the local signature). Also: `notarytool` needs
`--keychain …/fleet-signing.keychain-db` over SSH (login keychain is locked), and
`create-dmg` hangs headless — use `hdiutil create -format UDZO` then codesign.
