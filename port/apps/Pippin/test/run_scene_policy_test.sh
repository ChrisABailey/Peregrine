#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# run_scene_policy_test.sh — the scene-phase drawing gate, compiled and run on the mac.
#
# The same three lines as `run_place_link_test.sh` and for the same reason:
# `ScenePolicy` is Swift, the rest of this directory's tests are C++, and there
# is no gtest to hang it off. The policy is Foundation-only so that this works
# — copied to `main.swift` because swiftc allows top-level code in that name
# and no other.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/scene_policy"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/scene_policy_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/ScenePolicy.swift" "$WORK/ScenePolicy.swift"
xcrun swiftc -o "$WORK/scene_policy_test" "$WORK/main.swift" "$WORK/ScenePolicy.swift"
exec "$WORK/scene_policy_test"
