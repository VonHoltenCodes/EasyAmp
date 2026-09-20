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


def _bundle_roots() -> list[str]:
    """Directories that make up the frozen bundle: PyInstaller's unpack dir
    (``Contents/Frameworks`` in a .app) and the enclosing ``*.app``."""
    roots = []
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        roots.append(meipass)
    exe = os.path.realpath(sys.executable or "")
    head = exe
    while head and head != os.path.dirname(head):
        if head.endswith(".app"):
            roots.append(head)
            break
        head = os.path.dirname(head)
    return [os.path.realpath(r) for r in roots]


def _inside_bundle(path: str) -> bool:
    real = os.path.realpath(path)
    return any(real == root or real.startswith(root + os.sep)
               for root in _bundle_roots())


def ensure_private_gst_registry() -> str | None:
    """Frozen macOS bundles only: point GStreamer's plugin-registry cache
    at a per-user cache dir BEFORE anything imports Gst.

    Without this, GStreamer writes ``registry.bin`` next to its bundled
    plugins — inside the sealed, notarized .app — which invalidates the
    code signature after first launch (issue #4, diagnosed by
    @ContractorKeith: ``codesign --verify`` fails with "a sealed resource
    is missing or invalid" until the stray file is deleted).

    PyInstaller's own GStreamer runtime hook runs before the launcher and
    sets ``GST_REGISTRY`` to ``<bundle>/registry.bin``, so an already-set
    value is NOT proof of a user override: one that points inside the
    bundle is exactly the write we are here to prevent, and is replaced.
    Only a value outside the bundle is honoured.

    Returns the registry path when set; None when not applicable (other
    platforms, dev runs, or the user already set ``GST_REGISTRY``).
    """
    if sys.platform != "darwin" or not getattr(sys, "frozen", False):
        return None
    existing = os.environ.get("GST_REGISTRY")
    if existing and not _inside_bundle(existing):
        return None          # explicit user/system override wins
    cache = os.path.expanduser("~/Library/Caches/easyamp")
    os.makedirs(cache, exist_ok=True)
    path = os.path.join(cache, "registry.bin")
    os.environ["GST_REGISTRY"] = path
    return path
