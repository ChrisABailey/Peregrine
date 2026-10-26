// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// course_test.swift — course text and the COG vector end point, tested on the
// mac (`ctest -R pippin_course`).

import Foundation

var failures = 0

func expect<T: Equatable>(_ got: T, _ want: T, _ what: String) {
    if got != want {
        failures += 1
        print("FAIL \(what): got \(got), want \(want)")
    }
}

func expectNear(_ got: Double, _ want: Double, _ tol: Double, _ what: String) {
    if !(abs(got - want) <= tol) {
        failures += 1
        print("FAIL \(what): got \(got), want \(want) ± \(tol)")
    }
}

// Course text: three digits, wrapped into [0, 360).
expect(Course.text(47.2), "047°T", "pads to three digits")
expect(Course.text(359.6), "000°T", "rounds up through north")
expect(Course.text(-10), "350°T", "negative wraps")
expect(Course.text(720 + 5), "005°T", "multiple turns wrap")

let knot = 1852.0 / 3600.0
let degPerNm = 1852.0 / (6_371_008.8 * .pi / 180)

// 10 kn for six minutes is one nautical mile.
if let north = Course.vectorEnd(latitude: 32.75, longitude: -79.9, courseDegrees: 0,
                                speedMetersPerSecond: 10 * knot) {
    expectNear(north.latitude, 32.75 + degPerNm, 1e-9, "north: one nm of latitude")
    expectNear(north.longitude, -79.9, 1e-12, "north: no longitude change")
} else { failures += 1; print("FAIL north vector missing") }

// Due east at 32.75° N: one nm of longitude is 1/cos(lat) of a latitude nm.
if let east = Course.vectorEnd(latitude: 32.75, longitude: -79.9, courseDegrees: 90,
                               speedMetersPerSecond: 10 * knot) {
    expectNear(east.latitude, 32.75, 1e-9, "east: latitude unchanged")
    expectNear(east.longitude, -79.9 + degPerNm / cos(32.75 * .pi / 180), 1e-9,
               "east: departure over cos(lat)")
} else { failures += 1; print("FAIL east vector missing") }

// South-west: both components negative and the rhumb distance preserved.
if let sw = Course.vectorEnd(latitude: 25.0, longitude: -80.5, courseDegrees: 225,
                             speedMetersPerSecond: 20 * knot) {
    let dLat = (sw.latitude - 25.0) / degPerNm
    expectNear(dLat, -2.0 * cos(.pi / 4), 1e-6, "south-west: northing")
    if !(sw.longitude < -80.5) { failures += 1; print("FAIL south-west: not west") }
} else { failures += 1; print("FAIL south-west vector missing") }

// Across the antimeridian the longitude wraps into [-180, 180).
if let wrap = Course.vectorEnd(latitude: 0, longitude: 179.99, courseDegrees: 90,
                               speedMetersPerSecond: 10 * knot) {
    expectNear(wrap.longitude, 179.99 + degPerNm - 360, 1e-9, "antimeridian wrap")
} else { failures += 1; print("FAIL antimeridian vector missing") }

// Below half a knot, or with a non-finite course, there is no vector.
expect(Course.vectorEnd(latitude: 32.75, longitude: -79.9, courseDegrees: 10,
                        speedMetersPerSecond: 0.4 * knot) == nil, true, "too slow")
expect(Course.vectorEnd(latitude: 32.75, longitude: -79.9, courseDegrees: .nan,
                        speedMetersPerSecond: 5) == nil, true, "nan course")
expect(Course.vectorEnd(latitude: 32.75, longitude: -79.9, courseDegrees: 10,
                        speedMetersPerSecond: 0.5 * knot) != nil, true, "half a knot draws")

// CDI needle: the course line, opposite the boat's side, clamped at full scale.
expectNear(Course.needle(crossTrackMeters: 92.6, limitMeters: 185.2), -0.5, 1e-12, "right of track: needle left")
expectNear(Course.needle(crossTrackMeters: -46.3, limitMeters: 185.2), 0.25, 1e-12, "left of track: needle right")
expect(Course.needle(crossTrackMeters: 1000, limitMeters: 185.2), -1, "clamped full left")
expect(Course.needle(crossTrackMeters: 10, limitMeters: 0), 0, "no limit is centred")

// Cross-track text: two decimals in nm with the side.
expect(Course.crossTrackText(74.08, units: .nauticalMiles), "0.04 nm R", "nm right")
expect(Course.crossTrackText(-370.4, units: .nauticalMiles), "0.20 nm L", "nm left")
expect(Course.crossTrackText(0.2, units: .nauticalMiles), "0.00 nm", "on the line has no side")
expect(Course.crossTrackText(-120, units: .kilometres), "120 m L", "metric")

// Steer: inside a tenth of full scale is on track.
expect(Course.steerText(crossTrackMeters: 15, limitMeters: 185.2), "On track", "on track")
expect(Course.steerText(crossTrackMeters: 50, limitMeters: 185.2), "Steer left", "right of track")
expect(Course.steerText(crossTrackMeters: -50, limitMeters: 185.2), "Steer right", "left of track")

// Time to go.
expect(Course.timeToGoText(30), "<1 min", "under a minute")
expect(Course.timeToGoText(25 * 60 + 10), "25 min", "minutes")
expect(Course.timeToGoText(2 * 3600 + 5 * 60), "2 h 05 min", "hours")
expect(Course.timeToGoText(.infinity), "–", "no estimate")

if failures > 0 {
    print("\(failures) failure(s)")
    exit(1)
}
print("course: all passed")
