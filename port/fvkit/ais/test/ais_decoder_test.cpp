// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for the AIS sentence decoder. Expected values were cross-checked with
/// pyais 3.3.1; the live sentences come from an Open Waters capture of 2026-10-26.

#include "fvkit/ais/decoder.h"

#include <gtest/gtest.h>

namespace fv {
namespace ais {
namespace {

constexpr double kKnot = 1852.0 / 3600.0;

/// Feeds sentences in order and returns the last result.
FeedResult FeedAll(AisDecoder* d, std::initializer_list<const char*> lines, AisMessage* m,
                   double now_s = 0.0) {
  FeedResult r = FeedResult::kNotAis;
  for (const char* line : lines) r = d->Feed(line, now_s, m);
  return r;
}

TEST(AisDecoder, ClassAPositionReport) {
  AisDecoder d;
  AisMessage m;
  ASSERT_EQ(d.Feed("!AIVDM,1,1,,A,13HOI:0P0000VOHLCnHQKwvL05Ip,0*23", 0, &m), FeedResult::kMessage);
  EXPECT_EQ(m.type, 1);
  EXPECT_EQ(m.mmsi, 227006760u);
  EXPECT_EQ(m.talker, "AI");
  EXPECT_EQ(m.channel, 'A');
  EXPECT_FALSE(m.own_ship);
  EXPECT_EQ(m.kind, StationKind::kVessel);
  ASSERT_TRUE(m.has_position);
  EXPECT_NEAR(m.lat_deg, 49.475577, 1e-6);
  EXPECT_NEAR(m.lon_deg, 0.13138, 1e-6);
  ASSERT_TRUE(m.has_sog);
  EXPECT_DOUBLE_EQ(m.sog_mps, 0.0);
  ASSERT_TRUE(m.has_cog);
  EXPECT_NEAR(m.cog_deg, 36.7, 1e-9);
  EXPECT_FALSE(m.has_heading);  // 511
  EXPECT_FALSE(m.has_rot);      // -128
  EXPECT_EQ(m.turn_indicator, 0);
  ASSERT_TRUE(m.has_nav_status);
  EXPECT_EQ(m.nav_status, 0);
  ASSERT_TRUE(m.has_timestamp);
  EXPECT_EQ(m.timestamp_s, 14);
}

TEST(AisDecoder, ClassAPositionWithTagBlock) {
  AisDecoder d;
  AisMessage m;
  ASSERT_EQ(d.Feed("\\s:2573010,c:1787234980*03\\!BSVDM,1,1,,B,13noH:00000H@P@RSPEakGK@0D33,0*43", 0, &m),
            FeedResult::kMessage);
  EXPECT_EQ(m.talker, "BS");
  EXPECT_EQ(m.mmsi, 258857000u);
  EXPECT_NEAR(m.lat_deg, 60.388837, 1e-6);
  EXPECT_NEAR(m.lon_deg, 5.299213, 1e-6);
  EXPECT_NEAR(m.cog_deg, 250.9, 1e-9);
  EXPECT_EQ(m.heading_deg, 237);
  ASSERT_TRUE(m.has_rot);
  EXPECT_DOUBLE_EQ(m.rot_deg_per_min, 0.0);
  ASSERT_TRUE(m.tag.has_source);
  EXPECT_EQ(m.tag.source, "2573010");
  ASSERT_TRUE(m.tag.has_time);
  EXPECT_EQ(m.tag.unix_s, 1787234980);
}

TEST(AisDecoder, TagBlockWithBadChecksumIsDroppedButTheSentenceDecodes) {
  AisDecoder d;
  AisMessage m;
  ASSERT_EQ(d.Feed("\\s:2573010,c:1787234980*04\\!BSVDM,1,1,,B,13noH:00000H@P@RSPEakGK@0D33,0*43", 0, &m),
            FeedResult::kMessage);
  EXPECT_FALSE(m.tag.has_source);
  EXPECT_FALSE(m.tag.has_time);
}

TEST(AisDecoder, TagBlockTextAndMillisecondTime) {
  AisSentence s;
  std::string error;
  // Checksum of "c:1787234980123,t:NLOD-2.0" is 0x0E.
  ASSERT_TRUE(ParseAisSentence("\\c:1787234980123,t:NLOD-2.0*0E\\!AIVDM,1,1,,A,13HOI:0P0000VOHLCnHQKwvL05Ip,0*23",
                               &s, &error))
      << error;
  ASSERT_TRUE(s.tag.has_time);
  EXPECT_EQ(s.tag.unix_s, 1787234980);
  ASSERT_TRUE(s.tag.has_text);
  EXPECT_EQ(s.tag.text, "NLOD-2.0");
}

TEST(AisDecoder, ClassAMooredWithAccuracy) {
  AisDecoder d;
  AisMessage m;
  ASSERT_EQ(d.Feed("!AIVDM,1,1,,A,33`j7WE000PVUj8OgJ2VTr9b0Dg:,0*6A", 0, &m), FeedResult::kMessage);
  EXPECT_EQ(m.type, 3);
  EXPECT_EQ(m.mmsi, 244090781u);
  EXPECT_EQ(m.nav_status, 5);
  EXPECT_TRUE(m.position_accurate);
  EXPECT_NEAR(m.lat_deg, 55.47095, 1e-6);
  EXPECT_NEAR(m.lon_deg, 8.430193, 1e-6);
  EXPECT_EQ(m.heading_deg, 324);
  EXPECT_EQ(m.timestamp_s, 53);
}

TEST(AisDecoder, LimitsAndSouthWestCoordinates) {
  AisDecoder d;
  AisMessage m;
  ASSERT_EQ(d.Feed("!AIVDO,1,1,,B,15MwqgotOvC:D71V6qD>3h1qP000,0*06", 0, &m), FeedResult::kMessage);
  EXPECT_TRUE(m.own_ship);
  EXPECT_EQ(m.nav_status, 7);
  ASSERT_TRUE(m.has_sog);
  EXPECT_TRUE(m.sog_at_limit);
  EXPECT_NEAR(m.sog_mps, 102.2 * kKnot, 1e-9);
  EXPECT_NEAR(m.lon_deg, -179.5, 1e-6);
  EXPECT_NEAR(m.lat_deg, -45.25, 1e-6);
  EXPECT_NEAR(m.cog_deg, 359.9, 1e-9);
  ASSERT_TRUE(m.has_heading);
  EXPECT_EQ(m.heading_deg, 0);
  EXPECT_FALSE(m.has_timestamp);  // 60
  ASSERT_TRUE(m.has_rot);
  EXPECT_NEAR(m.rot_deg_per_min, -10.0, 0.5);
}

TEST(AisDecoder, BaseStation) {
  AisDecoder d;
  AisMessage m;
  ASSERT_EQ(d.Feed("!AIVDM,1,1,,A,402M3<1vbeAAW0i>SNRA<mgP2@GV,0*29", 0, &m), FeedResult::kMessage);
  EXPECT_EQ(m.type, 4);
  EXPECT_EQ(m.kind, StationKind::kBaseStation);
  EXPECT_EQ(m.mmsi, 2573104u);
  ASSERT_TRUE(m.has_utc);
  EXPECT_EQ(m.utc_year, 2026);
  EXPECT_EQ(m.utc_month, 10);
  EXPECT_EQ(m.utc_day, 26);
  EXPECT_EQ(m.utc_hour, 17);
  EXPECT_EQ(m.utc_minute, 17);
  EXPECT_EQ(m.utc_second, 39);
  EXPECT_NEAR(m.lat_deg, 59.888997, 1e-6);
  EXPECT_NEAR(m.lon_deg, 10.753892, 1e-6);
  EXPECT_EQ(m.epfd, 15);
  EXPECT_TRUE(m.raim);
}

TEST(AisDecoder, StaticAndVoyageDataInTwoFragments) {
  AisDecoder d;
  AisMessage m;
  EXPECT_EQ(d.Feed("!AIVDM,2,1,1,A,55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp8,0*1C", 0, &m),
            FeedResult::kPending);
  EXPECT_EQ(d.pending(), 1u);
  ASSERT_EQ(d.Feed("!AIVDM,2,2,1,A,88888888880,2*25", 0.5, &m), FeedResult::kMessage);
  EXPECT_EQ(d.pending(), 0u);
  EXPECT_EQ(m.type, 5);
  EXPECT_EQ(m.mmsi, 351759000u);
  ASSERT_TRUE(m.has_imo);
  EXPECT_EQ(m.imo, 9134270u);
  EXPECT_EQ(m.call_sign, "3FOF8");
  EXPECT_EQ(m.name, "EVER DIADEM");
  ASSERT_TRUE(m.has_ship_type);
  EXPECT_EQ(m.ship_type, 70);
  ASSERT_TRUE(m.has_dimensions);
  EXPECT_EQ(m.to_bow_m, 225);
  EXPECT_EQ(m.to_stern_m, 70);
  EXPECT_EQ(m.to_port_m, 1);
  EXPECT_EQ(m.to_starboard_m, 31);
  EXPECT_EQ(m.epfd, 1);
  ASSERT_TRUE(m.has_eta);
  EXPECT_EQ(m.eta_month, 5);
  EXPECT_EQ(m.eta_day, 15);
  EXPECT_EQ(m.eta_hour, 14);
  EXPECT_EQ(m.eta_minute, 0);
  ASSERT_TRUE(m.has_draught);
  EXPECT_NEAR(m.draught_m, 12.2, 1e-9);
  EXPECT_EQ(m.destination, "NEW YORK");
  EXPECT_FALSE(m.has_position);
}

TEST(AisDecoder, TextIsTrimmedAtBothEnds) {
  AisDecoder d;
  AisMessage m;
  // The name field is sent as " RUNE EGHOLM E 426".
  EXPECT_EQ(d.Feed("!AIVDM,2,1,4,A,53B2PD800000uT<L00219DpF0DLPthn0F3C;H00N1P843t00000000000000,0*69", 0, &m),
            FeedResult::kPending);
  ASSERT_EQ(d.Feed("!AIVDM,2,2,4,A,00000000000,2*20", 0, &m), FeedResult::kMessage);
  EXPECT_EQ(m.mmsi, 220242000u);
  EXPECT_EQ(m.name, "RUNE EGHOLM E 426");
}

TEST(AisDecoder, InterleavedMultipartMessagesStaySeparate) {
  AisDecoder d;
  AisMessage m;
  const char* a1 = "!AIVDM,2,1,1,A,55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp8,0*1C";
  const char* a2 = "!AIVDM,2,2,1,A,88888888880,2*25";
  // The same message on channel B with sequence id 2: a different key.
  const char* b1 = "!AIVDM,2,1,2,B,55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp8,0*1C";
  const char* b2 = "!AIVDM,2,2,2,B,88888888880,2*25";
  EXPECT_EQ(d.Feed(a1, 0, &m), FeedResult::kPending);
  EXPECT_EQ(d.Feed(b1, 0, &m), FeedResult::kPending);
  EXPECT_EQ(d.pending(), 2u);
  ASSERT_EQ(d.Feed(a2, 0, &m), FeedResult::kMessage);
  EXPECT_EQ(m.channel, 'A');
  EXPECT_EQ(m.name, "EVER DIADEM");
  ASSERT_EQ(d.Feed(b2, 0, &m), FeedResult::kMessage);
  EXPECT_EQ(m.channel, 'B');
  EXPECT_EQ(m.destination, "NEW YORK");
  EXPECT_EQ(d.stats().messages, 2u);
  EXPECT_EQ(d.stats().fragments_dropped, 0u);
}

TEST(AisDecoder, FragmentsOutOfOrderAndRepeatedFirstPart) {
  AisDecoder d;
  AisMessage m;
  const char* p1 = "!AIVDM,2,1,1,A,55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp8,0*1C";
  const char* p2 = "!AIVDM,2,2,1,A,88888888880,2*25";
  // A second part with no first part is dropped.
  EXPECT_EQ(d.Feed(p2, 0, &m), FeedResult::kError);
  EXPECT_EQ(d.stats().fragments_dropped, 1u);
  // A new first part replaces an unfinished one under the same key.
  EXPECT_EQ(d.Feed(p1, 0, &m), FeedResult::kPending);
  EXPECT_EQ(d.Feed(p1, 1, &m), FeedResult::kPending);
  EXPECT_EQ(d.stats().fragments_dropped, 2u);
  EXPECT_EQ(d.Feed(p2, 2, &m), FeedResult::kMessage);
  EXPECT_EQ(m.mmsi, 351759000u);
}

TEST(AisDecoder, UnfinishedMultipartTimesOut) {
  AisDecoder d(5.0);
  AisMessage m;
  EXPECT_EQ(d.Feed("!AIVDM,2,1,1,A,55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp8,0*1C", 0, &m),
            FeedResult::kPending);
  EXPECT_EQ(d.Feed("!AIVDM,2,2,1,A,88888888880,2*25", 6.0, &m), FeedResult::kError);
  EXPECT_EQ(d.pending(), 0u);
  EXPECT_EQ(d.stats().fragments_dropped, 2u);  // the expired first part and the orphan
}

TEST(AisDecoder, SarAircraft) {
  AisDecoder d;
  AisMessage m;
  ASSERT_EQ(d.Feed("!AIVDM,1,1,,A,91b6UbBIAkPir@HR5AWGg:P248I?,0*5C", 0, &m), FeedResult::kMessage);
  EXPECT_EQ(m.kind, StationKind::kSarAircraft);
  EXPECT_EQ(m.mmsi, 111257001u);
  ASSERT_TRUE(m.has_altitude);
  EXPECT_EQ(m.altitude_m, 613);
  EXPECT_NEAR(m.sog_mps, 115.0 * kKnot, 1e-9);
  EXPECT_NEAR(m.lat_deg, 59.563355, 1e-6);
  EXPECT_NEAR(m.lon_deg, 10.90306, 1e-6);
  EXPECT_NEAR(m.cog_deg, 198.0, 1e-9);
  EXPECT_EQ(m.timestamp_s, 42);
}

TEST(AisDecoder, ClassBPosition) {
  AisDecoder d;
  AisMessage m;
  ASSERT_EQ(d.Feed("!AIVDM,1,1,,A,B52K>;h00Fc>jpUlNV@ikwpUoP06,0*4C", 0, &m), FeedResult::kMessage);
  EXPECT_EQ(m.type, 18);
  EXPECT_EQ(m.mmsi, 338087471u);
  EXPECT_NEAR(m.sog_mps, 0.1 * kKnot, 1e-9);
  EXPECT_NEAR(m.lat_deg, 40.68454, 1e-6);
  EXPECT_NEAR(m.lon_deg, -74.072132, 1e-6);
  EXPECT_NEAR(m.cog_deg, 79.6, 1e-9);
  EXPECT_FALSE(m.has_heading);
  EXPECT_FALSE(m.has_nav_status);
  EXPECT_EQ(m.timestamp_s, 49);
}

TEST(AisDecoder, ClassBPositionWithNoCourse) {
  AisDecoder d;
  AisMessage m;
  ASSERT_EQ(d.Feed("!AIVDM,1,1,,A,B3@pOnP0009WuW7stkSQ3wsQjDhr,0*61", 0, &m), FeedResult::kMessage);
  EXPECT_EQ(m.mmsi, 219029466u);
  EXPECT_FALSE(m.has_cog);  // 3600
  EXPECT_NEAR(m.lat_deg, 55.481693, 1e-6);
}

TEST(AisDecoder, ClassBExtended) {
  AisDecoder d;
  AisMessage m;
  ASSERT_EQ(d.Feed("!AIVDO,1,1,,B,C5N7KvP0=NTS364d4vi=8vg0V:30fBL8000000000000BPH2Q120,0*6E", 0, &m),
            FeedResult::kMessage);
  EXPECT_EQ(m.type, 19);
  EXPECT_EQ(m.mmsi, 367123450u);
  EXPECT_NEAR(m.sog_mps, 5.3 * kKnot, 1e-9);
  EXPECT_TRUE(m.position_accurate);
  EXPECT_NEAR(m.lat_deg, 32.7765, 1e-6);
  EXPECT_NEAR(m.lon_deg, -79.9123, 1e-6);
  EXPECT_NEAR(m.cog_deg, 123.4, 1e-9);
  EXPECT_EQ(m.heading_deg, 125);
  EXPECT_EQ(m.name, "SEA WIND");
  EXPECT_EQ(m.ship_type, 37);
  EXPECT_EQ(m.to_bow_m, 6);
  EXPECT_EQ(m.to_stern_m, 5);
  EXPECT_EQ(m.to_port_m, 2);
  EXPECT_EQ(m.to_starboard_m, 2);
  EXPECT_EQ(m.epfd, 1);
}

TEST(AisDecoder, AidToNavigation) {
  AisDecoder d;
  AisMessage m;
  ASSERT_EQ(d.Feed("!AIVDM,1,1,,A,E>jVGSk32Tab2TW@14S<a0000000<VJb@m9:800000N000,4*1C", 0, &m),
            FeedResult::kMessage);
  EXPECT_EQ(m.kind, StationKind::kAidToNavigation);
  EXPECT_EQ(m.mmsi, 992581519u);
  EXPECT_EQ(m.ship_type, 6);  // light with sectors
  EXPECT_EQ(m.name, "FEISTEIN BIFYR");
  EXPECT_NEAR(m.lat_deg, 58.826375, 1e-6);
  EXPECT_NEAR(m.lon_deg, 5.505137, 1e-6);
  EXPECT_FALSE(m.has_dimensions);
  EXPECT_FALSE(m.has_timestamp);
  EXPECT_FALSE(m.off_position);
  EXPECT_FALSE(m.virtual_aid);
}

TEST(AisDecoder, StaticDataReportBothParts) {
  AisDecoder d;
  AisMessage m;
  ASSERT_EQ(d.Feed("!AIVDM,1,1,,A,H42O55i18tMET00000000000000,2*6D", 0, &m), FeedResult::kMessage);
  EXPECT_EQ(m.type, 24);
  EXPECT_EQ(m.part, 0);
  EXPECT_EQ(m.mmsi, 271041815u);
  EXPECT_EQ(m.name, "PROGUY");
  ASSERT_EQ(d.Feed("!AIVDM,1,1,,A,H42O55lti4hhhilD3nink000?050,0*40", 0, &m), FeedResult::kMessage);
  EXPECT_EQ(m.part, 1);
  EXPECT_EQ(m.ship_type, 60);
  EXPECT_EQ(m.call_sign, "TC6163");
  EXPECT_EQ(m.to_bow_m, 0);
  EXPECT_EQ(m.to_stern_m, 15);
  EXPECT_EQ(m.to_port_m, 0);
  EXPECT_EQ(m.to_starboard_m, 5);
  EXPECT_FALSE(m.has_mothership);
}

TEST(AisDecoder, LongRangeBroadcast) {
  AisDecoder d;
  AisMessage m;
  ASSERT_EQ(d.Feed("!AIVDO,1,1,,B,K5Mwqh@=4g2IQ6@p,0*78", 0, &m), FeedResult::kMessage);
  EXPECT_EQ(m.type, 27);
  EXPECT_EQ(m.mmsi, 367000001u);
  EXPECT_NEAR(m.lat_deg, 32.75, 1e-6);
  EXPECT_NEAR(m.lon_deg, -79.9, 1e-6);
  EXPECT_NEAR(m.sog_mps, 12.0 * kKnot, 1e-9);
  EXPECT_NEAR(m.cog_deg, 270.0, 1e-9);
  EXPECT_EQ(m.nav_status, 0);
}

TEST(AisDecoder, RejectsBadInput) {
  AisDecoder d;
  AisMessage m;
  std::string error;
  EXPECT_EQ(d.Feed("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A", 0, &m),
            FeedResult::kNotAis);
  EXPECT_EQ(d.Feed("!AIVDM,1,1,,A,13HOI:0P0000VOHLCnHQKwvL05Ip,0*24", 0, &m, &error), FeedResult::kError);
  EXPECT_EQ(error, "checksum mismatch");
  EXPECT_EQ(d.Feed("!AIVDM,1,1,,A,13HOI:0P0000VOHLCnHQKwvL05Ip,0", 0, &m, &error), FeedResult::kError);
  EXPECT_EQ(error, "no checksum");
  EXPECT_EQ(d.stats().errors, 2u);
}

TEST(AisDecoder, ShortAndUnsupportedMessages) {
  std::vector<std::uint8_t> bits;
  AisMessage m;
  std::string error;
  // Type 1 cut to 20 characters (120 bits).
  ASSERT_TRUE(UnarmorPayload("13HOI:0P0000VOHLCnHQ", 0, &bits));
  EXPECT_FALSE(DecodeAisBits(bits, &m, &error));
  EXPECT_EQ(error, "type 1 message has 120 bits, needs 149");
  // Type 8 (binary broadcast) is not decoded.
  ASSERT_TRUE(UnarmorPayload("8" + std::string(27, '0'), 0, &bits));
  EXPECT_FALSE(DecodeAisBits(bits, &m, &error));
  EXPECT_EQ(m.type, 8);
  EXPECT_FALSE(UnarmorPayload("13HOI:0P0000VOHLCnHQKwvL05I~", 0, &bits));  // '~' is not armour
}

TEST(AisDecoder, UnarmorDropsFillBits) {
  std::vector<std::uint8_t> bits;
  ASSERT_TRUE(UnarmorPayload("w0", 2, &bits));
  // 'w' is 63, '0' is 0: 111111 0000, the last two zeros removed.
  EXPECT_EQ(bits, (std::vector<std::uint8_t>{1, 1, 1, 1, 1, 1, 0, 0, 0, 0}));
}

}  // namespace
}  // namespace ais
}  // namespace fv
