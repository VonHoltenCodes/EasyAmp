"""GStreamer playback engine with a built-in 10-band EQ.

A `playbin` handles decoding of local files and network URIs (so anything
GStreamer can read works, not just WAV). A 10-band graphic equalizer is
inserted as playbin's `audio-filter`, giving a WinAmp-style EQ that affects
EasyAmp's own playback. Output goes to the system sink, so it's also picked
up by the system-wide spectrum capture like any other audio source.
"""

from __future__ import annotations

import os

import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst, GLib  # noqa: E402

Gst.init(None)

# equalizer-10bands centre frequencies (Hz), for UI labels
EQ_FREQS = [29, 59, 119, 237, 474, 947, 1889, 3770, 7523, 15011]
NBANDS = 10


class Player:
    def __init__(self, on_tags=None, on_eos=None, on_state=None):
        self.on_tags = on_tags
        self.on_eos = on_eos
        self.on_state = on_state
        self._uri: str | None = None
        self._bitrate = 0

        self.playbin = Gst.ElementFactory.make("playbin", "easyamp-player")

        # build the EQ filter bin: audioconvert -> equalizer-10bands
        self.eq = Gst.ElementFactory.make("equalizer-10bands", "eq")
        conv = Gst.ElementFactory.make("audioconvert", "eqconv")
        # shelving stage for the Bass / Loudness tone toggles (low + high)
        self.tone = Gst.ElementFactory.make("equalizer-3bands", "tone")
        eqbin = Gst.Bin.new("eqbin")
        eqbin.add(conv)
        eqbin.add(self.eq)
        conv.link(self.eq)
        last = self.eq
        if self.tone is not None:
            eqbin.add(self.tone)
            self.eq.link(self.tone)
            last = self.tone
        eqbin.add_pad(Gst.GhostPad.new("sink", conv.get_static_pad("sink")))
        eqbin.add_pad(Gst.GhostPad.new("src", last.get_static_pad("src")))
        self.playbin.set_property("audio-filter", eqbin)

        bus = self.playbin.get_bus()
        bus.add_signal_watch()
        bus.connect("message::tag", self._on_tag)
        bus.connect("message::eos", self._on_eos)
        bus.connect("message::error", self._on_error)

    # ---- transport ----------------------------------------------------
    def load(self, path_or_uri: str) -> None:
        if "://" in path_or_uri:
            uri = path_or_uri
        else:
            uri = Gst.filename_to_uri(os.path.abspath(path_or_uri))
        self.stop()
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

    # ---- equalizer ----------------------------------------------------
    def set_band(self, index: int, gain_db: float) -> None:
        if 0 <= index < NBANDS:
            self.eq.set_property(f"band{index}", float(gain_db))

    def reset_eq(self) -> None:
        for i in range(NBANDS):
            self.set_band(i, 0.0)

    def set_preamp(self, gain_db: float) -> None:
        """Preamp doubles as playback volume (dB -> linear)."""
        self.playbin.set_property("volume", float(10.0 ** (gain_db / 20.0)))

    def set_tone(self, bass: bool, loudness: bool) -> None:
        """Bass enhances the low shelf; Loudness boosts low + high (a
        loudness contour). Both are additive on the low band."""
        if self.tone is None:
            return
        low = (6.0 if bass else 0.0) + (4.0 if loudness else 0.0)
        high = 4.0 if loudness else 0.0
        self.tone.set_property("band0", float(low))   # ~100 Hz
        self.tone.set_property("band1", 0.0)          # ~1.1 kHz
        self.tone.set_property("band2", float(high))  # ~10 kHz

    # ---- pipeline / stream info --------------------------------------
    def stream_info(self) -> dict:
        """Current audio format: rate (Hz), channels, bitrate (bps)."""
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
        # surface as an EOS-like signal so the UI can advance/clear
        if self.on_eos:
            GLib.idle_add(self.on_eos)
