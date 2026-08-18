"""m3u round-trip: local paths, plain URLs, and source-bound tracks."""

import os
import tempfile
import unittest

from easyamp import m3u
from easyamp.sources.base import Track


class M3uTests(unittest.TestCase):
    def _roundtrip(self, tracks):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "list.m3u")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(m3u.serialize(tracks))
            return m3u.parse(path)

    def test_local_and_relative(self):
        tracks = [Track.local("/music/a.mp3")]
        out = self._roundtrip(tracks)
        self.assertEqual(out[0].uri, "/music/a.mp3")
        self.assertEqual(out[0].source_id, "")

    def test_url_not_mangled(self):
        """URLs must come back verbatim (the old loader dirname-joined them)."""
        url = "http://192.168.68.69:32400/library/parts/1/file.mp3"
        out = self._roundtrip([Track(uri=url)])
        self.assertEqual(out[0].uri, url)

    def test_source_bound_rebind(self):
        t = Track(uri="http://srv:32400/library/parts/9/f.flac",
                  title="Song", artist="Band", duration=213,
                  source_id="plex:1a2b", item_id="49123")
        out = self._roundtrip([t])
        self.assertEqual(out[0].source_id, "plex:1a2b")
        self.assertEqual(out[0].item_id, "49123")
        self.assertEqual(out[0].title, "Song")
        self.assertEqual(out[0].artist, "Band")
        self.assertEqual(out[0].duration, 213)

    def test_no_tokens_in_file(self):
        t = Track(uri="http://srv:32400/library/parts/9/f.flac",
                  source_id="plex:1a2b", item_id="9")
        self.assertNotIn("X-Plex-Token", m3u.serialize([t]))

    def test_foreign_extinf_still_loads(self):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "foreign.m3u")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write("#EXTM3U\n#EXTINF:123,Artist - Title\nsub/track.mp3\n")
            out = m3u.parse(path)
            self.assertEqual(out[0].uri, os.path.join(td, "sub/track.mp3"))
            self.assertEqual(out[0].artist, "Artist")
            self.assertEqual(out[0].title, "Title")


if __name__ == "__main__":
    unittest.main()
