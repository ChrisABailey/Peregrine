// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// energy_log_test.swift — the energy log's rows and interval arithmetic,
// tested off the phone.
//
//   swift port/apps/Pippin/test/energy_log_test.swift \
//         port/apps/Pippin/Pippin/EnergyLog.swift
//
// or, since it is wired into ctest:  ctest --test-dir build -R energy_log

import Foundation

var failures = 0
var checks = 0

func check(_ condition: Bool, _ what: String, line: UInt = #line) {
    checks += 1
    if !condition {
        failures += 1
        print("FAIL (line \(line)): \(what)")
    }
}

// MARK: - Mode

check(EnergyMode(following: false, recording: false) == nil, "idle has no mode")
check(EnergyMode(following: true, recording: false) == .following, "following")
check(EnergyMode(following: false, recording: true) == .recording, "recording")
check(EnergyMode(following: true, recording: true) == .both, "both")

// MARK: - Meter

var meter = EnergyMeter()
let first = EnergyCounters(frames: 100, baseDraws: 10, cacheHits: 90,
                           fixes: 50, cpuSeconds: 12.5)
check(meter.take(first) == EnergyCounters(), "first reading is the baseline, all zero")

let second = EnergyCounters(frames: 1300, baseDraws: 140, cacheHits: 1160,
                            fixes: 110, cpuSeconds: 20.0)
let delta = meter.take(second)
check(delta.frames == 1200, "frames differenced")
check(delta.baseDraws == 130, "base draws differenced")
check(delta.cacheHits == 1070, "cache hits differenced")
check(delta.fixes == 60, "fixes differenced")
check(abs(delta.cpuSeconds - 7.5) < 1e-9, "cpu differenced")
check(delta.frames == delta.baseDraws + delta.cacheHits,
      "frames still equal base draws plus hits after differencing")

// A reading equal to the last one is an idle interval, not a reset.
check(meter.take(second) == EnergyCounters(), "no change gives zeros")

// MARK: - CPU clock

let cpuA = EnergyCounters.processCPUSeconds()
var sink = 0.0
for i in 0..<2_000_000 { sink += sqrt(Double(i)) }
let cpuB = EnergyCounters.processCPUSeconds()
check(cpuA > 0, "process CPU time is readable")
check(cpuB > cpuA, "process CPU time advances with work (\(cpuA) → \(cpuB), \(sink > 0))")

// MARK: - Row

let t = Date(timeIntervalSince1970: 1_790_000_000)  // 2026-09-21T14:13:20Z
let row = EnergySample(time: t, mode: .following, batteryLevel: 0.85,
                       batteryState: "unplugged", thermalState: .fair,
                       interval: delta)
check(row.csvLine == "2026-09-21T14:13:20Z,following,0.85,unplugged,fair,1200,130,1070,60,7.500",
      "row: \(row.csvLine)")
check(row.csvLine.split(separator: ",", omittingEmptySubsequences: false).count
      == EnergySample.header.split(separator: ",").count,
      "row and header have the same number of columns")

var unknown = row
unknown.batteryLevel = -1
unknown.batteryState = "unknown"
unknown.thermalState = .critical
check(unknown.csvLine.hasPrefix("2026-09-21T14:13:20Z,following,,unknown,critical,"),
      "unknown battery level is an empty field: \(unknown.csvLine)")

check(EnergySample.header
      == "time,mode,battery_level,battery_state,thermal_state,frames,base_draws,cache_hits,fixes,cpu_s",
      "header")

print("\(checks - failures)/\(checks) checks passed")
exit(failures == 0 ? 0 : 1)
