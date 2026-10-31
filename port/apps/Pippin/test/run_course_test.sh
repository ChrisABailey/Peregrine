#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# Builds and runs course_test.swift against the Foundation-only Course.swift and DistanceUnits.swift.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/course"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/course_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/Course.swift" "$WORK/"
cp "$SRC/Pippin/DistanceUnits.swift" "$WORK/"
xcrun swiftc -o "$WORK/course_test" "$WORK/main.swift" "$WORK/Course.swift" "$WORK/DistanceUnits.swift"
exec "$WORK/course_test"
