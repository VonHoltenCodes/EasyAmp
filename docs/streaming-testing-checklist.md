# Streaming sources — Windows / macOS pre-release test checklist

Linux (dev box, Flatpak runtime) is fully verified: Plex linked against
starbase1, Jellyfin E2E against a container, radio, 5220-track stress,
keyring storage. The items below are what ONLY a real Windows / macOS run
can prove. Build both installers from the branch with **Actions →
"workflow_dispatch"** (no tag needed); the signed artifacts appear on the
run page.

## Windows (signed installer from the Actions artifact)

- [ ] Installer runs; SmartScreen shows publisher "trenton von holten" (not Unknown)
- [ ] App launches (first launch is ~20s — splash covers it) with the SOURCES tab present
- [ ] **+PLEX link flow**: code shows, `plex.tv/link` opens prefilled, account
      appears with green LED after approval, and **survives an app restart**
- [ ] **DPAPI secret storage**: `%APPDATA%\easyamp\secrets.json` exists and its
      entries say `"enc": "dpapi"` — if any say `"enc": "none"`, the DPAPI
      backend failed and fell back to plaintext → file a bug
- [ ] Browse Music → artist → album; play a track (Plex direct-play is plain
      progressive HTTP — `gstsoup` is bundled). **If a track errors with
      missing-plugin**, the server transcoded to HLS: fix is removing
      `gsthls`/`gstadaptivedemux` from `_GST_PLUGIN_DENY` in
      `packaging/windows/EasyAmp.spec`
- [ ] BUFF indicator appears on network play; pausing during buffering STAYS paused
- [ ] Save playlist with Plex tracks → restart app → load it → tracks rebind and play
- [ ] Regression: local MP3/FLAC playback, EQ toggles/presets, rapid track
      changes, update badge still fires
- [ ] Jellyfin sign-in (LAN server, or `https://demo.jellyfin.org/stable`,
      user `demo`, blank password) — browse + play

## macOS (notarized DMG from the Actions artifact)

⚠️ The macOS tester is remote — starbase1 is LAN-only and discovery skips
Plex relay connections, so a remote Mac CANNOT reach Trent's server. Test
account flows with the tester's own Plex server if they have one; otherwise
Jellyfin's public demo covers the streaming path.

- [ ] DMG opens clean under Gatekeeper (signed + notarized, no right-click dance)
- [ ] **HTTPS sanity**: the +PLEX popover fetches a code from plex.tv at all —
      this proves the bundled Python's TLS cert store works; if it errors
      immediately, we need certifi bundling in the spec
- [ ] **Keychain storage**: after adding an account, the token appears in
      Keychain Access under service `com.vonholtencodes.EasyAmp`, and there is
      NO `~/Library/Application Support/easyamp/secrets.json` fallback file
- [ ] Jellyfin demo server: sign in, browse, play a track; BUFF indicator behaves
- [ ] Config lands in `~/Library/Application Support/easyamp/sources.json`
- [ ] Regression: local playback, EQ, update badge

## Before the actual release tag

- [ ] Version sync: `easyamp/__init__.py`, `pyproject.toml`,
      `packaging/macos/EasyAmp.spec` (hardcoded, line ~118),
      `data/...metainfo.xml` release entry; `latest.json` after publish
- [ ] Metainfo/README copy still says "plays local music" — update the
      description for streaming
- [ ] Flathub manifest (stale at 0.3.1) needs `--share=network` +
      `--talk-name=org.freedesktop.secrets` whenever it's next synced
