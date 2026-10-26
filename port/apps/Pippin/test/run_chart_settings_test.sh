#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# Builds and runs chart_settings_test.swift against the Foundation-only ChartSettings.swift.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/chart_settings"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/chart_settings_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/ChartSettings.swift" "$WORK/"
xcrun swiftc -o "$WORK/chart_settings_test" "$WORK/main.swift" "$WORK/ChartSettings.swift"
exec "$WORK/chart_settings_test"
