#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
#
# run_energy_log_test.sh — the energy log's rows compiled and run on the mac,
# the same way as `run_render_gate_test.sh`.
#
#   $1  the Pippin source directory      $2  a directory to build in
set -e
SRC="$1"
WORK="$2/energy_log"
rm -rf "$WORK"
mkdir -p "$WORK"
cp "$SRC/test/energy_log_test.swift" "$WORK/main.swift"
cp "$SRC/Pippin/EnergyLog.swift" "$WORK/EnergyLog.swift"
xcrun swiftc -o "$WORK/energy_log_test" "$WORK/main.swift" "$WORK/EnergyLog.swift"
exec "$WORK/energy_log_test"
