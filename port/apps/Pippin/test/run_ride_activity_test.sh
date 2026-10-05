#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# run_ride_activity_test.sh — Live Activity content and update throttling.
#
# Both types are Foundation-only so they compile and run on the mac.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/ride_activity"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/ride_activity_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/DistanceUnits.swift" "$WORK/DistanceUnits.swift"
cp "$SRC/Pippin/GuidanceNotice.swift" "$WORK/GuidanceNotice.swift"
cp "$SRC/Pippin/RideActivityContent.swift" "$WORK/RideActivityContent.swift"
cp "$SRC/Pippin/RideActivityPolicy.swift" "$WORK/RideActivityPolicy.swift"
xcrun swiftc -o "$WORK/ride_activity_test" "$WORK/main.swift" "$WORK/DistanceUnits.swift" "$WORK/GuidanceNotice.swift" "$WORK/RideActivityContent.swift" "$WORK/RideActivityPolicy.swift"
exec "$WORK/ride_activity_test"
