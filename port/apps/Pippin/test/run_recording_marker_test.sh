#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# run_recording_marker_test.sh — the interrupted-ride marker, compiled and run on the mac.
#
# The same three lines as `run_place_link_test.sh` and for the same reason:
# `RecordingMarker` is Swift, the rest of this directory's tests are C++, and there
# is no gtest to hang it off. The marker logic is Foundation-only so that this works
# — copied to `main.swift` because swiftc allows top-level code in that name
# and no other.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/recording_marker"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/recording_marker_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/RecordingMarker.swift" "$WORK/RecordingMarker.swift"
xcrun swiftc -o "$WORK/recording_marker_test" "$WORK/main.swift" "$WORK/RecordingMarker.swift"
exec "$WORK/recording_marker_test"
