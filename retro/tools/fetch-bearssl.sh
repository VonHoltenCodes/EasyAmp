#!/bin/sh
# BearSSL is fetched at build time, pinned to one commit, rather than vendored
# (294 source files). Used only for the small HTTPS calls that link a Plex
# account: plex.tv requires TLS 1.2, which neither Windows 98 nor XP can do.
set -e
COMMIT=7bea48e5e850ab4cafbe68d3765cdaba13a86d6f
DIR="$(dirname "$0")/../third_party/bearssl"
[ -f "$DIR/inc/bearssl.h" ] && exit 0
rm -rf "$DIR"
git clone -q https://www.bearssl.org/git/BearSSL "$DIR"
git -C "$DIR" checkout -q "$COMMIT"
rm -rf "$DIR/.git"
