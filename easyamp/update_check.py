"""Lightweight 'new version available' check (all platforms).

On launch the app fetches a small JSON from the project server, compares it
to the running version, and — if newer — lights the bottom-right footer badge
amber with a link to the update page. The update page has per-OS instructions
(installer download for Windows/macOS; `flatpak update` / one-click for
Linux), so the same flow works everywhere. Fully best-effort: any network or
parse error is swallowed silently (no popups, no logs in the user's face).
"""

from __future__ import annotations

import json
import threading
import urllib.request

from gi.repository import GLib

from . import __version__

LATEST_URL = "https://dl.easyampstereo.com/latest.json"
_TIMEOUT = 6  # seconds


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
    """If a newer version is published, call ``on_update(version, update_url)``
    on the GLib main loop. Runs on every platform; never raises."""

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
