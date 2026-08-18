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
