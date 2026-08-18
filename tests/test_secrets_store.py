"""secrets_store JSON-fallback behaviour (platform keychains aren't
exercised in CI; the fallback path is the part with our own logic)."""

import json
import os
import sys
import tempfile
import unittest
from unittest import mock


class FallbackTests(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        patcher = mock.patch("easyamp.appdirs.config_dir",
                             return_value=self._td.name)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.addCleanup(self._td.cleanup)
        # reload so the module under test sees the patched config_dir
        sys.modules.pop("easyamp.secrets_store", None)
        import easyamp.secrets_store as ss
        self.ss = ss
        # force the platform backend to fail so the fallback engages
        mock.patch.object(ss, "_backend", return_value=(
            self._boom, self._boom, self._boom)).start()

    @staticmethod
    def _boom(*_a):
        raise OSError("no backend in tests")

    def test_set_get_delete(self):
        self.ss.set("plex:1a2b:token", "sekrit")
        self.assertEqual(self.ss.get("plex:1a2b:token"), "sekrit")
        self.ss.delete("plex:1a2b:token")
        self.assertIsNone(self.ss.get("plex:1a2b:token"))

    def test_file_is_0600_and_marked_plain(self):
        self.ss.set("k", "v")
        path = os.path.join(self._td.name, "secrets.json")
        self.assertEqual(os.stat(path).st_mode & 0o777, 0o600)
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
        self.assertEqual(data["k"], {"enc": "none", "data": "v"})

    def test_get_missing(self):
        self.assertIsNone(self.ss.get("nope"))


if __name__ == "__main__":
    unittest.main()
