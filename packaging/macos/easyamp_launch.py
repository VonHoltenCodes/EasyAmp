"""PyInstaller entry point for the macOS .app bundle."""
import sys

from easyamp.app import main

sys.exit(main())
