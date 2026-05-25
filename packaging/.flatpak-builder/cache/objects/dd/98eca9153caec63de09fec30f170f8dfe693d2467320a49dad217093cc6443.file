"""Install EasyAmp's bundled fonts into the user font directory on first run.

The CSS has generic fallbacks, so the app works without these — but copying
the bundled DSEG7 and Pixelify Sans (both SIL OFL) into the platform user
font dir lets the intended look show up with no manual setup, on Linux or
macOS.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
FONT_SRC = os.path.join(_HERE, "fonts")
FONT_FILES = ("DSEG7Classic-Regular.ttf", "DSEG7Classic-Bold.ttf", "PixelifySans.ttf")


def _user_font_dir() -> str:
    if sys.platform == "darwin":
        return os.path.expanduser("~/Library/Fonts")
    base = os.environ.get("XDG_DATA_HOME", os.path.expanduser("~/.local/share"))
    return os.path.join(base, "fonts", "EasyAmp")


def ensure_fonts() -> None:
    """Best-effort: copy bundled fonts to the user font dir; never raises."""
    try:
        dst = _user_font_dir()
        os.makedirs(dst, exist_ok=True)
        copied = False
        for name in FONT_FILES:
            src = os.path.join(FONT_SRC, name)
            tgt = os.path.join(dst, name)
            if os.path.isfile(src) and not os.path.isfile(tgt):
                shutil.copy2(src, tgt)
                copied = True
        if copied and shutil.which("fc-cache"):
            subprocess.run(["fc-cache", "-f", dst],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20)
    except Exception:
        pass  # fonts are optional; CSS falls back to monospace/sans
