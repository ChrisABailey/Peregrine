#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# run_guidance_notice_test.sh — guidance events to notification changes.
#
# Both types are Foundation-only so they compile and run on the mac.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/guidance_notice"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/guidance_notice_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/DistanceUnits.swift" "$WORK/DistanceUnits.swift"
cp "$SRC/Pippin/GuidanceNotice.swift" "$WORK/GuidanceNotice.swift"
xcrun swiftc -o "$WORK/guidance_notice_test" "$WORK/main.swift" "$WORK/DistanceUnits.swift" "$WORK/GuidanceNotice.swift"
exec "$WORK/guidance_notice_test"
