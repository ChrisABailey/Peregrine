// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The point document's life in the app, tested on the mac.
//
// The fvkit tests already cover the overlay: every column round-trips, a
// schema-1 document still opens, a hit reports the right point. None of that
// is about an app. This file tests the three sentences the app adds, each of
// which a simulator run would otherwise carry alone:
//
//     the shipped points are there on the first launch
//     an edit survives being killed
//     the pack never overwrites them afterwards
//
// The last is why `seeded()` exists: a seed that ran twice would hand a user
// back every point they had deleted.

#include "PPPointStore.h"

#include "fvkit/app/pick.h"
#include "fvkit/overlay/manager.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using fv::GeoPoint;
using fv::MapPoint;
using pippin::PointStore;

// A scratch directory of this test's own, removed on the way out. The route
// tests write into the current directory; a store that seeds and re-seeds
// wants two paths that are certainly unrelated to anything else.
class Scratch {
 public:
  explicit Scratch(const std::string& tag) {
    dir_ = std::filesystem::temp_directory_path() /
           ("pp_points_" + tag + "_" + std::to_string(::getpid()));
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
    std::filesystem::create_directories(dir_, ec);
  }
  ~Scratch() {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }
  std::string file(const std::string& name) const {
    return (dir_ / name).string();
  }

 private:
  std::filesystem::path dir_;
};

MapPoint MakePoint(int64_t id, const std::string& name, double lat,
                   double lon) {
  MapPoint p;
  p.id = id;
  p.name = name;
  p.position = GeoPoint{lat, lon};
  p.size_px = 22;
  return p;
}

// A pack's shipped set: two Kiawah points, one of them with the contact
// details schema 3 added.
std::string WriteSeed(const Scratch& scratch) {
  const std::string spec = scratch.file("kiawah.fvpoints");
  fv::PointOverlay pack("Kiawah");
  MapPoint a = MakePoint(1, "Ruddy Turnstone", 32.6044007, -80.1083007);
  a.remarks = "west end of the demo route";
  MapPoint b = MakePoint(2, "Freshfields Village", 32.6323, -80.1102);
  b.phone = "+1 843-768-6491";
  b.url = "https://freshfieldsvillage.com/";
  pack.SetPoints({a, b});
  EXPECT_TRUE(pack.FileSaveAs(spec, 0).ok());
  return spec;
}

fv::MapProjection KiawahProj() {
  fv::MapProjection p;
  p.SetSurfaceSize(800, 600);
  p.SetCenter({32.6044007, -80.1083007});
  p.SetScale(20000.0);
  return p;
}

// ---------------------------------------------------------------------------
// The seed
// ---------------------------------------------------------------------------

TEST(PointStore, AFirstLaunchCopiesThePacksSetIntoTheUsersDocument) {
  Scratch scratch("seed");
  const std::string seed = WriteSeed(scratch);
  const std::string doc = scratch.file("points.fvpoints");

  PointStore store(seed, doc);
  ASSERT_TRUE(store.LoadAtLaunch().ok());
  EXPECT_TRUE(store.seeded());
  ASSERT_EQ(2u, store.Points().size());
  EXPECT_EQ("Ruddy Turnstone", store.Points()[0].name);
  EXPECT_EQ("+1 843-768-6491", store.Points()[1].phone);

  // Written now, not at the first edit. From here the document exists and the
  // bundle's copy is never read again, which is the seed's whole contract.
  EXPECT_TRUE(std::filesystem::exists(doc));
  EXPECT_EQ("", store.last_write_error());
}

TEST(PointStore, TheSecondLaunchReadsTheUsersDocumentAndDoesNotReSeed) {
  Scratch scratch("reseed");
  const std::string seed = WriteSeed(scratch);
  const std::string doc = scratch.file("points.fvpoints");

  {
    PointStore first(seed, doc);
    ASSERT_TRUE(first.LoadAtLaunch().ok());
    ASSERT_EQ(2u, first.Points().size());
    // The user deletes one of the shipped points — the case that catches a
    // seed running twice.
    ASSERT_TRUE(first.RemovePoint(first.Points()[0].id));
  }

  PointStore second(seed, doc);
  ASSERT_TRUE(second.LoadAtLaunch().ok());
  EXPECT_FALSE(second.seeded()) << "the pack must not be read a second time";
  ASSERT_EQ(1u, second.Points().size());
  EXPECT_EQ("Freshfields Village", second.Points()[0].name)
      << "a deleted point came back, so the seed ran again";
}

TEST(PointStore, AnEmptySetIsWrittenRatherThanDeleted) {
  // The route's document is DELETED when the last waypoint goes, because "no
  // route" and "an empty route" are the same state. Points are not like that:
  // an absent document means "seed me", so a user who deleted every point
  // would be handed all of them back on the next launch.
  Scratch scratch("empty");
  const std::string seed = WriteSeed(scratch);
  const std::string doc = scratch.file("points.fvpoints");

  {
    PointStore store(seed, doc);
    ASSERT_TRUE(store.LoadAtLaunch().ok());
    while (!store.Points().empty())
      ASSERT_TRUE(store.RemovePoint(store.Points()[0].id));
    EXPECT_TRUE(std::filesystem::exists(doc)) << "an empty set is still a file";
  }

  PointStore again(seed, doc);
  ASSERT_TRUE(again.LoadAtLaunch().ok());
  EXPECT_FALSE(again.seeded());
  EXPECT_TRUE(again.Points().empty()) << "an emptied map came back full";
}

TEST(PointStore, APackWithNoPointsFileStartsEmptyAndIsNotAnError) {
  Scratch scratch("nopack");
  PointStore store(scratch.file("absent.fvpoints"),
                   scratch.file("points.fvpoints"));
  EXPECT_TRUE(store.LoadAtLaunch().ok());
  EXPECT_FALSE(store.seeded());
  EXPECT_TRUE(store.Points().empty());
}

TEST(PointStore, ADocumentThatWillNotReadIsReportedAndTheSeedIsNotUsedToHideIt) {
  Scratch scratch("corrupt");
  const std::string seed = WriteSeed(scratch);
  const std::string doc = scratch.file("points.fvpoints");
  {
    std::ofstream out(doc, std::ios::binary);
    out << "this is not a database";
  }

  PointStore store(seed, doc);
  const fv::Status s = store.LoadAtLaunch();
  EXPECT_FALSE(s.ok()) << "a file that is there and unreadable is worth saying";
  EXPECT_TRUE(store.Points().empty());
  // NOT re-seeded: overwriting a file we could not read with the factory set
  // would destroy whatever the user still had in it.
  EXPECT_FALSE(store.seeded());
}

TEST(PointStore, ThePacksOwnSetOpensThroughTheRealReader) {
  // The one check that the two schemas agree. `stage_data.py` writes
  // `kiawah.fvpoints` with Python's sqlite3 and its own copy of the CREATE
  // TABLE text; this reads it with the C++ that runs on the phone. Drift
  // would otherwise surface on the app's first launch.
  const std::string seed = FV_PIPPIN_POINTS_SEED;
  if (!std::filesystem::exists(seed))
    GTEST_SKIP() << "no staged pack; run port/apps/Pippin/stage_data.py";

  Scratch scratch("packseed");
  PointStore store(seed, scratch.file("points.fvpoints"));
  ASSERT_TRUE(store.LoadAtLaunch().ok());
  EXPECT_TRUE(store.seeded());
  ASSERT_FALSE(store.Points().empty());

  // The document names itself, the markers are sized in authored pixels, and
  // at least one of them carries the schema-3 columns the whole session is
  // about — a seed with no phone and no URL in it would demonstrate nothing.
  EXPECT_EQ("Kiawah", store.overlay()->Name());
  int with_phone = 0, with_url = 0;
  for (const MapPoint& p : store.Points()) {
    EXPECT_GT(p.size_px, 0.0) << p.name;
    EXPECT_FALSE(p.name.empty());
    if (!p.phone.empty()) ++with_phone;
    if (!p.url.empty()) ++with_url;
  }
  EXPECT_GT(with_phone, 0);
  EXPECT_GT(with_url, 0);

  // The embedded palette, and the property it exists for: the artwork is in
  // the document, so a `.fvpoints` opens with its symbology on a machine that
  // has never heard of maki. Every `symbol_id` a point carries must name a
  // row that is there, since a dangling one draws as the bare shape, which is
  // a staging bug that looks like a style choice.
  ASSERT_FALSE(store.Symbols().empty()) << "the seed embedded no artwork";
  int wearing = 0;
  for (const MapPoint& p : store.Points()) {
    if (p.symbol_id == 0) continue;
    ++wearing;
    EXPECT_NE(nullptr, store.overlay()->FindSymbol(p.symbol_id))
        << p.name << " wears symbol " << p.symbol_id << ", which is not in the"
        << " palette";
  }
  EXPECT_GT(wearing, 0) << "no point wears an icon";

  // A palette rather than a projection of the points: rows nothing references
  // are kept, saved and handed back, which lets the editor's picker offer a
  // set the author assembled before any point wore it.
  EXPECT_GT(store.Symbols().size(), (size_t)wearing)
      << "the palette offers nothing beyond what is already in use";

  // And the bytes are really PNGs — a row that will not decode is not an
  // error at the overlay's level (the point falls back to its shape), so a
  // truncated blob would be silent everywhere else.
  for (const fv::PointSymbol& sym : store.Symbols()) {
    ASSERT_GE(sym.image.size(), 8u) << sym.name;
    EXPECT_EQ(0x89, sym.image[0]) << sym.name << " is not a PNG";
    EXPECT_EQ('P', sym.image[1]) << sym.name << " is not a PNG";
  }
}

// ---------------------------------------------------------------------------
// Editing, and the write that comes with it
// ---------------------------------------------------------------------------

TEST(PointStore, AnAddedPointSurvivesARelaunch) {
  Scratch scratch("add");
  const std::string doc = scratch.file("points.fvpoints");
  int64_t id = 0;
  {
    PointStore store(std::string(), doc);
    ASSERT_TRUE(store.LoadAtLaunch().ok());
    MapPoint p = MakePoint(0, "Beach Club", 32.6014, -80.0967);
    p.phone = "843-555-0100";
    p.url = "https://example.invalid/beach";
    p.remarks = "showers round the back";
    id = store.AddPoint(p);
    EXPECT_GT(id, 0) << "an id of 0 gets the next free one";
    EXPECT_EQ("", store.last_write_error());
  }

  PointStore again(std::string(), doc);
  ASSERT_TRUE(again.LoadAtLaunch().ok());
  ASSERT_EQ(1u, again.Points().size());
  const MapPoint& got = again.Points()[0];
  EXPECT_EQ(id, got.id);
  EXPECT_EQ("Beach Club", got.name);
  EXPECT_EQ("843-555-0100", got.phone);
  EXPECT_EQ("https://example.invalid/beach", got.url);
  EXPECT_EQ("showers round the back", got.remarks);
}

TEST(PointStore, AnEditedPointSurvivesARelaunchAndKeepsItsPlaceInTheSet) {
  Scratch scratch("edit");
  const std::string seed = WriteSeed(scratch);
  const std::string doc = scratch.file("points.fvpoints");
  {
    PointStore store(seed, doc);
    ASSERT_TRUE(store.LoadAtLaunch().ok());
    MapPoint p = store.Points()[0];
    p.name = "Turnstone, moved";
    p.position = GeoPoint{32.61, -80.11};
    p.shape = fv::PointShape::kStar;
    p.url = "https://example.invalid/turnstone";
    ASSERT_TRUE(store.UpdatePoint(p));
  }

  PointStore again(seed, doc);
  ASSERT_TRUE(again.LoadAtLaunch().ok());
  ASSERT_EQ(2u, again.Points().size());
  EXPECT_EQ("Turnstone, moved", again.Points()[0].name)
      << "an edit must not reorder the set";
  EXPECT_NEAR(32.61, again.Points()[0].position.lat, 1e-9);
  EXPECT_EQ(fv::PointShape::kStar, again.Points()[0].shape);
  EXPECT_EQ("https://example.invalid/turnstone", again.Points()[0].url);
}

TEST(PointStore, AStaleIdIsRefusedRatherThanResurrected) {
  Scratch scratch("stale");
  const std::string doc = scratch.file("points.fvpoints");
  PointStore store(std::string(), doc);
  ASSERT_TRUE(store.LoadAtLaunch().ok());
  const int64_t id = store.AddPoint(MakePoint(0, "A", 32.6, -80.1));
  ASSERT_TRUE(store.RemovePoint(id));

  MapPoint ghost = MakePoint(id, "A, edited", 32.6, -80.1);
  EXPECT_FALSE(store.UpdatePoint(ghost));
  EXPECT_FALSE(store.RemovePoint(id));
  EXPECT_TRUE(store.Points().empty());
}

TEST(PointStore, AStoreWithNoDocumentPathEditsAndPersistsNothing) {
  // What `PPMap` has between construction and the moment Swift tells it where
  // `Documents/` is — and what a test that does not care about disk uses.
  PointStore store{std::string(), std::string()};  // braces: the
  // parenthesised form of two default-constructed strings is a FUNCTION
  // declaration, which is C++'s oldest joke and costs an afternoon.
  ASSERT_TRUE(store.LoadAtLaunch().ok());
  EXPECT_GT(store.AddPoint(MakePoint(0, "ephemeral", 32.6, -80.1)), 0);
  EXPECT_EQ(1u, store.Points().size());
  EXPECT_EQ("", store.last_write_error());
}

TEST(PointStore, AWriteThatFailsIsReportedAndTheEditStands) {
  // The bargain stated in the header: the point is on screen and correct, and
  // what the user loses is the next launch. A directory that does not exist is
  // the cheapest way to make sqlite refuse.
  Scratch scratch("nowrite");
  PointStore store(std::string(),
                   scratch.file("no/such/directory/points.fvpoints"));
  const int64_t id = store.AddPoint(MakePoint(0, "kept", 32.6, -80.1));
  EXPECT_GT(id, 0);
  ASSERT_EQ(1u, store.Points().size()) << "the edit was refused, not reported";
  EXPECT_FALSE(store.last_write_error().empty());
}

// ---------------------------------------------------------------------------
// What is under a finger
// ---------------------------------------------------------------------------

TEST(PointStore, TheHitTestAnswersTheNearestPointOrNothing) {
  Scratch scratch("hit");
  PointStore store{std::string(), std::string()};  // braces: the
  // parenthesised form of two default-constructed strings is a FUNCTION
  // declaration, which is C++'s oldest joke and costs an afternoon.
  // The projection's centre is the first point, so it lands on (399.5, 299.5).
  const int64_t here = store.AddPoint(MakePoint(0, "here", 32.6044007,
                                                -80.1083007));
  store.AddPoint(MakePoint(0, "far", 32.6323, -80.1102));

  const fv::MapProjection proj = KiawahProj();
  EXPECT_EQ(here, store.HitTest(proj, {400, 300}, 20.0));
  // Off in the ocean: nothing, and 0 is the answer rather than a nearest point
  // at any distance.
  EXPECT_EQ(0, store.HitTest(proj, {40, 560}, 20.0));
}

TEST(PointStore, TwoPointsUnderOneThumbAnswerTheNEARERWithNoQuestionAsked) {
  // A phone decision rather than a disagreement with A5: a rider with one
  // thumb gets the closest one and taps again if it was the wrong one.
  PointStore store{std::string(), std::string()};  // braces: the
  // parenthesised form of two default-constructed strings is a FUNCTION
  // declaration, which is C++'s oldest joke and costs an afternoon.
  const fv::MapProjection proj = KiawahProj();

  MapPoint a = MakePoint(0, "under the thumb", 32.6044007, -80.1083007);
  a.size_px = 10;
  const int64_t near_id = store.AddPoint(a);

  // A few pixels east of it, and added SECOND so document order and distance
  // order disagree — which is what makes this a test of the ranking.
  double sx = 0, sy = 0;
  ASSERT_TRUE(proj.GeoToSurface(a.position, &sx, &sy).ok());
  fv::GeoPoint east;
  ASSERT_TRUE(proj.SurfaceToGeo(sx + 6.0, sy, &east).ok());
  MapPoint b = MakePoint(0, "just beside it", east.lat, east.lon);
  b.size_px = 10;
  store.AddPoint(b);

  EXPECT_EQ(near_id, store.HitTest(proj, {(int)std::lround(sx),
                                          (int)std::lround(sy)}, 12.0));
}

TEST(PointStore, TheDpiScaleMakesAMarkerAsPressableAsItLooks) {
  // `PPMap` sets the scale per frame; the property that has to hold is that
  // the store's hit test sees the same number the drawing did.
  PointStore store{std::string(), std::string()};  // braces: the
  // parenthesised form of two default-constructed strings is a FUNCTION
  // declaration, which is C++'s oldest joke and costs an afternoon.
  MapPoint p = MakePoint(0, "small", 32.6044007, -80.1083007);
  p.size_px = 10;
  const int64_t id = store.AddPoint(p);
  const fv::MapProjection proj = KiawahProj();

  // 20.5 px out (the surface's centre is `(w-1)/2` = 399.5, not 400): past
  // 5 + 6 at 1x, inside 15 + 6 at 3x.
  const fv::PixelPoint probe{420, 300};
  EXPECT_EQ(0, store.HitTest(proj, probe, 6.0));
  store.overlay()->SetSymbolDpiScale(3.0);
  EXPECT_EQ(id, store.HitTest(proj, probe, 6.0));
}

// ---------------------------------------------------------------------------
// Display state, which is not document state
// ---------------------------------------------------------------------------

TEST(PointStore, VisibilityAndSelectionAreNotWrittenToTheDocument) {
  Scratch scratch("display");
  const std::string seed = WriteSeed(scratch);
  const std::string doc = scratch.file("points.fvpoints");
  {
    PointStore store(seed, doc);
    ASSERT_TRUE(store.LoadAtLaunch().ok());
    store.SetVisible(false);
    store.SetSelected(store.Points()[1].id);
    store.SetShowLabels(true);
    EXPECT_FALSE(store.visible());
  }

  PointStore again(seed, doc);
  ASSERT_TRUE(again.LoadAtLaunch().ok());
  // A `.fvpoints` file is a set of places, not a record of whether somebody
  // had them switched on: the startup state is the app's, from `pippin.ini`.
  EXPECT_TRUE(again.visible());
  EXPECT_EQ(0, again.selected());
  EXPECT_FALSE(again.show_labels());
}

// --- The snap, through the stack exactly as PPMap asks it ------------------

TEST(PointStoreSnap, TheStackAnswersForAPointStoresOverlay) {
  // PPMap's own arrangement, reproduced: the store's overlay goes into an
  // OverlayManager and the question is asked of the manager. If this passes
  // and the phone does not snap, the fault is above the core.
  Scratch scratch("snap");
  const std::string doc = scratch.file("points.fvpoints");
  ASSERT_TRUE(fv::PointOverlay::WriteSampleFile(doc).ok());

  PointStore store("", doc);
  ASSERT_TRUE(store.LoadAtLaunch().ok());
  ASSERT_FALSE(store.Points().empty());

  fv::OverlayManager overlays;
  ASSERT_TRUE(overlays.Add(store.overlay()).ok());

  const fv::MapPoint& target = store.Points().front();
  fv::MapProjection proj;
  proj.SetSurfaceSize(800, 600);
  proj.SetCenter(target.position);
  proj.SetScale(20000.0);

  double sx = 0, sy = 0;
  ASSERT_TRUE(proj.GeoToSurface(target.position, &sx, &sy).ok());

  const std::vector<fv::app::SnapToItem> got = fv::app::SnapCandidates(
      overlays, proj, fv::PixelPoint{(int)std::lround(sx) + 6,
                                     (int)std::lround(sy)}, 90.0);
  ASSERT_FALSE(got.empty()) << "the stack answered nothing for a point "
                            << "6 px from the cursor";
  EXPECT_EQ(target.name, got.front().description);
  EXPECT_DOUBLE_EQ(target.position.lat, got.front().point.lat);
}

}  // namespace
