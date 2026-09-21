#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# run_guidance_format_test.sh — the turn countdown's units (GD3).
#
# `DistanceUnits` is Swift, so it gets `run_place_link_test.sh`'s treatment:
# the type is Foundation-only precisely so it compiles and runs on the mac,
# and this is the three lines that do it.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/guidance_format"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/guidance_format_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/DistanceUnits.swift" "$WORK/DistanceUnits.swift"
xcrun swiftc -o "$WORK/guidance_format_test" "$WORK/main.swift" "$WORK/DistanceUnits.swift"
exec "$WORK/guidance_format_test"
