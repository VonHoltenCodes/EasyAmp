"""Small keyed secret store for streaming-account tokens.

``get(key)`` / ``set(key, value)`` / ``delete(key)`` with a per-platform
backend and zero pip dependencies:

- **Linux**: libsecret over D-Bus (GI ``Secret``; inside Flatpak this needs
  ``--talk-name=org.freedesktop.secrets`` in the manifest's finish-args).
- **Windows**: DPAPI (``CryptProtectData``) via ctypes; encrypted blobs are
  kept base64'd in ``<configdir>/secrets.json`` and can only be decrypted
  by the same Windows user.
- **macOS**: the login Keychain via the ``security`` CLI.
- **Fallback** (backend missing/broken): plaintext in the same JSON file,
  written 0o600 and marked ``"enc": "none"`` — worse than a keychain, but
  never worse than the config file next to it.

``secrets.json`` maps ``key -> {"enc": "dpapi"|"none", "data": <str>}``.
Keys are flat strings like ``plex:1a2b:token``. All calls are synchronous
and may block on D-Bus/subprocess — call them from worker threads, not the
GTK main loop.
"""

from __future__ import annotations

import base64
import json
import os
import subprocess
import sys

from .appdirs import config_dir

_SERVICE = "com.vonholtencodes.EasyAmp"


# ---- the JSON file (DPAPI blobs + plaintext fallback) --------------------
def _json_path() -> str:
    return os.path.join(config_dir(), "secrets.json")


def _json_read() -> dict:
    try:
        with open(_json_path(), encoding="utf-8") as fh:
            data = json.load(fh)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def _json_write(data: dict) -> None:
    fd = os.open(_json_path(), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=2)


def _json_set(key: str, enc: str, payload: str) -> None:
    data = _json_read()
    data[key] = {"enc": enc, "data": payload}
    _json_write(data)


def _json_delete(key: str) -> None:
    data = _json_read()
    if key in data:
        del data[key]
        _json_write(data)


def _json_get(key: str) -> str | None:
    """Decode a JSON-file entry (either encoding); None if absent/broken."""
    entry = _json_read().get(key)
    if not isinstance(entry, dict):
        return None
    try:
        if entry.get("enc") == "dpapi":
            blob = base64.b64decode(entry.get("data", ""))
            return _dpapi(blob, protect=False).decode("utf-8")
        return entry.get("data")
    except Exception:
        return None


# ---- Linux: libsecret ----------------------------------------------------
_secret_mod = None


def _libsecret():
    global _secret_mod
    if _secret_mod is None:
        import gi
        gi.require_version("Secret", "1")
        from gi.repository import Secret
        _secret_mod = Secret
    return _secret_mod


def _schema():
    Secret = _libsecret()
    return Secret.Schema.new(
        _SERVICE, Secret.SchemaFlags.NONE,
        {"key": Secret.SchemaAttributeType.STRING})


def _ls_get(key: str) -> str | None:
    Secret = _libsecret()
    return Secret.password_lookup_sync(_schema(), {"key": key}, None)


def _ls_set(key: str, value: str) -> None:
    Secret = _libsecret()
    ok = Secret.password_store_sync(
        _schema(), {"key": key}, Secret.COLLECTION_DEFAULT,
        f"EasyAmp: {key}", value, None)
    if not ok:
        raise OSError("libsecret store failed")


def _ls_delete(key: str) -> None:
    Secret = _libsecret()
    Secret.password_clear_sync(_schema(), {"key": key}, None)


# ---- Windows: DPAPI ------------------------------------------------------
def _dpapi(data: bytes, protect: bool) -> bytes:
    import ctypes
    import ctypes.wintypes as wt

    class BLOB(ctypes.Structure):
        _fields_ = [("cbData", wt.DWORD),
                    ("pbData", ctypes.POINTER(ctypes.c_char))]

    buf = ctypes.create_string_buffer(data, len(data))
    blob_in = BLOB(len(data), ctypes.cast(buf, ctypes.POINTER(ctypes.c_char)))
    blob_out = BLOB()
    fn = (ctypes.windll.crypt32.CryptProtectData if protect
          else ctypes.windll.crypt32.CryptUnprotectData)
    if not fn(ctypes.byref(blob_in), None, None, None, None, 0,
              ctypes.byref(blob_out)):
        raise OSError("DPAPI call failed")
    try:
        return ctypes.string_at(blob_out.pbData, blob_out.cbData)
    finally:
        ctypes.windll.kernel32.LocalFree(blob_out.pbData)


def _win_get(key: str) -> str | None:
    return _json_get(key)


def _win_set(key: str, value: str) -> None:
    blob = _dpapi(value.encode("utf-8"), protect=True)
    _json_set(key, "dpapi", base64.b64encode(blob).decode("ascii"))


# ---- macOS: login Keychain ----------------------------------------------
def _sec(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["security", *args], capture_output=True,
                          text=True, timeout=15)


def _mac_get(key: str) -> str | None:
    r = _sec("find-generic-password", "-s", _SERVICE, "-a", key, "-w")
    return r.stdout.rstrip("\n") if r.returncode == 0 else None


def _mac_set(key: str, value: str) -> None:
    r = _sec("add-generic-password", "-U", "-s", _SERVICE, "-a", key,
             "-w", value)
    if r.returncode != 0:
        raise OSError(f"security add-generic-password failed: {r.stderr}")


def _mac_delete(key: str) -> None:
    _sec("delete-generic-password", "-s", _SERVICE, "-a", key)


# ---- public API ----------------------------------------------------------
def _backend():
    if sys.platform == "win32":
        return _win_get, _win_set, _json_delete
    if sys.platform == "darwin":
        return _mac_get, _mac_set, _mac_delete
    return _ls_get, _ls_set, _ls_delete


def get(key: str) -> str | None:
    getter = _backend()[0]
    try:
        val = getter(key)
    except Exception:
        val = None
    # covers the plaintext-fallback case on Linux/macOS (Windows getter
    # already reads the JSON file, so this is a no-op repeat there)
    return val if val is not None else _json_get(key)


def set(key: str, value: str) -> None:
    setter = _backend()[1]
    try:
        setter(key, value)
        if sys.platform != "win32":
            _json_delete(key)   # drop any stale plaintext-fallback copy
    except Exception:
        _json_set(key, "none", value)


def delete(key: str) -> None:
    deleter = _backend()[2]
    try:
        deleter(key)
    except Exception:
        pass
    _json_delete(key)
