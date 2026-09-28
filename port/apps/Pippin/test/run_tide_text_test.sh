#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# run_tide_text_test.sh — the tide card's formatting, compiled for the mac.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/tide_text"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/tide_text_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/TideText.swift" "$WORK/TideText.swift"
cp "$SRC/Pippin/DistanceUnits.swift" "$WORK/DistanceUnits.swift"
xcrun swiftc -o "$WORK/tide_text_test" "$WORK/main.swift" "$WORK/TideText.swift" "$WORK/DistanceUnits.swift"
exec "$WORK/tide_text_test"
