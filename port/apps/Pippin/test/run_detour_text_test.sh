#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# run_detour_text_test.sh — the detour button's wording, on the mac.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/detour_text"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/detour_text_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/DetourText.swift" "$WORK/DetourText.swift"
cp "$SRC/Pippin/DistanceUnits.swift" "$WORK/DistanceUnits.swift"
xcrun swiftc -o "$WORK/detour_text_test" "$WORK/main.swift" "$WORK/DetourText.swift" \
  "$WORK/DistanceUnits.swift"
exec "$WORK/detour_text_test"
