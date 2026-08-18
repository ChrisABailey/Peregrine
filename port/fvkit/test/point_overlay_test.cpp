// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// A6: the first C++ FILE overlay — a point set in a SQLite document, drawn as
// geometric shapes and answering picks.
//
// What is worth pinning here, as opposed to what merely passes: the document
// ROUND TRIP (every column back, including the two attributes a pick reports),
// the hit test agreeing with what was DRAWN, and the two sample pairs really
// being ambiguous at the tolerance the pick session uses. That last one is a
// property of the DATA and would otherwise rot silently the first time somebody
// edited a coordinate.

#include "fvkit/overlay/point_overlay.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "fvkit/app/pick.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/detail/sqlite.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/tools/png_write.h"

namespace {

using fv::MapPoint;
using fv::PointOverlay;
using fv::PointShape;
using fv::PointSymbol;
using fv::Status;

std::string TempSpec(const char* stem) {
  // Per-test filename: the ledger's own hygiene rule about tests that share a
  // scratch database.
  return std::string(::testing::UnitTest::GetInstance()->current_test_info()
                         ? ::testing::UnitTest::GetInstance()
                               ->current_test_info()
                               ->name()
                         : "x") +
         "_" + stem + ".fvpoints";
}

// Charleston harbour at a scale where the sample's paired points are a few
// pixels apart -- the arrangement the ambiguity policies need.
fv::MapProjection HarbourProj() {
  fv::MapProjection p;
  p.SetSurfaceSize(800, 600);
  p.SetCenter({32.74, -79.89});
  p.SetScale(50000.0);
  return p;
}

MapPoint MakePoint(int64_t id, const char* name, double lat, double lon) {
  MapPoint p;
  p.id = id;
  p.name = name;
  p.position = {lat, lon};
  return p;
}

// --- schema 2: the embedded symbol palette ---------------------------------

// A scratch directory of loose PNGs, which is the form an icon set ships in
// and the form SampleSymbols() reads.
class ScratchDir {
 public:
  explicit ScratchDir(const char* stem) {
    path_ = std::filesystem::temp_directory_path() /
            ("fvpoints_" + std::string(stem) + "_" +
             std::to_string(
                 (unsigned long long)(uintptr_t)this));  // NOLINT
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  std::string str() const { return path_.string(); }
  std::string file(const std::string& name) const {
    return (path_ / name).string();
  }

 private:
  std::filesystem::path path_;
};

fv::PixelBuffer Solid(int w, int h, unsigned char r, unsigned char g,
                      unsigned char b) {
  fv::PixelBuffer buf(w, h);
  for (int y = 0; y < h; ++y) {
    unsigned char* row = buf.Row(y);
    for (int x = 0; x < w; ++x) {
      row[x * 4 + 0] = r;
      row[x * 4 + 1] = g;
      row[x * 4 + 2] = b;
      row[x * 4 + 3] = 255;
    }
  }
  return buf;
}

// A PointSymbol carrying `w` x `h` of one opaque colour.
PointSymbol PngSymbol(const ScratchDir& dir, const std::string& name, int w,
                      int h, unsigned char r, unsigned char g,
                      unsigned char b) {
  const std::string path = dir.file(name + ".png");
  EXPECT_TRUE(fv::WritePng(Solid(w, h, r, g, b), path).ok());
  PointOverlay tmp;
  EXPECT_TRUE(tmp.AddSymbolFromPngFile(path).ok());
  EXPECT_EQ(1u, tmp.symbols().size());
  return tmp.symbols().empty() ? PointSymbol() : tmp.symbols()[0];
}

// ---------------------------------------------------------------------------
// The document
// ---------------------------------------------------------------------------

TEST(PointOverlay, ADocumentRoundTripsEveryColumn) {
  const std::string spec = TempSpec("rt");
  std::remove(spec.c_str());

  PointOverlay out("Harbour Marks");
  MapPoint p = MakePoint(7, "Castle Pinckney", 32.7783, -79.9114);
  p.shape = PointShape::kStar;
  p.size_px = 13.5;
  p.color = fv::FvColor{10, 20, 30, 200};
  p.category = "landmark";
  p.elevation_ft = 42.0;
  p.remarks = "a value the pick reports";
  out.SetPoints({p});
  ASSERT_TRUE(out.FileSaveAs(spec, 0).ok());

  PointOverlay in("something else");
  ASSERT_TRUE(in.FileOpen(spec).ok());
  ASSERT_EQ(1u, in.points().size());
  const MapPoint& got = in.points()[0];
  EXPECT_EQ(7, got.id);
  EXPECT_EQ("Castle Pinckney", got.name);
  EXPECT_NEAR(32.7783, got.position.lat, 1e-9);
  EXPECT_NEAR(-79.9114, got.position.lon, 1e-9);
  EXPECT_EQ(PointShape::kStar, got.shape);
  EXPECT_NEAR(13.5, got.size_px, 1e-9);
  EXPECT_EQ(10, got.color.r);
  EXPECT_EQ(200, got.color.a);
  EXPECT_EQ("landmark", got.category);
  EXPECT_NEAR(42.0, got.elevation_ft, 1e-9);
  EXPECT_EQ("a value the pick reports", got.remarks);
  // The document carries its own name, so an overlay renames itself to the set
  // the author named rather than to a path.
  EXPECT_EQ("Harbour Marks", in.Name());
  std::remove(spec.c_str());
}

// ---------------------------------------------------------------------------
// Schema 2: the symbol palette
// ---------------------------------------------------------------------------

TEST(PointOverlay, ThePaletteRoundTripsAndManyPointsShareOneRow) {
  // The point of the second table, tested as the property it exists for: two
  // points, ONE symbol row, and the artwork stored once.
  ScratchDir icons("share");
  const std::string spec = TempSpec("palette");
  std::remove(spec.c_str());

  PointOverlay out("Marks");
  PointSymbol castle = PngSymbol(icons, "castle", 16, 16, 10, 200, 30);
  castle.pixel_ratio = 2.0;
  castle.has_pivot = true;
  castle.pivot_x = 3.5;
  castle.pivot_y = 12.25;
  const size_t bytes = castle.image.size();
  const int64_t id = out.AddSymbol(castle);
  ASSERT_GT(id, 0);

  MapPoint a = MakePoint(1, "Fort Sumter", 32.7522, -79.8747);
  MapPoint b = MakePoint(2, "Fort Moultrie", 32.7594, -79.8577);
  a.symbol_id = id;
  b.symbol_id = id;
  out.SetPoints({a, b});
  ASSERT_TRUE(out.FileSaveAs(spec, 0).ok());

  PointOverlay in;
  ASSERT_TRUE(in.FileOpen(spec).ok());
  ASSERT_EQ(1u, in.symbols().size()) << "one row for two points";
  const PointSymbol& got = in.symbols()[0];
  EXPECT_EQ(id, got.id);
  EXPECT_EQ("castle", got.name);
  EXPECT_EQ(bytes, got.image.size());
  EXPECT_EQ(castle.image, got.image) << "the PNG comes back byte for byte";
  EXPECT_DOUBLE_EQ(2.0, got.pixel_ratio);
  EXPECT_TRUE(got.has_pivot);
  EXPECT_DOUBLE_EQ(3.5, got.pivot_x);
  EXPECT_DOUBLE_EQ(12.25, got.pivot_y);
  ASSERT_EQ(2u, in.points().size());
  EXPECT_EQ(id, in.points()[0].symbol_id);
  EXPECT_EQ(id, in.points()[1].symbol_id);
  std::remove(spec.c_str());
}

TEST(PointOverlay, AnUnsetPivotStaysUnsetRatherThanBecomingTheOrigin) {
  // sqlite hands back 0.0 for a NULL, so "no pivot" and "pivot at the tile's
  // top-left corner" are one value unless the reader asks for the column's
  // TYPE. Getting this wrong puts every icon down and right of where it goes.
  ScratchDir icons("pivot");
  const std::string spec = TempSpec("pivot");
  std::remove(spec.c_str());
  PointOverlay out;
  out.AddSymbol(PngSymbol(icons, "plain", 8, 8, 0, 0, 200));
  ASSERT_TRUE(out.FileSaveAs(spec, 0).ok());

  PointOverlay in;
  ASSERT_TRUE(in.FileOpen(spec).ok());
  ASSERT_EQ(1u, in.symbols().size());
  EXPECT_FALSE(in.symbols()[0].has_pivot);
  std::remove(spec.c_str());
}

TEST(PointOverlay, AddSymbolDedupsByNameAndNamesAFileByItsStem) {
  ScratchDir icons("dedup");
  PointOverlay o;
  const std::string path = icons.file("harbor.png");
  ASSERT_TRUE(fv::WritePng(Solid(4, 4, 1, 2, 3), path).ok());

  int64_t first = 0, second = 0;
  ASSERT_TRUE(o.AddSymbolFromPngFile(path, "", &first).ok());
  ASSERT_TRUE(o.AddSymbolFromPngFile(path, "", &second).ok());
  EXPECT_EQ(first, second) << "the same name is the same row";
  EXPECT_EQ(1u, o.symbols().size());
  EXPECT_EQ("harbor", o.symbols()[0].name) << "named by the file's stem";
  ASSERT_NE(nullptr, o.FindSymbolByName("harbor"));
  EXPECT_EQ(first, o.FindSymbolByName("harbor")->id);

  // An @2x file is the same icon at twice the tile resolution: the suffix
  // comes off the name and lands on pixel_ratio.
  const std::string retina = icons.file("beach@2x.png");
  ASSERT_TRUE(fv::WritePng(Solid(4, 4, 1, 2, 3), retina).ok());
  int64_t hi = 0;
  ASSERT_TRUE(o.AddSymbolFromPngFile(retina, "", &hi).ok());
  ASSERT_NE(nullptr, o.FindSymbol(hi));
  EXPECT_EQ("beach", o.FindSymbol(hi)->name);
  EXPECT_DOUBLE_EQ(2.0, o.FindSymbol(hi)->pixel_ratio);

  EXPECT_TRUE(o.RemoveSymbol(first));
  EXPECT_FALSE(o.RemoveSymbol(first));
  EXPECT_EQ(nullptr, o.FindSymbolByName("harbor"));
}

TEST(PointOverlay, ASchema1DocumentStillOpensAndIsSavedForward) {
  // The port has written schema-1 files. A reader that rejected one would make
  // the version number a wall instead of a record.
  const std::string spec = TempSpec("v1");
  std::remove(spec.c_str());
  {
    fv::detail::SqliteDb db;
    ASSERT_TRUE(db.Open(spec).ok());
    ASSERT_TRUE(db.Exec("CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);"
                        "INSERT INTO meta VALUES('schema_version','1');"
                        "INSERT INTO meta VALUES('name','Old Set');"
                        "CREATE TABLE points("
                        "  id INTEGER PRIMARY KEY, name TEXT NOT NULL,"
                        "  lat REAL NOT NULL, lon REAL NOT NULL,"
                        "  shape TEXT NOT NULL, size_px REAL NOT NULL,"
                        "  color TEXT NOT NULL, category TEXT NOT NULL,"
                        "  elevation_ft REAL NOT NULL, remarks TEXT NOT NULL);"
                        "INSERT INTO points VALUES(4,'Buoy',32.7,-79.9,"
                        "  'square',10,'#102030','buoy',0,'');")
                    .ok());
  }
  PointOverlay o;
  ASSERT_TRUE(o.FileOpen(spec).ok());
  ASSERT_EQ(1u, o.points().size());
  EXPECT_EQ("Buoy", o.points()[0].name);
  EXPECT_EQ(PointShape::kSquare, o.points()[0].shape);
  EXPECT_EQ(0, o.points()[0].symbol_id) << "no symbol is not a broken one";
  EXPECT_TRUE(o.symbols().empty());
  EXPECT_EQ("Old Set", o.Name());

  // Saving it back adds the column CREATE TABLE IF NOT EXISTS could not.
  ScratchDir icons("v1");
  o.AddSymbol(PngSymbol(icons, "marker", 8, 8, 200, 0, 0));
  MapPoint p = o.points()[0];
  p.symbol_id = o.symbols()[0].id;
  o.SetPoints({p});
  ASSERT_TRUE(o.FileSaveAs(spec, 0).ok());

  PointOverlay again;
  ASSERT_TRUE(again.FileOpen(spec).ok());
  ASSERT_EQ(1u, again.points().size());
  EXPECT_EQ(p.symbol_id, again.points()[0].symbol_id);
  ASSERT_EQ(1u, again.symbols().size());
  std::remove(spec.c_str());
}

TEST(PointOverlay, SaveAsReplacesRatherThanAppends) {
  const std::string spec = TempSpec("replace");
  std::remove(spec.c_str());
  PointOverlay o("Set");
  o.SetPoints({MakePoint(1, "A", 1, 1), MakePoint(2, "B", 2, 2)});
  ASSERT_TRUE(o.FileSaveAs(spec, 0).ok());
  o.SetPoints({MakePoint(9, "C", 3, 3)});
  ASSERT_TRUE(o.FileSaveAs(spec, 0).ok());

  PointOverlay in;
  ASSERT_TRUE(in.FileOpen(spec).ok());
  ASSERT_EQ(1u, in.points().size());
  EXPECT_EQ("C", in.points()[0].name);
  std::remove(spec.c_str());
}

TEST(PointOverlay, OpeningSomethingThatIsNotAPointDocumentFails) {
  const std::string spec = TempSpec("bogus");
  std::remove(spec.c_str());
  {
    FILE* f = std::fopen(spec.c_str(), "wb");
    ASSERT_NE(nullptr, f);
    std::fputs("not a database", f);
    std::fclose(f);
  }
  PointOverlay o;
  const Status s = o.FileOpen(spec);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(fv::kIoError, s.code);
  std::remove(spec.c_str());
}

TEST(PointOverlay, AnUnsupportedSaveFormatIsRefusedRatherThanIgnored) {
  PointOverlay o;
  const Status s = o.FileSaveAs(TempSpec("fmt"), 3);
  EXPECT_EQ(fv::kUnsupported, s.code);
}

TEST(PointOverlay, EditsDirtyTheDocumentAndSelectionDoesNot) {
  PointOverlay o;
  ASSERT_TRUE(o.FileNew().ok());
  EXPECT_FALSE(o.is_dirty());  // an empty new document has nothing to lose

  const int64_t id = o.AddPoint(MakePoint(0, "new", 32.6, -80.1));
  EXPECT_GT(id, 0);  // an unsaved point is still addressable
  EXPECT_TRUE(o.is_dirty());

  o.set_dirty(false);
  o.SetSelected(id);
  EXPECT_FALSE(o.is_dirty());  // selection is not a document change

  EXPECT_TRUE(o.RemovePoint(id));
  EXPECT_TRUE(o.is_dirty());
  EXPECT_EQ(0, o.selected());  // removing the selected point deselects
  EXPECT_FALSE(o.RemovePoint(id));
}

TEST(PointOverlay, RevertRereadsTheDocumentAndClearsDirty) {
  const std::string spec = TempSpec("revert");
  std::remove(spec.c_str());
  PointOverlay o("Set");
  o.SetPoints({MakePoint(1, "A", 1, 1)});
  ASSERT_TRUE(o.FileSaveAs(spec, 0).ok());
  o.AddPoint(MakePoint(0, "B", 2, 2));
  ASSERT_EQ(2u, o.points().size());
  ASSERT_TRUE(o.is_dirty());

  ASSERT_TRUE(o.SupportsRevert());
  ASSERT_TRUE(o.Revert(spec).ok());
  EXPECT_EQ(1u, o.points().size());
  EXPECT_FALSE(o.is_dirty());
  std::remove(spec.c_str());
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

TEST(PointOverlay, EveryShapeDrawsInkAtTheProjectedPosition) {
  const fv::MapProjection proj = HarbourProj();
  const PointShape shapes[] = {PointShape::kCircle,   PointShape::kSquare,
                              PointShape::kTriangle, PointShape::kDiamond,
                              PointShape::kCross,    PointShape::kStar};
  for (PointShape shape : shapes) {
    fv::CpuCanvas canvas(800, 600);
    canvas.Clear(fv::FvColor{255, 255, 255, 255});
    PointOverlay o;
    MapPoint p = MakePoint(1, "X", 32.74, -79.89);  // the projection's centre
    p.shape = shape;
    p.size_px = 15;
    p.color = fv::FvColor{0, 0, 255, 255};
    o.SetPoints({p});
    ASSERT_TRUE(o.OnDraw(proj, canvas).ok()) << fv::ToString(shape);

    // The centre pixel is inked for every shape: the fills cover it and both
    // strokes of the cross pass through it. Blue, specifically -- a test that
    // only asked "not white" would pass on the black outline alone, which is
    // ink the shape did not choose.
    const unsigned char* px = canvas.Buffer().Row(300) + 400 * 4;
    EXPECT_GT((int)px[2], 200) << "no blue ink at the centre for "
                               << fv::ToString(shape);
    EXPECT_LT((int)px[0], 60) << "the centre is not the point's colour for "
                              << fv::ToString(shape);
  }
}

TEST(PointOverlay, SelectionIsTheEDGEAndIsDrawnOUTSIDETheFill) {
  // G4. Selection is now a RenderState rather than a swapped edge colour, so
  // the marker gains a band instead of trading one: outside-in the column
  // above a selected point reads highlight, black edge, own colour. Before
  // G4 the yellow REPLACED the black edge, and this test could not tell the
  // difference — the sibling below pins the ordering that says which it is.
  const fv::MapProjection proj = HarbourProj();
  auto edge_and_centre = [&](bool select) {
    fv::CpuCanvas canvas(800, 600);
    canvas.Clear(fv::FvColor{255, 255, 255, 255});
    PointOverlay o;
    MapPoint p = MakePoint(1, "X", 32.74, -79.89);
    p.shape = PointShape::kSquare;
    p.size_px = 15;
    p.color = fv::FvColor{0, 0, 255, 255};
    o.SetPoints({p});
    if (select) o.SetSelected(1);
    EXPECT_TRUE(o.OnDraw(proj, canvas).ok());
    // Straight up from the centre: the first ink met is the edge.
    int edge_y = -1;
    for (int y = 280; y <= 300; ++y) {
      const unsigned char* px = canvas.Buffer().Row(y) + 400 * 4;
      if (px[0] != 255 || px[1] != 255 || px[2] != 255) {
        edge_y = y;
        break;
      }
    }
    const unsigned char* edge = canvas.Buffer().Row(edge_y) + 400 * 4;
    const unsigned char* mid = canvas.Buffer().Row(300) + 400 * 4;
    return std::make_pair(fv::FvColor{edge[0], edge[1], edge[2], 255},
                          fv::FvColor{mid[0], mid[1], mid[2], 255});
  };

  auto plain = edge_and_centre(false);
  EXPECT_LT((int)plain.first.r, 60) << "an unselected edge is black";
  EXPECT_LT((int)plain.first.b, 60);
  EXPECT_GT((int)plain.second.b, 200) << "the fill is the point's own colour";

  auto sel = edge_and_centre(true);
  EXPECT_GT((int)sel.first.r, 200) << "a selected edge is yellow";
  EXPECT_GT((int)sel.first.g, 180);
  EXPECT_LT((int)sel.first.b, 80);
  EXPECT_GT((int)sel.second.b, 200)
      << "selection must not repaint the fill — that is what identifies it";
}

TEST(PointOverlay, ASelectedMarkerGAINSABandRatherThanRecolouringOne) {
  // The G4 property the test above cannot see. Walking IN from outside a
  // selected point must meet three bands in this order — the highlight, the
  // badge's own black edge, then the point's colour. Pre-G4 the middle band
  // did not exist: the yellow WAS the edge, so a selected point silently lost
  // its outline, and on a light chart it lost the contrast the outline is for.
  const fv::MapProjection proj = HarbourProj();
  fv::CpuCanvas canvas(800, 600);
  canvas.Clear(fv::FvColor{255, 255, 255, 255});
  PointOverlay o;
  MapPoint p = MakePoint(1, "X", 32.74, -79.89);
  p.shape = PointShape::kSquare;
  p.size_px = 21;
  p.color = fv::FvColor{0, 0, 255, 255};
  o.SetPoints({p});
  o.SetSelected(1);
  ASSERT_TRUE(o.OnDraw(proj, canvas).ok());

  // The bands met walking down column 400 towards the centre at y = 300.
  std::vector<char> bands;
  for (int y = 275; y <= 300; ++y) {
    const unsigned char* px = canvas.Buffer().Row(y) + 400 * 4;
    char band = 0;
    if (px[0] > 200 && px[1] > 180 && px[2] < 80) band = 'H';        // yellow
    else if (px[0] < 60 && px[1] < 60 && px[2] < 60) band = 'E';     // black
    else if (px[2] > 150 && px[0] < 80 && px[1] < 80) band = 'F';    // blue
    if (band != 0 && (bands.empty() || bands.back() != band))
      bands.push_back(band);
  }
  ASSERT_EQ(bands.size(), 3u)
      << "expected highlight, edge, fill — got " << std::string(bands.begin(),
                                                               bands.end());
  EXPECT_EQ(bands[0], 'H');
  EXPECT_EQ(bands[1], 'E') << "the badge keeps its own outline when selected";
  EXPECT_EQ(bands[2], 'F');
}

TEST(PointOverlay, AnIconIsStampedOnTopOfItsOwnBadge) {
  // The badge is the point's colour and the icon rides on it, so BOTH are
  // visible: the icon at the centre, the badge in the ring around it. A test
  // that only looked at the centre would pass on an icon drawn instead of the
  // badge, which throws the colour column away.
  ScratchDir icons("draw");
  const fv::MapProjection proj = HarbourProj();
  fv::CpuCanvas canvas(800, 600);
  canvas.Clear(fv::FvColor{255, 255, 255, 255});

  PointOverlay o;
  const int64_t id = o.AddSymbol(PngSymbol(icons, "dot", 16, 16, 0, 200, 0));
  MapPoint p = MakePoint(1, "X", 32.74, -79.89);
  p.size_px = 40;
  p.color = fv::FvColor{0, 0, 255, 255};
  p.symbol_id = id;
  o.SetPoints({p});
  ASSERT_TRUE(o.OnDraw(proj, canvas).ok());

  auto at = [&](int x, int y) {
    const unsigned char* px = canvas.Buffer().Row(y) + x * 4;
    return fv::FvColor{px[0], px[1], px[2], 255};
  };
  const fv::FvColor centre = at(400, 300);
  EXPECT_GT((int)centre.g, 150) << "the icon is drawn";
  EXPECT_LT((int)centre.b, 80) << "and it is ON TOP of the badge";
  // 0.62 of 40 px is a 25 px icon, so 15 px out is badge and not icon.
  const fv::FvColor ring = at(415, 300);
  EXPECT_GT((int)ring.b, 150) << "the badge is the point's own colour";
  EXPECT_LT((int)ring.g, 80);
}

TEST(PointOverlay, ATransparentColourIsHowADocumentAsksForTheBareIcon) {
  ScratchDir icons("bare");
  const fv::MapProjection proj = HarbourProj();
  auto ink = [&](unsigned char alpha, bool select) {
    fv::CpuCanvas canvas(800, 600);
    canvas.Clear(fv::FvColor{255, 255, 255, 255});
    PointOverlay o;
    const int64_t id = o.AddSymbol(PngSymbol(icons, "dot", 8, 8, 0, 200, 0));
    MapPoint p = MakePoint(1, "X", 32.74, -79.89);
    p.size_px = 40;
    p.color = fv::FvColor{0, 0, 255, alpha};
    p.symbol_id = id;
    o.SetPoints({p});
    if (select) o.SetSelected(1);
    EXPECT_TRUE(o.OnDraw(proj, canvas).ok());
    int blue = 0, yellow = 0, green = 0;
    for (int y = 260; y < 340; ++y)
      for (int x = 360; x < 440; ++x) {
        const unsigned char* px = canvas.Buffer().Row(y) + x * 4;
        if (px[2] > 150 && px[0] < 80 && px[1] < 80) ++blue;
        if (px[0] > 200 && px[1] > 180 && px[2] < 80) ++yellow;
        if (px[1] > 150 && px[0] < 80 && px[2] < 80) ++green;
      }
    return std::make_tuple(blue, yellow, green);
  };

  const auto opaque = ink(255, false);
  EXPECT_GT(std::get<0>(opaque), 0) << "an opaque colour badges";
  const auto clear = ink(0, false);
  EXPECT_EQ(0, std::get<0>(clear)) << "alpha 0 suppresses the badge";
  EXPECT_GT(std::get<2>(clear), 0) << "and the icon is still drawn";
  // Selection has to remain visible on a point with no badge to ring.
  const auto clear_selected = ink(0, true);
  EXPECT_GT(std::get<1>(clear_selected), 0)
      << "a selected bare icon still wears the selection edge";
}

TEST(PointOverlay, ASymbolThatWillNotDecodeLeavesThePointDrawnAsItsShape) {
  // The colour parser's rule, applied to artwork: one bad row must not cost
  // the document a point, and it must not cost it a frame either.
  const fv::MapProjection proj = HarbourProj();
  fv::CpuCanvas canvas(800, 600);
  canvas.Clear(fv::FvColor{255, 255, 255, 255});

  PointOverlay o;
  PointSymbol junk;
  junk.name = "not-a-png";
  junk.image = {'n', 'o', 'p', 'e'};
  const int64_t id = o.AddSymbol(junk);
  MapPoint p = MakePoint(1, "X", 32.74, -79.89);
  p.size_px = 20;
  p.color = fv::FvColor{0, 0, 255, 255};
  p.symbol_id = id;
  // An id pointing at NO row at all is the other half of the same rule.
  MapPoint q = MakePoint(2, "Y", 32.74, -79.88);
  q.size_px = 20;
  q.color = fv::FvColor{0, 0, 255, 255};
  q.symbol_id = 9999;
  o.SetPoints({p, q});
  ASSERT_TRUE(o.OnDraw(proj, canvas).ok());

  const unsigned char* px = canvas.Buffer().Row(300) + 400 * 4;
  EXPECT_GT((int)px[2], 200) << "the shape is still there";
}

TEST(PointOverlay, ALabelIsHaloedSoItReadsOverWhateverIsUnderIt) {
  // The hand-rolled label could not have a halo; a LabelStyle one gets T2's
  // for free. Drawn over a DARK background, where an unhaloed black name is
  // invisible and a haloed one is not.
  const fv::MapProjection proj = HarbourProj();
  const char* fonts[] = {"/System/Library/Fonts/Supplemental/Arial.ttf",
                         "/System/Library/Fonts/Supplemental/Courier New.ttf",
                         "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
  std::string font;
  for (const char* f : fonts)
    if (FILE* fp = fopen(f, "rb")) {
      fclose(fp);
      font = f;
      break;
    }
  if (font.empty()) GTEST_SKIP() << "no known system TTF";

  auto ink_over_dark = [&](bool labels) {
    fv::CpuCanvas canvas(800, 600);
    EXPECT_TRUE(canvas.SetDefaultFont(font).ok());
    canvas.Clear(fv::FvColor{20, 20, 20, 255});
    PointOverlay o;
    MapPoint p = MakePoint(1, "Fort Sumter", 32.74, -79.89);
    o.SetPoints({p});
    o.SetShowLabels(labels);
    EXPECT_TRUE(o.OnDraw(proj, canvas).ok());
    int white = 0;
    for (int y = 0; y < 600; ++y)
      for (int x = 0; x < 800; ++x) {
        const unsigned char* px = canvas.Buffer().Row(y) + x * 4;
        if (px[0] > 200 && px[1] > 200 && px[2] > 200) ++white;
      }
    return white;
  };
  EXPECT_EQ(ink_over_dark(false), 0);
  EXPECT_GT(ink_over_dark(true), 0)
      << "the halo is the only white ink this overlay draws";
}

TEST(PointOverlay, APointOffTheProjectionDrawsNothingAndDoesNotFail) {
  const fv::MapProjection proj = HarbourProj();
  fv::CpuCanvas canvas(800, 600);
  canvas.Clear(fv::FvColor{255, 255, 255, 255});
  PointOverlay o;
  o.SetPoints({MakePoint(1, "far away", -40.0, 150.0)});
  EXPECT_TRUE(o.OnDraw(proj, canvas).ok());
}

// ---------------------------------------------------------------------------
// Pick
// ---------------------------------------------------------------------------

TEST(PointOverlay, AHitReportsThePointsOwnIdAndAttributes) {
  const fv::MapProjection proj = HarbourProj();
  PointOverlay o("Marks");
  MapPoint p = MakePoint(11, "Charleston Light", 32.74, -79.89);
  p.category = "light";
  p.elevation_ft = 163;
  p.remarks = "Sullivans Island";
  o.SetPoints({p});

  std::vector<fv::app::HitItem> hits;
  o.HitTestPoint(proj, {400, 300}, 8.0, hits);
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ(&o, hits[0].overlay);
  EXPECT_EQ(11u, hits[0].feature);  // the id in the document, not a minted one
  EXPECT_LT(hits[0].distance_px, 1.0);
  EXPECT_EQ("Charleston Light", hits[0].hint.tool_tip);
  // The two attributes are what make a hit attributable to THIS point.
  EXPECT_NE(std::string::npos, hits[0].hint.status.find("light"));
  EXPECT_NE(std::string::npos, hits[0].hint.status.find("163 ft"));
  EXPECT_NE(std::string::npos, hits[0].hint.status.find("Sullivans Island"));
  EXPECT_EQ(fv::app::CursorId::kHand, hits[0].cursor);
}

TEST(PointOverlay, ABigSymbolIsHitAnywhereOnItsInk) {
  const fv::MapProjection proj = HarbourProj();
  PointOverlay o;
  MapPoint p = MakePoint(1, "big", 32.74, -79.89);
  p.size_px = 40;  // half-width 20, well beyond the 8 px tolerance
  o.SetPoints({p});

  std::vector<fv::app::HitItem> hits;
  o.HitTestPoint(proj, {418, 300}, 8.0, hits);  // 18 px off centre
  EXPECT_EQ(1u, hits.size());
  hits.clear();
  o.HitTestPoint(proj, {440, 300}, 8.0, hits);  // 40 px off: past the ink
  EXPECT_TRUE(hits.empty());
}

TEST(PointOverlay, TheSampleDocumentPairsTwoPointsWithinPickTolerance) {
  // A DATA property, pinned deliberately: the ambiguous-pick policies have
  // nothing to work on unless two sample points really do land under one tap.
  const fv::MapProjection proj = HarbourProj();
  PointOverlay o("Sample");
  o.SetPoints(PointOverlay::SamplePoints());

  const MapPoint* r2 = o.Find(10);
  ASSERT_NE(nullptr, r2);
  double sx = 0, sy = 0;
  ASSERT_TRUE(proj.GeoToSurface(r2->position, &sx, &sy).ok());

  std::vector<fv::app::HitItem> hits;
  o.HitTestPoint(proj, {(int)sx, (int)sy}, 8.0, hits);
  ASSERT_EQ(2u, hits.size()) << "the R2/G1 buoy pair is no longer ambiguous";
  EXPECT_NE(hits[0].feature, hits[1].feature);
}

TEST(PointOverlay, TheSampleDocumentSharesOneIconAcrossTheThreeForts) {
  // The other DATA property worth pinning, and the reason the palette is a
  // second table: the forts wear one `castle` row between them, so the file
  // carries the artwork once however many points name it. Only the icons the
  // scratch directory actually holds are embedded, which is also the test that
  // a missing icon leaves a hole instead of shifting the ids.
  ScratchDir icons("sample");
  ASSERT_TRUE(fv::WritePng(Solid(8, 8, 200, 0, 0), icons.file("castle.png")).ok());
  ASSERT_TRUE(fv::WritePng(Solid(8, 8, 0, 200, 0), icons.file("marker.png")).ok());

  const std::vector<PointSymbol> palette =
      PointOverlay::SampleSymbols(icons.str());
  ASSERT_EQ(2u, palette.size()) << "only what the directory has";

  PointOverlay o("Sample");
  o.SetPoints(PointOverlay::SamplePoints());
  o.SetSymbols(palette);
  const PointSymbol* castle = o.FindSymbolByName("castle");
  ASSERT_NE(nullptr, castle);

  int forts = 0;
  for (const MapPoint& p : o.points())
    if (p.symbol_id == castle->id) ++forts;
  EXPECT_EQ(3, forts) << "Sumter, Pinckney and Moultrie share one row";

  // Every sample point names an icon, whether or not this directory had it —
  // the id is the sample's own and does not depend on what was embeddable.
  for (const MapPoint& p : o.points())
    EXPECT_NE(0, p.symbol_id) << p.name << " names no icon";
}

TEST(PointOverlay, TheSampleFileEmbedsWhateverIconsItWasGiven) {
  ScratchDir icons("samplefile");
  ASSERT_TRUE(fv::WritePng(Solid(8, 8, 200, 0, 0), icons.file("castle.png")).ok());
  const std::string spec = TempSpec("sample");
  std::remove(spec.c_str());
  ASSERT_TRUE(PointOverlay::WriteSampleFile(spec, icons.str()).ok());

  PointOverlay o;
  ASSERT_TRUE(o.FileOpen(spec).ok());
  ASSERT_EQ(1u, o.symbols().size());
  EXPECT_EQ("castle", o.symbols()[0].name);
  EXPECT_FALSE(o.symbols()[0].image.empty());
  EXPECT_EQ(PointOverlay::SamplePoints().size(), o.points().size());
  std::remove(spec.c_str());

  // No directory: the schema-1 picture exactly, and still a usable document.
  ASSERT_TRUE(PointOverlay::WriteSampleFile(spec).ok());
  PointOverlay bare;
  ASSERT_TRUE(bare.FileOpen(spec).ok());
  EXPECT_TRUE(bare.symbols().empty());
  EXPECT_EQ(PointOverlay::SamplePoints().size(), bare.points().size());
  std::remove(spec.c_str());
}

TEST(PointOverlay, TheContextMenuNamesWhatIsUnderThePointAndSelectsIt) {
  const fv::MapProjection proj = HarbourProj();
  PointOverlay o("Sample");
  o.SetPoints(PointOverlay::SamplePoints());
  const MapPoint* light = o.Find(8);
  ASSERT_NE(nullptr, light);
  double sx = 0, sy = 0;
  ASSERT_TRUE(proj.GeoToSurface(light->position, &sx, &sy).ok());

  fv::app::MenuNode menu;
  o.AppendMenuItems(proj, {(int)sx, (int)sy}, menu);
  ASSERT_FALSE(menu.children.empty());
  EXPECT_NE(std::string::npos, menu.children[0].label.find("Charleston Light"));
  ASSERT_TRUE(menu.children[0].action != nullptr);
  menu.children[0].action();
  EXPECT_EQ(8, o.selected());

  // Nothing under the point contributes nothing -- an empty section would
  // otherwise separator its way into every right-click menu.
  fv::app::MenuNode empty;
  o.AppendMenuItems(proj, {5, 5}, empty);
  EXPECT_TRUE(empty.children.empty());
}

// ---------------------------------------------------------------------------
// Through the real pick session and the registry
// ---------------------------------------------------------------------------

TEST(PointOverlay, ThePickSessionRanksTheSamplePairByPolicy) {
  const fv::MapProjection proj = HarbourProj();
  fv::OverlayManager manager;
  auto o = std::make_shared<PointOverlay>("Sample");
  o->SetPoints(PointOverlay::SamplePoints());
  ASSERT_TRUE(manager.Add(o).ok());

  // A shell that answers nothing: this test never asks a question.
  struct Silent : fv::app::AppShell {
    SaveAnswer AskSave(const std::string&) override { return SaveAnswer::kCancel; }
    std::vector<std::string> ChooseFilesToOpen(
        const fv::app::FileTypeDesc&) override { return {}; }
    std::pair<std::string, int> ChooseSaveSpec(
        const fv::app::FileTypeDesc&, const std::string&) override {
      return {"", 0};
    }
    std::optional<int> ChooseFromList(const std::string&,
                                      const std::vector<std::string>& rows) override {
      last_rows = rows;
      return 1;  // always the second row, so the choice is visible
    }
    bool ConfirmRevert(const std::string&) override { return false; }
    void SetCursor(fv::app::CursorId c) override { cursor = c; }
    void ShowHint(const fv::app::HintText& h) override { hint = h; }
    void ShowContextMenu(fv::PixelPoint, const fv::app::MenuNode&) override {}
    void RequestInvalidate() override {}
    void OnEditorChanged(const fv::app::TypeId&, fv::app::OverlayEditor*) override {}
    void ReportError(const Status&) override {}
    std::vector<std::string> last_rows;
    fv::app::CursorId cursor = fv::app::CursorId::kDefault;
    fv::app::HintText hint;
  } shell;

  fv::app::PickSession pick(manager, shell);
  const MapPoint* r2 = o->Find(10);
  ASSERT_NE(nullptr, r2);
  double sx = 0, sy = 0;
  ASSERT_TRUE(proj.GeoToSurface(r2->position, &sx, &sy).ok());
  const fv::PixelPoint at{(int)sx, (int)sy};

  // Hover: the shell hears the winner's cursor and hint.
  pick.UpdateHover(proj, at);
  ASSERT_NE(nullptr, pick.hovered());
  EXPECT_EQ(fv::app::CursorId::kHand, shell.cursor);
  EXPECT_FALSE(shell.hint.status.empty());

  // Ambiguity: two candidates under one tap, so the chooser is asked, and the
  // answer is the row the user picked rather than the topmost.
  const auto ranked = pick.HitTestPoint(proj, at, fv::app::PickPolicy::kNearest);
  ASSERT_EQ(2u, ranked.size());
  const auto chosen =
      pick.ResolveClick(proj, at, fv::app::PickPolicy::kAskWhenAmbiguous);
  ASSERT_TRUE(chosen.has_value());
  EXPECT_EQ(2u, shell.last_rows.size());
  EXPECT_NE(ranked[0].feature, chosen->feature);
}

TEST(PointOverlay, TheBuiltinRegistryOffersItAsAFileType) {
  fv::app::OverlayTypeRegistry registry;
  ASSERT_TRUE(fv::app::RegisterBuiltinOverlayTypes(registry).ok());

  const fv::app::OverlayTypeDesc* desc = registry.Find(PointOverlay::kTypeId);
  ASSERT_NE(nullptr, desc);
  EXPECT_TRUE(registry.IsFile(PointOverlay::kTypeId));
  ASSERT_TRUE(desc->file.has_value());
  EXPECT_EQ(PointOverlay::kExtension, desc->file->default_extension);
  // Dispatch by extension is what File > Open uses, and a leading dot is
  // tolerated because a file spec has one.
  EXPECT_EQ(desc, registry.FindByExtension(".FVPOINTS"));

  auto made = desc->factory();
  ASSERT_NE(nullptr, made);
  EXPECT_NE(nullptr, made->AsPersistence());
  EXPECT_NE(nullptr, made->AsHitTest());
  EXPECT_NE(nullptr, made->AsContextMenu());
}

}  // namespace
