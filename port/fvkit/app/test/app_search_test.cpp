// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Global search, S1: the seam (port/search-plan-COMPLETE.md).
//
// What has to be pinned here is not "a search returns something" -- it is the
// handful of rules that only the AGGREGATOR can get right, because a provider
// cannot see enough to get them wrong on its own:
//
//   * the match rule is SHARED. "rud tur" has to mean the same thing to every
//     source, or a user learns a search box that behaves differently depending
//     on what happens to be in the stack;
//   * ordering is the SESSION's, tie-broken by stack order, so the same query
//     twice comes back in the same order;
//   * the radius is a CIRCLE even though the providers were handed a box;
//   * hidden overlays ANSWER, which is the deliberate opposite of pick, and
//     `visible_only` is how a caller asks for pick's rule instead;
//   * a cancelled search RETURNS, with what it had, ranked.

#include "fvkit/app/search.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/overlay/manager.h"
#include "fvkit/overlay/point_overlay.h"

namespace {

using fv::GeoPoint;
using fv::GeoRect;
using fv::MapPoint;
using fv::Overlay;
using fv::OverlayManager;
using fv::PointOverlay;
using fv::app::SearchOrder;
using fv::app::SearchProvider;
using fv::app::SearchQuery;
using fv::app::SearchResult;
using fv::app::SearchSession;

// One metre of latitude, in degrees -- every fixture below is laid out in
// these so a distance in the assertions is a distance in the fixture.
constexpr double kMetersPerDegLat = 6371008.8 * 3.14159265358979323846 / 180.0;
double MetersLat(double m) { return m / kMetersPerDegLat; }
double MetersLon(double m, double lat) {
  return m / (kMetersPerDegLat * std::cos(lat * 3.14159265358979323846 / 180.0));
}

// ---------------------------------------------------------------------------
// Doubles
// ---------------------------------------------------------------------------

// A provider with a scripted answer. It honours area and text through the
// SHARED helpers, exactly as a real ten-line provider does -- a double that
// matched by its own rule would be testing nothing about the seam.
class Findable : public Overlay, public SearchProvider {
 public:
  struct Item {
    std::string title;
    GeoPoint position;
  };

  explicit Findable(std::string name) : Overlay(std::move(name)) {}

  SearchProvider* AsSearch() override { return this; }

  void Search(const SearchQuery& q, const std::atomic<bool>& cancel,
              std::vector<SearchResult>& out) override {
    ++calls;
    last_area = q.area;
    for (const Item& i : items) {
      if (cancel.load()) return;
      if (cancel_after_first != nullptr) cancel_after_first->store(true);
      if (q.max_results > 0 && appended >= q.max_results) return;
      if (!fv::app::SearchAreaAccepts(q, i.position)) continue;
      int quality = 0;
      if (!fv::app::SearchTextAccepts(q, i.title, &quality)) continue;
      SearchResult r;
      r.title = i.title;
      r.detail = "fake";
      r.position = i.position;
      r.bounds = GeoRect{i.position, i.position};
      r.match_quality = quality;
      r.feature = ++minted;
      out.push_back(std::move(r));
      ++appended;
    }
  }

  std::vector<Item> items;
  int calls = 0;
  size_t appended = 0;
  uint64_t minted = 0;
  std::optional<GeoRect> last_area;
  // Set the flag as soon as one item has been considered, to prove a provider
  // that gives up mid-scan is still a legal one.
  std::atomic<bool>* cancel_after_first = nullptr;
};

// A provider that ignores max_results entirely, to prove the session does not
// merely ASK for a cap.
class Chatty : public Overlay, public SearchProvider {
 public:
  Chatty() : Overlay("Chatty") {}
  SearchProvider* AsSearch() override { return this; }
  void Search(const SearchQuery&, const std::atomic<bool>&,
              std::vector<SearchResult>& out) override {
    for (int i = 0; i < 100; ++i) {
      SearchResult r;
      r.title = "row " + std::to_string(i);
      r.position = GeoPoint{32.0, -80.0};
      out.push_back(std::move(r));
    }
  }
};

std::shared_ptr<PointOverlay> MakePoints(std::string name,
                                         std::vector<MapPoint> pts) {
  auto o = std::make_shared<PointOverlay>(std::move(name));
  o->SetPoints(std::move(pts));
  return o;
}

MapPoint Pt(int64_t id, std::string name, double lat, double lon,
            std::string category = std::string()) {
  MapPoint p;
  p.id = id;
  p.name = std::move(name);
  p.position = GeoPoint{lat, lon};
  p.category = std::move(category);
  return p;
}

std::vector<std::string> Titles(const std::vector<SearchResult>& r) {
  std::vector<std::string> out;
  for (const SearchResult& s : r) out.push_back(s.title);
  return out;
}

// ---------------------------------------------------------------------------
// The shared match rule
// ---------------------------------------------------------------------------

TEST(SearchMatch, QualityLadder) {
  using fv::app::TextMatchQuality;
  // 0 exact, and case is not part of it.
  EXPECT_EQ(0, TextMatchQuality("Ruddy Turnstone", "Ruddy Turnstone"));
  EXPECT_EQ(0, TextMatchQuality("ruddy turnstone", "Ruddy Turnstone"));
  EXPECT_EQ(0, TextMatchQuality("RUDDY TURNSTONE", "Ruddy Turnstone"));
  // 1 a prefix of the whole string.
  EXPECT_EQ(1, TextMatchQuality("ruddy tur", "Ruddy Turnstone"));
  EXPECT_EQ(1, TextMatchQuality("r", "Ruddy Turnstone"));
  // 2 token prefixes, in any order -- the contract the plan states by name.
  EXPECT_EQ(2, TextMatchQuality("rud tur", "Ruddy Turnstone"));
  EXPECT_EQ(2, TextMatchQuality("turnstone", "Ruddy Turnstone"));
  EXPECT_EQ(2, TextMatchQuality("turn rud", "Ruddy Turnstone"));
  // Every token has to land: a second word NARROWS a search.
  EXPECT_EQ(-1, TextMatchQuality("ruddy bufflehead", "Ruddy Turnstone"));
  EXPECT_EQ(-1, TextMatchQuality("urnstone", "Ruddy Turnstone"));
  // An empty query matches everything, at the top quality -- a spatial-only
  // search is not a text search that everything fails.
  EXPECT_EQ(0, TextMatchQuality("", "Ruddy Turnstone"));
  EXPECT_EQ(0, TextMatchQuality("   ", "Ruddy Turnstone"));
}

TEST(SearchMatch, WhitespaceDoesNotDemoteAnExactHit) {
  using fv::app::TextMatchQuality;
  // A search box hands over whatever was typed, and a trailing space is the
  // most ordinary thing in it.
  EXPECT_EQ(0, TextMatchQuality("ruddy turnstone ", "Ruddy Turnstone"));
  EXPECT_EQ(0, TextMatchQuality(" ruddy  turnstone", "Ruddy Turnstone"));
}

TEST(SearchMatch, PunctuationStaysWithItsWord) {
  using fv::app::TextMatchQuality;
  EXPECT_EQ(2, TextMatchQuality("st hel", "St. Helena Sound"));
  EXPECT_EQ(1, TextMatchQuality("st. hel", "St. Helena Sound"));
}

TEST(SearchMatch, NonAsciiComparesExactly) {
  using fv::app::TextMatchQuality;
  // The stated limit: ASCII folding only, because the alternative is ICU and
  // the usual shortcut corrupts UTF-8. A name still matches itself.
  EXPECT_EQ(0, TextMatchQuality("\xc3\x85ngstr\xc3\xb6m",
                                "\xc3\x85ngstr\xc3\xb6m"));
  EXPECT_EQ(-1, TextMatchQuality("\xc3\xa5ngstr\xc3\xb6m",
                                 "\xc3\x85ngstr\xc3\xb6m"));
}

// ---------------------------------------------------------------------------
// The metre
// ---------------------------------------------------------------------------

TEST(SearchDistance, IsTheSameMetreAsTheRoadSnapper) {
  // One degree of latitude, in the WGS-84 mean-radius metre port/Routing
  // builds arcs in. If this number ever moves, a "within 500 m" search and a
  // 500 m road snap have stopped meaning the same thing.
  EXPECT_NEAR(kMetersPerDegLat,
              fv::app::SearchDistanceMeters(GeoPoint{0.0, 0.0},
                                            GeoPoint{1.0, 0.0}),
              0.01);
  // East-west shrinks with the cosine.
  EXPECT_NEAR(100.0,
              fv::app::SearchDistanceMeters(
                  GeoPoint{32.6, -80.1},
                  GeoPoint{32.6, -80.1 + MetersLon(100.0, 32.6)}),
              0.05);
  EXPECT_DOUBLE_EQ(0.0, fv::app::SearchDistanceMeters(GeoPoint{32.6, -80.1},
                                                      GeoPoint{32.6, -80.1}));
}

// ---------------------------------------------------------------------------
// PointOverlay, the first provider
// ---------------------------------------------------------------------------

TEST(SearchPointOverlay, FindsByNameAndCarriesProvenance) {
  auto points = MakePoints("Points", {Pt(7, "Ruddy Turnstone", 32.60, -80.10),
                                      Pt(8, "Bufflehead Dr", 32.61, -80.11)});
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(points).ok());

  SearchQuery q;
  q.text = "rud tur";
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ("Ruddy Turnstone", r[0].title);
  EXPECT_EQ(2, r[0].match_quality);
  EXPECT_EQ(points.get(), r[0].overlay);
  // The document's own id, which survives a redraw -- a point HAS identity in
  // the file, unlike a vector feature's minted handle.
  EXPECT_EQ(7u, r[0].feature);
  EXPECT_DOUBLE_EQ(32.60, r[0].position.lat);
  // A point has no extent: the box is the point.
  EXPECT_DOUBLE_EQ(32.60, r[0].bounds.ll.lat);
  EXPECT_DOUBLE_EQ(32.60, r[0].bounds.ur.lat);
  EXPECT_DOUBLE_EQ(-80.10, r[0].bounds.ur.lon);
}

TEST(SearchPointOverlay, FallsBackToCategoryForTheLabel) {
  auto points = MakePoints(
      "Points", {Pt(1, "", 32.60, -80.10, "restaurant"),
                 Pt(2, "Ruddy Turnstone", 32.61, -80.11, "junction")});
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(points).ok());

  SearchQuery q;
  q.text = "restaurant";
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ("restaurant", r[0].title);
  EXPECT_EQ(1u, r[0].feature);
  // The detail line does not repeat a title that IS the category.
  EXPECT_EQ("point", r[0].detail);

  // A named point states its category there instead, which is what tells two
  // rows with the same name apart.
  q.text = "ruddy";
  const std::vector<SearchResult> named = SearchSession(mgr).Search(q);
  ASSERT_EQ(1u, named.size());
  EXPECT_EQ("point \xc2\xb7 junction", named[0].detail);
}

TEST(SearchPointOverlay, AreaFiltersWithoutText) {
  auto points = MakePoints("Points", {Pt(1, "In", 32.60, -80.10),
                                      Pt(2, "Out", 33.60, -80.10)});
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(points).ok());

  SearchQuery q;
  q.area = GeoRect{{32.5, -80.2}, {32.7, -80.0}};
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ("In", r[0].title);
  // Spatial-only: nothing was matched, so nothing is demoted.
  EXPECT_EQ(0, r[0].match_quality);
}

// ---------------------------------------------------------------------------
// The session: who is asked
// ---------------------------------------------------------------------------

TEST(SearchSessionTest, HiddenOverlaysAnswerUnlessAskedNotTo) {
  auto visible = MakePoints("Visible", {Pt(1, "Ruddy Turnstone", 32.60, -80.10)});
  auto hidden = MakePoints("Hidden", {Pt(2, "Ruddy Turnstone", 32.61, -80.11)});
  hidden->SetVisible(false);
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(visible).ok());
  ASSERT_TRUE(mgr.Add(hidden).ok());

  SearchQuery q;
  q.text = "ruddy";
  // The default, and the whole reason search is not a flavour of pick: "where
  // is X" is a legitimate question about a layer that is switched off.
  std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(2u, r.size());

  q.visible_only = true;
  r = SearchSession(mgr).Search(q);
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ(visible.get(), r[0].overlay);
}

TEST(SearchSessionTest, VisibleOnlyIsExactlyThePickLayersSet) {
  auto a = MakePoints("A", {Pt(1, "Ruddy Turnstone", 32.60, -80.10)});
  auto b = MakePoints("B", {Pt(2, "Ruddy Turnstone", 32.61, -80.11)});
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(a).ok());
  ASSERT_TRUE(mgr.Add(b).ok());
  ASSERT_TRUE(mgr.MakeCurrent(b).ok());
  mgr.SetDeclutter(true);

  SearchQuery q;
  q.text = "ruddy";
  q.visible_only = true;
  std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ(b.get(), r[0].overlay);

  // Declutter is a DRAWING decision, so it stops mattering the moment the
  // question stops being about the screen.
  q.visible_only = false;
  EXPECT_EQ(2u, SearchSession(mgr).Search(q).size());
}

TEST(SearchSessionTest, AnOverlayWithoutTheCapabilityIsSkipped) {
  auto plain = std::make_shared<Overlay>("Plain");
  auto points = MakePoints("Points", {Pt(1, "Ruddy Turnstone", 32.60, -80.10)});
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(plain).ok());
  ASSERT_TRUE(mgr.Add(points).ok());
  EXPECT_EQ(nullptr, plain->AsSearch());

  SearchQuery q;
  q.text = "ruddy";
  EXPECT_EQ(1u, SearchSession(mgr).Search(q).size());
}

// ---------------------------------------------------------------------------
// The session: ordering
// ---------------------------------------------------------------------------

TEST(SearchOrdering, BestMatchIsQualityThenDistanceThenStackOrder) {
  auto lower = std::make_shared<Findable>("Lower");
  auto upper = std::make_shared<Findable>("Upper");
  const GeoPoint here{32.60, -80.10};
  // Same quality (2, a token match), different distances -- and the NEARER one
  // is in the LOWER overlay, the only arrangement where the two keys disagree.
  lower->items = {{"Turnstone Court", {here.lat + MetersLat(100), here.lon}}};
  upper->items = {{"Turnstone Lane", {here.lat + MetersLat(400), here.lon}},
                  // An exact hit, furthest of all: quality wins over distance.
                  {"turnstone", {here.lat + MetersLat(900), here.lon}}};
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(lower).ok());
  ASSERT_TRUE(mgr.Add(upper).ok());

  SearchQuery q;
  q.text = "turnstone";
  q.near = here;
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(3u, r.size());
  EXPECT_EQ("turnstone", r[0].title);        // quality 0
  EXPECT_EQ("Turnstone Court", r[1].title);  // quality 2, 100 m
  EXPECT_EQ("Turnstone Lane", r[2].title);   // quality 2, 400 m
}

TEST(SearchOrdering, StackOrderIsTheStableTieBreak) {
  auto lower = std::make_shared<Findable>("Lower");
  auto upper = std::make_shared<Findable>("Upper");
  // Same title, same position: nothing separates them but the stack.
  lower->items = {{"Marker", {32.60, -80.10}}};
  upper->items = {{"Marker", {32.60, -80.10}}};
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(lower).ok());
  ASSERT_TRUE(mgr.Add(upper).ok());

  SearchQuery q;
  q.text = "marker";
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(2u, r.size());
  EXPECT_EQ(upper.get(), r[0].overlay);  // topmost first
  EXPECT_EQ(lower.get(), r[1].overlay);

  // And it is STABLE: the same query twice is the same list.
  const std::vector<SearchResult> again = SearchSession(mgr).Search(q);
  EXPECT_EQ(Titles(r), Titles(again));
  EXPECT_EQ(upper.get(), again[0].overlay);
}

TEST(SearchOrdering, AutoIsNearestWithoutTextAndBestMatchWithIt) {
  auto o = std::make_shared<Findable>("O");
  const GeoPoint here{32.60, -80.10};
  o->items = {{"far exact", {here.lat + MetersLat(900), here.lon}},
              {"near token thing", {here.lat + MetersLat(100), here.lon}}};
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(o).ok());

  // Spatial only: there is no quality to rank by, so distance decides.
  SearchQuery spatial;
  spatial.near = here;
  const std::vector<SearchResult> by_distance =
      SearchSession(mgr).Search(spatial);
  ASSERT_EQ(2u, by_distance.size());
  EXPECT_EQ("near token thing", by_distance[0].title);

  // With text, the exact hit comes first even though it is nine times further.
  SearchQuery text = spatial;
  text.text = "far exact";
  const std::vector<SearchResult> by_match = SearchSession(mgr).Search(text);
  ASSERT_EQ(1u, by_match.size());
  EXPECT_EQ("far exact", by_match[0].title);

  // ...and kNearest is how a caller says "order by distance anyway".
  text.text = "";
  text.order = SearchOrder::kNearest;
  EXPECT_EQ("near token thing", SearchSession(mgr).Search(text)[0].title);
}

TEST(SearchOrdering, NearestFallsBackToTheAreaCentre) {
  auto o = std::make_shared<Findable>("O");
  o->items = {{"north", {32.69, -80.10}}, {"middle", {32.60, -80.10}}};
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(o).ok());

  SearchQuery q;
  q.area = GeoRect{{32.5, -80.2}, {32.7, -80.0}};  // centre 32.60
  q.order = SearchOrder::kNearest;
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(2u, r.size());
  EXPECT_EQ("middle", r[0].title);
}

// ---------------------------------------------------------------------------
// The session: the circle the providers never see
// ---------------------------------------------------------------------------

TEST(SearchRadius, ProvidersGetABoxAndTheSessionCutsTheCircle) {
  auto o = std::make_shared<Findable>("O");
  const GeoPoint here{32.60, -80.10};
  // Due north at 400 m: inside a 500 m circle.
  const GeoPoint north{here.lat + MetersLat(400), here.lon};
  // On the diagonal at 400 m each way -- 566 m away, so INSIDE the box the
  // provider is given and OUTSIDE the circle the caller asked for. This is the
  // whole reason the cut exists.
  const GeoPoint corner{here.lat + MetersLat(400),
                        here.lon + MetersLon(400, here.lat)};
  o->items = {{"north", north}, {"corner", corner}};
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(o).ok());

  SearchQuery q;
  q.near = here;
  q.radius_m = 500.0;
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ("north", r[0].title);

  // The provider was handed a BOX, not a circle: it saw the corner and said
  // yes to it, and the session is what threw it away.
  ASSERT_TRUE(o->last_area.has_value());
  EXPECT_TRUE(o->last_area->Contains(corner));
  EXPECT_NEAR(500.0,
              fv::app::SearchDistanceMeters(here, {o->last_area->ur.lat,
                                                   here.lon}),
              1.0);
}

TEST(SearchRadius, WithNoRadiusTheOriginOnlyOrders) {
  auto o = std::make_shared<Findable>("O");
  const GeoPoint here{32.60, -80.10};
  o->items = {{"far", {here.lat + 1.0, here.lon}}, {"near", here}};
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(o).ok());

  SearchQuery q;
  q.near = here;  // radius_m stays 0
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(2u, r.size());
  EXPECT_EQ("near", r[0].title);
  EXPECT_FALSE(o->last_area.has_value());  // no cut, so no box either
}

TEST(SearchRadius, TheCoarseBoxIsTheIntersectionOfBothFilters) {
  auto o = std::make_shared<Findable>("O");
  const GeoPoint here{32.60, -80.10};
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(o).ok());

  SearchQuery q;
  q.area = GeoRect{{32.0, -81.0}, {33.0, -79.0}};  // a county
  q.near = here;
  q.radius_m = 500.0;  // a street corner
  SearchSession(mgr).Search(q);
  ASSERT_TRUE(o->last_area.has_value());
  // The provider is asked about the smaller of the two, which is what keeps a
  // tile-scanning provider from reading the county.
  EXPECT_LT(o->last_area->ur.lat - o->last_area->ll.lat, 0.02);
  EXPECT_TRUE(o->last_area->Contains(here));
}

// ---------------------------------------------------------------------------
// The session: caps and cancellation
// ---------------------------------------------------------------------------

TEST(SearchLimits, MaxResultsIsEnforcedOnAProviderThatIgnoresIt) {
  auto chatty = std::make_shared<Chatty>();
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(chatty).ok());

  SearchQuery q;
  q.max_results = 5;
  // The provider appends a hundred rows and never looks at the cap. The
  // session trims it rather than trusting it, which is what stops one
  // overlay's table from being the whole answer.
  EXPECT_EQ(5u, SearchSession(mgr).Search(q).size());
}

TEST(SearchLimits, TheCapIsAppliedAfterRankingSoTheBestRowsSurvive) {
  // The case the cap is really for: a chatty provider ON TOP of a small one.
  // Cutting the merged list at the walk order would lose the point overlay's
  // row entirely; cutting it after the RANKING keeps the row that answers the
  // question and drops a hundred that do not.
  const GeoPoint here{32.60, -80.10};
  auto points = MakePoints("Points", {Pt(1, "Ruddy Turnstone", here.lat,
                                         here.lon)});
  auto chatty = std::make_shared<Chatty>();  // every row at 32.0, -80.0
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(points).ok());
  ASSERT_TRUE(mgr.Add(chatty).ok());  // topmost, so it answers first

  SearchQuery q;
  q.near = here;
  q.max_results = 5;
  const std::vector<SearchResult> r = SearchSession(mgr).Search(q);
  ASSERT_EQ(5u, r.size());
  EXPECT_EQ(points.get(), r[0].overlay);
  EXPECT_EQ("Ruddy Turnstone", r[0].title);
}

TEST(SearchLimits, ZeroMeansNoCap) {
  auto chatty = std::make_shared<Chatty>();
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(chatty).ok());

  SearchQuery q;
  q.max_results = 0;
  EXPECT_EQ(100u, SearchSession(mgr).Search(q).size());
}

TEST(SearchCancel, APartialAnswerIsStillRankedAndTheWalkStops) {
  std::atomic<bool> cancel{false};
  auto upper = std::make_shared<Findable>("Upper");
  auto lower = std::make_shared<Findable>("Lower");
  const GeoPoint here{32.60, -80.10};
  upper->items = {{"first", here},
                  {"second", {here.lat + MetersLat(10), here.lon}}};
  lower->items = {{"never asked", here}};
  // Set the flag while the first item is being considered: the provider
  // returns after appending it, and the session must not go on to `lower`.
  upper->cancel_after_first = &cancel;
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(lower).ok());
  ASSERT_TRUE(mgr.Add(upper).ok());

  const std::vector<SearchResult> r = SearchSession(mgr).Search({}, cancel);
  EXPECT_EQ(1, upper->calls);
  EXPECT_EQ(0, lower->calls);
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ("first", r[0].title);
}

TEST(SearchCancel, AFlagAlreadySetAsksNobody) {
  std::atomic<bool> cancel{true};
  auto o = std::make_shared<Findable>("O");
  o->items = {{"anything", {32.60, -80.10}}};
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(o).ok());

  EXPECT_TRUE(SearchSession(mgr).Search({}, cancel).empty());
  EXPECT_EQ(0, o->calls);
}

TEST(SearchSessionTest, AnEmptyStackAnswersNothingRatherThanFailing) {
  OverlayManager mgr;
  SearchQuery q;
  q.text = "ruddy";
  EXPECT_TRUE(SearchSession(mgr).Search(q).empty());
}

TEST(SearchSessionTest, TheProviderNeedNotStampItself) {
  auto o = std::make_shared<Findable>("O");
  o->items = {{"row", {32.60, -80.10}}};
  OverlayManager mgr;
  ASSERT_TRUE(mgr.Add(o).ok());
  // Findable leaves `overlay` null, like PickSession::Gather's contract.
  const std::vector<SearchResult> r = SearchSession(mgr).Search({});
  ASSERT_EQ(1u, r.size());
  EXPECT_EQ(o.get(), r[0].overlay);
}

}  // namespace
