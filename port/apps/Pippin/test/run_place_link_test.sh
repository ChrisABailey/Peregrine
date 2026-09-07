#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# run_place_link_test.sh — the share parser's gtest stand-in (P14).
#
# `PlaceLink` is Swift and the rest of this directory's tests are C++, so
# there is no gtest to hang it off. What there IS, is a compiler: the parser
# is Foundation-only precisely so it builds and runs on the mac, and this
# script is the three lines that do it — copied to `main.swift` because
# swiftc allows top-level code in that name and no other.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/place_link"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/place_link_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/PlaceLink.swift" "$WORK/PlaceLink.swift"
xcrun swiftc -o "$WORK/place_link_test" "$WORK/main.swift" "$WORK/PlaceLink.swift"
exec "$WORK/place_link_test"
