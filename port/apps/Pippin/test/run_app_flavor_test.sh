#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# Builds and runs app_flavor_test.swift against the Foundation-only
# AppFlavor.swift and the DistanceUnits.swift it names.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/app_flavor"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/app_flavor_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/AppFlavor.swift" "$SRC/Pippin/DistanceUnits.swift" "$WORK/"
xcrun swiftc -o "$WORK/app_flavor_test" "$WORK/main.swift" \
    "$WORK/AppFlavor.swift" "$WORK/DistanceUnits.swift"
exec "$WORK/app_flavor_test"
