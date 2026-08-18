"""Platform-aware per-user config directory.

Historic app data (EQ presets) lives under ``~/.config/easyamp`` via
``eqpresets.USER_DIR`` on every platform; new subsystems should use
:func:`config_dir` instead so Windows/macOS data lands in the native
location (APPDATA / Application Support).
"""

from __future__ import annotations

import os
import sys


def config_dir() -> str:
    """Return (and create) the per-user EasyAmp config directory."""
    if sys.platform == "win32":
        base = os.environ.get("APPDATA") or os.path.expanduser("~")
    elif sys.platform == "darwin":
        base = os.path.expanduser("~/Library/Application Support")
    else:
        base = os.environ.get("XDG_CONFIG_HOME",
                              os.path.expanduser("~/.config"))
    path = os.path.join(base, "easyamp")
    os.makedirs(path, exist_ok=True)
    return path


def ensure_private_gst_registry() -> str | None:
    """Frozen macOS bundles only: point GStreamer's plugin-registry cache
    at a per-user cache dir BEFORE anything imports Gst.

    Without this, GStreamer writes ``registry.bin`` next to its bundled
    plugins — inside the sealed, notarized .app — which invalidates the
    code signature after first launch (issue #4, diagnosed by
    @ContractorKeith: ``codesign --verify`` fails with "a sealed resource
    is missing or invalid" until the stray file is deleted).

    Returns the registry path when set; None when not applicable (other
    platforms, dev runs, or the user already set ``GST_REGISTRY``).
    """
    if sys.platform != "darwin" or not getattr(sys, "frozen", False):
        return None
    if os.environ.get("GST_REGISTRY"):
        return None          # explicit user/system override wins
    cache = os.path.expanduser("~/Library/Caches/easyamp")
    os.makedirs(cache, exist_ok=True)
    path = os.path.join(cache, "registry.bin")
    os.environ["GST_REGISTRY"] = path
    return path
