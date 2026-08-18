"""Playlist (.m3u) I/O with source-bound remote tracks.

Saved playlists stay interoperable: standard ``#EXTM3U`` / ``#EXTINF``
lines plus, for tracks that belong to a configured source, an EasyAmp
comment other players ignore:

    #EXTINF:213,Artist - Title
    #EASYAMP:plex:1a2b:49123
    http://192.168.68.69:32400/library/parts/…/file.mp3

URLs are written **token-free** (the canonical ``Track.uri``); playback
re-attaches credentials via the source at play time. On load, a line with
a scheme is taken verbatim (fixing the old bug where ``os.path.isabs``
treated URLs as relative and mangled them against the playlist's dir) and
a preceding ``#EASYAMP:`` tag rebinds it to its account.
"""

from __future__ import annotations

import os

from .sources.base import Track


def parse(path: str) -> list[Track]:
    base = os.path.dirname(path)
    out: list[Track] = []
    duration = 0
    title = ""
    source_id = ""
    item_id = ""
    with open(path, encoding="utf-8", errors="ignore") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#EXTINF:"):
                body = line[len("#EXTINF:"):]
                dur_part, _, title = body.partition(",")
                try:
                    duration = max(0, int(float(dur_part.split()[0])))
                except (ValueError, IndexError):
                    duration = 0
                title = title.strip()
                continue
            if line.startswith("#EASYAMP:"):
                # #EASYAMP:<source_id>:<item_id> — source ids themselves
                # contain one colon ("plex:1a2b"), so split from the right
                body = line[len("#EASYAMP:"):]
                source_id, _, item_id = body.rpartition(":")
                continue
            if line.startswith("#"):
                continue
            uri = line if "://" in line else (
                line if os.path.isabs(line) else os.path.join(base, line))
            artist, _, track_title = title.partition(" - ")
            if not track_title:
                artist, track_title = "", title
            out.append(Track(uri=uri, title=track_title.strip(),
                             artist=artist.strip(), duration=duration,
                             source_id=source_id, item_id=item_id))
            duration, title, source_id, item_id = 0, "", "", ""
    return out


def serialize(tracks: list[Track]) -> str:
    lines = ["#EXTM3U"]
    for t in tracks:
        if t.title:
            name = f"{t.artist} - {t.title}" if t.artist else t.title
            lines.append(f"#EXTINF:{t.duration or -1},{name}")
        if t.source_id:
            lines.append(f"#EASYAMP:{t.source_id}:{t.item_id}")
        lines.append(t.uri)
    return "\n".join(lines) + "\n"
