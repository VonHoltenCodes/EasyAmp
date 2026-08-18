"""Plex source: PIN/link auth, server discovery, library browse, direct-play.

Auth is Plex's sanctioned third-party flow: create a PIN at plex.tv, send
the user to the link page to approve it, poll the PIN for an account
token, then exchange that for per-server access via the resources API.
Every request sends ``Accept: application/json`` (Plex answers XML
otherwise) and the standard ``X-Plex-*`` client headers.

Browse item ids encode the hierarchy as plain strings:
``sections`` → ``sec/<key>`` → ``artist/<ratingKey>`` →
``album/<ratingKey>``, plus ``playlists`` → ``playlist/<ratingKey>``.
"""

from __future__ import annotations

import socket
import urllib.error
import urllib.parse
import urllib.request

from .. import __version__
from .. import secrets_store
from .base import BrowseItem, MusicSource, SourceError, Track, http_json

PLEX_TV = "https://plex.tv"
_PRODUCT = "EasyAmp"


def _headers(client_id: str, token: str = "") -> dict:
    h = {"X-Plex-Product": _PRODUCT,
         "X-Plex-Version": __version__,
         "X-Plex-Client-Identifier": client_id,
         "X-Plex-Device-Name": _PRODUCT,
         "X-Plex-Platform": "GTK"}
    if token:
        h["X-Plex-Token"] = token
    return h


# ---- PIN / link auth (module-level: runs before an account exists) -------
def start_pin(client_id: str) -> tuple[int, str]:
    """Create a link PIN; returns (pin_id, 4-char code)."""
    data = http_json(f"{PLEX_TV}/api/v2/pins?strong=false",
                     headers=_headers(client_id), data={}, method="POST")
    return int(data["id"]), str(data["code"])


def link_url(client_id: str, code: str) -> str:
    q = urllib.parse.urlencode({"clientID": client_id, "code": code,
                                "context[device][product]": _PRODUCT})
    return f"https://app.plex.tv/auth#?{q}"


def poll_pin(client_id: str, pin_id: int) -> str | None:
    """One poll of the PIN; the account token once the user has linked."""
    data = http_json(f"{PLEX_TV}/api/v2/pins/{pin_id}",
                     headers=_headers(client_id))
    token = data.get("authToken")
    return str(token) if token else None


def discover_servers(client_id: str, account_token: str) -> list[dict]:
    """Reachable PMS instances for this account. Each entry:
    ``{name, machine_id, base_url, token}`` — ``token`` is the
    server-specific access token from the resources API, ``base_url`` the
    fastest reachable connection (LAN preferred).

    Probing is two-pass: the first HTTPS contact on a cold resolver (DNS
    for *.plex.direct + TLS handshake) can exceed a short timeout on
    Windows, so a full miss gets one warm retry. If every probe still
    misses, the best local candidate is returned unprobed — a red status
    LED and refresh_auth() beat a dead-end "no reachable server"."""
    resources = http_json(
        f"{PLEX_TV}/api/v2/resources?includeHttps=1&includeRelay=0",
        headers=_headers(client_id, account_token), timeout=10)
    out = []
    for res in resources if isinstance(resources, list) else []:
        if "server" not in str(res.get("provides", "")):
            continue
        token = res.get("accessToken") or account_token
        conns = sorted(res.get("connections") or [],
                       key=lambda c: (not c.get("local"), bool(c.get("relay"))))
        base = None
        for _attempt in range(2):
            base = next((c["uri"] for c in conns
                         if _reachable(c.get("uri", ""), token)), None)
            if base:
                break
        if not base:
            base = next((c["uri"] for c in conns if c.get("local")), None)
        if base:
            out.append({"name": res.get("name", "Plex Server"),
                        "machine_id": res.get("clientIdentifier", ""),
                        "base_url": base.rstrip("/"), "token": token})
    return out


def _reachable(base: str, token: str) -> bool:
    if not base:
        return False
    try:
        req = urllib.request.Request(
            f"{base}/identity",
            headers={"X-Plex-Token": token, "Accept": "application/json"})
        with urllib.request.urlopen(req, timeout=5):
            return True
    except urllib.error.HTTPError:
        return True         # any HTTP answer (401/403/...) IS reachable
    except (OSError, socket.timeout, Exception):
        return False


# ---- the source ----------------------------------------------------------
class PlexSource(MusicSource):
    type = "plex"

    @property
    def base_url(self) -> str:
        return self.account.get("base_url", "").rstrip("/")

    @property
    def client_id(self) -> str:
        return self.account.get("client_id", "")

    def _token(self) -> str:
        return secrets_store.get(f"{self.id}:token") or ""

    def _get(self, path: str, timeout: float = 8.0):
        return http_json(f"{self.base_url}{path}",
                         headers=_headers(self.client_id, self._token()),
                         timeout=timeout)

    def verify(self) -> bool:
        try:
            self._get("/identity", timeout=3)
            return True
        except SourceError:
            return False

    # ---- browse -------------------------------------------------------
    def browse_root(self) -> list[BrowseItem]:
        return self.browse("sections")

    def browse(self, item_id: str) -> list[BrowseItem]:
        kind, _, key = item_id.partition("/")
        if kind == "sections":
            out = []
            for d in self._dir("/library/sections"):
                if d.get("type") == "artist":     # a Plex *music* library
                    out.append(BrowseItem(f"sec/{d['key']}",
                                          d.get("title", "Music"), "folder"))
            out.append(BrowseItem("playlists", "Playlists", "folder"))
            return out
        if kind == "sec":
            return [BrowseItem(f"artist/{d['ratingKey']}",
                               d.get("title", "?"), "artist")
                    for d in self._dir(f"/library/sections/{key}/all?type=8")]
        if kind == "artist":
            return [BrowseItem(f"album/{d['ratingKey']}",
                               d.get("title", "?"), "album")
                    for d in self._dir(f"/library/metadata/{key}/children")]
        if kind == "album":
            return [self._track_item(d)
                    for d in self._dir(f"/library/metadata/{key}/children")
                    if d.get("type") == "track"]
        if kind == "playlists":
            return [BrowseItem(f"playlist/{d['ratingKey']}",
                               d.get("title", "?"), "playlist")
                    for d in self._dir("/playlists?playlistType=audio")]
        if kind == "playlist":
            return [self._track_item(d)
                    for d in self._dir(f"/playlists/{key}/items")
                    if d.get("type") == "track"]
        raise SourceError(f"unknown browse id {item_id!r}")

    def _dir(self, path: str, page: int = 1000) -> list[dict]:
        # paged: a full-library playlist can hold many thousands of items,
        # and one giant response is slow to fetch and parse
        out: list[dict] = []
        start = 0
        while True:
            sep = "&" if "?" in path else "?"
            data = self._get(f"{path}{sep}X-Plex-Container-Start={start}"
                             f"&X-Plex-Container-Size={page}", timeout=20)
            mc = (data.get("MediaContainer") or {})
            # sections arrive under Directory; artists/albums/tracks/
            # playlists under Metadata — merge so every level browses
            chunk = (mc.get("Metadata") or []) + (mc.get("Directory") or [])
            out += chunk
            total = int(mc.get("totalSize", mc.get("size", len(out))) or 0)
            start += len(chunk)
            if not chunk or start >= total:
                return out

    def _track_item(self, d: dict) -> BrowseItem:
        track = self._make_track(d)
        return BrowseItem(f"track/{d['ratingKey']}",
                          track.display(), "track", track=track)

    def _make_track(self, d: dict) -> Track:
        part = ((d.get("Media") or [{}])[0].get("Part") or [{}])[0]
        return Track(
            uri=f"{self.base_url}{part.get('key', '')}",
            title=d.get("title", ""),
            artist=d.get("grandparentTitle", ""),
            album=d.get("parentTitle", ""),
            duration=int(d.get("duration", 0)) // 1000,
            source_id=self.id,
            item_id=str(d.get("ratingKey", "")),
            artwork=d.get("thumb", ""))

    # ---- playback -----------------------------------------------------
    def stream_uri(self, track: Track) -> tuple[str, dict | None]:
        sep = "&" if "?" in track.uri else "?"
        return f"{track.uri}{sep}X-Plex-Token={self._token()}", None

    def artwork_url(self, item: BrowseItem, size: int = 128) -> str:
        thumb = item.track.artwork if item.track else ""
        if not thumb:
            return ""
        q = urllib.parse.urlencode({"width": size, "height": size,
                                    "url": thumb,
                                    "X-Plex-Token": self._token()})
        return f"{self.base_url}/photo/:/transcode?{q}"

    # ---- recovery -----------------------------------------------------
    def refresh_auth(self) -> bool:
        """The usual failure is a changed LAN address, not token expiry:
        re-discover this machine's connections and re-probe."""
        if self.verify():
            return True     # server fine; the track itself may need resolve()
        try:
            for srv in discover_servers(self.client_id, self._token()):
                if srv["machine_id"] == self.account.get("machine_id"):
                    self.account["base_url"] = srv["base_url"]
                    secrets_store.set(f"{self.id}:token", srv["token"])
                    from . import registry
                    registry.save()
                    return True
        except SourceError:
            pass
        return False

    def resolve(self, track: Track) -> Track | None:
        if not track.item_id:
            return track
        try:
            items = self._dir(f"/library/metadata/{track.item_id}")
        except SourceError:
            return None
        return self._make_track(items[0]) if items else None
