// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// P20: what a search row is CALLED, and which of two answers about one road
// the phone shows — on the mac.
//
// Both rules fail invisibly, which is the whole reason they are here. A row
// classified POI when it is a street looks exactly like a chart that spelled
// its layer differently; a duplicate that was not caught looks exactly like a
// pack that really does hold two Ruddy Turnstones. Neither is a thing anyone
// notices in a screenshot of a list.

#include "PPSearchRules.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using fv::GeoPoint;
using fv::GeoRect;
using pippin::ClassifySearchResult;
using pippin::DedupeSearchRows;
using pippin::FoldSearchTitle;
using pippin::SearchKind;
using pippin::SearchKindWord;
using pippin::SearchRow;
using pippin::SearchRowsAreDuplicates;
using pippin::SearchSource;

// A box about `centre`, `half_deg` on a side. Small enough at Kiawah's
// latitude that the difference between a degree of latitude and one of
// longitude does not matter to anything being asserted here.
GeoRect Box(const GeoPoint& centre, double half_deg) {
  return GeoRect{{centre.lat - half_deg, centre.lon - half_deg},
                 {centre.lat + half_deg, centre.lon + half_deg}};
}

GeoRect Point(const GeoPoint& p) { return GeoRect{p, p}; }

SearchRow Row(std::string title, SearchKind kind, GeoPoint position,
              GeoRect bounds) {
  SearchRow r;
  r.title = std::move(title);
  r.kind = kind;
  r.position = position;
  r.bounds = bounds;
  return r;
}

const GeoPoint kRuddyTurnstone{32.6044007, -80.1083007};
const GeoPoint kBeachClub{32.59297596527702, -80.11767417454368};

// --- rule 1: the word ------------------------------------------------------

TEST(SearchRules, ThereAreExactlyThreeWords) {
  EXPECT_STREQ(SearchKindWord(SearchKind::kPoint), "Point");
  EXPECT_STREQ(SearchKindWord(SearchKind::kRoad), "Road");
  EXPECT_STREQ(SearchKindWord(SearchKind::kPoi), "POI");
}

TEST(SearchRules, TheRidersOwnDocumentIsAlwaysAPoint) {
  // Whatever the point overlay put in `detail` — "point", "point · restaurant",
  // and one day something else entirely — a row out of the rider's own
  // `.fvpoints` is theirs, and that is a fact about WHO ANSWERED rather than
  // about the string.
  EXPECT_EQ(ClassifySearchResult(SearchSource::kPoints, "point"),
            SearchKind::kPoint);
  EXPECT_EQ(ClassifySearchResult(SearchSource::kPoints, "point \xc2\xb7 golf"),
            SearchKind::kPoint);
  EXPECT_EQ(ClassifySearchResult(SearchSource::kPoints, ""), SearchKind::kPoint);
}

TEST(SearchRules, TheGraphIsAlwaysARoad) {
  // `RoadGraphOverlay` answers "road" and "road · residential"; it is the
  // network the router plans on, so there is nothing else it could be.
  EXPECT_EQ(ClassifySearchResult(SearchSource::kRoadGraph, "road"),
            SearchKind::kRoad);
  EXPECT_EQ(
      ClassifySearchResult(SearchSource::kRoadGraph, "road \xc2\xb7 cycleway"),
      SearchKind::kRoad);
}

TEST(SearchRules, TheChartsTransportationLayersAreRoadsAndTheRestArePois) {
  // The labelled half is the one a text search actually finds, and it is the
  // one with the suffix — which is exactly why the test is a PREFIX.
  EXPECT_EQ(ClassifySearchResult(SearchSource::kChart,
                                 "transportation_name \xc2\xb7 residential"),
            SearchKind::kRoad);
  EXPECT_EQ(ClassifySearchResult(SearchSource::kChart, "transportation"),
            SearchKind::kRoad);
  EXPECT_EQ(ClassifySearchResult(SearchSource::kChart,
                                 "transportation \xc2\xb7 cycleway"),
            SearchKind::kRoad);

  // Everything else the chart knows a name for.
  EXPECT_EQ(ClassifySearchResult(SearchSource::kChart, "poi \xc2\xb7 restaurant"),
            SearchKind::kPoi);
  EXPECT_EQ(ClassifySearchResult(SearchSource::kChart, "place \xc2\xb7 island"),
            SearchKind::kPoi);
  EXPECT_EQ(ClassifySearchResult(SearchSource::kChart, "water_name"),
            SearchKind::kPoi);
  EXPECT_EQ(ClassifySearchResult(SearchSource::kChart, "park"),
            SearchKind::kPoi);
  EXPECT_EQ(ClassifySearchResult(SearchSource::kChart, ""), SearchKind::kPoi);
}

TEST(SearchRules, APoiLayerWhoseNameMerelyStartsLikeATransportOneIsStillNotARoad) {
  // The prefix is over the LAYER, and a layer is the head of the detail
  // string. A word that merely contains it is not one.
  EXPECT_EQ(ClassifySearchResult(SearchSource::kChart, "transport"),
            SearchKind::kPoi);  // shorter than the prefix
  EXPECT_EQ(ClassifySearchResult(SearchSource::kChart,
                                 "poi \xc2\xb7 transportation"),
            SearchKind::kPoi);  // the word is in the SUB-TYPE, not the layer
}

// --- the fold --------------------------------------------------------------

TEST(SearchRules, TheFoldIsAsciiCaseAndWhitespaceAndNothingElse) {
  EXPECT_EQ(FoldSearchTitle("Ruddy Turnstone"), "ruddy turnstone");
  EXPECT_EQ(FoldSearchTitle("  RUDDY   turnstone  "), "ruddy turnstone");
  EXPECT_EQ(FoldSearchTitle("Ruddy\tTurnstone"), "ruddy turnstone");
  // Bytes above 0x7F compare exactly — the stated limit, matching
  // `fv::app::TextMatchQuality`. A name still folds to itself, which is all
  // the duplicate test ever asks of it.
  const std::string angstrom = "\xc3\x85ngstr\xc3\xb6m";
  EXPECT_EQ(FoldSearchTitle(angstrom), FoldSearchTitle(angstrom));
  EXPECT_NE(FoldSearchTitle(angstrom), FoldSearchTitle("\xc3\xa5ngstr\xc3\xb6m"));
}

// --- rule 2: the duplicate -------------------------------------------------

TEST(SearchRules, OneStreetRecordedTwiceIsOneRow) {
  // The case the rule exists for: the chart's merged row and the graph's row
  // for the same street. Their ANCHORS are far apart — a merged chart row is
  // anchored on its biggest piece's label point and the graph on its own —
  // so what identifies them as one thing is that their boxes lie along the
  // same street.
  const GeoRect street{{32.600, -80.115}, {32.610, -80.100}};
  const GeoRect same_street{{32.601, -80.114}, {32.609, -80.101}};
  const std::vector<SearchRow> in{
      Row("Ruddy Turnstone", SearchKind::kRoad, {32.6044, -80.1083}, street),
      Row("Ruddy Turnstone", SearchKind::kRoad, {32.6015, -80.1140},
          same_street),
  };
  const std::vector<SearchRow> out = DedupeSearchRows(in, 250.0);
  ASSERT_EQ(out.size(), 1u);
  // The first survives, which honours the session's ranking rather than
  // preferring one provider. That is what keeps the rule safe when the chart
  // cannot answer at all, as in a global query against a pack with no name
  // index, where the graph's row is the first one there is.
  EXPECT_NEAR(out[0].position.lat, 32.6044, 1e-9);
}

TEST(SearchRules, TwoStreetsOfOneNameTwoCountiesApartAreTwoRows) {
  // The reason "same name" alone is not the test. Disjoint boxes, far apart:
  // both are real answers and a rider choosing a destination has to see both.
  const std::vector<SearchRow> in{
      Row("Main Street", SearchKind::kRoad, kRuddyTurnstone,
          Box(kRuddyTurnstone, 0.002)),
      Row("Main Street", SearchKind::kRoad, {33.5, -80.9}, Box({33.5, -80.9},
                                                               0.002)),
  };
  EXPECT_EQ(DedupeSearchRows(in, 250.0).size(), 2u);
}

TEST(SearchRules, ARidersOwnMarkerNamedForAStreetSurvivesBesideIt) {
  // Kind is part of the test. Somebody who dropped a point on Ruddy Turnstone
  // and named it that meant to have both, and the rows read "Ruddy Turnstone
  // — Point" and "Ruddy Turnstone — Road", which is the distinction the
  // vocabulary was chosen to make.
  const std::vector<SearchRow> in{
      Row("Ruddy Turnstone", SearchKind::kRoad, kRuddyTurnstone,
          Box(kRuddyTurnstone, 0.004)),
      Row("Ruddy Turnstone", SearchKind::kPoint, kRuddyTurnstone,
          Point(kRuddyTurnstone)),
  };
  EXPECT_EQ(DedupeSearchRows(in, 250.0).size(), 2u);
}

TEST(SearchRules, TwoDegeneratePointsFallBackToTheRadius) {
  // A point's honest bounds is `ll == ur`, so two of them never overlap and
  // the distance is the only test there is.
  const GeoPoint a = kRuddyTurnstone;
  const GeoPoint near_a{a.lat + 0.0009, a.lon};  // ~100 m north
  const std::vector<SearchRow> close{
      Row("Sandcastle", SearchKind::kPoint, a, Point(a)),
      Row("Sandcastle", SearchKind::kPoint, near_a, Point(near_a)),
  };
  EXPECT_EQ(DedupeSearchRows(close, 250.0).size(), 1u);
  // And the same pair with the radius tightened below their separation is two
  // rows — which is what makes `search.duplicate_radius_m` a real knob rather
  // than a number nothing reads.
  EXPECT_EQ(DedupeSearchRows(close, 50.0).size(), 2u);
}

TEST(SearchRules, ARadiusOfZeroLeavesOnlyTheBoxTest) {
  // 0 is the honest way to spell "only merge what demonstrably overlaps".
  const GeoPoint a = kRuddyTurnstone;
  const GeoPoint near_a{a.lat + 0.00001, a.lon};  // about a metre
  const std::vector<SearchRow> in{
      Row("Sandcastle", SearchKind::kPoint, a, Point(a)),
      Row("Sandcastle", SearchKind::kPoint, near_a, Point(near_a)),
  };
  EXPECT_EQ(DedupeSearchRows(in, 0.0).size(), 2u);
}

TEST(SearchRules, TheOrderOfTheRankedListIsNeverDisturbed) {
  const std::vector<SearchRow> in{
      Row("Flyaway Drive", SearchKind::kRoad, kBeachClub, Box(kBeachClub, 0.002)),
      Row("Ruddy Turnstone", SearchKind::kRoad, kRuddyTurnstone,
          Box(kRuddyTurnstone, 0.002)),
      Row("Ruddy Turnstone", SearchKind::kRoad, kRuddyTurnstone,
          Box(kRuddyTurnstone, 0.001)),
      Row("Turtle Point", SearchKind::kPoi, kBeachClub, Point(kBeachClub)),
  };
  const std::vector<SearchRow> out = DedupeSearchRows(in, 250.0);
  ASSERT_EQ(out.size(), 3u);
  EXPECT_EQ(out[0].title, "Flyaway Drive");
  EXPECT_EQ(out[1].title, "Ruddy Turnstone");
  EXPECT_EQ(out[2].title, "Turtle Point");
}

TEST(SearchRules, CaseAndSpacingDoNotMakeASecondStreet) {
  const GeoRect street{{32.600, -80.115}, {32.610, -80.100}};
  const std::vector<SearchRow> in{
      Row("Ruddy Turnstone", SearchKind::kRoad, kRuddyTurnstone, street),
      Row("RUDDY  turnstone", SearchKind::kRoad, kRuddyTurnstone, street),
  };
  EXPECT_EQ(DedupeSearchRows(in, 250.0).size(), 1u);
}

TEST(SearchRules, AnEmptyListIsAnEmptyList) {
  EXPECT_TRUE(DedupeSearchRows({}, 250.0).empty());
}

TEST(SearchRules, TouchingBoxesCount) {
  // Two halves of one street cut at a tile seam meet exactly on it, so the
  // overlap test is closed rather than open.
  const SearchRow west = Row("Sea Marsh", SearchKind::kRoad, {32.60, -80.12},
                             GeoRect{{32.60, -80.13}, {32.61, -80.11}});
  const SearchRow east = Row("Sea Marsh", SearchKind::kRoad, {32.60, -80.10},
                             GeoRect{{32.60, -80.11}, {32.61, -80.09}});
  EXPECT_TRUE(SearchRowsAreDuplicates(west, east, 0.0));
}

}  // namespace
