"""GStreamer playback engine with a built-in parametric EQ + tone shaping.

A ``playbin`` decodes local files and network URIs. Its ``audio-filter`` is a
bin holding the processing chain:

    in-gain → pitch (cassette varispeed) → equalizer-nbands → tone
    (bass/loudness) → balance → out-gain

The EQ is a single ``equalizer-nbands`` of N fully-parametric bands (10–32,
independent centre freq, Q, gain), so it can model Equalizer-APO style filter
sets. Output goes to the system sink, so the system-wide spectrum capture
still sees it. (A per-channel deinterleave/interleave variant was tried but
deadlocked on state changes — froze on track change — so the EQ is stereo.)
"""

from __future__ import annotations

import math
import os

import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst, GLib  # noqa: E402

Gst.init(None)

EQ_FREQS = [29, 59, 119, 237, 474, 947, 1889, 3770, 7523, 15011]
NBANDS = 10
MIN_BANDS, MAX_BANDS = 10, 32
DEFAULT_Q = 1.41

BAND_PEAK, BAND_LOW_SHELF, BAND_HIGH_SHELF = 0, 1, 2

# channels
STEREO, LEFT, RIGHT = "stereo", "left", "right"


def _band_freqs(n: int) -> list[float]:
    if n == 10:
        return [float(f) for f in EQ_FREQS]
    lo, hi = 30.0, 16000.0
    return [lo * (hi / lo) ** (i / (n - 1)) for i in range(n)]


def _interp(x: float, xs: list[float], ys: list[float]) -> float:
    if not xs:
        return 0.0
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        return ys[-1]
    for i in range(1, len(xs)):
        if x <= xs[i]:
            d = xs[i] - xs[i - 1]
            t = (x - xs[i - 1]) / d if d else 0.0
            return ys[i - 1] + (ys[i] - ys[i - 1]) * t
    return ys[-1]


class Player:
    def __init__(self, on_tags=None, on_eos=None, on_state=None, on_error=None):
        self.on_tags = on_tags
        self.on_eos = on_eos
        self.on_state = on_state
        self.on_error = on_error
        self._uri: str | None = None
        self._bitrate = 0
        self._nbands = NBANDS
        self._bands: list[list] = []
        self._gen = 0               # bumped per load(); guards stale errors
        self._error_reported = False

        self.playbin = Gst.ElementFactory.make("playbin", "easyamp-player")
        self.playbin.set_property("audio-filter", self._build_eqbin())

        bus = self.playbin.get_bus()
        bus.add_signal_watch()
        bus.connect("message::tag", self._on_tag)
        bus.connect("message::eos", self._on_eos)
        bus.connect("message::error", self._on_error)

    # ---- pipeline construction ---------------------------------------
    def _build_eqbin(self) -> Gst.Bin:
        """Single-EQ chain: in-gain -> pitch -> equalizer-nbands -> tone ->
        balance -> out-gain. (A per-channel deinterleave/interleave variant
        was tried but its pipeline deadlocked on state changes — froze on
        track change — so the EQ is a single stereo equalizer-nbands.)"""
        conv = Gst.ElementFactory.make("audioconvert", "eqconv")
        self.in_gain = Gst.ElementFactory.make("volume", "ingain")
        # cassette varispeed: prefer SoundTouch 'pitch' (Windows), else the
        # dependency-free 'speed' resampler (in the GNOME runtime + brew bad).
        self.pitch = Gst.ElementFactory.make("pitch", "pitch")
        self._pitch_prop = "rate"
        if self.pitch is None:
            self.pitch = Gst.ElementFactory.make("speed", "speed")
            self._pitch_prop = "speed"
        self.eq = Gst.ElementFactory.make("equalizer-nbands", "eq")
        self.tone = Gst.ElementFactory.make("equalizer-3bands", "tone")
        self.balance = Gst.ElementFactory.make("audiopanorama", "balance")
        self.out_gain = Gst.ElementFactory.make("volume", "outgain")
        conv2 = Gst.ElementFactory.make("audioconvert", "eqconv2")

        self.eq.set_property("num-bands", self._nbands)
        self._configure_bands(_band_freqs(self._nbands))

        eqbin = Gst.Bin.new("eqbin")
        chain = [conv, self.in_gain]
        if self.pitch is not None:
            chain.append(self.pitch)
        chain.append(self.eq)
        if self.tone is not None:
            chain.append(self.tone)
        if self.balance is not None:
            chain.append(self.balance)
        chain += [self.out_gain, conv2]

        for el in chain:
            eqbin.add(el)
        for a, b in zip(chain, chain[1:]):
            a.link(b)

        eqbin.add_pad(Gst.GhostPad.new("sink", chain[0].get_static_pad("sink")))
        eqbin.add_pad(Gst.GhostPad.new("src", chain[-1].get_static_pad("src")))
        return eqbin

    def _band_obj(self, i):
        try:
            return self.eq.get_child_by_index(i)
        except Exception:
            return None

    def _configure_bands(self, freqs, q: float = DEFAULT_Q) -> None:
        self._bands = []
        n = len(freqs)
        for i, f in enumerate(freqs):
            btype = (BAND_LOW_SHELF if i == 0 else
                     BAND_HIGH_SHELF if i == n - 1 else BAND_PEAK)
            bw = f / q
            b = self._band_obj(i)
            if b is not None:
                b.set_property("freq", float(f))
                b.set_property("bandwidth", float(bw))
                b.set_property("type", btype)
                b.set_property("gain", 0.0)
            self._bands.append([float(f), float(q), 0.0, btype])

    # ---- transport ----------------------------------------------------
    def load(self, path_or_uri: str) -> None:
        uri = path_or_uri if "://" in path_or_uri else \
            Gst.filename_to_uri(os.path.abspath(path_or_uri))
        self.stop()
        self._gen += 1
        self._error_reported = False
        self._uri = uri
        self.playbin.set_property("uri", uri)

    def play(self) -> None:
        self.playbin.set_state(Gst.State.PLAYING)

    def pause(self) -> None:
        self.playbin.set_state(Gst.State.PAUSED)

    def stop(self) -> None:
        self.playbin.set_state(Gst.State.NULL)

    def is_playing(self) -> bool:
        return self.playbin.get_state(0)[1] == Gst.State.PLAYING

    def toggle(self) -> None:
        self.pause() if self.is_playing() else self.play()

    # ---- position / seek ---------------------------------------------
    def position(self) -> int:
        ok, pos = self.playbin.query_position(Gst.Format.TIME)
        return pos if ok else 0

    def duration(self) -> int:
        ok, dur = self.playbin.query_duration(Gst.Format.TIME)
        return dur if ok else 0

    def seek_fraction(self, frac: float) -> None:
        dur = self.duration()
        if dur > 0:
            self.playbin.seek_simple(
                Gst.Format.TIME,
                Gst.SeekFlags.FLUSH | Gst.SeekFlags.KEY_UNIT,
                int(frac * dur),
            )

    # ---- equalizer: gain (graphic-compatible) ------------------------
    # `channel` is accepted for call-site compatibility but ignored — the EQ
    # is a single stereo bank (per-channel split was removed; it deadlocked).
    def set_band(self, index: int, gain_db: float, channel=STEREO) -> None:
        b = self._band_obj(index)
        if b is not None:
            b.set_property("gain", float(gain_db))
        if 0 <= index < len(self._bands):
            self._bands[index][2] = float(gain_db)

    def reset_eq(self) -> None:
        for i in range(self._nbands):
            self.set_band(i, 0.0)

    def reset_bands(self) -> None:
        self._configure_bands(_band_freqs(self._nbands))

    # ---- equalizer: parametric ---------------------------------------
    def band_count(self) -> int:
        return self._nbands

    def get_bands(self, channel=STEREO) -> list[list]:
        return [list(b) for b in self._bands]

    def set_band_param(self, index, freq=None, q=None, gain=None,
                       btype=None, channel=STEREO) -> None:
        if not (0 <= index < len(self._bands)):
            return
        b = self._bands[index]
        if freq is not None:
            b[0] = float(freq)
        if q is not None:
            b[1] = float(q)
        if gain is not None:
            b[2] = float(gain)
        if btype is not None:
            b[3] = int(btype)
        obj = self._band_obj(index)
        if obj is not None:
            obj.set_property("freq", b[0])
            obj.set_property("bandwidth", b[0] / max(b[1], 0.05))
            obj.set_property("gain", b[2])
            obj.set_property("type", b[3])

    def set_band_count(self, n: int) -> None:
        """Change band count (10–32), preserving the curve via log-frequency
        interpolation."""
        n = max(MIN_BANDS, min(MAX_BANDS, int(n)))
        if n == self._nbands:
            return
        old_f = [math.log10(b[0]) for b in self._bands]
        old_g = [b[2] for b in self._bands]
        self._nbands = n
        new_freqs = _band_freqs(n)
        try:
            self.eq.set_property("num-bands", n)
        except Exception:
            pass
        self._configure_bands(new_freqs)
        for i, f in enumerate(new_freqs):
            self.set_band(i, round(_interp(math.log10(f), old_f, old_g), 1))

    def load_bands(self, bands, preamp=None, channel=STEREO) -> None:
        n = max(MIN_BANDS, min(MAX_BANDS, len(bands)))
        self._nbands = n
        try:
            self.eq.set_property("num-bands", n)
        except Exception:
            pass
        self._configure_bands(_band_freqs(n))
        for i in range(n):
            if i < len(bands):
                f, q, g, t = bands[i]
                self.set_band_param(i, freq=f, q=q, gain=g, btype=t)
        if preamp is not None:
            self.set_preamp(float(preamp))

    # ---- gains / pitch / balance -------------------------------------
    @staticmethod
    def _db_to_lin(db: float) -> float:
        return float(10.0 ** (db / 20.0))

    def set_preamp(self, gain_db: float) -> None:
        self.playbin.set_property("volume", self._db_to_lin(gain_db))

    def set_in_gain(self, gain_db: float) -> None:
        if self.in_gain is not None:
            self.in_gain.set_property("volume", self._db_to_lin(gain_db))

    def set_out_gain(self, gain_db: float) -> None:
        if self.out_gain is not None:
            self.out_gain.set_property("volume", self._db_to_lin(gain_db))

    def set_pitch(self, rate: float) -> None:
        if self.pitch is not None:
            self.pitch.set_property(self._pitch_prop, float(rate))

    def has_pitch(self) -> bool:
        return self.pitch is not None

    def set_balance(self, pos: float) -> None:
        if self.balance is not None:
            self.balance.set_property("panorama", max(-1.0, min(1.0, float(pos))))

    def set_tone(self, bass: bool, loudness: bool) -> None:
        if self.tone is None:
            return
        low = (6.0 if bass else 0.0) + (4.0 if loudness else 0.0)
        high = 4.0 if loudness else 0.0
        self.tone.set_property("band0", float(low))
        self.tone.set_property("band1", 0.0)
        self.tone.set_property("band2", float(high))

    # ---- pipeline / stream info --------------------------------------
    def stream_info(self) -> dict:
        info = {"rate": 0, "channels": 0, "bitrate": self._bitrate}
        try:
            pad = self.playbin.emit("get-audio-pad", 0)
            caps = pad.get_current_caps() if pad else None
            if caps and caps.get_size():
                st = caps.get_structure(0)
                ok, rate = st.get_int("rate")
                if ok:
                    info["rate"] = rate
                ok, ch = st.get_int("channels")
                if ok:
                    info["channels"] = ch
        except Exception:
            pass
        return info

    # ---- bus callbacks ------------------------------------------------
    def _on_tag(self, _bus, msg) -> None:
        if not self.on_tags:
            return
        tags = msg.parse_tag()
        info = {}
        ok, title = tags.get_string(Gst.TAG_TITLE)
        if ok:
            info["title"] = title
        ok, artist = tags.get_string(Gst.TAG_ARTIST)
        if ok:
            info["artist"] = artist
        for tag in (Gst.TAG_BITRATE, Gst.TAG_NOMINAL_BITRATE):
            ok, br = tags.get_uint(tag)
            if ok and br:
                self._bitrate = br
                break
        if info:
            GLib.idle_add(self.on_tags, info)

    def _on_eos(self, _bus, _msg) -> None:
        if self.on_eos:
            GLib.idle_add(self.on_eos)

    def _on_error(self, _bus, msg) -> None:
        err, _dbg = msg.parse_error()
        # Defer handling onto the main loop: calling set_state(NULL) directly
        # inside a bus callback deadlocks (it joins the streaming threads from
        # within their own message dispatch). Reporting the error here (not
        # as EOS) also avoids the auto-advance cascade down the playlist.
        GLib.idle_add(self._handle_error, err.message, self._gen)

    def _handle_error(self, message: str, gen: int) -> bool:
        if gen != self._gen or self._error_reported:
            return False        # stale error from a previous track, or already reported
        self._error_reported = True
        self.stop()
        if self.on_error:
            self.on_error(message)
        return False
