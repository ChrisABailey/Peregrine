// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// place_link_test.swift — the share parser, tested off the phone.
//
// `PlaceLink` is Foundation-only for exactly this reason: the table below is
// real URLs copied out of real share sheets, and running it costs a `swift`
// invocation rather than a simulator.
//
//   swift port/apps/Pippin/test/place_link_test.swift \
//         port/apps/Pippin/Pippin/PlaceLink.swift
//
// or, since it is wired into ctest:  ctest --test-dir build -R place_link
//
// Nothing here touches the network. `PlaceLink.resolve` is the one part that
// does, and it is deliberately untested: it depends on somebody else's
// redirect chain, so a test over it would test Apple's servers and fail on a
// train. What is tested is `mayBeShortened`, which decides whether the
// network is reached at all, and the parse of every hop it would see.

import Foundation

var failures = 0
var checks = 0

func check(_ condition: Bool, _ what: String,
           file: StaticString = #file, line: UInt = #line) {
    checks += 1
    if !condition {
        failures += 1
        print("FAIL (line \(line)): \(what)")
    }
}

/// A place parsed out of `source`, checked to about a metre.
func expectPlace(_ source: String,
                 lat: Double, lon: Double,
                 name: String? = nil,
                 address: String? = nil,
                 line: UInt = #line) {
    let place: SharedPlace?
    if let url = URL(string: source), url.scheme != nil {
        place = PlaceLink.place(in: url) ?? PlaceLink.place(in: source)
    } else {
        place = PlaceLink.place(in: source)
    }
    guard let place else {
        check(false, "no place parsed from \(source)", line: line)
        return
    }
    check(abs(place.latitude - lat) < 1e-5,
          "latitude \(place.latitude) != \(lat) for \(source)", line: line)
    check(abs(place.longitude - lon) < 1e-5,
          "longitude \(place.longitude) != \(lon) for \(source)", line: line)
    if let name {
        check(place.name == name,
              "name \"\(place.name)\" != \"\(name)\" for \(source)", line: line)
    }
    if let address {
        check(place.address == address,
              "address \"\(place.address)\" != \"\(address)\" for \(source)", line: line)
    }
}

func expectNothing(_ source: String, line: UInt = #line) {
    let place: SharedPlace?
    if let url = URL(string: source), url.scheme != nil {
        place = PlaceLink.place(in: url)
    } else {
        place = PlaceLink.place(in: source)
    }
    check(place == nil, "expected no place from \(source), got \(place as Any)", line: line)
}

// MARK: - Apple Maps

// The modern share, which is the case this whole feature exists for: the
// coordinate and the postal address both arrive, offline, exact.
expectPlace(
    "https://maps.apple.com/place?address=1%20Apple%20Park%20Way,%20Cupertino,%20CA%20%2095014,%20United%20States&coordinate=37.334859,-122.009040&name=Apple%20Park&place-id=I7C250D2CDCB364A&map=explore",
    lat: 37.334859, lon: -122.009040,
    name: "Apple Park",
    address: "1 Apple Park Way, Cupertino, CA  95014, United States")

// The older spelling, still what a dropped pin produces.
expectPlace("https://maps.apple.com/?ll=32.60841,-80.07213&q=Beachwalker%20Park",
            lat: 32.60841, lon: -80.07213, name: "Beachwalker Park")

// `q` alone, carrying the coordinate rather than a name.
expectPlace("https://maps.apple.com/?q=32.60841,-80.07213",
            lat: 32.60841, lon: -80.07213, name: "")

// The `maps:` scheme, which is what Pippin's own "Open in Maps" writes — so
// this is the round trip of `PointShare.mapsAppURL`.
expectPlace("maps://?ll=32.60841,-80.07213&q=Rhett%27s%20Bluff",
            lat: 32.60841, lon: -80.07213, name: "Rhett's Bluff")

// iOS 26's short link. NO coordinate, and it must parse as nothing rather
// than as something — this is the case that has to reach the network.
expectNothing("https://maps.apple/p/U8rE9v8n8iVZjr")
check(PlaceLink.mayBeShortened(URL(string: "https://maps.apple/p/U8rE9v8n8iVZjr")!),
      "an Apple short link should be worth resolving")
check(!PlaceLink.mayBeShortened(
        URL(string: "https://maps.apple.com/?ll=32.60841,-80.07213")!),
      "a link that already parsed must not go to the network")

// MARK: - Google Maps

// The desktop link. TWO pairs are in it and the `!3d/!4d` one is the place;
// the `@` one is where the camera was. Getting this backwards is a marker in
// the wrong spot, so it is the single most important case in the file.
expectPlace(
    "https://www.google.com/maps/place/Beachwalker+Park/@32.6051234,-80.0777,17z/data=!3m1!4b1!4m6!3m5!1s0x88fbf1234:0xabc!8m2!3d32.60841!4d-80.07213!16s%2Fg%2F1td",
    lat: 32.60841, lon: -80.07213, name: "Beachwalker Park")

// A link with only the camera pair in it: the fallback fires.
expectPlace("https://www.google.com/maps/@32.60841,-80.07213,15z",
            lat: 32.60841, lon: -80.07213)

// The query form the app writes when you share a dropped pin.
expectPlace("https://www.google.com/maps/search/?api=1&query=32.60841%2C-80.07213",
            lat: 32.60841, lon: -80.07213)

// The short link, which carries nothing at all.
expectNothing("https://maps.app.goo.gl/aBcDeFgHiJkLmN")

// Google's OTHER share: a string with the name on one line and the link on
// the next. The name comes out of the PROSE, because the link carries none —
// which only matters once the link itself has been resolved, so the test is
// over the long form the resolver hands back.
expectPlace("Beachwalker Park\nhttps://www.google.com/maps/@32.60841,-80.07213,15z\n",
            lat: 32.60841, lon: -80.07213, name: "Beachwalker Park")
check(PlaceLink.place(in: "Beachwalker Park\nhttps://maps.app.goo.gl/x") == nil,
      "a short link inside prose still has no coordinate to give")

// MARK: - geo:, vCards and bare pairs

expectPlace("geo:32.60841,-80.07213", lat: 32.60841, lon: -80.07213)
expectPlace("geo:0,0?q=32.60841,-80.07213(Beachwalker%20Park)",
            lat: 32.60841, lon: -80.07213, name: "Beachwalker Park")
expectPlace("geo:32.60841,-80.07213?q=32.60841,-80.07213(Rhett%27s%20Bluff)",
            lat: 32.60841, lon: -80.07213, name: "Rhett's Bluff")

expectPlace("32.60841, -80.07213", lat: 32.60841, lon: -80.07213)
expectPlace("32.60841° N, 80.07213° W", lat: 32.60841, lon: -80.07213)

let vcard = """
BEGIN:VCARD
VERSION:3.0
FN:Beachwalker Park
ADR;TYPE=WORK:;;8 Beachwalker Dr;Kiawah Island;SC;29455;USA
GEO:32.60841;-80.07213
END:VCARD
"""
expectPlace(vcard, lat: 32.60841, lon: -80.07213,
            name: "Beachwalker Park",
            address: "8 Beachwalker Dr, Kiawah Island, SC, 29455, USA")

// MARK: - What must NOT parse

// Apple's own spelling of "I have no coordinate, search for the words".
expectNothing("https://maps.apple.com/?q=0,0")
expectNothing("https://maps.apple.com/?q=Pizza")
// Out of range, which is a corrupt link and not a place at the pole.
expectNothing("https://maps.apple.com/?ll=132.6,-80.07")
// A plain web page. The feature must be silent about things that are not
// places, or every shared link would drop a marker.
expectNothing("https://www.kiawahresort.com/dining")
// Three numbers that are not a coordinate and a zoom.
expectNothing("https://example.com/?q=1,2,3")

// MARK: - The handoff round trip

let original = SharedPlace(latitude: 32.60841, longitude: -80.07213,
                           name: "Beachwalker Park",
                           address: "8 Beachwalker Dr, Kiawah Island, SC")!
if let callback = PlaceLink.callbackURL(for: original),
   let returned = PlaceLink.place(inCallback: callback) {
    check(abs(returned.latitude - original.latitude) < 1e-7, "callback latitude")
    check(abs(returned.longitude - original.longitude) < 1e-7, "callback longitude")
    check(returned.name == original.name, "callback name")
    check(returned.address == original.address, "callback address")
} else {
    check(false, "the callback URL did not round trip")
}
// Somebody else's scheme is not ours, whatever it says.
check(PlaceLink.place(inCallback: URL(string: "other://place?lat=1&lon=2")!) == nil,
      "a foreign scheme must not be accepted as a callback")

// MARK: - The fallback name

check(SharedPlace(latitude: 1, longitude: 1)!.displayName == "Shared place",
      "a nameless place still has something to call itself")
check(SharedPlace(latitude: 1, longitude: 1,
                  address: "8 Beachwalker Dr, Kiawah Island, SC")!.displayName
        == "8 Beachwalker Dr",
      "a nameless place with an address is called by its street")

print("place_link_test: \(checks - failures)/\(checks) checks passed")
exit(failures == 0 ? 0 : 1)
