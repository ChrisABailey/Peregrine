#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# run_frame_policy_test.sh — FramePolicy compiled and run on the mac, the same
# way as run_render_gate_test.sh.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/frame_policy"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/frame_policy_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/FramePolicy.swift" "$WORK/FramePolicy.swift"
xcrun swiftc -o "$WORK/frame_policy_test" "$WORK/main.swift" "$WORK/FramePolicy.swift"
exec "$WORK/frame_policy_test"
