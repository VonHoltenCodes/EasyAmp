#!/bin/bash
# Launch EasyAmp from the project directory (no install needed).
cd "$(dirname "$0")" && exec python3 -m easyamp "$@"
