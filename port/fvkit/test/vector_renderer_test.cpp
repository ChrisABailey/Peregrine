// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// VectorRenderer tests (vpf-geosym plan phase V5b).
//
// The renderer is the SHARED side of the vector seam, so everything here is
// synthetic on purpose: a stub IVectorSource and a stub IStyleEngine. If this
// file ever needs VPF or GeoSym to pass, the seam has leaked.
//
// The GeoSym/DNC end-to-end render (real assets, golden hash) lives in
// port/GeoSymServer/test/geosym_style_test.cpp.

#include "fvkit/vector/renderer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/canvas/cpu_canvas.h"

namespace {

using fv::ClipPolygon;
using fv::ClipPolyline;
using fv::PixelPoint;
using fv::SurfacePoint;

// --- clipping --------------------------------------------------------------

TEST(VectorClip, PolylineWhollyInsideIsUnchanged) {
  std::vector<SurfacePoint> pts{{10, 10}, {20, 30}, {40, 15}};
  auto runs = ClipPolyline(pts, 64, 64);
  ASSERT_EQ(runs.size(), 1u);
  ASSERT_EQ(runs[0].size(), 3u);
  EXPECT_EQ(runs[0][0].x, 10);
  EXPECT_EQ(runs[0][2].y, 15);
}

TEST(VectorClip, PolylineWhollyOutsideDisappears) {
  std::vector<SurfacePoint> pts{{-50, -50}, {-20, -30}};
  EXPECT_TRUE(ClipPolyline(pts, 64, 64).empty());
}

TEST(VectorClip, PolylineCrossingTheEdgeIsCutAtTheBoundary) {
  // Horizontal line from off-left to the middle: enters at x = 0.
  std::vector<SurfacePoint> pts{{-30, 20}, {30, 20}};
  auto runs = ClipPolyline(pts, 64, 64);
  ASSERT_EQ(runs.size(), 1u);
  ASSERT_EQ(runs[0].size(), 2u);
  EXPECT_EQ(runs[0][0].x, 0);
  EXPECT_EQ(runs[0][0].y, 20);
  EXPECT_EQ(runs[0][1].x, 30);
}

TEST(VectorClip, PolylineLeavingAndReenteringYieldsTwoRuns) {
  // in -> out the top -> back in: two separate visible runs.
  std::vector<SurfacePoint> pts{{10, 10}, {20, -40}, {30, -40}, {40, 10}};
  auto runs = ClipPolyline(pts, 64, 64);
  ASSERT_EQ(runs.size(), 2u);
  EXPECT_EQ(runs[0].front().x, 10);
  EXPECT_EQ(runs[1].back().x, 40);
}

TEST(VectorClip, PolygonIsClippedToTheViewport) {
  // A square hanging off the left edge; the survivor is the right half.
  std::vector<SurfacePoint> ring{{-20, 10}, {30, 10}, {30, 50}, {-20, 50}};
  auto out = ClipPolygon(ring, 64, 64);
  ASSERT_GE(out.size(), 3u);
  for (const PixelPoint& p : out) {
    EXPECT_GE(p.x, 0);
    EXPECT_LE(p.x, 63);
  }
}

TEST(VectorClip, PolygonEntirelyOutsideIsEmpty) {
  std::vector<SurfacePoint> ring{{100, 100}, {140, 100}, {120, 140}};
  EXPECT_TRUE(ClipPolygon(ring, 64, 64).empty());
}

// --- stub source / style ---------------------------------------------------

class StubSource : public fv::IVectorSource {
 public:
  std::vector<fv::VectorFeature> features;
  fv::VectorQuery last_query;

  fv::Status Open(const std::string&) override { return fv::Status::Ok(); }
  bool IsOpen() const override { return true; }
  fv::GeoRect Bounds() const override { return fv::GeoRect::World(); }
  std::vector<std::string> Layers() const override { return {"stub"}; }
  fv::Status Query(const fv::VectorQuery& q,
                   std::vector<fv::VectorFeature>* out) override {
    last_query = q;
    for (const auto& f : features) out->push_back(f);
    return fv::Status::Ok();
  }
};

// Styles by layer name: colour comes from the layer, priority from a map, so
// tests can assert draw ORDER independently of query order.
class StubStyle : public fv::IStyleEngine {
 public:
  fv::FvColor color{255, 0, 0, 255};
  int priority = 0;
  bool emit_symbol = false;
  int wide_pen = 1;
  fv::VectorSymbol symbol;
  std::vector<std::string> styled_keys;

  fv::Status Style(const fv::VectorFeature& f, const fv::StyleContext& ctx,
                   std::vector<fv::StyleResult>* out) override {
    styled_keys.push_back(f.style_key);
    last_ctx = ctx;
    fv::StyleResult r;
    r.priority = priority;
    if (emit_symbol) {
      r.symbol.valid = true;
      r.symbol.symbol_id = "stub";
    } else {
      r.stroke.valid = true;
      r.stroke.pen.color = color;
      r.stroke.pen.width = wide_pen;
    }
    out->push_back(r);
    return fv::Status::Ok();
  }

  const fv::VectorSymbol* Symbol(const std::string& id) override {
    return id == "stub" ? &symbol : nullptr;
  }

  fv::StyleContext last_ctx;
};

// Styles each feature at the priority named by its style_key ("1", "2", ...)
// with that feature's own colour, so the LAST one drawn wins the pixel.
class PriorityStyle : public fv::IStyleEngine {
 public:
  fv::Status Style(const fv::VectorFeature& f, const fv::StyleContext&,
                   std::vector<fv::StyleResult>* out) override {
    fv::StyleResult r;
    r.priority = std::stoi(f.style_key);
    r.stroke.valid = true;
    r.stroke.pen.width = 1;
    r.stroke.pen.color = f.style_key == "1" ? fv::FvColor{255, 0, 0, 255}
                                            : fv::FvColor{0, 255, 0, 255};
    out->push_back(r);
    return fv::Status::Ok();
  }
  const fv::VectorSymbol* Symbol(const std::string&) override {
    return nullptr;
  }
};

fv::VectorFeature Line(const std::string& key,
                       std::vector<fv::GeoPoint> pts) {
  fv::VectorFeature f;
  f.type = fv::VectorGeometryType::kLine;
  f.style_key = key;
  f.layer = "stub";
  f.parts.push_back(std::move(pts));
  return f;
}

fv::MapProjection Proj(int w, int h, double lat, double lon, double dpp) {
  fv::MapProjection p;
  p.SetSurfaceSize(w, h);
  p.SetCenter(fv::GeoPoint{lat, lon});
  p.SetResolution(dpp, dpp);
  return p;
}

const unsigned char* Px(const fv::PixelBuffer& b, int x, int y) {
  return b.Row(y) + 4 * x;
}

// --- renderer --------------------------------------------------------------

TEST(VectorRenderer, DrawsAStrokedLineAndReportsStats) {
  auto src = std::make_shared<StubSource>();
  // A horizontal line through the centre of a 1 deg/px, 64x64 viewport at 0,0.
  src->features.push_back(Line("a", {{0.0, -10.0}, {0.0, 10.0}}));
  auto style = std::make_shared<StubStyle>();

  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});

  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  EXPECT_EQ(r.features_queried(), 1u);
  EXPECT_EQ(r.draws_emitted(), 1u);
  // Centre row is red; a row well away from it is untouched.
  EXPECT_EQ(Px(canvas.Buffer(), 32, 32)[0], 255);
  EXPECT_EQ(Px(canvas.Buffer(), 32, 10)[0], 0);
}

TEST(VectorRenderer, PassesTheViewportAndScaleToTheSource) {
  auto src = std::make_shared<StubSource>();
  auto style = std::make_shared<StubStyle>();
  fv::CpuCanvas canvas(64, 64);

  fv::MapProjection p;
  p.SetSurfaceSize(64, 64);
  p.SetCenter(fv::GeoPoint{35.0, -84.0});
  p.SetScale(500000.0);

  fv::VectorRenderer r(src, style);
  r.SetMaxFeatures(7);
  ASSERT_TRUE(r.Render(p, &canvas).ok());

  EXPECT_EQ(src->last_query.max_features, 7u);
  EXPECT_DOUBLE_EQ(src->last_query.scale_denominator, 500000.0);
  EXPECT_TRUE(src->last_query.area.Contains(fv::GeoPoint{35.0, -84.0}));
}

TEST(VectorRenderer, HigherPriorityDrawsOnTop) {
  // Same geometry twice; the priority-2 green must win the pixel even though
  // it is queried first.
  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("2", {{0.0, -10.0}, {0.0, 10.0}}));
  src->features.push_back(Line("1", {{0.0, -10.0}, {0.0, 10.0}}));
  auto style = std::make_shared<PriorityStyle>();

  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  const unsigned char* px = Px(canvas.Buffer(), 32, 32);
  EXPECT_EQ(px[0], 0);    // not red
  EXPECT_EQ(px[1], 255);  // green, the priority-2 pass
}

TEST(VectorRenderer, DrawsAPointSymbolAnchoredAtTheFeature) {
  auto src = std::make_shared<StubSource>();
  fv::VectorFeature pt;
  pt.type = fv::VectorGeometryType::kPoint;
  pt.style_key = "p";
  pt.parts.push_back({fv::GeoPoint{0.0, 0.0}});
  src->features.push_back(pt);

  auto style = std::make_shared<StubStyle>();
  style->emit_symbol = true;
  // A filled 200x200-HIMETRIC box centred on the symbol origin. At the
  // renderer's symbol scale (1/25.4 px per HIMETRIC unit) that is ~8 px.
  fv::SymbolPrimitive box;
  box.type = fv::SymbolPrimitiveType::kPolygon;
  box.points = {{-100, -100}, {100, -100}, {100, 100}, {-100, 100}};
  box.has_fill = true;
  box.fill_color = fv::FvColor{0, 0, 255, 255};
  style->symbol.primitives.push_back(box);

  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  EXPECT_EQ(r.draws_emitted(), 1u);
  EXPECT_EQ(Px(canvas.Buffer(), 32, 32)[2], 255);  // blue at the anchor
  EXPECT_EQ(Px(canvas.Buffer(), 32, 50)[2], 0);    // and not far away
  // The symbol is small: ~200/25.4 px across, so 8 px out is already clear.
  EXPECT_EQ(Px(canvas.Buffer(), 45, 32)[2], 0);
}

TEST(VectorRenderer, LongLineIsClippedNotDropped) {
  auto src = std::make_shared<StubSource>();
  // Spans far beyond the viewport in both directions.
  src->features.push_back(Line("a", {{-80.0, -170.0}, {80.0, 170.0}}));
  auto style = std::make_shared<StubStyle>();

  fv::CpuCanvas canvas(64, 64);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  EXPECT_GE(r.draws_emitted(), 1u);
  // Clipped, not dropped: the visible run reaches both side edges. (The
  // exact centre pixel is not asserted — the projection puts the map centre
  // at (w-1)/2, i.e. 31.5, so the crossing straddles two pixels.)
  auto column_has_red = [&](int x) {
    for (int y = 0; y < 64; ++y)
      if (Px(canvas.Buffer(), x, y)[0] == 255) return true;
    return false;
  };
  EXPECT_TRUE(column_has_red(0));
  EXPECT_TRUE(column_has_red(63));
}

TEST(VectorRenderer, RejectsANullCanvasAndAnUnreadyProjection) {
  auto src = std::make_shared<StubSource>();
  auto style = std::make_shared<StubStyle>();
  fv::VectorRenderer r(src, style);
  EXPECT_EQ(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), nullptr).code,
            fv::kInvalidArg);

  fv::CpuCanvas canvas(8, 8);
  fv::MapProjection empty;
  EXPECT_EQ(r.Render(empty, &canvas).code, fv::kInvalidArg);
}

// --- pick index (identify, plan §5.3) --------------------------------------

TEST(VectorRendererPick, IndexesWhatWasDrawnAndHitTestsIt) {
  auto src = std::make_shared<StubSource>();
  fv::VectorFeature f = Line("a", {{0.0, -10.0}, {0.0, 10.0}});
  f.ref.layer = 0;
  f.ref.tile = 4;
  f.ref.feature = 77;
  src->features.push_back(f);
  auto style = std::make_shared<StubStyle>();

  fv::CpuCanvas canvas(64, 64);
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.pick_enabled()) << "identify is on by default";
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  // The line is drawn along the centre row; a tap there finds exactly it.
  auto hits = r.pick_index().HitTest(32, 32, 2.0);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].ref.feature, 77);
  EXPECT_EQ(hits[0].ref.tile, 4);
  // A tap far from any ink finds nothing.
  EXPECT_TRUE(r.pick_index().HitTest(32, 5, 2.0).empty());
}

TEST(VectorRendererPick, HitTestFollowsTheINKNotTheSourceGeometry) {
  // The plan's rule: the index is built from DRAWN geometry. A fat pen is
  // tappable across its whole width, and a feature clipped away is gone.
  auto src = std::make_shared<StubSource>();
  fv::VectorFeature on_screen = Line("a", {{0.0, -10.0}, {0.0, 10.0}});
  on_screen.ref.layer = 0;
  on_screen.ref.feature = 1;
  fv::VectorFeature off_screen = Line("a", {{80.0, -10.0}, {80.0, 10.0}});
  off_screen.ref.layer = 0;
  off_screen.ref.feature = 2;
  src->features.push_back(on_screen);
  src->features.push_back(off_screen);

  auto style = std::make_shared<StubStyle>();
  style->wide_pen = 9;  // ink spans +-4 px around the centreline

  fv::CpuCanvas canvas(64, 64);
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  EXPECT_EQ(r.features_queried(), 2u);
  // 4 px off the centreline is still on the 9-px-wide stroke...
  auto wide = r.pick_index().HitTest(32, 36, 0.0);
  ASSERT_EQ(wide.size(), 1u);
  EXPECT_EQ(wide[0].ref.feature, 1);
  // ...and the feature that never reached the canvas is not in the index.
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x)
      for (const auto& h : r.pick_index().HitTest(x, y, 0.0))
        EXPECT_NE(h.ref.feature, 2) << "clipped-away feature is pickable";
}

TEST(VectorRendererPick, DisablingPickingLeavesTheIndexEmpty) {
  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("a", {{0.0, -10.0}, {0.0, 10.0}}));
  auto style = std::make_shared<StubStyle>();

  fv::CpuCanvas canvas(64, 64);
  fv::VectorRenderer r(src, style);
  r.SetPickEnabled(false);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());
  EXPECT_EQ(r.draws_emitted(), 1u) << "still drawn";
  EXPECT_TRUE(r.pick_index().empty());
}

TEST(VectorRendererPick, IndexIsRebuiltEachRender) {
  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("a", {{0.0, -10.0}, {0.0, 10.0}}));
  auto style = std::make_shared<StubStyle>();

  fv::CpuCanvas canvas(64, 64);
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());
  const size_t first = r.pick_index().shape_count();
  ASSERT_GT(first, 0u);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());
  EXPECT_EQ(r.pick_index().shape_count(), first) << "cleared, not appended";

  // Pan the map away from the feature: the index empties with the canvas.
  ASSERT_TRUE(r.Render(Proj(64, 64, 70.0, 70.0, 1.0), &canvas).ok());
  EXPECT_TRUE(r.pick_index().empty());
}

TEST(VectorRendererPick, PointSymbolsGetATappableBox) {
  auto src = std::make_shared<StubSource>();
  fv::VectorFeature p;
  p.type = fv::VectorGeometryType::kPoint;
  p.style_key = "a";
  p.layer = "stub";
  p.parts.push_back({fv::GeoPoint{0.0, 0.0}});
  p.ref.layer = 0;
  p.ref.feature = 5;
  src->features.push_back(p);

  auto style = std::make_shared<StubStyle>();
  style->emit_symbol = true;
  // A minute symbol: two HIMETRIC units across, so it inks about one pixel.
  fv::SymbolPrimitive prim;
  prim.type = fv::SymbolPrimitiveType::kPolyline;
  prim.has_stroke = true;
  prim.points = {{-1.0, 0.0}, {1.0, 0.0}};
  style->symbol.primitives.push_back(prim);

  fv::CpuCanvas canvas(64, 64);
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());

  auto hits = r.pick_index().HitTest(32, 32, 0.0);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].ref.feature, 5);
  // Its ink is about a pixel, but the box is padded to a finger-sized
  // minimum — that is the point of hit-testing the glyph, not the anchor.
  EXPECT_EQ(r.pick_index().HitTest(35, 34, 0.0).size(), 1u);
  EXPECT_TRUE(r.pick_index().HitTest(50, 50, 0.0).empty());
}

TEST(VectorRendererPick, ASymbolThatDrewNothingIsNotPickable) {
  // The index follows the ink: an unknown/empty symbol emits no draw call, so
  // it must not leave a phantom hit box behind either.
  auto src = std::make_shared<StubSource>();
  fv::VectorFeature p;
  p.type = fv::VectorGeometryType::kPoint;
  p.style_key = "a";
  p.layer = "stub";
  p.parts.push_back({fv::GeoPoint{0.0, 0.0}});
  p.ref.layer = 0;
  p.ref.feature = 6;
  src->features.push_back(p);

  auto style = std::make_shared<StubStyle>();
  style->emit_symbol = true;  // stub symbol has no primitives at all

  fv::CpuCanvas canvas(64, 64);
  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 1.0), &canvas).ok());
  EXPECT_EQ(r.draws_emitted(), 0u);
  EXPECT_TRUE(r.pick_index().empty());
}

TEST(VectorRenderer, StyleContextCarriesDpiAndSymbolScale) {
  auto src = std::make_shared<StubSource>();
  src->features.push_back(Line("a", {{0.0, -1.0}, {0.0, 1.0}}));
  auto style = std::make_shared<StubStyle>();

  fv::CpuCanvas canvas(32, 32);
  fv::VectorRenderer r(src, style);
  r.SetDeviceDpi(144.0);
  r.SetSymbolScale(2.0);
  ASSERT_TRUE(r.Render(Proj(32, 32, 0.0, 0.0, 1.0), &canvas).ok());

  EXPECT_DOUBLE_EQ(style->last_ctx.device_dpi, 144.0);
  EXPECT_DOUBLE_EQ(style->last_ctx.symbol_scale, 2.0);
}

}  // namespace
