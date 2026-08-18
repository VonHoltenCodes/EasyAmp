"""Registry of configured streaming accounts.

Accounts (minus their secrets, which live in :mod:`easyamp.secrets_store`)
persist as JSON in ``<configdir>/sources.json``:

    {"client_id": "<uuid>",
     "accounts": [{"id": "plex:1a2b", "type": "plex", "name": "Starbase1",
                   "base_url": "http://…:32400", …}]}

``client_id`` is the stable device identifier sent to Plex/Jellyfin
(minted once per install). The module singleton ``registry`` is what the
UI and window use.
"""

from __future__ import annotations

import json
import os
import secrets as _secrets
import uuid

from ..appdirs import config_dir
from .. import secrets_store
from .base import MusicSource, SourceError, Track, BrowseItem  # noqa: F401 (re-export)


def _source_class(type_: str):
    # lazy imports keep startup light and avoid import cycles
    if type_ == "plex":
        from .plex import PlexSource
        return PlexSource
    if type_ == "jellyfin":
        from .jellyfin import JellyfinSource
        return JellyfinSource
    return None


class Registry:
    def __init__(self):
        self._path = os.path.join(config_dir(), "sources.json")
        self._data = self._read()
        self._clients: dict[str, MusicSource] = {}

    def _read(self) -> dict:
        try:
            with open(self._path, encoding="utf-8") as fh:
                data = json.load(fh)
            if isinstance(data, dict) and isinstance(data.get("accounts"), list):
                return data
        except (OSError, ValueError):
            pass
        return {"accounts": []}

    def save(self) -> None:
        with open(self._path, "w", encoding="utf-8") as fh:
            json.dump(self._data, fh, indent=2)

    # ---- device identity ---------------------------------------------
    @property
    def client_id(self) -> str:
        if not self._data.get("client_id"):
            self._data["client_id"] = str(uuid.uuid4())
            self.save()
        return self._data["client_id"]

    # ---- accounts -----------------------------------------------------
    def accounts(self) -> list[dict]:
        return list(self._data["accounts"])

    def add_account(self, type_: str, name: str, **fields) -> dict:
        acct = {"id": f"{type_}:{_secrets.token_hex(2)}",
                "type": type_, "name": name, **fields}
        self._data["accounts"].append(acct)
        self.save()
        return acct

    def remove_account(self, source_id: str) -> None:
        self._data["accounts"] = [a for a in self._data["accounts"]
                                  if a["id"] != source_id]
        self._clients.pop(source_id, None)
        self.save()
        for suffix in ("token", "password"):
            secrets_store.delete(f"{source_id}:{suffix}")

    def get(self, source_id: str) -> MusicSource | None:
        client = self._clients.get(source_id)
        if client is not None:
            return client
        for acct in self._data["accounts"]:
            if acct["id"] == source_id:
                cls = _source_class(acct.get("type", ""))
                if cls is None:
                    return None
                client = cls(acct)
                self._clients[source_id] = client
                return client
        return None


registry = Registry()
