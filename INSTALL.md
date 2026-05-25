# Installing EasyAmp

> **Just want to run it?** Prebuilt, self-contained downloads for Windows,
> macOS, and Linux are on the
> **[latest release](https://github.com/VonHoltenCodes/EasyAmp/releases/latest)**
> (see the Download table in the [README](README.md)) — no Python or GTK setup
> required. The steps below are for **building/running from source**.

EasyAmp is a GTK4 + GStreamer Python app. GTK4, PyGObject, and GStreamer are
**native system libraries** — they come from your OS package manager (or
Homebrew on macOS, MSYS2 on Windows), not from pip. Once those are present,
EasyAmp installs as a normal Python package and gives you an `easyamp` launcher.

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

## Windows

> Most users should just grab the **[installer or portable zip](https://github.com/VonHoltenCodes/EasyAmp/releases/latest)**.
> The steps here are for running from source.
>
> Unlike macOS, the visualizer works out of the box: it captures the system
> output via **WASAPI loopback**, so no extra loopback device is needed.

GTK4 for Windows comes from **[MSYS2](https://www.msys2.org/)**. Run the
following in an **MSYS2 MINGW64** shell (not the plain MSYS or UCRT shell):

### 1. System prerequisites (MSYS2 MINGW64)

```bash
pacman -S mingw-w64-x86_64-gtk4 mingw-w64-x86_64-python-gobject \
    mingw-w64-x86_64-python-cairo mingw-w64-x86_64-gstreamer \
    mingw-w64-x86_64-gst-plugins-base mingw-w64-x86_64-gst-plugins-good \
    mingw-w64-x86_64-gst-plugins-bad mingw-w64-x86_64-adwaita-icon-theme \
    mingw-w64-x86_64-python-numpy mingw-w64-x86_64-python-pip
```

> `gst-plugins-bad` provides the `wasapi2` element used for system-audio
> capture; `adwaita-icon-theme` supplies GTK's stock icons.

### 2. Install EasyAmp

```bash
# from a clone of this repo, in the MINGW64 shell
pip install --no-deps .       # numpy/GTK already come from MSYS2
python -m easyamp
```

### Building the installer

The signed-off recipe lives in `packaging/windows/` (PyInstaller spec +
Inno Setup script) and runs in CI via `.github/workflows/windows.yml`, which
produces the `.exe` installer and portable `.zip` attached to each release.

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

## Packaging status

- **Windows:** ✅ Inno Setup installer + portable zip (PyInstaller, MSYS2).
- **macOS:** ✅ self-contained `.app`/`.dmg` (PyInstaller). The visualizer still
  needs a loopback device (e.g. BlackHole) since macOS has no output monitor.
- **Linux:** ✅ Flatpak bundle on the releases page; Flathub submission in review.
