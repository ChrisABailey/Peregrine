// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// P5: the `.fvrte` document, and the claim that makes it worth moving to C++ —
// a route PythonView saved opens in Pippin and comes back out BYTE FOR BYTE.
//
// The centrepiece is `RewritesTheFixtureByteForByte`: the fixture on disk was
// written by route.py's `json.dump(..., indent=2)` and nothing here has ever
// touched it, so reading it and re-emitting it is a direct comparison against
// CPython's serializer. Everything else in this file exists to pin the pieces
// that comparison depends on — above all `PythonFloatRepr`, whose expectations
// are literally `repr(float)` output and are the half a generic JSON writer
// gets subtly wrong.

#include "fv_route_doc.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using fv::PythonFloatRepr;
using fv::RouteDoc;
using fv::RouteWaypoint;

std::string ReadWhole(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string TempSpec(const char* stem) {
  const ::testing::TestInfo* info =
      ::testing::UnitTest::GetInstance()->current_test_info();
  // Per-test filename: the ledger's hygiene rule about tests that share one
  // scratch file and therefore cannot run in parallel.
  const std::string name = info != nullptr ? info->name() : "x";
  return (std::filesystem::temp_directory_path() /
          ("fvrte_" + name + "_" + stem + ".fvrte"))
      .string();
}

// --- PythonFloatRepr -------------------------------------------------------
//
// Every expectation below is the exact output of CPython's `repr()`. Three
// families are what a %g-based writer gets wrong and are therefore the point:
// the fixed/scientific cut (1e+16 is scientific, 9007199254740992.0 is not),
// the ".0" that keeps an integral float a float, and shortest-round-trip
// digits (0.3333333333333333 is sixteen digits, not seventeen).

TEST(RouteDocFloat, MatchesPythonRepr) {
  EXPECT_EQ("32.6044007", PythonFloatRepr(32.6044007));
  EXPECT_EQ("-80.11767417454368", PythonFloatRepr(-80.11767417454368));
  EXPECT_EQ("0.0", PythonFloatRepr(0.0));
  EXPECT_EQ("-0.0", PythonFloatRepr(-0.0));
  EXPECT_EQ("32.0", PythonFloatRepr(32.0));
  EXPECT_EQ("123.456", PythonFloatRepr(123.456));
  EXPECT_EQ("-0.5", PythonFloatRepr(-0.5));
  EXPECT_EQ("0.1", PythonFloatRepr(0.1));
  EXPECT_EQ("0.3333333333333333", PythonFloatRepr(1.0 / 3.0));
}

TEST(RouteDocFloat, FixedAndScientificSplitWherePythonSplitsThem) {
  // The cut is on `decpt`, the decimal point's position: scientific when it is
  // <= -4 or > 16. Each pair below straddles one edge of that rule, which is
  // exactly where %g and repr() disagree.
  EXPECT_EQ("1000000.0", PythonFloatRepr(1e6));    // %g would say 1e+06
  EXPECT_EQ("1234567.0", PythonFloatRepr(1234567.0));
  EXPECT_EQ("9007199254740992.0", PythonFloatRepr(9007199254740992.0));
  EXPECT_EQ("1e+16", PythonFloatRepr(1e16));
  EXPECT_EQ("1e+17", PythonFloatRepr(1e17));
  EXPECT_EQ("1e+21", PythonFloatRepr(1e21));
  EXPECT_EQ("0.0001", PythonFloatRepr(1e-4));
  EXPECT_EQ("1e-05", PythonFloatRepr(1e-5));       // two-digit exponent
  EXPECT_EQ("5e-324", PythonFloatRepr(5e-324));    // the smallest subnormal
  EXPECT_EQ("1.7976931348623157e+308", PythonFloatRepr(1.7976931348623157e308));
}

TEST(RouteDocFloat, RoundTripsEveryValueItWrites) {
  // The property under the table: whatever comes out reads back as the same
  // double. A shortest representation that does not round-trip would corrupt a
  // waypoint by a metre or two and look perfectly plausible on the page.
  const double values[] = {32.6044007,     -80.11767417454368, 1.0 / 3.0,
                           1e-5,           1e16,               -0.0,
                           1.7976931348623157e308,             5e-324,
                           32.59303776115624};
  for (double v : values) {
    EXPECT_EQ(v, std::strtod(PythonFloatRepr(v).c_str(), nullptr))
        << PythonFloatRepr(v);
  }
}

// --- the fixture -----------------------------------------------------------

TEST(RouteDoc, ReadsTheFixtureRoutePythonWrote) {
  RouteDoc doc;
  const fv::Status s = doc.Read(FV_ROUTE_FIXTURE_FILE);
  ASSERT_TRUE(s.ok()) << s.message;

  EXPECT_EQ("Ruddy Turnstone to the beach", doc.name());
  EXPECT_EQ(220, doc.color().r);
  EXPECT_EQ(30, doc.color().g);
  EXPECT_EQ(30, doc.color().b);
  EXPECT_EQ(255, doc.color().a);  // the format carries three channels
  EXPECT_EQ("", doc.profile());

  ASSERT_EQ(3u, doc.waypoints().size());
  EXPECT_EQ("RTURN", doc.waypoints()[0].label);
  EXPECT_DOUBLE_EQ(32.6044007, doc.waypoints()[0].position.lat);
  EXPECT_DOUBLE_EQ(-80.1083007, doc.waypoints()[0].position.lon);
  EXPECT_EQ("WP2", doc.waypoints()[1].label);
  EXPECT_DOUBLE_EQ(32.59297596527702, doc.waypoints()[1].position.lat);
  EXPECT_DOUBLE_EQ(-80.11767417454368, doc.waypoints()[1].position.lon);
  EXPECT_EQ("WP3", doc.waypoints()[2].label);
  EXPECT_DOUBLE_EQ(32.59303776115624, doc.waypoints()[2].position.lat);
  EXPECT_DOUBLE_EQ(-80.11886651782085, doc.waypoints()[2].position.lon);
}

TEST(RouteDoc, RewritesTheFixtureByteForByte) {
  // THE WHOLE POINT OF THIS FILE. The fixture was written by route.py and has
  // never been touched by C++; if this passes, the two writers agree.
  RouteDoc doc;
  ASSERT_TRUE(doc.Read(FV_ROUTE_FIXTURE_FILE).ok());
  EXPECT_EQ(ReadWhole(FV_ROUTE_FIXTURE_FILE), doc.ToJson());
}

TEST(RouteDoc, WritesAndReadsBackThroughAFile) {
  RouteDoc doc;
  ASSERT_TRUE(doc.Read(FV_ROUTE_FIXTURE_FILE).ok());
  const std::string spec = TempSpec("roundtrip");
  ASSERT_TRUE(doc.Write(spec).ok());

  RouteDoc back;
  ASSERT_TRUE(back.Read(spec).ok());
  EXPECT_EQ(doc.ToJson(), back.ToJson());
  EXPECT_EQ(ReadWhole(FV_ROUTE_FIXTURE_FILE), ReadWhole(spec));
  std::remove(spec.c_str());
}

// --- the shapes json.dump has an opinion about -----------------------------

TEST(RouteDoc, EmptyWaypointsIsAOneLineList) {
  // `json.dump` puts an EMPTY container on one line — there is nothing to
  // indent — and gets this wrong invisibly until a shell saves a route with no
  // waypoints in it.
  RouteDoc doc;
  doc.set_name("Empty");
  EXPECT_EQ(
      "{\n"
      "  \"format\": \"peregrine-route\",\n"
      "  \"version\": 1,\n"
      "  \"name\": \"Empty\",\n"
      "  \"color\": [\n"
      "    220,\n"
      "    30,\n"
      "    30\n"
      "  ],\n"
      "  \"profile\": \"\",\n"
      "  \"waypoints\": []\n"
      "}\n",
      doc.ToJson());
}

TEST(RouteDoc, NonAsciiIsEscapedTheWayJsonDumpEscapesIt) {
  // `ensure_ascii=True` is json.dump's default, so a name with an accent in it
  // goes out as \uXXXX — and an emoji goes out as a SURROGATE PAIR, which is
  // where a naive escaper and Python part company.
  RouteDoc doc;
  doc.set_name("Caf\xc3\xa9 \xe2\x80\x94 na\xc3\xafve \xf0\x9f\x9a\xb2");
  const std::string json = doc.ToJson();
  EXPECT_NE(std::string::npos,
            json.find("\"name\": \"Caf\\u00e9 \\u2014 na\\u00efve "
                      "\\ud83d\\udeb2\""))
      << json;

  RouteDoc back;
  ASSERT_TRUE(back.Parse(json, "<memory>").ok());
  EXPECT_EQ(doc.name(), back.name());
}

// --- refusing what it should refuse ----------------------------------------

TEST(RouteDoc, RejectsADocumentThatIsNotARoute) {
  RouteDoc doc;
  doc.set_name("Keep me");
  const fv::Status s = doc.Parse(R"({"format": "something-else"})", "x.json");
  EXPECT_FALSE(s.ok());
  // Untouched on failure: a bad file must not cost the caller the route they
  // had open.
  EXPECT_EQ("Keep me", doc.name());
}

TEST(RouteDoc, RejectsAVersionFromTheFuture) {
  RouteDoc doc;
  const fv::Status s = doc.Parse(
      R"({"format": "peregrine-route", "version": 99, "waypoints": []})",
      "x.fvrte");
  EXPECT_EQ(fv::kUnsupported, s.code);
  EXPECT_NE(std::string::npos, s.message.find("99")) << s.message;
}

TEST(RouteDoc, AWaypointWithoutAPositionFailsTheWholeDocument) {
  RouteDoc doc;
  doc.set_waypoints({RouteWaypoint{"KEEP", {1.0, 2.0}}});
  const fv::Status s = doc.Parse(
      R"({"format": "peregrine-route", "version": 1, "waypoints": [
            {"label": "A", "lat": 1.0, "lon": 2.0},
            {"label": "B", "lat": 3.0}]})",
      "x.fvrte");
  EXPECT_FALSE(s.ok());
  EXPECT_NE(std::string::npos, s.message.find("\"B\"")) << s.message;
  ASSERT_EQ(1u, doc.waypoints().size());
  EXPECT_EQ("KEEP", doc.waypoints()[0].label);
}

TEST(RouteDoc, AnIntegerCoordinateIsAValidCoordinate) {
  // route.py's reader calls float() on whatever is there, so `"lat": 32` is a
  // document a human may legitimately have hand-written.
  RouteDoc doc;
  ASSERT_TRUE(doc
                  .Parse(R"({"format": "peregrine-route", "version": 1,
                             "waypoints": [{"label": "A", "lat": 32, "lon": -80}]})",
                         "x.fvrte")
                  .ok());
  ASSERT_EQ(1u, doc.waypoints().size());
  EXPECT_DOUBLE_EQ(32.0, doc.waypoints()[0].position.lat);
  // ... and it comes back out as a FLOAT, which is what Python would write.
  EXPECT_NE(std::string::npos, doc.ToJson().find("\"lat\": 32.0"));
}

TEST(RouteDoc, AnEmptyNameDoesNotBlankTheOneAlreadyThere) {
  // route.py's reader guards `name` and `color` on truthiness, so a document
  // saying `"name": ""` leaves the overlay's own name alone. Matched here
  // because a difference would show up as an overlay that renames itself to
  // nothing on open.
  RouteDoc doc;
  doc.set_name("Kept");
  ASSERT_TRUE(doc
                  .Parse(R"({"format": "peregrine-route", "version": 1,
                             "name": "", "color": [], "waypoints": []})",
                         "x.fvrte")
                  .ok());
  EXPECT_EQ("Kept", doc.name());
  EXPECT_EQ(220, doc.color().r);
}

}  // namespace
