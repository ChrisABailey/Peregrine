// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::VectorMapOverlay tests (search-plan-COMPLETE.md S2) — the wrapper that
// lets a
// vector MAP be found by the same session that finds a point set.
//
// The source here is a fake, and deliberately: everything this class decides
// (which tag is the label, what counts as one row, where a row's anchor sits,
// what a query with no area answers) is decided ABOVE the source seam, so a
// fake that returns three features is a better test of it than a pyramid that
// returns thirty thousand. The real pyramid, the tile budget and "Ruddy
// Turnstone finds the road AND the point" are pinned in port/Osm/test.

#include "fvkit/overlay/vector_map_overlay.h"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fvkit/app/search.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/overlay/point_overlay.h"

namespace {

using fv::FeatureRef;
using fv::GeoPoint;
using fv::GeoRect;
using fv::VectorFeature;
using fv::VectorGeometryType;
using fv::VectorMapOverlay;
using fv::app::SearchQuery;
using fv::app::SearchResult;

// A source that hands back whatever it was loaded with, area-filtered on the
// feature box the way every real source does.
class FakeSource : public fv::IVectorSource {
 public:
  fv::Status Open(const std::string&) override {
    open_ = true;
    return fv::Status::Ok();
  }
  bool IsOpen() const override { return open_; }
  void SetOpen(bool o) { open_ = o; }
  GeoRect Bounds() const override { return GeoRect::World(); }
  std::vector<std::string> Layers() const override { return layers_; }

  fv::Status Query(const fv::VectorQuery& q,
                   std::vector<VectorFeature>* out) override {
    ++queries;
    last_scale = q.scale_denominator;
    last_max_features = q.max_features;
    last_area = q.area;
    if (fail) return fv::Status::Error(fv::kNotFound, "no tiles");
    for (const VectorFeature& f : features_) {
      if (!f.bounds.Intersects(q.area)) continue;
      if (q.max_features != 0 && out->size() >= q.max_features) break;
      out->push_back(f);
    }
    return fv::Status::Ok();
  }

  // --- the name index (S3) -------------------------------------------------
  //
  // A DUMB index on purpose: it hands back everything it holds (minus the
  // area cut and the cap a real one would do in SQL), so that what these
  // tests pin is what the OVERLAY does with an indexed answer — the shared
  // match rule, the area, the cap, the mint — rather than a fake's cleverness.
  bool HasNameIndex() const override { return has_index_; }

  fv::Status SearchNames(const fv::VectorNameQuery& q,
                         std::vector<fv::VectorNameHit>* out) override {
    ++name_queries;
    last_name_max = q.max_results;
    last_name_text = q.text;
    last_name_area = q.area;
    if (index_fails) return fv::Status::Error(fv::kIoError, "index gone");
    for (const fv::VectorNameHit& h : index_) {
      if (q.max_results != 0 && out->size() >= q.max_results) break;
      if (q.area && !q.area->Intersects(h.bounds)) continue;
      out->push_back(h);
    }
    return fv::Status::Ok();
  }

  // Index the feature `f` as the name index would have stored it: its own
  // name tag, its box, its ref.
  void Index(const VectorFeature& f, int rank = 14) {
    fv::VectorNameHit h;
    const std::string* n = f.Attribute("name");
    h.name = n != nullptr ? *n : std::string();
    h.layer = f.layer;
    h.style_key = f.style_key;
    h.bounds = f.bounds;
    h.position = GeoPoint{0.5 * (f.bounds.ll.lat + f.bounds.ur.lat),
                          0.5 * (f.bounds.ll.lon + f.bounds.ur.lon)};
    h.rank = rank;
    h.ref = f.ref;
    index_.push_back(std::move(h));
    has_index_ = true;
  }
  void SetHasIndex(bool on) { has_index_ = on; }

  int name_queries = 0;
  size_t last_name_max = 0;
  std::string last_name_text;
  std::optional<GeoRect> last_name_area;
  bool index_fails = false;

  fv::Status Describe(const FeatureRef& ref,
                      fv::FeatureDescription* out) override {
    for (const VectorFeature& f : features_) {
      if (!(f.ref == ref)) continue;
      out->ref = ref;
      out->layer_name = f.layer;
      const std::string* n = f.Attribute("name");
      if (n != nullptr) out->title = *n;
      return fv::Status::Ok();
    }
    return fv::Status::Error(fv::kNotFound, "no such feature");
  }

  void Add(VectorFeature f) {
    if (std::find(layers_.begin(), layers_.end(), f.layer) == layers_.end()) {
      layers_.push_back(f.layer);
    }
    features_.push_back(std::move(f));
  }

  int queries = 0;
  double last_scale = -1.0;
  size_t last_max_features = 999;
  GeoRect last_area{};
  bool fail = false;

 private:
  bool open_ = false;
  bool has_index_ = false;
  std::vector<VectorFeature> features_;
  std::vector<fv::VectorNameHit> index_;
  std::vector<std::string> layers_;
};

// A line feature, one part, from a list of (lat, lon).
VectorFeature Road(const char* name, const char* klass,
                   std::vector<GeoPoint> pts, int tile, int feature) {
  VectorFeature f;
  f.type = VectorGeometryType::kLine;
  f.layer = "transportation_name";
  f.style_key = klass;
  if (name != nullptr) f.attributes.push_back({"name", name});
  f.parts.push_back(pts);
  f.bounds.ll = f.bounds.ur = pts.front();
  for (const GeoPoint& p : pts) {
    f.bounds.ll.lat = std::min(f.bounds.ll.lat, p.lat);
    f.bounds.ll.lon = std::min(f.bounds.ll.lon, p.lon);
    f.bounds.ur.lat = std::max(f.bounds.ur.lat, p.lat);
    f.bounds.ur.lon = std::max(f.bounds.ur.lon, p.lon);
  }
  f.ref.layer = 0;
  f.ref.tile = tile;
  f.ref.feature = feature;
  return f;
}

VectorFeature Poi(const char* name, const char* klass, GeoPoint p, int feature) {
  VectorFeature f;
  f.type = VectorGeometryType::kPoint;
  f.layer = "poi";
  f.style_key = klass;
  if (name != nullptr) f.attributes.push_back({"name", name});
  f.parts.push_back({p});
  f.bounds = GeoRect{p, p};
  f.ref.layer = 1;
  f.ref.tile = 0;
  f.ref.feature = feature;
  return f;
}

const std::atomic<bool>& NotCancelled() {
  static const std::atomic<bool> flag{false};
  return flag;
}

// Roughly a metre in degrees of latitude, for laying out fixtures by hand.
constexpr double kMetreLat = 1.0 / 111194.9;

std::shared_ptr<FakeSource> KiawahIsh() {
  auto src = std::make_shared<FakeSource>();
  src->Open("");
  // Three tile-cut pieces of ONE road, meeting end to end.
  src->Add(Road("Ruddy Turnstone", "residential",
                {{32.600, -80.100}, {32.600, -80.090}}, 1, 0));
  src->Add(Road("Ruddy Turnstone", "residential",
                {{32.600, -80.090}, {32.600, -80.080}}, 2, 0));
  src->Add(Road("Ruddy Turnstone", "residential",
                {{32.600, -80.080}, {32.600, -80.070}}, 3, 0));
  // A different road with a name that shares a token.
  src->Add(Road("Ruddy Duck Court", "residential",
                {{32.610, -80.100}, {32.610, -80.095}}, 1, 1));
  // Unnamed geometry, which is most of a real tile.
  src->Add(Road(nullptr, "service", {{32.605, -80.100}, {32.605, -80.099}}, 1,
                2));
  // A POI.
  src->Add(Poi("Turnstone Grill", "restaurant", {32.6005, -80.0955}, 0));
  return src;
}

GeoRect Island() { return GeoRect{{32.55, -80.17}, {32.67, -79.97}}; }

}  // namespace

// ---------------------------------------------------------------------------
// The label
// ---------------------------------------------------------------------------

TEST(VectorMapOverlay, MatchesTheLabelTagAndNotTheOtherTags) {
  VectorMapOverlay ov("Kiawah", KiawahIsh());
  SearchQuery q;
  q.area = Island();
  q.text = "ruddy turnstone";
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ("Ruddy Turnstone", out[0].title);
  EXPECT_EQ(0, out[0].match_quality);

  // `residential` is a style key, not a label: searching for it finds nothing,
  // because a provider decides WHICH string it matches and this one says the
  // name tag.
  out.clear();
  q.text = "residential";
  ov.Search(q, NotCancelled(), out);
  EXPECT_TRUE(out.empty());
}

TEST(VectorMapOverlay, LabelTagsAreAKnobAndTheFirstPresentOneWins) {
  auto src = std::make_shared<FakeSource>();
  src->Open("");
  VectorFeature f =
      Road(nullptr, "residential", {{32.6, -80.1}, {32.6, -80.09}}, 1, 0);
  f.attributes.push_back({"name:latin", "Ruddy Turnstone"});
  f.attributes.push_back({"OBJNAM", "Kiawah River"});
  src->Add(f);

  VectorMapOverlay ov("Pack", src);
  SearchQuery q;
  q.area = Island();
  q.text = "ruddy";
  std::vector<SearchResult> out;
  // The default chain reaches name:latin, which is what the delivered Kiawah
  // cut actually carries.
  ov.Search(q, NotCancelled(), out);
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ("Ruddy Turnstone", out[0].title);

  // A product that spells it something else says so, and nothing else changes.
  ov.SetLabelTags({"OBJNAM"});
  out.clear();
  ov.Search(q, NotCancelled(), out);
  EXPECT_TRUE(out.empty());
  out.clear();
  q.text = "kiawah";
  ov.Search(q, NotCancelled(), out);
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ("Kiawah River", out[0].title);
}

TEST(VectorMapOverlay, UnnamedGeometryIsNeverAResultEvenSpatially) {
  VectorMapOverlay ov("Kiawah", KiawahIsh());
  SearchQuery q;
  q.area = Island();  // spatial only, no text
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  for (const SearchResult& r : out) EXPECT_FALSE(r.title.empty());
  // The named things: the two roads and the POI. The unnamed service road is
  // read from the source and dropped here.
  EXPECT_EQ(3u, out.size());
  EXPECT_EQ(6u, ov.last_search_features());
  EXPECT_EQ(3u, ov.last_search_results());
}

TEST(VectorMapOverlay, TokenPrefixIsTheSharedRuleNotTheProvidersOwn) {
  VectorMapOverlay ov("Kiawah", KiawahIsh());
  SearchQuery q;
  q.area = Island();
  q.text = "rud tur";
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ("Ruddy Turnstone", out[0].title);
  EXPECT_EQ(2, out[0].match_quality);  // token match

  out.clear();
  q.text = "ruddy";
  ov.Search(q, NotCancelled(), out);
  EXPECT_EQ(2u, out.size());  // Turnstone and Duck Court
}

TEST(VectorMapOverlay, DetailIsTheLayerAndItsSubType) {
  VectorMapOverlay ov("Kiawah", KiawahIsh());
  SearchQuery q;
  q.area = Island();
  q.text = "turnstone grill";
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ("poi \xc2\xb7 restaurant", out[0].detail);
}

// ---------------------------------------------------------------------------
// One road, one row
// ---------------------------------------------------------------------------

TEST(VectorMapOverlay, TileCutPiecesOfOneRoadAreOneRow) {
  VectorMapOverlay ov("Kiawah", KiawahIsh());
  SearchQuery q;
  q.area = Island();
  q.text = "ruddy turnstone";
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  ASSERT_EQ(1u, out.size());
  // The merged row spans all three pieces...
  EXPECT_NEAR(-80.100, out[0].bounds.ll.lon, 1e-9);
  EXPECT_NEAR(-80.070, out[0].bounds.ur.lon, 1e-9);
  // ...and its anchor is ON the road, not merely inside the union box.
  EXPECT_NEAR(32.600, out[0].position.lat, 1e-9);

  // Merging off gives the raw pieces, which is what a caller building its own
  // index asks for.
  ov.SetMergeGapMeters(-1.0);
  out.clear();
  ov.Search(q, NotCancelled(), out);
  EXPECT_EQ(3u, out.size());
}

TEST(VectorMapOverlay, TheGapIsInMetresAndBoundsWhatMerges) {
  auto src = std::make_shared<FakeSource>();
  src->Open("");
  // Two pieces of "Main Street" 200 m apart in latitude.
  src->Add(Road("Main Street", "residential",
                {{32.600, -80.100}, {32.600, -80.090}}, 1, 0));
  src->Add(Road("Main Street", "residential",
                {{32.600 + 200.0 * kMetreLat, -80.100},
                 {32.600 + 200.0 * kMetreLat, -80.090}},
                1, 1));
  VectorMapOverlay ov("Pack", src);
  SearchQuery q;
  q.area = Island();
  q.text = "main";

  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);  // default gap 100 m
  EXPECT_EQ(2u, out.size());

  ov.SetMergeGapMeters(250.0);
  out.clear();
  ov.Search(q, NotCancelled(), out);
  EXPECT_EQ(1u, out.size());
}

TEST(VectorMapOverlay, ADifferentClassIsADifferentRowEvenWithTheSameName) {
  auto src = std::make_shared<FakeSource>();
  src->Open("");
  src->Add(Road("Kiawah Island Parkway", "primary",
                {{32.600, -80.100}, {32.600, -80.090}}, 1, 0));
  src->Add(Road("Kiawah Island Parkway", "cycleway",
                {{32.6001, -80.100}, {32.6001, -80.090}}, 1, 1));
  VectorMapOverlay ov("Pack", src);
  SearchQuery q;
  q.area = Island();
  q.text = "parkway";
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  // Two answers, and both are true -- the parkway a car takes and the path
  // beside it are different things with one name (Pippin P11's own case).
  ASSERT_EQ(2u, out.size());
  EXPECT_NE(out[0].detail, out[1].detail);
}

TEST(VectorMapOverlay, ABridgingPieceJoinsTwoClustersRatherThanPickingOne) {
  auto src = std::make_shared<FakeSource>();
  src->Open("");
  // Left and right, 300 m apart -- two clusters at the default gap...
  src->Add(Road("Long Road", "residential",
                {{32.600, -80.1000}, {32.600, -80.0990}}, 1, 0));
  src->Add(Road("Long Road", "residential",
                {{32.600, -80.0950}, {32.600, -80.0940}}, 1, 1));
  // ...and the middle piece, read LAST, which touches both.
  src->Add(Road("Long Road", "residential",
                {{32.600, -80.0990}, {32.600, -80.0950}}, 1, 2));
  VectorMapOverlay ov("Pack", src);
  SearchQuery q;
  q.area = Island();
  q.text = "long road";
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  ASSERT_EQ(1u, out.size());
  EXPECT_NEAR(-80.1000, out[0].bounds.ll.lon, 1e-9);
  EXPECT_NEAR(-80.0940, out[0].bounds.ur.lon, 1e-9);
}

// ---------------------------------------------------------------------------
// The area
// ---------------------------------------------------------------------------

TEST(VectorMapOverlay, NoAreaFindsNothingAndDoesNotAskTheSource) {
  auto src = KiawahIsh();
  VectorMapOverlay ov("Kiawah", src);
  SearchQuery q;
  q.text = "ruddy turnstone";  // no area: tier 1 has no index to answer with
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(0, src->queries);
}

// --- the lent window -------------------------------------------------------
//
// The way a shell says "you cannot answer globally, so look HERE" without
// putting that area on the QUERY — where it would also cut down the point
// documents and road graphs in the same stack, which never needed one.

TEST(VectorMapOverlay, TheFallbackAreaAnswersAQueryThatBroughtNone) {
  auto src = KiawahIsh();
  VectorMapOverlay ov("Kiawah", src);
  ov.SetSearchFallbackArea(Island());
  SearchQuery q;
  q.text = "ruddy turnstone";  // still no area of its own
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ("Ruddy Turnstone", out[0].title);
  // The source WAS asked, and asked about the lent box.
  EXPECT_EQ(1, src->queries);
  EXPECT_NEAR(Island().ll.lat, src->last_area.ll.lat, 1e-9);
  EXPECT_NEAR(Island().ur.lon, src->last_area.ur.lon, 1e-9);
}

TEST(VectorMapOverlay, AQueryWithItsOwnAreaIgnoresTheFallback) {
  auto src = KiawahIsh();
  VectorMapOverlay ov("Kiawah", src);
  // A window somewhere else entirely: if it were ever preferred over the
  // query's own box, this search would answer nothing.
  ov.SetSearchFallbackArea(GeoRect{{10.0, 10.0}, {11.0, 11.0}});
  SearchQuery q;
  q.area = Island();
  q.text = "ruddy turnstone";
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  EXPECT_EQ(1u, out.size());
  EXPECT_NEAR(Island().ll.lat, src->last_area.ll.lat, 1e-9);
}

TEST(VectorMapOverlay, ASpatialOnlyQueryIsNeverWindowed) {
  // "What is in this box" already has its box, and substituting a different
  // one answers a question nobody asked. A query with no text and no area is
  // not a search at all and still finds nothing.
  auto src = KiawahIsh();
  VectorMapOverlay ov("Kiawah", src);
  ov.SetSearchFallbackArea(Island());
  SearchQuery q;  // no text, no area
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(0, src->queries);
}

TEST(VectorMapOverlay, AnIndexedPackIsGlobalAndIsNeverWindowed) {
  // THE WHOLE REASON THE FALLBACK IS LAST. Tier 2 answers a global text query
  // properly, so a pack that has an index must not be quietly cut down to the
  // caller's viewport — the window is what a source does INSTEAD of failing,
  // not a scope the shell gets to impose.
  auto src = KiawahIsh();
  // Index Ruddy Turnstone, which lies OUTSIDE the tiny window lent below.
  src->Index(Road("Ruddy Turnstone", "residential",
                  {{32.600, -80.100}, {32.600, -80.070}}, 1, 0));
  VectorMapOverlay ov("Kiawah", src);
  ov.SetSearchFallbackArea(GeoRect{{10.0, 10.0}, {11.0, 11.0}});
  SearchQuery q;
  q.text = "ruddy turnstone";
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  EXPECT_TRUE(ov.last_search_used_index());
  EXPECT_EQ(1u, out.size());
  // The index was asked with NO area, which is what "global" means here.
  EXPECT_FALSE(src->last_name_area.has_value());
}

TEST(VectorMapOverlay, AFailedIndexFallsBackToTheWindowRatherThanToNothing) {
  auto src = KiawahIsh();
  src->Index(Road("Ruddy Turnstone", "residential",
                  {{32.600, -80.100}, {32.600, -80.070}}, 1, 0));
  src->index_fails = true;
  VectorMapOverlay ov("Kiawah", src);
  ov.SetSearchFallbackArea(Island());
  SearchQuery q;
  q.text = "ruddy turnstone";
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  EXPECT_FALSE(ov.last_search_used_index());
  EXPECT_EQ(1u, out.size());
  EXPECT_EQ(1, src->queries);
}

TEST(VectorMapOverlay, ADegenerateWindowIsNoWindow) {
  // A projection nobody has drawn with reports a zero-sized viewport, and a
  // shell should be able to hand that straight over — the alternative is
  // every caller repeating the same two comparisons before every search.
  auto src = KiawahIsh();
  VectorMapOverlay ov("Kiawah", src);
  const GeoPoint p{32.6, -80.1};
  ov.SetSearchFallbackArea(GeoRect{p, p});
  EXPECT_FALSE(ov.search_fallback_area().has_value());
  SearchQuery q;
  q.text = "ruddy turnstone";
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(0, src->queries);
}

TEST(VectorMapOverlay, TheWindowCanBeTakenBack) {
  auto src = KiawahIsh();
  VectorMapOverlay ov("Kiawah", src);
  ov.SetSearchFallbackArea(Island());
  ov.SetSearchFallbackArea(std::nullopt);
  EXPECT_FALSE(ov.search_fallback_area().has_value());
  SearchQuery q;
  q.text = "ruddy turnstone";
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  EXPECT_TRUE(out.empty());
}

TEST(VectorMapOverlay, ARoadThroughTheAreaIsInItEvenIfItsAnchorIsNot) {
  auto src = std::make_shared<FakeSource>();
  src->Open("");
  // A long road; the query box catches only its west end.
  src->Add(Road("Long Road", "residential",
                {{32.600, -80.100}, {32.600, -80.000}}, 1, 0));
  VectorMapOverlay ov("Pack", src);
  SearchQuery q;
  q.area = GeoRect{{32.599, -80.101}, {32.601, -80.099}};
  q.text = "long";
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  ASSERT_EQ(1u, out.size());
  // The anchor is the road's own midpoint, well outside the query box: the
  // result is honest about where the thing IS, and the session's ordering
  // measures to that.
  EXPECT_FALSE(q.area->Contains(out[0].position));
  EXPECT_NEAR(-80.05, out[0].position.lon, 1e-6);
}

TEST(VectorMapOverlay, MaxResultsCapsRowsAndNotPieces) {
  VectorMapOverlay ov("Kiawah", KiawahIsh());
  SearchQuery q;
  q.area = Island();
  q.max_results = 1;
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  // One ROW. Capping the scan instead would have spent the whole budget on
  // the first of three pieces of one road.
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ("Ruddy Turnstone", out[0].title);
}

// ---------------------------------------------------------------------------
// The source
// ---------------------------------------------------------------------------

TEST(VectorMapOverlay, ScaleAndFeatureCapAreHandedToTheSource) {
  auto src = KiawahIsh();
  VectorMapOverlay ov("Kiawah", src);
  SearchQuery q;
  q.area = Island();
  std::vector<SearchResult> out;

  ov.Search(q, NotCancelled(), out);
  // The defaults: no scale and no cap, so the SOURCE's own budget is the one
  // number that owns how much pyramid a search reads.
  EXPECT_EQ(0.0, src->last_scale);
  EXPECT_EQ(0u, src->last_max_features);

  ov.SetSearchScaleDenominator(50000.0);
  ov.SetMaxFeaturesPerSearch(2);
  out.clear();
  ov.Search(q, NotCancelled(), out);
  EXPECT_EQ(50000.0, src->last_scale);
  EXPECT_EQ(2u, src->last_max_features);
}

TEST(VectorMapOverlay, NoSourceAndAClosedSourceAreQuietlyEmpty) {
  VectorMapOverlay none("Empty");
  SearchQuery q;
  q.area = Island();
  q.text = "anything";
  std::vector<SearchResult> out;
  none.Search(q, NotCancelled(), out);
  EXPECT_TRUE(out.empty());

  auto src = KiawahIsh();
  src->SetOpen(false);
  VectorMapOverlay shut("Shut", src);
  shut.Search(q, NotCancelled(), out);
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(0, src->queries);
}

TEST(VectorMapOverlay, AFailedReadIsNoRowsRatherThanPartialOnes) {
  auto src = KiawahIsh();
  src->fail = true;
  VectorMapOverlay ov("Kiawah", src);
  SearchQuery q;
  q.area = Island();
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(1, src->queries);
}

TEST(VectorMapOverlay, CancelBeforeTheReadCostsTheReadItself) {
  auto src = KiawahIsh();
  VectorMapOverlay ov("Kiawah", src);
  SearchQuery q;
  q.area = Island();
  std::atomic<bool> cancel{true};
  std::vector<SearchResult> out;
  ov.Search(q, cancel, out);
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(0, src->queries);
}

TEST(VectorMapOverlay, ReplacingTheSourceDropsTheMint) {
  auto src = KiawahIsh();
  VectorMapOverlay ov("Kiawah", src);
  SearchQuery q;
  q.area = Island();
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  EXPECT_GT(ov.minted_count(), 0u);
  ov.SetSource(KiawahIsh());
  EXPECT_EQ(0u, ov.minted_count());
  fv::FeatureRef ref;
  EXPECT_FALSE(ov.FeatureRefFor(1, &ref));
}

// ---------------------------------------------------------------------------
// The mint
// ---------------------------------------------------------------------------

TEST(VectorMapOverlay, TheSameFeatureMintsTheSameIdTwice) {
  VectorMapOverlay ov("Kiawah", KiawahIsh());
  SearchQuery q;
  q.area = Island();
  q.text = "ruddy turnstone";
  std::vector<SearchResult> first, second;
  ov.Search(q, NotCancelled(), first);
  ov.Search(q, NotCancelled(), second);
  ASSERT_EQ(1u, first.size());
  ASSERT_EQ(1u, second.size());
  EXPECT_EQ(first[0].feature, second[0].feature);
  EXPECT_NE(0u, first[0].feature);
  EXPECT_EQ(1u, ov.minted_count());
}

TEST(VectorMapOverlay, DifferentFeaturesNeverShareAnId) {
  VectorMapOverlay ov("Kiawah", KiawahIsh());
  SearchQuery q;
  q.area = Island();
  q.text = "ruddy";
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  ASSERT_EQ(2u, out.size());
  EXPECT_NE(out[0].feature, out[1].feature);
}

TEST(VectorMapOverlay, TheIdGoesBackToTheHundredAndTwentyEightBitRef) {
  VectorMapOverlay ov("Kiawah", KiawahIsh());
  SearchQuery q;
  q.area = Island();
  q.text = "turnstone grill";
  std::vector<SearchResult> out;
  ov.Search(q, NotCancelled(), out);
  ASSERT_EQ(1u, out.size());

  FeatureRef ref;
  ASSERT_TRUE(ov.FeatureRefFor(out[0].feature, &ref));
  EXPECT_EQ(1, ref.layer);  // the poi layer in the fixture
  EXPECT_TRUE(ref.valid());
  EXPECT_FALSE(ov.FeatureRefFor(0, &ref));
  EXPECT_FALSE(ov.FeatureRefFor(out[0].feature + 99, &ref));

  fv::FeatureDescription d;
  const fv::Status s = ov.DescribeFeature(out[0].feature, &d);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ("Turnstone Grill", d.title);
  EXPECT_EQ("poi", d.layer_name);
  EXPECT_FALSE(ov.DescribeFeature(0, &d).ok());
}

// ---------------------------------------------------------------------------
// Through the session — the point of the whole exercise
// ---------------------------------------------------------------------------

TEST(VectorMapOverlay, OneSessionFindsTheRoadAndThePointTogether) {
  fv::OverlayManager manager;
  auto points = std::make_shared<fv::PointOverlay>("Points");
  fv::MapPoint mp;
  mp.name = "Ruddy Turnstone";
  mp.category = "landmark";
  mp.position = GeoPoint{32.6006, -80.0900};
  points->AddPoint(mp);
  ASSERT_TRUE(manager.Add(points).ok());

  auto map = std::make_shared<VectorMapOverlay>("Kiawah", KiawahIsh());
  // A search provider a shell need never draw: not visible, still found.
  map->SetVisible(false);
  ASSERT_TRUE(manager.Add(map).ok());

  fv::app::SearchSession session(manager);
  SearchQuery q;
  q.area = Island();
  q.text = "ruddy turnstone";
  const std::vector<SearchResult> hits = session.Search(q);

  ASSERT_EQ(2u, hits.size());
  bool from_map = false, from_points = false;
  for (const SearchResult& r : hits) {
    EXPECT_EQ("Ruddy Turnstone", r.title);
    if (r.overlay == map.get()) from_map = true;
    if (r.overlay == points.get()) from_points = true;
  }
  EXPECT_TRUE(from_map);
  EXPECT_TRUE(from_points);

  // ...and `visible_only` is the switch that hides the map again, because a
  // hidden overlay is not on the screen.
  q.visible_only = true;
  const std::vector<SearchResult> visible = session.Search(q);
  ASSERT_EQ(1u, visible.size());
  EXPECT_EQ(points.get(), visible[0].overlay);
}

// ---------------------------------------------------------------------------
// Tier 2 — the source's own name index (search-plan-COMPLETE.md, S3)
// ---------------------------------------------------------------------------
//
// Everything below is about the CHOICE between the two tiers and what the
// overlay does with an indexed answer. What an index actually contains, and
// that a built one agrees with the scan, is pinned over the real pyramid in
// port/Osm/test/osm_search_test.cpp — a fake index here would only be able to
// agree with itself.

namespace {

// The Kiawah-ish fixture with an index over the three named roads and the POI,
// as a build would have left it: one entry per named thing.
std::shared_ptr<FakeSource> IndexedKiawahIsh() {
  auto src = std::make_shared<FakeSource>();
  src->Open("");
  VectorFeature road = Road("Ruddy Turnstone", "residential",
                            {{32.600, -80.100}, {32.600, -80.070}}, 1, 0);
  VectorFeature other = Road("Ruddy Duck Court", "residential",
                             {{32.610, -80.100}, {32.610, -80.095}}, 1, 1);
  VectorFeature poi = Poi("Turnstone Grill", "restaurant",
                          {32.6005, -80.0955}, 0);
  src->Add(road);
  src->Add(other);
  src->Add(poi);
  src->Index(road, 14);
  src->Index(other, 14);
  src->Index(poi, 14);
  return src;
}

}  // namespace

TEST(VectorMapOverlayIndex, TextWithNoAreaAtAllIsAnsweredByTheIndex) {
  auto src = IndexedKiawahIsh();
  VectorMapOverlay map("Kiawah", src);

  SearchQuery q;
  q.text = "ruddy turnstone";  // no area: the query S2 could only refuse
  std::vector<SearchResult> hits;
  map.Search(q, NotCancelled(), hits);

  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("Ruddy Turnstone", hits[0].title);
  EXPECT_EQ("transportation_name \xc2\xb7 residential", hits[0].detail);
  EXPECT_TRUE(map.last_search_used_index());
  // AND NOT A SINGLE TILE WAS READ. That is the whole of S3: the pyramid was
  // walked once, at staging time, by somebody else.
  EXPECT_EQ(0, src->queries);
}

TEST(VectorMapOverlayIndex, TheIndexNarrowsButTheSharedRuleDecides) {
  auto src = IndexedKiawahIsh();
  VectorMapOverlay map("Kiawah", src);

  SearchQuery q;
  q.text = "ruddy turnstone";
  std::vector<SearchResult> hits;
  map.Search(q, NotCancelled(), hits);

  // The fake index hands back all three rows, the way a real FTS tokeniser
  // hands back more than the port's rule accepts. Exactly one survives, and
  // "Ruddy Duck Court" — which shares a token — is not it.
  EXPECT_EQ(3u, map.last_search_features());
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ(0, hits[0].match_quality);  // the rule's own quality, not the index's
}

TEST(VectorMapOverlayIndex, ASpatialOnlyQueryStillReadsTiles) {
  auto src = IndexedKiawahIsh();
  VectorMapOverlay map("Kiawah", src);

  SearchQuery q;
  q.area = Island();  // no text
  std::vector<SearchResult> hits;
  map.Search(q, NotCancelled(), hits);

  // "What is in this box" is a question about the tiles, and the tiles are
  // right there. The index is for the question they cannot answer.
  EXPECT_FALSE(map.last_search_used_index());
  EXPECT_EQ(1, src->queries);
  EXPECT_EQ(0, src->name_queries);
  EXPECT_EQ(3u, hits.size());
}

TEST(VectorMapOverlayIndex, TheAreaStillCutsAnIndexedAnswer) {
  auto src = IndexedKiawahIsh();
  VectorMapOverlay map("Kiawah", src);

  SearchQuery q;
  q.text = "ruddy";
  q.area = GeoRect{{32.605, -80.101}, {32.615, -80.090}};  // only the Court
  std::vector<SearchResult> hits;
  map.Search(q, NotCancelled(), hits);

  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("Ruddy Duck Court", hits[0].title);
  EXPECT_TRUE(map.last_search_used_index());
  // The area went DOWN to the index (a real one cuts it in SQL) as well as
  // being applied here — the seam says a source may answer with a superset,
  // never with less.
  ASSERT_TRUE(src->last_name_area.has_value());
}

TEST(VectorMapOverlayIndex, AnUnsupportedOrFailedIndexFallsBackToTheTiles) {
  auto src = IndexedKiawahIsh();
  src->index_fails = true;
  VectorMapOverlay map("Kiawah", src);

  SearchQuery q;
  q.text = "ruddy turnstone";
  q.area = Island();
  std::vector<SearchResult> hits;
  map.Search(q, NotCancelled(), hits);

  EXPECT_EQ(1, src->name_queries);
  EXPECT_FALSE(map.last_search_used_index());
  EXPECT_EQ(1, src->queries);  // the scan ran
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("Ruddy Turnstone", hits[0].title);
}

TEST(VectorMapOverlayIndex, AFailedIndexAndNoAreaIsHonestlyNothing) {
  auto src = IndexedKiawahIsh();
  src->index_fails = true;
  VectorMapOverlay map("Kiawah", src);

  SearchQuery q;
  q.text = "ruddy turnstone";
  std::vector<SearchResult> hits;
  map.Search(q, NotCancelled(), hits);
  // The fallback is the tile scan, and the tile scan needs an area. Not an
  // error, not the pack: nothing.
  EXPECT_TRUE(hits.empty());
  EXPECT_EQ(0, src->queries);
}

TEST(VectorMapOverlayIndex, TurningTheIndexOffPinsTheOverlayToTheTiles) {
  auto src = IndexedKiawahIsh();
  VectorMapOverlay map("Kiawah", src);
  map.SetUseNameIndex(false);

  SearchQuery q;
  q.text = "ruddy turnstone";
  q.area = Island();
  std::vector<SearchResult> hits;
  map.Search(q, NotCancelled(), hits);

  // What the INDEX BUILDER needs — a pack being re-indexed must not be asked
  // about itself — and what a caller comparing the two tiers needs.
  EXPECT_EQ(0, src->name_queries);
  EXPECT_EQ(1, src->queries);
  EXPECT_FALSE(map.last_search_used_index());
  EXPECT_EQ(1u, hits.size());
}

TEST(VectorMapOverlayIndex, ASourceWithNoIndexIsNeverAsked) {
  auto src = IndexedKiawahIsh();
  src->SetHasIndex(false);
  VectorMapOverlay map("Kiawah", src);

  SearchQuery q;
  q.text = "ruddy turnstone";
  std::vector<SearchResult> hits;
  map.Search(q, NotCancelled(), hits);
  EXPECT_EQ(0, src->name_queries);
  EXPECT_TRUE(hits.empty());
}

TEST(VectorMapOverlayIndex, TheCapBindsHereAndTheIndexIsAskedForMore) {
  auto src = IndexedKiawahIsh();
  VectorMapOverlay map("Kiawah", src);

  SearchQuery q;
  q.text = "ruddy";
  q.max_results = 1;
  std::vector<SearchResult> hits;
  map.Search(q, NotCancelled(), hits);

  EXPECT_EQ(1u, hits.size());
  // Over-fetched, because rows fall out between the index's tokeniser and the
  // port's rule and a page that came back half empty would be the index's
  // fault rather than the pack's.
  EXPECT_GT(src->last_name_max, 1u);
}

TEST(VectorMapOverlayIndex, AnIndexedRowMintsAndDescribesLikeAScannedOne) {
  auto src = IndexedKiawahIsh();
  VectorMapOverlay map("Kiawah", src);

  SearchQuery indexed;
  indexed.text = "ruddy turnstone";
  std::vector<SearchResult> from_index;
  map.Search(indexed, NotCancelled(), from_index);
  ASSERT_EQ(1u, from_index.size());

  SearchQuery scanned = indexed;
  scanned.area = Island();
  map.SetUseNameIndex(false);
  std::vector<SearchResult> from_tiles;
  map.Search(scanned, NotCancelled(), from_tiles);
  ASSERT_EQ(1u, from_tiles.size());

  // THE SAME FEATURE, THE SAME NUMBER. The mint is over the ref, not over the
  // tier that found it, so a result held from a global search still names the
  // road a later area search finds.
  EXPECT_EQ(from_tiles[0].feature, from_index[0].feature);

  fv::FeatureDescription d;
  const fv::Status s = map.DescribeFeature(from_index[0].feature, &d);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ("Ruddy Turnstone", d.title);
}

TEST(VectorMapOverlayIndex, CancelBeforeTheIndexReadCostsTheReadItself) {
  auto src = IndexedKiawahIsh();
  VectorMapOverlay map("Kiawah", src);

  SearchQuery q;
  q.text = "ruddy turnstone";
  const std::atomic<bool> cancelled{true};
  std::vector<SearchResult> hits;
  map.Search(q, cancelled, hits);

  EXPECT_TRUE(hits.empty());
  EXPECT_EQ(0, src->name_queries);
  // AND IT DOES NOT THEN SCAN: a cancelled search is not a reason to go and
  // do the expensive half of the work instead.
  EXPECT_EQ(0, src->queries);
}
