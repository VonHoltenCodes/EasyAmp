"""ensure_private_gst_registry: the issue-#4 guard that keeps GStreamer's
registry cache out of the sealed macOS .app bundle."""

import os
import sys
import tempfile
import unittest
from unittest import mock

from easyamp import appdirs


class GstRegistryTests(unittest.TestCase):
    def _darwin_frozen(self, home):
        return (mock.patch.object(sys, "platform", "darwin"),
                mock.patch.object(sys, "frozen", True, create=True),
                mock.patch.dict(os.environ, {"HOME": home}, clear=False))

    def test_sets_registry_in_user_cache_on_frozen_darwin(self):
        with tempfile.TemporaryDirectory() as home:
            p1, p2, p3 = self._darwin_frozen(home)
            with p1, p2, p3:
                os.environ.pop("GST_REGISTRY", None)
                path = appdirs.ensure_private_gst_registry()
                self.assertEqual(
                    path, os.path.join(home, "Library", "Caches",
                                       "easyamp", "registry.bin"))
                self.assertEqual(os.environ.get("GST_REGISTRY"), path)
                self.assertTrue(os.path.isdir(os.path.dirname(path)))
                # crucially: nothing inside an .app-like location
                self.assertNotIn(".app", path)

    def test_existing_override_is_respected(self):
        with tempfile.TemporaryDirectory() as home:
            p1, p2, p3 = self._darwin_frozen(home)
            with p1, p2, p3:
                os.environ["GST_REGISTRY"] = "/custom/registry.bin"
                try:
                    self.assertIsNone(appdirs.ensure_private_gst_registry())
                    self.assertEqual(os.environ["GST_REGISTRY"],
                                     "/custom/registry.bin")
                finally:
                    os.environ.pop("GST_REGISTRY", None)

    def test_noop_off_darwin_and_unfrozen(self):
        os.environ.pop("GST_REGISTRY", None)
        with mock.patch.object(sys, "platform", "linux"):
            self.assertIsNone(appdirs.ensure_private_gst_registry())
        with mock.patch.object(sys, "platform", "darwin"):
            # not frozen (dev run): Homebrew GStreamer already uses the
            # XDG cache; leave it alone
            self.assertIsNone(appdirs.ensure_private_gst_registry())
        self.assertIsNone(os.environ.get("GST_REGISTRY"))


if __name__ == "__main__":
    unittest.main()
