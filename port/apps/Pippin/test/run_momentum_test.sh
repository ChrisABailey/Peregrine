#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# run_momentum_test.sh — Momentum compiled and run on the mac, the same
# way as run_frame_policy_test.sh.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/momentum"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/momentum_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/Momentum.swift" "$WORK/Momentum.swift"
xcrun swiftc -o "$WORK/momentum_test" "$WORK/main.swift" "$WORK/Momentum.swift"
exec "$WORK/momentum_test"
