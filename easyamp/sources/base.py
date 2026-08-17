"""Shared model + interface for remote music sources (Plex, Jellyfin, …).

A :class:`Track` is what the playlist holds — local files are Tracks too
(``Track.local(path)``), so the window/playlist code never branches on
where a track came from. A :class:`MusicSource` wraps one configured
account and knows how to browse it and turn its tracks into playable URIs.

Threading contract: every ``MusicSource`` method may hit the network and
**must only be called from a worker thread** — except :meth:`stream_uri`,
which is pure string construction (no I/O) so the window can call it on
the main loop at play time.
"""

from __future__ import annotations

import json
import os
import urllib.error
import urllib.request
from dataclasses import dataclass, field

from .. import __version__


class SourceError(Exception):
    """A source operation failed (network, auth, bad response)."""


@dataclass
class Track:
    uri: str                    # local path, or canonical *token-free* URL
    title: str = ""
    artist: str = ""
    album: str = ""
    duration: int = 0           # seconds; 0 = unknown
    source_id: str = ""         # "" = local file / plain URL
    item_id: str = ""           # server-side id (ratingKey / Jellyfin Id)
    artwork: str = ""

    @classmethod
    def local(cls, path: str) -> "Track":
        return cls(uri=path)

    def display(self) -> str:
        if self.title:
            return f"{self.artist} - {self.title}" if self.artist else self.title
        if "://" in self.uri:
            return self.uri
        return os.path.splitext(os.path.basename(self.uri))[0]


@dataclass
class BrowseItem:
    """One row in the source browser. ``kind`` is one of ``folder``,
    ``artist``, ``album``, ``playlist``, ``track``; tracks carry their
    playable :class:`Track`."""
    id: str
    name: str
    kind: str
    track: Track | None = None
    extra: dict = field(default_factory=dict)


class MusicSource:
    """One configured account. Subclasses set ``type`` and implement the
    browse/auth methods; ``account`` is the registry's JSON dict for this
    account (mutations to it are persisted by the registry)."""

    type = ""

    def __init__(self, account: dict):
        self.account = account

    @property
    def id(self) -> str:
        return self.account["id"]

    @property
    def name(self) -> str:
        return self.account.get("name", self.type)

    # ---- to implement -------------------------------------------------
    def verify(self) -> bool:
        """Cheap connectivity/auth check. Never raises."""
        raise NotImplementedError

    def browse_root(self) -> list[BrowseItem]:
        raise NotImplementedError

    def browse(self, item_id: str) -> list[BrowseItem]:
        raise NotImplementedError

    def stream_uri(self, track: Track) -> tuple[str, dict | None]:
        """(playable URI, extra HTTP headers or None). Pure string
        construction — no network; safe on the main loop."""
        raise NotImplementedError

    def artwork_url(self, item: BrowseItem, size: int = 128) -> str:
        return ""

    def refresh_auth(self) -> bool:
        """Try to restore a working session after a playback failure
        (worker thread). True = worth retrying the track."""
        return False

    def resolve(self, track: Track) -> Track | None:
        """Re-derive a track's canonical URI from ``item_id`` after
        ``refresh_auth`` (worker thread; may hit the network). None if the
        item is gone."""
        return track


# ---- shared HTTP helper --------------------------------------------------
def http_json(url: str, headers: dict | None = None, data: dict | None = None,
              timeout: float = 8.0, method: str | None = None):
    """POST ``data`` as JSON / GET, parse a JSON response. Raises
    :class:`SourceError` on any network or parse failure."""
    hdrs = {"User-Agent": f"EasyAmp/{__version__}",
            "Accept": "application/json"}
    if headers:
        hdrs.update(headers)
    body = None
    if data is not None:
        body = json.dumps(data).encode("utf-8")
        hdrs["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=body, headers=hdrs, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read()
    except urllib.error.HTTPError as e:
        raise SourceError(f"HTTP {e.code} from {url.split('?')[0]}") from e
    except (urllib.error.URLError, OSError, TimeoutError) as e:
        raise SourceError(f"network error: {e}") from e
    if not raw:
        return {}
    try:
        return json.loads(raw.decode("utf-8"))
    except ValueError as e:
        raise SourceError("bad JSON response") from e
