#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# run_replan_notice_test.sh — the re-plan notice's wording, on the mac.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/replan_notice"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/replan_notice_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/ReplanNotice.swift" "$WORK/ReplanNotice.swift"
cp "$SRC/Pippin/DistanceUnits.swift" "$WORK/DistanceUnits.swift"
xcrun swiftc -o "$WORK/replan_notice_test" "$WORK/main.swift" "$WORK/ReplanNotice.swift" \
  "$WORK/DistanceUnits.swift"
exec "$WORK/replan_notice_test"
