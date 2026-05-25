# Installing EasyAmp

EasyAmp is a GTK4 + GStreamer Python app. GTK4, PyGObject, and GStreamer are
**native system libraries** — they come from your OS package manager (or
Homebrew on macOS), not from pip. Once those are present, EasyAmp installs as
a normal Python package and gives you an `easyamp` launcher.

---

## Linux

### 1. System prerequisites

Debian / Ubuntu / Pop!_OS:

```bash
sudo apt install python3-gi python3-gi-cairo gir1.2-gtk-4.0 \
    gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-pulseaudio \
    pipewire-pulse python3-numpy
```

Fedora:

```bash
sudo dnf install python3-gobject gtk4 \
    gstreamer1-plugins-base gstreamer1-plugins-good pipewire-pulseaudio python3-numpy
```

Arch:

```bash
sudo pacman -S python-gobject gtk4 gstreamer gst-plugins-base gst-plugins-good \
    pipewire-pulse python-numpy
```

> The visualizer captures the system output via GStreamer's `pulsesrc`
> (`gstreamer1.0-pulseaudio`), so it reflects everything playing.

### 2. Install EasyAmp

Because the app needs the system PyGObject, install it where it can see system
packages:

```bash
# from a clone of this repo
pipx install . --system-site-packages
# …or:
pip install --user .            # (or: sudo pip install --break-system-packages .)
```

Then run it from your launcher or:

```bash
easyamp
```

Optionally install the desktop entry:

```bash
install -Dm644 data/easyamp.desktop ~/.local/share/applications/easyamp.desktop
```

---

## macOS

> **Caveat:** the **visualizer** needs a system-audio capture source; macOS has
> none built in, so install a loopback device such as
> [BlackHole](https://github.com/ExistentialAudio/BlackHole) to feed the meters.
> Playback + the built-in EQ work without it.

### 1. System prerequisites (Homebrew)

```bash
brew install pygobject3 gtk4 gstreamer gst-plugins-base gst-plugins-good numpy
```

### 2. Install EasyAmp

```bash
pipx install . --system-site-packages   # from a clone of this repo
# …or:
pip3 install .
easyamp
```

---

## Run without installing

From a clone, with the system prerequisites present:

```bash
./run.sh          # or: python3 -m easyamp
```

## Flatpak (Linux, self-contained)

A Flatpak manifest is in `packaging/`. It bundles everything (GTK4 + GStreamer
come from the GNOME runtime; NumPy and the fonts are bundled), so no host Python
setup is needed.

```bash
# one-time: build tooling + runtimes
sudo apt install flatpak-builder            # or your distro's package
flatpak install --user flathub org.gnome.Platform//50 org.gnome.Sdk//50

# build + install
cd packaging
flatpak-builder --user --install --force-clean build-dir com.vonholtencodes.EasyAmp.yaml

# run
flatpak run com.vonholtencodes.EasyAmp
```

Audio (playback + the visualizer's monitor capture) goes through the
`--socket=pulseaudio` permission; files open via the file-chooser portal
(plus read-only `~/Music`).

## Roadmap for packaging

- **Linux:** publish the Flatpak to Flathub.
- **macOS:** a self-contained `.app`/`.dmg` via Briefcase, plus a native
  CoreAudio capture backend for the visualizer.
