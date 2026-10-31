#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# Builds and runs chart_regions_test.swift against the Foundation-only ChartRegions.swift.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/chart_regions"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/chart_regions_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/ChartRegions.swift" "$WORK/"
xcrun swiftc -o "$WORK/chart_regions_test" "$WORK/main.swift" "$WORK/ChartRegions.swift"
exec "$WORK/chart_regions_test"
