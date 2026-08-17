"""The SOURCES page: streaming accounts (left) + a library browser (right).

Third `Gtk.Stack` page next to PLAYER / EQUALIZER. The accounts panel
lists configured sources with a status LED and popover add-flows (Plex
PIN-link, Jellyfin server sign-in); the library panel drills through
whatever the selected source exposes and sends tracks to the playlist.

Every source call runs in a daemon worker thread with results marshalled
back via ``GLib.idle_add`` and guarded by a generation counter, matching
the app-wide convention (`update_check.py`, `player.py`).
"""

from __future__ import annotations

import threading

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, Gdk, GLib  # noqa: E402

from .widgets import panel_bar, make_button  # noqa: E402
from .sources import registry  # noqa: E402
from .sources.base import BrowseItem, SourceError  # noqa: E402
from .sources import plex as plex_mod  # noqa: E402
from .sources import jellyfin as jf_mod  # noqa: E402

_PIN_POLL_MS = 2000
_PIN_MAX_POLLS = 150      # ~5 min, matches plex.tv PIN lifetime


class SourcesView(Gtk.Box):
    def __init__(self, window):
        super().__init__(orientation=Gtk.Orientation.HORIZONTAL, spacing=3)
        self.add_css_class("eaa-chassis")
        self.window = window
        self._source_id = ""          # selected account
        self._crumbs: list[tuple[str, str]] = []   # (item_id, name)
        self._items: list[BrowseItem] = []
        self._gen = 0                 # invalidates in-flight browse results

        # ---- accounts (left column, same width as the player column) ----
        left = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        left.set_size_request(300, -1)
        left.append(panel_bar("EASYAMP SOURCES"))
        acc_scroll = Gtk.ScrolledWindow()
        acc_scroll.set_vexpand(True)
        acc_scroll.add_css_class("eaa-playlist")
        self.acc_list = Gtk.ListBox()
        self.acc_list.set_selection_mode(Gtk.SelectionMode.SINGLE)
        self.acc_list.connect("row-selected", self._on_account_selected)
        acc_scroll.set_child(self.acc_list)
        left.append(acc_scroll)

        acc_actions = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        acc_actions.add_css_class("eaa-panel")
        btn_plex = make_button("+PLEX")
        btn_plex.connect("clicked", self._on_add_plex)
        btn_jf = make_button("+JELLYFIN")
        btn_jf.connect("clicked", self._on_add_jellyfin)
        btn_rem = make_button("REM")
        btn_rem.connect("clicked", self._on_remove_account)
        for b in (btn_plex, btn_jf, btn_rem):
            acc_actions.append(b)
        left.append(acc_actions)
        self.append(left)

        # ---- library browser (right) ----
        right = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        right.set_hexpand(True)
        right.append(panel_bar("LIBRARY"))
        self.crumb = Gtk.Label(label="SELECT A SOURCE", xalign=0)
        self.crumb.add_css_class("eaa-crumb")
        self.crumb.set_ellipsize(1)   # PANGO_ELLIPSIZE_START — keep the tail
        right.append(self.crumb)
        lib_scroll = Gtk.ScrolledWindow()
        lib_scroll.set_vexpand(True)
        lib_scroll.add_css_class("eaa-playlist")
        self.lib_list = Gtk.ListBox()
        self.lib_list.set_selection_mode(Gtk.SelectionMode.SINGLE)
        self.lib_list.set_activate_on_single_click(False)
        self.lib_list.connect("row-activated", self._on_item_activated)
        lib_scroll.set_child(self.lib_list)
        right.append(lib_scroll)

        lib_actions = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        lib_actions.add_css_class("eaa-panel")
        for label, cb, tip in (
                ("BACK", self._on_back, "up one level"),
                ("PLAY", self._on_play_all,
                 "replace the playlist with the selection (or this view) and play"),
                ("+SEL", self._on_add_selected,
                 "append the selected track/album/playlist to the playlist"),
                ("+ALL", self._on_add_all,
                 "append every track in this view to the playlist")):
            b = make_button(label)
            b.set_tooltip_text(tip)
            b.connect("clicked", cb)
            lib_actions.append(b)
        lib_actions.append(Gtk.Box(hexpand=True))
        self.lib_status = Gtk.Label(label="")
        self.lib_status.add_css_class("eaa-ind")
        lib_actions.append(self.lib_status)
        right.append(lib_actions)
        self.append(right)

        self.refresh()

    # ---- worker plumbing ----------------------------------------------
    def _async(self, fn, done) -> None:
        """Run ``fn`` on a worker thread; ``done(result, error)`` on the
        main loop, dropped if the view has since navigated elsewhere."""
        gen = self._gen

        def worker():
            try:
                result, err = fn(), None
            except SourceError as e:
                result, err = None, str(e)
            except Exception as e:            # never kill the thread silently
                result, err = None, f"{type(e).__name__}: {e}"

            def deliver():
                if gen == self._gen:
                    done(result, err)
                return False
            GLib.idle_add(deliver)

        threading.Thread(target=worker, daemon=True).start()

    def _busy(self, text: str = "") -> None:
        self.lib_status.set_text(text)

    # ---- accounts list ------------------------------------------------
    def refresh(self) -> None:
        child = self.acc_list.get_first_child()
        while child:
            nxt = child.get_next_sibling()
            self.acc_list.remove(child)
            child = nxt
        for acct in registry.accounts():
            row = Gtk.ListBoxRow()
            box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
            led = Gtk.Box()
            led.add_css_class("eaa-src-led")
            led.set_valign(Gtk.Align.CENTER)
            box.append(led)
            lbl = Gtk.Label(label=f"{acct.get('name', '?')}  "
                                  f"[{acct.get('type', '?').upper()}]", xalign=0)
            lbl.add_css_class("eaa-track")
            lbl.set_ellipsize(3)
            box.append(lbl)
            row.set_child(box)
            row._source_id = acct["id"]
            row._led = led
            self.acc_list.append(row)
            self._probe(row)

    def _probe(self, row) -> None:
        src = registry.get(row._source_id)
        if src is None:
            row._led.add_css_class("err")
            return

        def done(ok, _err):
            row._led.remove_css_class("ok")
            row._led.remove_css_class("err")
            row._led.add_css_class("ok" if ok else "err")

        # probes are per-row, not per-navigation: bypass the gen guard
        def worker():
            ok = src.verify()
            GLib.idle_add(lambda: (done(ok, None), False)[1])
        threading.Thread(target=worker, daemon=True).start()

    def _on_account_selected(self, _lb, row) -> None:
        if row is None:
            return
        self._source_id = row._source_id
        self._crumbs = []
        self._browse(None)

    def _on_remove_account(self, _b) -> None:
        row = self.acc_list.get_selected_row()
        if row is None:
            return
        registry.remove_account(row._source_id)
        if self._source_id == row._source_id:
            self._source_id = ""
            self._set_items([])
            self.crumb.set_text("SELECT A SOURCE")
        self.refresh()

    # ---- browsing ------------------------------------------------------
    def _browse(self, item_id: str | None) -> None:
        src = registry.get(self._source_id)
        if src is None:
            return
        self._gen += 1
        self._busy("LOADING…")

        def fetch():
            return src.browse(item_id) if item_id else src.browse_root()

        def done(items, err):
            self._busy(err.upper()[:40] if err else "")
            if items is not None:
                if self._crumbs:
                    # a file-manager style "up" row, so backing out is
                    # discoverable right in the list (BACK button also works)
                    items = [BrowseItem("__up__", "‹ BACK", "up")] + items
                self._set_items(items)
                self._update_crumb()
        self._async(fetch, done)

    def _set_items(self, items: list[BrowseItem]) -> None:
        """Populate the browser in idle-sized chunks — a full-library Plex
        playlist can be thousands of rows, and building them synchronously
        freezes the UI for many seconds."""
        self._items = items
        gen = self._gen
        child = self.lib_list.get_first_child()
        while child:
            nxt = child.get_next_sibling()
            self.lib_list.remove(child)
            child = nxt
        it = iter(items)

        def add_chunk():
            if gen != self._gen:
                return False        # user navigated away mid-build
            for _ in range(150):
                item = next(it, None)
                if item is None:
                    return False
                suffix = "  /" if item.kind not in ("track", "up") else ""
                lbl = Gtk.Label(label=f"{item.name}{suffix}", xalign=0)
                lbl.add_css_class("eaa-track")
                lbl.set_ellipsize(3)
                row = Gtk.ListBoxRow()
                row.set_child(lbl)
                self.lib_list.append(row)
            return True

        if add_chunk():
            GLib.idle_add(add_chunk)

    def _update_crumb(self) -> None:
        src = registry.get(self._source_id)
        name = src.name if src else "?"
        trail = " > ".join(n for _i, n in self._crumbs)
        self.crumb.set_text(f"{name}{' > ' + trail if trail else ''}")

    def _on_item_activated(self, _lb, row) -> None:
        idx = row.get_index()
        if not (0 <= idx < len(self._items)):
            return
        item = self._items[idx]
        if item.kind == "up":
            self._on_back(None)
        elif item.kind == "track" and item.track:
            self.window.enqueue_tracks([item.track], play_now=True)
        else:
            self._crumbs.append((item.id, item.name))
            self._browse(item.id)

    def _on_back(self, _b) -> None:
        if not self._crumbs:
            return
        self._crumbs.pop()
        self._browse(self._crumbs[-1][0] if self._crumbs else None)

    # ---- sending tracks to the playlist -------------------------------
    def _gather_and(self, then, use_selection=True) -> None:
        """Collect tracks for the action buttons: the selected track, the
        selected container's contents, or (``use_selection=False`` / nothing
        selected) every track in the current view."""
        src = registry.get(self._source_id)
        if src is None:
            return
        sel = None
        if use_selection:
            row = self.lib_list.get_selected_row()
            sel = (self._items[row.get_index()]
                   if row and 0 <= row.get_index() < len(self._items) else None)
            if sel is not None and sel.kind == "up":
                sel = None          # the "‹ BACK" row is never a target

        def fetch():
            if sel is not None and sel.kind == "track":
                return [sel.track]
            if sel is not None:
                return [i.track for i in src.browse(sel.id)
                        if i.kind == "track" and i.track]
            return [i.track for i in self._items
                    if i.kind == "track" and i.track]

        def done(tracks, err):
            self._busy(err.upper()[:40] if err else "")
            if tracks:
                then(tracks)
                self._busy(f"{len(tracks)} TRACK{'S' if len(tracks) != 1 else ''} SENT")
            elif not err:
                self._busy("NO TRACKS HERE")
        self._busy("LOADING…")
        self._async(fetch, done)

    def _on_play_all(self, _b) -> None:
        self._gather_and(lambda ts: self.window.enqueue_tracks(
            ts, play_now=True, replace=True))

    def _on_add_selected(self, _b) -> None:
        self._gather_and(lambda ts: self.window.enqueue_tracks(ts))

    def _on_add_all(self, _b) -> None:
        self._gather_and(lambda ts: self.window.enqueue_tracks(ts),
                         use_selection=False)

    # ---- add: Plex PIN link -------------------------------------------
    def _on_add_plex(self, btn) -> None:
        pop = Gtk.Popover()
        pop.set_parent(btn)
        # the user is about to switch to a browser to approve the code —
        # an autohiding popover would close (and read as "flow gone") the
        # moment focus leaves the app
        pop.set_autohide(False)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        for m in ("top", "bottom", "start", "end"):
            getattr(box, f"set_margin_{m}")(8)
        box.append(Gtk.Label(label="Enter this code at plex.tv/link:",
                             xalign=0))
        code_lbl = Gtk.Label(label="····")
        # NOT eaa-bignum: DSEG7 is a 7-segment digit font and Plex codes are
        # alphanumeric — letters render as nonsense segments
        code_lbl.add_css_class("eaa-linkcode")
        code_lbl.set_selectable(True)
        box.append(code_lbl)
        link_btn = make_button("OPEN PLEX.TV/LINK")
        box.append(link_btn)
        status = Gtk.Label(label="requesting code…", xalign=0)
        status.add_css_class("eaa-ind")
        box.append(status)
        cancel_btn = make_button("CANCEL")
        box.append(cancel_btn)
        pop.set_child(box)
        pop.popup()

        cid = registry.client_id
        state = {"pin_id": None, "polls": 0, "polling": False, "done": False}
        cancel_btn.connect("clicked", lambda *_: (
            state.update(done=True), pop.popdown()))

        def show_code(res, err):
            if err or res is None:
                status.set_text(f"error: {err or 'no response'}")
                return
            pin_id, code = res
            state["pin_id"] = pin_id
            code_lbl.set_text(code)
            status.set_text("waiting for approval…")
            # the plex.tv/link page with the code prefilled: the deep-link
            # auto-auth page (app.plex.tv/auth#?…) proved flaky in testing
            url = f"https://plex.tv/link/?pin={code}"
            link_btn.connect("clicked", lambda *_: self._open_url(url))
            GLib.timeout_add(_PIN_POLL_MS, tick)

        def tick():
            # keep polling even if the popover somehow closed — the account
            # should appear regardless of whether the status is visible
            if state["done"] or state["pin_id"] is None:
                return False
            state["polls"] += 1
            if state["polls"] > _PIN_MAX_POLLS:
                status.set_text("code expired — reopen to retry")
                return False
            if state["polling"]:
                return True

            state["polling"] = True

            def poll():
                token = plex_mod.poll_pin(cid, state["pin_id"])
                if not token:
                    return None
                servers = plex_mod.discover_servers(cid, token)
                return token, servers

            def polled(res, err):
                state["polling"] = False
                if err:
                    status.set_text(f"error: {err}")
                    return
                if res is None:
                    return              # not approved yet; keep ticking
                _token, servers = res
                state["done"] = True
                if not servers:
                    status.set_text("linked, but no reachable server")
                    return
                from . import secrets_store
                for srv in servers:
                    acct = registry.add_account(
                        "plex", srv["name"], client_id=cid,
                        base_url=srv["base_url"],
                        machine_id=srv["machine_id"])
                    secrets_store.set(f"{acct['id']}:token", srv["token"])
                self._finish_plex(servers, status, pop)

            # bypass the browse gen guard: linking isn't navigation
            def worker():
                try:
                    res, err = poll(), None
                except SourceError as e:
                    res, err = None, str(e)
                except Exception as e:
                    res, err = None, f"{type(e).__name__}: {e}"
                GLib.idle_add(lambda: (polled(res, err), False)[1])
            threading.Thread(target=worker, daemon=True).start()
            return True

        def worker_start():
            try:
                res, err = plex_mod.start_pin(cid), None
            except SourceError as e:
                res, err = None, str(e)
            GLib.idle_add(lambda: (show_code(res, err), False)[1])
        threading.Thread(target=worker_start, daemon=True).start()

    def _finish_plex(self, servers, status, pop) -> None:
        status.set_text(f"linked ✓  ({len(servers)} server"
                        f"{'s' if len(servers) != 1 else ''})")
        self.refresh()
        GLib.timeout_add(1200, lambda: (pop.popdown(), False)[1])

    def _open_url(self, url: str) -> None:
        try:
            Gtk.show_uri(self.window, url, Gdk.CURRENT_TIME)
        except Exception:
            import webbrowser
            webbrowser.open(url)

    # ---- add: Jellyfin sign-in ----------------------------------------
    def _on_add_jellyfin(self, btn) -> None:
        pop = Gtk.Popover()
        pop.set_parent(btn)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        for m in ("top", "bottom", "start", "end"):
            getattr(box, f"set_margin_{m}")(8)
        url_e = Gtk.Entry(placeholder_text="http://server:8096")
        user_e = Gtk.Entry(placeholder_text="username")
        pass_e = Gtk.Entry(placeholder_text="password")
        pass_e.set_visibility(False)
        remember = Gtk.CheckButton(label="remember password (silent re-login)")
        status = Gtk.Label(label="", xalign=0)
        status.add_css_class("eaa-ind")
        add_btn = make_button("SIGN IN + ADD")
        for w in (url_e, user_e, pass_e, remember, add_btn, status):
            box.append(w)
        pop.set_child(box)

        def on_add(_b):
            base = url_e.get_text().strip().rstrip("/")
            user = user_e.get_text().strip()
            pw = pass_e.get_text()
            if not base or not user:
                status.set_text("server + username required")
                return
            if "://" not in base:
                base = f"http://{base}"
            status.set_text("signing in…")
            cid = registry.client_id

            def work():
                return jf_mod.authenticate(base, cid, user, pw)

            def done(res, err):
                if err or res is None:
                    status.set_text(f"failed: {err or 'no response'}")
                    return
                token, user_id = res
                acct = registry.add_account(
                    "jellyfin", f"{user}@{base.split('://', 1)[1]}",
                    client_id=cid, base_url=base, username=user,
                    user_id=user_id)
                from . import secrets_store
                secrets_store.set(f"{acct['id']}:token", token)
                if remember.get_active():
                    secrets_store.set(f"{acct['id']}:password", pw)
                status.set_text("added ✓")
                self.refresh()
                GLib.timeout_add(900, lambda: (pop.popdown(), False)[1])

            # sign-in isn't navigation: bypass the gen guard
            def worker():
                try:
                    res, err = work(), None
                except SourceError as e:
                    res, err = None, str(e)
                except Exception as e:
                    res, err = None, f"{type(e).__name__}: {e}"
                GLib.idle_add(lambda: (done(res, err), False)[1])
            threading.Thread(target=worker, daemon=True).start()

        add_btn.connect("clicked", on_add)
        pop.popup()
