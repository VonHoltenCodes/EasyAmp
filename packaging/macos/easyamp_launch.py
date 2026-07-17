"""PyInstaller entry point for the macOS .app bundle."""
import os
import sys


def _log_to_file():
    """Mirror stdout/stderr to ~/Library/Logs/EasyAmp.log when there is no
    terminal (Finder launch). PyGObject prints exceptions raised in signal
    handlers to stderr and keeps running, so without this a startup failure
    is completely invisible. Terminal runs keep their normal output."""
    try:
        if os.isatty(2):
            return
    except OSError:
        pass
    try:
        path = os.path.expanduser("~/Library/Logs/EasyAmp.log")
        if os.path.exists(path) and os.path.getsize(path) > 1_000_000:
            os.remove(path)
        log = open(path, "a", buffering=1, encoding="utf-8", errors="replace")
        os.dup2(log.fileno(), 1)
        os.dup2(log.fileno(), 2)
        sys.stdout = sys.stderr = log
        import datetime
        print(f"\n=== EasyAmp launch {datetime.datetime.now().isoformat()} ===")
    except Exception:
        pass  # logging is best-effort; never block the app


_log_to_file()

from easyamp.app import main  # noqa: E402

sys.exit(main())
