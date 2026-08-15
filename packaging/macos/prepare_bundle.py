#!/usr/bin/env python3
"""Normalize PyInstaller's app layout for Apple notarization checks."""

from __future__ import annotations

import argparse
from pathlib import Path


def resource_framework_links(app: Path) -> list[Path]:
    """Return Resources symlinks that point back into Frameworks."""
    resources = app / "Contents" / "Resources"
    frameworks = app / "Contents" / "Frameworks"
    if not resources.is_dir() or not frameworks.is_dir():
        raise ValueError(f"app is missing Resources or Frameworks: {app}")

    frameworks_root = frameworks.resolve()
    links = []
    for path in resources.rglob("*"):
        if not path.is_symlink():
            continue
        try:
            path.resolve(strict=False).relative_to(frameworks_root)
        except ValueError:
            continue
        links.append(path)
    return links


def prepare_bundle(app: Path) -> list[Path]:
    """Remove PyInstaller reverse cross-links that Apple treats as code in Resources."""
    links = resource_framework_links(app)
    for path in links:
        path.unlink()
    return links


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    args = parser.parse_args()
    removed = prepare_bundle(args.app)
    print(f"Removed {len(removed)} Resources-to-Frameworks compatibility links.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
