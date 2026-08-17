"""Jellyfin source: native API auth, library browse, universal streaming.

Sign-in is ``POST /Users/AuthenticateByName`` with the ``MediaBrowser``
authorization header; the returned access token is kept in the secret
store (plus the password, only if the user opted to store it for silent
re-login — Jellyfin tokens are long-lived but revocable).

Browse ids: ``libs`` → ``lib/<id>`` (album artists) → ``artist/<id>``
(albums) → ``album/<id>`` (tracks), plus ``playlists`` → ``playlist/<id>``.
Playback uses ``/Audio/<id>/universal`` so the server decides direct-play
vs transcode; the canonical Track URI is token-free and ``stream_uri``
appends ``api_key`` at play time.
"""

from __future__ import annotations

import urllib.parse

from .. import __version__
from .. import secrets_store
from .base import BrowseItem, MusicSource, SourceError, Track, http_json

_UNIVERSAL_Q = ("MaxStreamingBitrate=140000000"
                "&Container=opus,webm|opus,mp3,aac,m4a|aac,m4b|aac,flac,"
                "webma,webm|webma,wav,ogg"
                "&TranscodingContainer=mp3&TranscodingProtocol=http"
                "&AudioCodec=mp3")


def _auth_header(client_id: str, token: str = "") -> dict:
    parts = ['MediaBrowser Client="EasyAmp"', 'Device="EasyAmp"',
             f'DeviceId="{client_id}"', f'Version="{__version__}"']
    if token:
        parts.append(f'Token="{token}"')
    return {"Authorization": ", ".join(parts)}


def authenticate(base_url: str, client_id: str, username: str,
                 password: str) -> tuple[str, str]:
    """Sign in; returns (access_token, user_id). Raises SourceError."""
    data = http_json(f"{base_url.rstrip('/')}/Users/AuthenticateByName",
                     headers=_auth_header(client_id),
                     data={"Username": username, "Pw": password},
                     method="POST")
    token = data.get("AccessToken")
    user = (data.get("User") or {}).get("Id")
    if not token or not user:
        raise SourceError("Jellyfin sign-in gave no token")
    return str(token), str(user)


class JellyfinSource(MusicSource):
    type = "jellyfin"

    @property
    def base_url(self) -> str:
        return self.account.get("base_url", "").rstrip("/")

    @property
    def user_id(self) -> str:
        return self.account.get("user_id", "")

    @property
    def client_id(self) -> str:
        return self.account.get("client_id", "")

    def _token(self) -> str:
        return secrets_store.get(f"{self.id}:token") or ""

    def _get(self, path: str, timeout: float = 8.0):
        return http_json(f"{self.base_url}{path}",
                         headers=_auth_header(self.client_id, self._token()),
                         timeout=timeout)

    def verify(self) -> bool:
        try:
            self._get(f"/Users/{self.user_id}", timeout=3)
            return True
        except SourceError:
            return False

    # ---- browse -------------------------------------------------------
    def browse_root(self) -> list[BrowseItem]:
        return self.browse("libs")

    def browse(self, item_id: str) -> list[BrowseItem]:
        kind, _, key = item_id.partition("/")
        if kind == "libs":
            views = self._get(f"/Users/{self.user_id}/Views")
            out = [BrowseItem(f"lib/{v['Id']}", v.get("Name", "Music"),
                              "folder")
                   for v in views.get("Items", [])
                   if v.get("CollectionType") == "music"]
            out.append(BrowseItem("playlists", "Playlists", "folder"))
            return out
        if kind == "lib":
            q = urllib.parse.urlencode({"ParentId": key, "UserId": self.user_id,
                                        "SortBy": "SortName"})
            data = self._get(f"/Artists/AlbumArtists?{q}")
            return [BrowseItem(f"artist/{a['Id']}", a.get("Name", "?"),
                               "artist")
                    for a in data.get("Items", [])]
        if kind == "artist":
            return [BrowseItem(f"album/{a['Id']}", a.get("Name", "?"), "album")
                    for a in self._items(AlbumArtistIds=key, Recursive="true",
                                         IncludeItemTypes="MusicAlbum",
                                         SortBy="PremiereDate,SortName")]
        if kind == "album":
            return [self._track_item(t)
                    for t in self._items(ParentId=key,
                                         SortBy="ParentIndexNumber,IndexNumber")
                    if t.get("Type") == "Audio"]
        if kind == "playlists":
            return [BrowseItem(f"playlist/{p['Id']}", p.get("Name", "?"),
                               "playlist")
                    for p in self._items(Recursive="true",
                                         IncludeItemTypes="Playlist")]
        if kind == "playlist":
            q = urllib.parse.urlencode({"UserId": self.user_id})
            data = self._get(f"/Playlists/{key}/Items?{q}")
            return [self._track_item(t) for t in data.get("Items", [])
                    if t.get("Type") == "Audio"]
        raise SourceError(f"unknown browse id {item_id!r}")

    def _items(self, **params) -> list[dict]:
        q = urllib.parse.urlencode(params)
        data = self._get(f"/Users/{self.user_id}/Items?{q}")
        return data.get("Items", [])

    def _track_item(self, t: dict) -> BrowseItem:
        track = self._make_track(t)
        return BrowseItem(f"track/{t['Id']}", track.display(), "track",
                          track=track)

    def _canonical_uri(self, item_id: str) -> str:
        return (f"{self.base_url}/Audio/{item_id}/universal"
                f"?UserId={self.user_id}&DeviceId={self.client_id}"
                f"&{_UNIVERSAL_Q}")

    def _make_track(self, t: dict) -> Track:
        artists = t.get("Artists") or []
        return Track(
            uri=self._canonical_uri(str(t.get("Id", ""))),
            title=t.get("Name", ""),
            artist=artists[0] if artists else t.get("AlbumArtist", ""),
            album=t.get("Album", ""),
            duration=int(t.get("RunTimeTicks", 0)) // 10_000_000,
            source_id=self.id,
            item_id=str(t.get("Id", "")))

    # ---- playback -----------------------------------------------------
    def stream_uri(self, track: Track) -> tuple[str, dict | None]:
        sep = "&" if "?" in track.uri else "?"
        return f"{track.uri}{sep}api_key={self._token()}", None

    def artwork_url(self, item: BrowseItem, size: int = 128) -> str:
        item_id = item.id.partition("/")[2] or (item.track.item_id
                                                if item.track else "")
        if not item_id:
            return ""
        return (f"{self.base_url}/Items/{item_id}/Images/Primary"
                f"?maxWidth={size}&api_key={self._token()}")

    # ---- recovery -----------------------------------------------------
    def refresh_auth(self) -> bool:
        if self.verify():
            return True
        password = secrets_store.get(f"{self.id}:password")
        if not password:
            return False
        try:
            token, user_id = authenticate(self.base_url, self.client_id,
                                          self.account.get("username", ""),
                                          password)
        except SourceError:
            return False
        secrets_store.set(f"{self.id}:token", token)
        self.account["user_id"] = user_id
        from . import registry
        registry.save()
        return True

    def resolve(self, track: Track) -> Track | None:
        if not track.item_id:
            return track
        fixed = Track(**{**track.__dict__,
                         "uri": self._canonical_uri(track.item_id)})
        return fixed
