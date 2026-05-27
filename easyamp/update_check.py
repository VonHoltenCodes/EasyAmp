"""Lightweight 'new version available' check for the standalone builds.

Windows and macOS have no package manager to update EasyAmp, so on launch the
app fetches a small JSON from the project server, compares it to the running
version, and — if newer — reveals a dismissible banner with a download link.

Skipped entirely under Flatpak, where the software center / ``flatpak update``
already handle updates. Fully best-effort: any network or parse error is
swallowed silently (no popups, no logs in the user's face).
"""

from __future__ import annotations

import json
import os
import threading
import urllib.request

from gi.repository import GLib

from . import __version__

LATEST_URL = "https://dl.easyampstereo.com/latest.json"
_TIMEOUT = 6  # seconds


def _running_in_flatpak() -> bool:
    return os.path.exists("/.flatpak-info") or "FLATPAK_ID" in os.environ


def _parse(v: str) -> tuple[int, ...]:
    """Turn '0.3.4' (or 'v0.3.4') into (0, 3, 4) for comparison."""
    out: list[int] = []
    for chunk in str(v).strip().lstrip("vV").split("."):
        digits = ""
        for ch in chunk:
            if ch.isdigit():
                digits += ch
            else:
                break
        out.append(int(digits) if digits else 0)
    return tuple(out)


def is_newer(remote: str, local: str) -> bool:
    return _parse(remote) > _parse(local)


def check_async(on_update) -> None:
    """If a newer version is published, call ``on_update(version, download_url)``
    on the GLib main loop. Never raises; a no-op under Flatpak."""
    if _running_in_flatpak():
        return

    def worker() -> None:
        try:
            req = urllib.request.Request(
                LATEST_URL, headers={"User-Agent": f"EasyAmp/{__version__}"})
            with urllib.request.urlopen(req, timeout=_TIMEOUT) as resp:
                data = json.loads(resp.read().decode("utf-8"))
            remote = str(data.get("version", "")).strip()
            url = str(data.get("download_url", "")).strip()
            if remote and url and is_newer(remote, __version__):
                GLib.idle_add(on_update, remote, url)
        except Exception:
            pass  # offline / 404 / malformed → silently skip

    threading.Thread(target=worker, daemon=True).start()
