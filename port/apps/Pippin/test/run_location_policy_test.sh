#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# run_location_policy_test.sh — P17's accuracy policy compiled and run on the mac.
#
# The same three lines as `run_render_gate_test.sh` and for the same reason:
# `LocationPolicy` is Swift, the rest of this directory's tests are C++, and
# there is no gtest to hang it off. The policy is CoreGraphics-only so that
# this works — copied to `main.swift` because swiftc allows top-level code in
# that name and no other.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/location_policy"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/location_policy_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/LocationPolicy.swift" "$WORK/LocationPolicy.swift"
xcrun swiftc -o "$WORK/location_policy_test" "$WORK/main.swift" "$WORK/LocationPolicy.swift"
exec "$WORK/location_policy_test"
