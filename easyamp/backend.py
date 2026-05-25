"""EasyEffects control backend.

EasyAmp does not fork or embed EasyEffects. It drives a normally-installed
EasyEffects instance from the outside using the interfaces EasyEffects already
exposes:

  * its command-line options (load preset, global bypass, query state), which
    are forwarded to the running instance over D-Bus by GApplication, and
  * its GSettings schemas, for reading richer state.

This keeps EasyAmp a thin, dependency-light "skin shell": EasyEffects does all
the DSP; EasyAmp is just a retro face and remote control.
"""

from __future__ import annotations

import shutil
import subprocess
from dataclasses import dataclass, field

APP_ID = "com.github.wwmm.easyeffects"


def _ee_cmd() -> list[str]:
    """Return the argv prefix used to invoke the EasyEffects binary.

    Prefers a native install; falls back to the Flatpak (the common case on
    this machine).
    """
    if shutil.which("easyeffects"):
        return ["easyeffects"]
    return ["flatpak", "run", f"--command=easyeffects", APP_ID]


def _run(args: list[str], timeout: float = 10.0) -> str:
    """Run an EasyEffects CLI invocation and return stripped stdout ('' on error)."""
    try:
        out = subprocess.run(
            _ee_cmd() + args,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        return out.stdout.strip()
    except (subprocess.SubprocessError, OSError):
        return ""


def _gsettings(args: list[str], timeout: float = 10.0) -> str:
    """Run gsettings inside the EasyEffects sandbox (schemas live there)."""
    base = ["flatpak", "run", "--command=sh", APP_ID, "-c"]
    cmd = "gsettings " + " ".join(args)
    try:
        out = subprocess.run(base + [cmd], capture_output=True, text=True, timeout=timeout)
        return out.stdout.strip()
    except (subprocess.SubprocessError, OSError):
        return ""


@dataclass
class Presets:
    output: list[str] = field(default_factory=list)
    input: list[str] = field(default_factory=list)


class EasyEffects:
    """Thin wrapper around the EasyEffects external control surface."""

    # ---- presets -------------------------------------------------------
    def list_presets(self) -> Presets:
        """Parse `easyeffects -p` output into output/input preset lists."""
        text = _run(["-p"])
        presets = Presets()
        for line in text.splitlines():
            if ":" not in line:
                continue
            label, _, rest = line.partition(":")
            names = [p.strip() for p in rest.split(",") if p.strip()]
            if "output" in label.lower():
                presets.output = names
            elif "input" in label.lower():
                presets.input = names
        return presets

    def load_preset(self, name: str) -> None:
        _run(["-l", name])

    def active_preset(self, category: str = "output") -> str:
        """category is 'input' or 'output'."""
        return _run(["-s", category])

    # ---- global bypass -------------------------------------------------
    def get_bypass(self) -> bool:
        """True if global bypass is currently enabled."""
        return _run(["-b", "3"]).strip().startswith("1")

    def set_bypass(self, enabled: bool) -> None:
        _run(["-b", "1" if enabled else "2"])

    def toggle_bypass(self) -> bool:
        new = not self.get_bypass()
        self.set_bypass(new)
        return new

    # ---- lifecycle -----------------------------------------------------
    def is_running(self) -> bool:
        names = subprocess.run(
            ["gdbus", "call", "--session", "--dest", "org.freedesktop.DBus",
             "--object-path", "/org/freedesktop/DBus",
             "--method", "org.freedesktop.DBus.ListNames"],
            capture_output=True, text=True,
        ).stdout
        return APP_ID in names

    def ensure_running(self) -> None:
        """Start EasyEffects as a headless service if it isn't up yet."""
        if not self.is_running():
            subprocess.Popen(
                _ee_cmd() + ["--gapplication-service"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )

    def show_window(self) -> None:
        """Raise/open the real EasyEffects window (the full equalizer)."""
        subprocess.Popen(
            _ee_cmd(),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )

    # ---- spectrum settings (used later by the visualizer) --------------
    def spectrum_color(self) -> tuple[float, float, float, float]:
        raw = _gsettings(["get", f"{APP_ID}.spectrum", "color"])
        try:
            nums = [float(x) for x in raw.strip("()").split(",")]
            return tuple(nums)  # type: ignore[return-value]
        except ValueError:
            return (0.094, 0.878, 0.094, 1.0)
