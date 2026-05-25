"""Live audio spectrum capture.

Captures the monitor of the current default sink with `parec` (PipeWire's
PulseAudio-compatible recorder), runs an FFT in numpy, and reports
log-spaced band levels via a callback marshalled onto the GTK main loop.

This is fully decoupled from EasyEffects: we visualise the system output
(post-effects), so the bars reflect exactly what you hear.
"""

from __future__ import annotations

import subprocess
import threading

import numpy as np
from gi.repository import GLib

RATE = 48000
FFT = 2048
EPS = 1e-9


def _default_monitor() -> str:
    """Monitor source name for the current default sink (best-effort)."""
    try:
        sink = subprocess.run(
            ["pactl", "get-default-sink"], capture_output=True, text=True, timeout=4
        ).stdout.strip()
        if sink:
            return f"{sink}.monitor"
    except (subprocess.SubprocessError, OSError):
        pass
    return "@DEFAULT_MONITOR@"


class SpectrumCapture:
    def __init__(self, bands: int = 14, on_data=None):
        self.bands = bands
        self.on_data = on_data
        self.levels = np.zeros(bands, dtype=np.float32)
        self._proc: subprocess.Popen | None = None
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._window = np.hanning(FFT).astype(np.float32)

        freqs = np.fft.rfftfreq(FFT, 1.0 / RATE)
        edges = np.logspace(np.log10(40.0), np.log10(RATE / 2), bands + 1)
        self._bins = [
            np.where((freqs >= edges[i]) & (freqs < edges[i + 1]))[0]
            for i in range(bands)
        ]

    def start(self) -> None:
        if self._proc is not None:
            return
        self._stop.clear()
        try:
            self._proc = subprocess.Popen(
                ["parec", "--format=float32le", f"--rate={RATE}",
                 "--channels=1", "--raw", f"--device={_default_monitor()}",
                 "--latency-msec=40"],
                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0,
            )
        except OSError:
            self._proc = None
            return
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self) -> None:
        need = FFT * 4  # float32 mono
        buf = bytearray()
        smoothed = np.zeros(self.bands, dtype=np.float32)
        while not self._stop.is_set() and self._proc and self._proc.stdout:
            chunk = self._proc.stdout.read(need - len(buf))
            if not chunk:
                break
            buf += chunk
            if len(buf) < need:
                continue
            samples = np.frombuffer(bytes(buf), dtype=np.float32)
            buf.clear()

            mag = np.abs(np.fft.rfft(samples * self._window)) / (FFT / 2)
            out = np.empty(self.bands, dtype=np.float32)
            for i, b in enumerate(self._bins):
                band = mag[b].mean() if len(b) else 0.0
                db = 20.0 * np.log10(band + EPS)
                out[i] = float(np.clip((db + 60.0) / 60.0, 0.0, 1.0))

            # fast attack, slow decay for VU-like motion
            smoothed = np.maximum(out, smoothed * 0.80)
            self.levels = smoothed
            if self.on_data:
                GLib.idle_add(self.on_data, smoothed.copy())

    def stop(self) -> None:
        self._stop.set()
        if self._proc is not None:
            try:
                self._proc.terminate()
            except OSError:
                pass
            self._proc = None
