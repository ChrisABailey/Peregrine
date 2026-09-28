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
// Nothing here touches the network. `PlaceLink.resolve` fetches; its decision
// over what came back is `PlaceLink.classify`, which is tested here against
// captured redirect chains replayed as plain URLs. So is `mayBeShortened`,
// which decides whether the network is reached at all.

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

// MARK: - The resolve decision

let original0 = SharedPlace(latitude: 32.60841, longitude: -80.07213)!

let named = URL(string: "https://maps.google.com/?q=Kiawah+Island+Golf+Resort,+1+Sanctuary+Beach+Dr,+Kiawah+Island,+SC+29455")!
// Captured from the mac 2026-09-25 with `resolve`'s user agent: two hops, no
// coordinate in either, and a 200 page whose search runs in JavaScript.
let namedHops = [
    URL(string: "https://maps.google.com/maps?q=Kiawah+Island+Golf+Resort,+1+Sanctuary+Beach+Dr,+Kiawah+Island,+SC+29455")!,
    URL(string: "https://www.google.com/maps?q=Kiawah+Island+Golf+Resort,+1+Sanctuary+Beach+Dr,+Kiawah+Island,+SC+29455")!,
]
// The body of that page carries the REQUESTER's IP location (north Georgia
// for the probe), in both shapes below. Neither may become the place.
let ipLocationBody = """
<meta content="https://maps.google.com/maps/api/staticmap?center=34.1384%2C-84.2367&amp;zoom=12">
<script>window.APP_INITIALIZATION_STATE=[[[123456.7,-84.2367,34.1384],[0,0,0],[1024,768],13.1]];</script>
<a href="/maps/@34.1384,-84.2367,12z">
"""
check(PlaceLink.classify(start: named, hops: namedHops, found: nil,
                         body: ipLocationBody, failure: nil)
        == .noCoordinate(lastHop: namedHops[1],
                         query: "Kiawah Island Golf Resort, 1 Sanctuary Beach Dr, Kiawah Island, SC 29455"),
      "a named Google place is no coordinate plus its query, whatever the body says")

// A real Google short link, followed 2026-09-25 with `PlaceLink.userAgent`:
// the first hop is the place, and the `!3d/!4d` pair wins over the camera.
let saltCreekHop = URL(string: "https://www.google.com/maps/place/Salt+Creek+Beach+Bluff+Park/@33.4767032,-117.7229774,17z/data=!3m1!4b1!4m6!3m5!1s0x80dcf1f4fb6b2b4d:0x4e5aa3eaf342a120!8m2!3d33.4766988!4d-117.7204025!16s%2Fg%2F11pl801bt9?authuser=0&entry=tts&g_ep=EgoyMDI1MDYwMS4wIPu8ASoASAFQAw%3D%3D&skid=1287a7ca-f5b7-4b7b-a4a6-dcdefdd8555d")!
if case .place(let p) = PlaceLink.classify(start: URL(string: "https://maps.app.goo.gl/qyPwDBM8KQb32MV89")!,
                                           hops: [saltCreekHop], found: nil,
                                           body: nil, failure: .cancelled) {
    check(abs(p.latitude - 33.4766988) < 1e-7 && abs(p.longitude + 117.7204025) < 1e-7,
          "the Google hop's place pair, not its camera")
    check(p.name == "Salt Creek Beach Bluff Park", "the Google hop's name")
} else {
    check(false, "a real Google short-link hop should resolve to a place")
}
// Chris's dropped pin on Kiawah, shared from Google Maps on the phone
// 2026-09-25: the first hop's `q=` is the coordinate.
let pinShare = URL(string: "https://maps.app.goo.gl/VRGAiFK5bhKpWrfUA?g_st=ic")!
check(PlaceLink.mayBeShortened(pinShare), "the pin's short link is worth resolving")
let pinFirstHop = URL(string: "https://maps.google.com?q=32.5869379,-80.1306942&entry=gps&shh=CAE&g_ep=CAISEjI2LjM4LjEuOTgwODE1NDQ1MBgAIIgnKmcsOTQyOTc2OTksOTQyMzExODgsOTQyODA1NjgsMTAwODIxNTU5LDQ3MDcxNzA0LDk0MjE4NjQxLDk0MjgyMTM0LDEwMDgzNTY5NCw5NDI4Njg2OSwxMDA4MjAyNDcsMTAwODIyNTA0QgJVUw%3D%3D&skid=1b613ce9-5bf4-474b-a673-5363a41fa430&g_st=ic&g_st=ic")!
if case .place(let p) = PlaceLink.classify(start: pinShare, hops: [pinFirstHop], found: nil,
                                           body: nil, failure: .cancelled) {
    check(abs(p.latitude - 32.5869379) < 1e-7 && abs(p.longitude + 80.1306942) < 1e-7,
          "the dropped pin lands exactly")
    check(!p.isApproximate, "a dropped pin is exact")
} else {
    check(false, "the dropped pin's first hop should resolve to a place")
}

// Chris's share of Beachwalker Park picked from a Google search, 2026-09-25:
// three hops, a name, an address and an `ftid`, and no coordinate.
let searchedShare = URL(string: "https://maps.app.goo.gl/kQhNJDGkrNNGkKPV6?g_st=ic")!
let searchedHops = [
    "https://maps.google.com?q=Kiawah+Beachwalker+Park+Parking+Lot,+8+Beachwalker+Dr,+Kiawah+Island,+SC+29455&ftid=0x88fc2dc755f6d78d:0x3600bdeb7cb8b49a&entry=gps&shh=CAE&skid=bb674181-5a85-4fb2-9924-18c40263a052&g_st=ic&g_st=ic",
    "https://maps.google.com/maps?q=Kiawah+Beachwalker+Park+Parking+Lot,+8+Beachwalker+Dr,+Kiawah+Island,+SC+29455&ftid=0x88fc2dc755f6d78d:0x3600bdeb7cb8b49a&entry=gps&shh=CAE&skid=bb674181-5a85-4fb2-9924-18c40263a052&g_st=ic&g_st=ic",
    "https://www.google.com/maps?q=Kiawah+Beachwalker+Park+Parking+Lot,+8+Beachwalker+Dr,+Kiawah+Island,+SC+29455&ftid=0x88fc2dc755f6d78d:0x3600bdeb7cb8b49a&entry=gps&shh=CAE&skid=bb674181-5a85-4fb2-9924-18c40263a052&g_st=ic&g_st=ic",
].map { URL(string: $0)! }
let searchedQuery = "Kiawah Beachwalker Park Parking Lot, 8 Beachwalker Dr, Kiawah Island, SC 29455"
check(PlaceLink.classify(start: searchedShare, hops: searchedHops, found: nil,
                         body: nil, failure: nil)
        == .noCoordinate(lastHop: searchedHops[2], query: searchedQuery),
      "a place picked from a Google search is a name for Apple's search")
check(PlaceLink.namePart(of: searchedQuery) == "Kiawah Beachwalker Park Parking Lot",
      "the marker name is the query's name part")
check(PlaceLink.namePart(of: "Beachwalker Park") == "Beachwalker Park", "no comma, all name")
// What Apple's address search returned for it from the mac (40 m from the pin).
check(PlaceLink.searchResultMatches(query: searchedQuery, name: "8 Beachwalker Dr",
                                    address: "8 Beachwalker Dr, Johns Island, SC  29455, United States"),
      "Apple's address result for the searched share is accepted")

// A pin dropped on the beach, away from any road: no address, but the
// first hop still carries the coordinate.
let beachHop = URL(string: "https://maps.google.com?q=32.5876340,-80.1273267&entry=gps&shh=CAE&g_st=ic&g_st=ic")!
if case .place(let p) = PlaceLink.classify(start: URL(string: "https://maps.app.goo.gl/sdk6UqCRuxJikyHB7?g_st=ic")!,
                                           hops: [beachHop], found: nil,
                                           body: nil, failure: .cancelled) {
    check(abs(p.latitude - 32.587634) < 1e-7 && abs(p.longitude + 80.1273267) < 1e-7,
          "a beach pin lands exactly")
} else {
    check(false, "a beach pin's first hop should resolve to a place")
}

// Desktop Safari gets Google's JavaScript page and no redirect; this pins
// the agent to mobile Safari.
check(PlaceLink.userAgent.contains("iPhone") && PlaceLink.userAgent.contains("Mobile"),
      "resolve must send a mobile Safari agent")

// The same chain with no signal: offline wins over "no coordinate", since
// the chain never finished.
for code in [URLError.Code.notConnectedToInternet, .networkConnectionLost, .timedOut,
             .cannotFindHost, .dataNotAllowed, .internationalRoamingOff] {
    check(PlaceLink.classify(start: URL(string: "https://maps.app.goo.gl/aBcDeFgHiJkLmN")!,
                             hops: [], found: nil, body: nil, failure: code)
            == .offline(code),
          "URLError \(code.rawValue) should read as offline")
}
// A server error is not a missing signal.
check(PlaceLink.classify(start: URL(string: "https://maps.app.goo.gl/aBcDeFgHiJkLmN")!,
                         hops: [], found: nil, body: nil, failure: .badServerResponse)
        == .noCoordinate(lastHop: URL(string: "https://maps.app.goo.gl/aBcDeFgHiJkLmN")!, query: nil),
      "a bad response is no coordinate, not offline")

// A hop that parses wins even when the fetch then died: the sniffer's cancel.
let pinHop = URL(string: "https://maps.google.com/?q=32.60841,-80.07213")!
if case .place(let p) = PlaceLink.classify(start: URL(string: "https://maps.app.goo.gl/x")!,
                                           hops: [pinHop], found: nil,
                                           body: nil, failure: .cancelled) {
    check(abs(p.latitude - 32.60841) < 1e-7 && abs(p.longitude + 80.07213) < 1e-7,
          "a dropped pin's hop carries its coordinate")
} else {
    check(false, "a dropped pin's hop should resolve to a place")
}

// The page body still counts when it carries the place pair itself.
if case .place(let p) = PlaceLink.classify(
        start: URL(string: "https://maps.app.goo.gl/y")!, hops: [], found: nil,
        body: "<a href=\"/maps/place/Beachwalker+Park/@34.1,-84.2,12z/data=!3d32.60841!4d-80.07213\">",
        failure: nil) {
    check(abs(p.latitude - 32.60841) < 1e-7, "the body's !3d/!4d is the place, not its @ camera")
    check(p.name == "Beachwalker Park", "the body's place name")
} else {
    check(false, "a body carrying !3d/!4d should resolve to a place")
}

// Three outcomes, three different sentences.
let messages = [
    PlaceLink.failureMessage(for: .offline(.notConnectedToInternet)),
    PlaceLink.failureMessage(for: .noCoordinate(lastHop: nil, query: nil)),
    PlaceLink.failureMessage(for: .noCoordinate(lastHop: nil, query: "Beachwalker Park")),
]
check(Set(messages.compactMap { $0 }).count == 3, "each failure says something different")
check(messages[2]?.hasSuffix("Beachwalker Park") == true, "the name Google sent is shown")
check(PlaceLink.failureMessage(for: .place(original0)) == nil, "a place is not a failure")

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

// MARK: - Search results

let resort = "Kiawah Island Golf Resort, 1 Sanctuary Beach Dr, Kiawah Island, SC 29455"
check(PlaceLink.searchResultMatches(query: resort, name: "The Sanctuary at Kiawah Island Golf Resort",
                                    address: "1 Sanctuary Beach Dr, Kiawah Island, SC 29455, United States"),
      "the resort itself matches")
check(PlaceLink.searchResultMatches(query: "Beachwalker Park", name: "",
                                    address: "8 Beachwalker Dr, Kiawah Island, SC"),
      "an address-only result matches through its street")
// The rule is deliberately loose: one shared word, even a generic one, is
// enough. It guards against a result that has nothing to do with the query;
// the approximate remark covers the rest.
check(PlaceLink.searchResultMatches(query: "Beachwalker Park", name: "Folly Beach County Park",
                                    address: "1100 W Ashley Ave, Folly Beach, SC"),
      "a generic shared word ('park') is accepted")
check(!PlaceLink.searchResultMatches(query: "Café Rhett, 8 Dr", name: "Pizza Hut",
                                     address: "12 Main St, Atlanta, GA"),
      "a result with no word in common is refused")
check(!PlaceLink.searchResultMatches(query: "The Dr", name: "The Dr", address: ""),
      "stop words alone do not make a match")
check(PlaceLink.searchResultMatches(query: "CAFÉ RHETT", name: "Cafe Rhett", address: ""),
      "case and accents fold")
check(PlaceLink.addressPart(of: resort) == "1 Sanctuary Beach Dr, Kiawah Island, SC 29455",
      "the address is everything after the first comma")
check(PlaceLink.addressPart(of: "Beachwalker Park") == nil, "no comma, no address")
check(PlaceLink.addressPart(of: "Beachwalker Park, ") == nil, "an empty tail is no address")

// An approximate place keeps its query through the callback; an exact one
// carries no `approx` at all.
var searched = SharedPlace(latitude: 32.60841, longitude: -80.07213, name: "Beachwalker Park")!
searched.searchQuery = "Beachwalker Park, 8 Beachwalker Dr"
if let url = PlaceLink.callbackURL(for: searched), let back = PlaceLink.place(inCallback: url) {
    check(back.isApproximate && back.searchQuery == searched.searchQuery,
          "the search query survives the callback")
} else {
    check(false, "an approximate callback did not round trip")
}
check(PlaceLink.callbackURL(for: original0)?.absoluteString.contains("approx") == false,
      "an exact place's callback says nothing about approximation")
check(PlaceLink.place(inCallback: URL(string: "pippin://place?lat=32.6&lon=-80.07&query=x")!)?
        .isApproximate == false,
      "a query without approx=1 does not make a place approximate")

// MARK: - The fallback name

check(SharedPlace(latitude: 1, longitude: 1)!.displayName == "Shared place",
      "a nameless place still has something to call itself")
check(SharedPlace(latitude: 1, longitude: 1,
                  address: "8 Beachwalker Dr, Kiawah Island, SC")!.displayName
        == "8 Beachwalker Dr",
      "a nameless place with an address is called by its street")

print("place_link_test: \(checks - failures)/\(checks) checks passed")
exit(failures == 0 ? 0 : 1)
