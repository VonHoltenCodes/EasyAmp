"""Live audio capture for the visualizer, via GStreamer.

Captures a sink **monitor** source (the system output) through a
``pulsesrc -> appsink`` pipeline, then per FFT frame computes:

  * a log-spaced FFT spectrum (mono downmix) for the bar display,
  * per-channel RMS levels (L/R) for the VU meters, and
  * a decimated time-domain waveform for the mini scope.

Using GStreamer (rather than shelling out to ``parec``) means this works
both natively and inside the Flatpak sandbox, where the GNOME runtime
provides the pulse/pipewire GStreamer plugins. The monitor device is found
with ``GstDeviceMonitor`` so no host ``pactl`` is required.
"""

from __future__ import annotations

import sys

import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst, GLib  # noqa: E402
import numpy as np  # noqa: E402

from .player import make_element  # noqa: E402

Gst.init(None)

RATE = 48000
FFT = 2048
EPS = 1e-9


def _find_monitor_device():
    """Return a GstDevice for a sink-monitor audio source, or None."""
    dm = Gst.DeviceMonitor.new()
    dm.add_filter("Audio/Source", None)
    dm.start()
    chosen = None
    try:
        for dev in dm.get_devices() or []:
            props = dev.get_properties()
            name = cls = ""
            if props:
                name = props.get_string("node.name") or props.get_string("device.name") or ""
                cls = props.get_string("device.class") or ""
            if cls == "monitor" or name.endswith(".monitor"):
                chosen = dev
                break
    finally:
        dm.stop()
    return chosen


def _make_capture_source():
    """Create a GStreamer element that captures the system audio output.

    * Windows: WASAPI loopback records the default render device directly, so
      the visualizer works with no extra setup.
    * Linux/PipeWire: a sink-monitor source captures the system output.
    * Otherwise (macOS, etc.): fall back to the platform default input, which
      only carries system audio if a loopback device is the default input.
    """
    if sys.platform == "win32":
        for factory in ("wasapi2src", "wasapisrc"):
            el = make_element(factory, None)
            if el is not None:
                el.set_property("loopback", True)
                return el
    dev = _find_monitor_device()
    if dev is not None:
        return dev.create_element(None)
    return (make_element("autoaudiosrc", None)
            or make_element("pulsesrc", None))


class SpectrumCapture:
    def __init__(self, bands: int = 20, on_data=None):
        self.bands = bands
        self.on_data = on_data
        self.levels = np.zeros(bands, dtype=np.float32)
        self.vu = (0.0, 0.0)
        self._pipeline = None
        self._external = None       # appsink we attached to, if any
        self._buf = np.empty(0, dtype=np.float32)   # interleaved stereo
        self._window = np.hanning(FFT).astype(np.float32)
        self._smoothed = np.zeros(bands, dtype=np.float32)
        self._vu_l = 0.0
        self._vu_r = 0.0

        freqs = np.fft.rfftfreq(FFT, 1.0 / RATE)
        edges = np.logspace(np.log10(40.0), np.log10(RATE / 2), bands + 1)
        self._bins = [
            np.where((freqs >= edges[i]) & (freqs < edges[i + 1]))[0]
            for i in range(bands)
        ]

    def attach(self, appsink) -> bool:
        """Drive the visualizer from an existing appsink — the player's own
        output tap (:attr:`Player.viz_sink`). Preferred over :meth:`start`
        because it reflects exactly what EasyAmp is playing and needs no
        system-audio monitor (macOS has none). Returns True if attached."""
        if appsink is None:
            return False
        appsink.connect("new-sample", self._on_sample)
        self._external = appsink
        return True

    def start(self) -> None:
        if self._pipeline is not None:
            return
        src = _make_capture_source()
        if src is None:
            return
        conv = make_element("audioconvert", None)
        capsf = make_element("capsfilter", None)
        capsf.set_property("caps", Gst.Caps.from_string(
            f"audio/x-raw,format=F32LE,channels=2,rate={RATE},layout=interleaved"))
        sink = make_element("appsink", "sink")
        sink.set_property("emit-signals", True)
        sink.set_property("max-buffers", 6)
        sink.set_property("drop", True)
        sink.set_property("sync", False)
        sink.connect("new-sample", self._on_sample)

        pipeline = Gst.Pipeline.new("easyamp-capture")
        for el in (src, conv, capsf, sink):
            pipeline.add(el)
        if not (src.link(conv) and conv.link(capsf) and capsf.link(sink)):
            return
        self._pipeline = pipeline
        pipeline.set_state(Gst.State.PLAYING)

    def _on_sample(self, sink):
        sample = sink.emit("pull-sample")
        if sample is None:
            return Gst.FlowReturn.OK
        buf = sample.get_buffer()
        ok, mapinfo = buf.map(Gst.MapFlags.READ)
        if not ok:
            return Gst.FlowReturn.OK
        try:
            chunk = np.frombuffer(mapinfo.data, dtype=np.float32).copy()
        finally:
            buf.unmap(mapinfo)
        self._buf = np.concatenate((self._buf, chunk))
        need = FFT * 2  # stereo interleaved
        while len(self._buf) >= need:
            frame = self._buf[:need]
            self._buf = self._buf[need:]
            self._process(frame)
        return Gst.FlowReturn.OK

    def _process(self, frame) -> None:
        stereo = frame.reshape(-1, 2)
        left, right = stereo[:, 0], stereo[:, 1]
        mono = (left + right) * 0.5

        mag = np.abs(np.fft.rfft(mono * self._window)) / (FFT / 2)
        out = np.empty(self.bands, dtype=np.float32)
        for i, b in enumerate(self._bins):
            band = mag[b].mean() if len(b) else 0.0
            db = 20.0 * np.log10(band + EPS)
            out[i] = float(np.clip((db + 60.0) / 60.0, 0.0, 1.0))
        self._smoothed = np.maximum(out, self._smoothed * 0.80)
        self.levels = self._smoothed

        def lvl(ch):
            rms = float(np.sqrt(np.mean(ch * ch)))
            db = 20.0 * np.log10(rms + EPS)
            return float(np.clip((db + 50.0) / 50.0, 0.0, 1.0))

        self._vu_l = self._vu_l * 0.7 + lvl(left) * 0.3
        self._vu_r = self._vu_r * 0.7 + lvl(right) * 0.3
        self.vu = (self._vu_l, self._vu_r)

        step = max(1, FFT // 64)
        wave = mono[::step][:64].astype(np.float32).copy()

        if self.on_data:
            GLib.idle_add(self.on_data, self._smoothed.copy(), (self._vu_l, self._vu_r), wave)

    def stop(self) -> None:
        if self._pipeline is not None:
            self._pipeline.set_state(Gst.State.NULL)
            self._pipeline = None
        self._buf = np.empty(0, dtype=np.float32)
