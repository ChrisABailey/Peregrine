// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for the Open Waters frame parser, the subscription box, and frames
/// fed through the `fv.ais` overlay. Frames follow the shape of the live
/// stream (2026-10-26) with made-up vessels; the sentences are from the
/// synthetic Charleston recording.

#include "fvkit/ais/openwaters.h"

#include <gtest/gtest.h>

#include <string>

#include "fvkit/overlay/ais_overlay.h"

namespace fv {
namespace ais {
namespace {

constexpr double kKnot = 1852.0 / 3600.0;
constexpr double kT0 = 1793016000.0;  // 2026-10-26 12:00:00 UTC

const char kWelcome[] =
    R"j({"type":"welcome","sub":"anon:192.0.2.1","role":"anonymous","terms":"https://example.org/terms",)j"
    R"j("limits":{"conns":2,"rate":20,"area":100,"mmsis":10,"publish":false,"connects_per_min":20}})j";

// The frames' `mmsi` fields are made up; the sentences carry their own MMSI,
// which the overlay tests read back from the table.
const char kPositionEvent[] =
    R"j({"type":"event","id":"a1","time":"2026-10-26T11:58:00.25Z","source":"aishub",)j"
    R"j("license":"aishub-terms","attribution":"Open Waters AIS (https://openwaters.io/ais/). AISHub (https://www.aishub.net)",)j"
    R"j("station":"aishub","channel":"","nmea":["!AIVDM,1,1,,A,15M2oq@01TrBW?0BeP`;f9H00000,0*35"],)j"
    R"j("mmsi":366045678,"msg_type":"PositionReport","lat":32.75,"lon":-79.9,"message":{"MessageID":1},"synthesized":true})j";

const char kMultipartEvent[] =
    R"j({"type":"event","time":"2026-10-26T11:59:00Z","source":"aisstream","license":"aisstream-io-terms",)j"
    R"j("attribution":"Open Waters AIS (https://openwaters.io/ais/). aisstream.io","station":"aisstream","channel":"A",)j"
    R"j("nmea":["!AIVDM,2,1,5,A,55M2oq@2=r25L=8OL005@h4q@T>1=@5800000016Bh`@@6eBNGhj0DS1Dm3k,0*02",)j"
    R"j("!AIVDM,2,2,5,A,P0000000000,2*41"],"mmsi":366045678,"msg_type":"ShipStaticData","message":{}})j";

const char kStaticFallback[] =
    R"j({"type":"event","time":"2026-10-26T11:59:30Z","source":"aishub","license":"aishub-terms",)j"
    R"j("attribution":"Open Waters AIS (https://openwaters.io/ais/). AISHub (https://www.aishub.net)","station":"aishub",)j"
    R"j("channel":"","mmsi":338000001,"msg_type":"ShipStaticData","lat":32.78,"lon":-79.95,)j"
    R"j("message":{"MessageID":5,"UserID":338000001,"Valid":true,"ImoNumber":0,"CallSign":"WXY1234",)j"
    R"j("Name":"SAMPLE BOAT@@@@","Type":36,"Dimension":{"A":8,"B":4,"C":2,"D":2},)j"
    R"j("Eta":{"Month":0,"Day":0,"Hour":24,"Minute":60},"MaximumStaticDraught":1.8,"Destination":"CHARLESTON  "},)j"
    R"j("synthesized":true})j";

const char kPositionFallback[] =
    R"j({"type":"event","time":"2026-10-26T11:59:45Z","source":"aishub","attribution":"credit",)j"
    R"j("mmsi":338000002,"msg_type":"PositionReport",)j"
    R"j("message":{"MessageID":1,"UserID":338000002,"NavigationalStatus":15,"Sog":102.3,)j"
    R"j("Longitude":-79.9,"Latitude":32.7,"Cog":360,"TrueHeading":511}})j";

TEST(AisOpenWaters, SsePayload) {
  EXPECT_EQ(OpenWatersSsePayload("data: {\"type\":\"x\"}\r"), "{\"type\":\"x\"}");
  EXPECT_EQ(OpenWatersSsePayload("data:{}"), "{}");
  EXPECT_TRUE(OpenWatersSsePayload(": keep-alive").empty());
  EXPECT_TRUE(OpenWatersSsePayload("event: message").empty());
  EXPECT_TRUE(OpenWatersSsePayload("").empty());
}

TEST(AisOpenWaters, ParsesWelcomeLimits) {
  OpenWatersFrame f;
  ASSERT_TRUE(ParseOpenWatersFrame(kWelcome, &f));
  EXPECT_EQ(f.kind, OpenWatersFrameKind::kWelcome);
  EXPECT_EQ(f.role, "anonymous");
  EXPECT_DOUBLE_EQ(f.limits.area_deg2, 100.0);
  EXPECT_DOUBLE_EQ(f.limits.rate_per_s, 20.0);
  EXPECT_EQ(f.limits.connections, 2);
  EXPECT_EQ(f.limits.connects_per_min, 20);
}

TEST(AisOpenWaters, ParsesEvents) {
  OpenWatersFrame f;
  ASSERT_TRUE(ParseOpenWatersFrame(kPositionEvent, &f));
  EXPECT_EQ(f.kind, OpenWatersFrameKind::kEvent);
  EXPECT_EQ(f.mmsi, 366045678u);
  EXPECT_EQ(f.msg_type, "PositionReport");
  EXPECT_DOUBLE_EQ(f.time_s, kT0 - 120.0 + 0.25);
  EXPECT_TRUE(f.synthesized);
  EXPECT_EQ(f.source, "aishub");
  EXPECT_EQ(f.license, "aishub-terms");
  EXPECT_EQ(f.station, "aishub");
  EXPECT_NE(f.attribution.find("AISHub"), std::string::npos);
  ASSERT_EQ(f.nmea.size(), 1u);
  EXPECT_FALSE(f.has_fallback);

  ASSERT_TRUE(ParseOpenWatersFrame(kMultipartEvent, &f));
  EXPECT_EQ(f.nmea.size(), 2u);
  EXPECT_FALSE(f.synthesized);
  EXPECT_FALSE(f.has_fallback);
}

TEST(AisOpenWaters, FallbackFromDecodedMessage) {
  OpenWatersFrame f;
  ASSERT_TRUE(ParseOpenWatersFrame(kStaticFallback, &f));
  ASSERT_TRUE(f.has_fallback);
  const AisMessage& m = f.fallback;
  EXPECT_EQ(m.type, 5);
  EXPECT_EQ(m.mmsi, 338000001u);
  EXPECT_EQ(m.name, "SAMPLE BOAT");
  EXPECT_EQ(m.call_sign, "WXY1234");
  EXPECT_EQ(m.destination, "CHARLESTON");
  EXPECT_TRUE(m.has_ship_type);
  EXPECT_EQ(m.ship_type, 36);
  EXPECT_TRUE(m.has_dimensions);
  EXPECT_EQ(m.to_bow_m + m.to_stern_m, 12);
  EXPECT_TRUE(m.has_draught);
  EXPECT_FALSE(m.has_eta);
  EXPECT_FALSE(m.has_imo);
  EXPECT_FALSE(m.has_position) << "a static message's event lat/lon is not a report";

  ASSERT_TRUE(ParseOpenWatersFrame(kPositionFallback, &f));
  ASSERT_TRUE(f.has_fallback);
  EXPECT_TRUE(f.fallback.has_position);
  EXPECT_DOUBLE_EQ(f.fallback.lat_deg, 32.7);
  EXPECT_FALSE(f.fallback.has_sog);
  EXPECT_FALSE(f.fallback.has_cog);
  EXPECT_FALSE(f.fallback.has_heading);
  EXPECT_FALSE(f.fallback.has_nav_status);
}

TEST(AisOpenWaters, OtherAndBadFrames) {
  OpenWatersFrame f;
  std::string error;
  EXPECT_FALSE(ParseOpenWatersFrame("not json", &f, &error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(ParseOpenWatersFrame("[1,2]", &f));
  ASSERT_TRUE(ParseOpenWatersFrame(R"({"type":"heartbeat"})", &f));
  EXPECT_EQ(f.kind, OpenWatersFrameKind::kOther);
  ASSERT_TRUE(ParseOpenWatersFrame(R"({"type":"error","message":"bbox too large"})", &f));
  EXPECT_EQ(f.kind, OpenWatersFrameKind::kError);
  EXPECT_EQ(f.error, "bbox too large");
}

TEST(AisOpenWaters, BoxPadsSmallViews) {
  const OpenWatersLimits limits;
  const GeoRect view{{32.6, -80.1}, {32.8, -79.8}};
  const GeoRect box = OpenWatersBox(view, limits, 0.25);
  EXPECT_NEAR(box.ll.lat, 32.55, 1e-9);
  EXPECT_NEAR(box.ur.lat, 32.85, 1e-9);
  EXPECT_NEAR(box.ll.lon, -80.175, 1e-9);
  EXPECT_NEAR(box.ur.lon, -79.725, 1e-9);
  EXPECT_TRUE(OpenWatersBoxServes(box, view, limits));
  const GeoRect panned{{32.6, -79.9}, {32.8, -79.6}};
  EXPECT_FALSE(OpenWatersBoxServes(box, panned, limits));
}

TEST(AisOpenWaters, BoxShrinksToTheTierArea) {
  const OpenWatersLimits limits;  // 100 square degrees
  const GeoRect view{{20.0, -90.0}, {40.0, -60.0}};  // 600 square degrees
  const GeoRect box = OpenWatersBox(view, limits, 0.25);
  const double h = box.ur.lat - box.ll.lat;
  const double w = box.ur.lon - box.ll.lon;
  EXPECT_LE(h * w, 100.0);
  EXPECT_GT(h * w, 95.0);
  EXPECT_NEAR(w / h, 1.5, 1e-9) << "keeps the view's shape";
  EXPECT_NEAR((box.ll.lat + box.ur.lat) / 2.0, 30.0, 1e-9);
  EXPECT_NEAR((box.ll.lon + box.ur.lon) / 2.0, -75.0, 1e-9);

  // Panning a little keeps the stream; panning past the middle half does not.
  EXPECT_TRUE(OpenWatersBoxServes(box, view, limits));
  const GeoRect nudged{{21.0, -89.0}, {41.0, -59.0}};
  EXPECT_TRUE(OpenWatersBoxServes(box, nudged, limits));
  const GeoRect moved{{25.0, -85.0}, {45.0, -55.0}};
  EXPECT_FALSE(OpenWatersBoxServes(box, moved, limits));
}

TEST(AisOpenWaters, BoxClipsToTheWorld) {
  const OpenWatersLimits limits;
  const GeoRect polar{{88.0, 10.0}, {89.9, 12.0}};
  const GeoRect box = OpenWatersBox(polar, limits, 0.5);
  EXPECT_LE(box.ur.lat, 90.0);
  const GeoRect dateline{{50.0, 179.0}, {51.0, -179.0}};
  const GeoRect cut = OpenWatersBox(dateline, limits, 0.0);
  EXPECT_DOUBLE_EQ(cut.ll.lon, 179.0);
  EXPECT_DOUBLE_EQ(cut.ur.lon, 180.0);
}

TEST(AisOpenWaters, StreamUrl) {
  const GeoRect box{{32.55, -80.175}, {32.85, -79.72501}};
  EXPECT_EQ(OpenWatersStreamUrl(box, true),
            "https://ais.openwaters.io/v1/stream?bbox=32.5500,-80.1750,32.8500,-79.7250&snapshot=1");
  EXPECT_EQ(OpenWatersStreamUrl(box, false),
            "https://ais.openwaters.io/v1/stream?bbox=32.5500,-80.1750,32.8500,-79.7250");
}

TEST(AisOpenWaters, MintMessage) {
  const char kPub[] = "11qYAYKxCrfVS_7TyWQHOg7hcvPapiMlrwIaaPcHURo";
  EXPECT_EQ(OpenWatersMintMessage(kPub, 1793016000),
            std::string("aiscast-key\n") + kPub + "\n1793016000\n0\n\n");
  EXPECT_EQ(OpenWatersMintMessage(kPub, 5, true, "Dock 3", "Pelican"),
            std::string("aiscast-key\n") + kPub + "\n5\n1\nDock 3\nPelican");
}

TEST(AisOpenWaters, MintBody) {
  EXPECT_EQ(OpenWatersMintBody("PK", 1793016000, "SIG"),
            R"({"pubkey":"PK","sig":"SIG","ts":1793016000})");
}

TEST(AisOpenWaters, ParsesToken) {
  OpenWatersToken t;
  ASSERT_TRUE(ParseOpenWatersToken(
      R"j({"token":"ak1.e30.c2ln","signed":true,"claims":{"kid":"k1","sub":"st_abc","role":"personal",)j"
      R"j("iat":1793016000,"conns":2,"rate":50,"area":400}})j",
      &t));
  EXPECT_EQ(t.token, "ak1.e30.c2ln");
  EXPECT_TRUE(t.is_signed);
  EXPECT_EQ(t.sub, "st_abc");
  EXPECT_EQ(t.role, "personal");
  EXPECT_DOUBLE_EQ(t.exp_s, 0.0);
  EXPECT_DOUBLE_EQ(t.limits.area_deg2, 400.0);
  EXPECT_DOUBLE_EQ(t.limits.rate_per_s, 50.0);
  EXPECT_EQ(t.limits.connections, 2);
  EXPECT_EQ(t.limits.connects_per_min, 20);  // not in the claims; the default stands

  // A server that ignored the signature still issues a token.
  ASSERT_TRUE(ParseOpenWatersToken(R"({"token":"ak1.x.y","claims":{},"name_error":"taken"})", &t));
  EXPECT_FALSE(t.is_signed);
  EXPECT_EQ(t.name_error, "taken");
  EXPECT_DOUBLE_EQ(t.limits.area_deg2, 100.0);

  std::string error;
  EXPECT_FALSE(ParseOpenWatersToken(R"({"error":"bad signature"})", &t, &error));
  EXPECT_EQ(error, "bad signature");
  EXPECT_FALSE(ParseOpenWatersToken("rate limited", &t, &error));
}

TEST(AisOpenWaters, FramesFeedTheOverlay) {
  AisOverlay overlay;
  OpenWatersFrame f;
  ASSERT_TRUE(ParseOpenWatersFrame(kWelcome, &f));
  overlay.PushFrame(f);  // ignored
  for (const char* json : {kPositionEvent, kMultipartEvent, kStaticFallback}) {
    ASSERT_TRUE(ParseOpenWatersFrame(json, &f));
    overlay.PushFrame(f);
  }
  EXPECT_TRUE(overlay.Tick(kT0));
  ASSERT_EQ(overlay.table().size(), 2u);

  const Vessel* fallback = overlay.table().Find(338000001);
  ASSERT_NE(fallback, nullptr);
  EXPECT_EQ(fallback->name, "SAMPLE BOAT");
  EXPECT_DOUBLE_EQ(fallback->last_heard_s, kT0 - 30.0);

  const Vessel* ship = nullptr;
  overlay.table().ForEach([&](const Vessel& v) {
    if (v.mmsi != 338000001) ship = &v;
  });
  ASSERT_NE(ship, nullptr);
  EXPECT_TRUE(ship->has_position);
  EXPECT_FALSE(ship->name.empty()) << "the multipart static message was reassembled";
  EXPECT_DOUBLE_EQ(ship->position_time_s, kT0 - 120.0 + 0.25) << "aged from reception";
  EXPECT_DOUBLE_EQ(ship->last_heard_s, kT0 - 60.0);
  EXPECT_EQ(ship->station, "aisstream");

  const auto sheet = overlay.Details(ship->mmsi);
  ASSERT_TRUE(sheet.has_value());
  bool credited = false;
  for (const auto& row : sheet->rows)
    if (row.key == "Credit") credited = row.value.find("aisstream.io") != std::string::npos;
  EXPECT_TRUE(credited);
}

TEST(AisOpenWaters, SnapshotOlderThanDropAgeIsRemoved) {
  AisOverlay overlay;
  OpenWatersFrame f;
  ASSERT_TRUE(ParseOpenWatersFrame(kPositionEvent, &f));
  overlay.PushFrame(f);
  overlay.Tick(kT0 + 3 * 3600.0);
  EXPECT_EQ(overlay.table().size(), 0u);

  // A reception time ahead of the local clock counts as now.
  overlay.PushFrame(f);
  overlay.Tick(kT0 - 3600.0);
  ASSERT_EQ(overlay.table().size(), 1u);
  overlay.table().ForEach([](const Vessel& v) { EXPECT_DOUBLE_EQ(v.last_heard_s, kT0 - 3600.0); });
}

}  // namespace
}  // namespace ais
}  // namespace fv
