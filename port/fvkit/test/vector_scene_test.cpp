// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// VectorScene tests (vpf-geosym plan §5.4, session R3a).
//
// HERMETIC, like the renderer and rule-layer tests beside it: the source and
// the style engine here are ~30 lines each, invented in this file. No VPF, no
// GeoSym, no ENC. A retained scene is a cache, and a cache's failure mode is
// serving something stale — so the reuse predicate is pinned against a style
// engine whose epoch this file controls directly, rather than against a real
// product where "did anything change?" is a data question.
//
// The real-data half (the harbour render must be byte-identical whether the
// scene was rebuilt or reused) is in geosym_style_test.cpp, where the assets
// and the golden already live.

#include "fvkit/vector/scene.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/vector/lookup_engine.h"
#include "fvkit/vector/renderer.h"

namespace {

using fv::GeoPoint;
using fv::GeoRect;
using fv::SimplifyPath;
using fv::StyleContext;
using fv::VectorScene;

// --- Douglas-Peucker -------------------------------------------------------

std::vector<GeoPoint> Pts(std::vector<std::pair<double, double>> ll) {
  std::vector<GeoPoint> out;
  for (const auto& p : ll) out.push_back(GeoPoint{p.first, p.second});
  return out;
}

TEST(SimplifyPath, CollinearInteriorVerticesCollapse) {
  // Nine points on one straight line: only the ends are load-bearing.
  std::vector<GeoPoint> line;
  for (int i = 0; i < 9; ++i) line.push_back(GeoPoint{0.0, i * 1.0});
  const auto out = SimplifyPath(line, 0.1, 0.1, false);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_DOUBLE_EQ(out.front().lon, 0.0);
  EXPECT_DOUBLE_EQ(out.back().lon, 8.0);
}

TEST(SimplifyPath, ADetourLargerThanTheToleranceSurvives) {
  // A spike 1.0 deg off the chord, against a 0.1 deg tolerance.
  const auto line = Pts({{0, 0}, {1.0, 1}, {0, 2}});
  const auto out = SimplifyPath(line, 0.1, 0.1, false);
  ASSERT_EQ(out.size(), 3u);
  EXPECT_DOUBLE_EQ(out[1].lat, 1.0);
}

TEST(SimplifyPath, ADetourSmallerThanTheToleranceIsDropped) {
  const auto line = Pts({{0, 0}, {0.01, 1}, {0, 2}});
  const auto out = SimplifyPath(line, 0.1, 0.1, false);
  EXPECT_EQ(out.size(), 2u);
}

TEST(SimplifyPath, ToleranceIsPerAxis) {
  // The same 0.05 deg latitude detour: kept under a tight lat tolerance,
  // dropped under a loose one. This is what makes a pixel tolerance mean the
  // same thing on both axes of an anisotropic projection.
  const auto line = Pts({{0, 0}, {0.05, 1}, {0, 2}});
  EXPECT_EQ(SimplifyPath(line, 0.01, 10.0, false).size(), 3u);
  EXPECT_EQ(SimplifyPath(line, 0.5, 10.0, false).size(), 2u);
}

TEST(SimplifyPath, EndpointsAreAlwaysKept) {
  const auto line = Pts({{0, 0}, {0.001, 1}, {0.001, 2}, {0, 3}});
  const auto out = SimplifyPath(line, 1.0, 1.0, false);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_DOUBLE_EQ(out.front().lon, 0.0);
  EXPECT_DOUBLE_EQ(out.back().lon, 3.0);
}

TEST(SimplifyPath, ARingNeverCollapsesToASliver) {
  // A square whose vertices all sit inside a huge tolerance. Simplified as an
  // open path it would come back as two points — a ring that draws nothing.
  // The closed guard hands back the original instead.
  const auto ring = Pts({{0, 0}, {0, 1}, {1, 1}, {1, 0}});
  EXPECT_EQ(SimplifyPath(ring, 10.0, 10.0, true).size(), 4u);
  EXPECT_EQ(SimplifyPath(ring, 10.0, 10.0, false).size(), 2u);
}

TEST(SimplifyPath, ZeroOrNegativeToleranceIsExact) {
  const auto line = Pts({{0, 0}, {0.001, 1}, {0, 2}});
  EXPECT_EQ(SimplifyPath(line, 0.0, 0.0, false).size(), 3u);
  EXPECT_EQ(SimplifyPath(line, -1.0, -1.0, false).size(), 3u);
}

// --- a source and a style engine this file owns entirely -------------------

class TinySource : public fv::IVectorSource {
 public:
  std::vector<fv::VectorFeature> features;
  size_t queries = 0;
  bool fail = false;

  fv::Status Open(const std::string&) override { return fv::Status::Ok(); }
  bool IsOpen() const override { return true; }
  GeoRect Bounds() const override { return GeoRect::World(); }
  std::vector<std::string> Layers() const override { return {"tiny"}; }
  fv::Status Query(const fv::VectorQuery&,
                   std::vector<fv::VectorFeature>* out) override {
    ++queries;
    if (fail) return fv::Status::Error(fv::kIoError, "no");
    for (const auto& f : features) out->push_back(f);
    return fv::Status::Ok();
  }
};

// Priority comes from the style_key, so draw order is asserted independently
// of query order. `epoch` is a knob, which is the whole point of the file.
class TinyStyle : public fv::IStyleEngine {
 public:
  uint64_t epoch = 1;
  size_t styled = 0;

  fv::Status Style(const fv::VectorFeature& f, const StyleContext&,
                   std::vector<fv::StyleResult>* out) override {
    ++styled;
    fv::StyleResult r;
    r.priority = std::stoi(f.style_key);
    r.stroke.valid = true;
    r.stroke.pen.width = 1;
    out->push_back(r);
    return fv::Status::Ok();
  }
  const fv::VectorSymbol* Symbol(const std::string&) override {
    return nullptr;
  }
  uint64_t style_epoch() const override { return epoch; }
};

fv::VectorFeature Line(const std::string& key, std::vector<GeoPoint> pts,
                       int32_t id = 0) {
  fv::VectorFeature f;
  f.type = fv::VectorGeometryType::kLine;
  f.style_key = key;
  f.layer = "tiny";
  f.parts.push_back(std::move(pts));
  f.ref.layer = 0;
  f.ref.feature = id;
  return f;
}

fv::SceneBuildParams MakeParams(const GeoRect& area, double scale = 50000.0) {
  fv::SceneBuildParams p;
  p.area = area;
  p.ctx.scale_denominator = scale;
  p.ctx.device_dpi = 96.0;
  p.ctx.symbol_scale = 1.0;
  return p;
}

const GeoRect kArea{{-10.0, -10.0}, {10.0, 10.0}};

// --- build -----------------------------------------------------------------

TEST(VectorScene, BuildFlattensGeometryAndKeepsPartOffsets) {
  auto src = std::make_shared<TinySource>();
  fv::VectorFeature f = Line("0", {{0, 0}, {1, 1}, {2, 2}});
  f.parts.push_back({GeoPoint{3, 3}, GeoPoint{4, 4}});  // second run
  src->features.push_back(f);
  TinyStyle style;

  VectorScene scene;
  ASSERT_TRUE(scene.Build(src.get(), &style, MakeParams(kArea)).ok());

  ASSERT_EQ(scene.items().size(), 1u);
  const fv::SceneItem& it = scene.items()[0];
  EXPECT_EQ(it.part_count, 2u);
  EXPECT_EQ(scene.vertices_in(), 5u);
  EXPECT_EQ(scene.vertices_kept(), 5u);

  // part 0 = 3 vertices, part 1 = 2, contiguous in one array.
  const auto& pf = scene.part_first();
  ASSERT_EQ(pf.size(), 3u);
  EXPECT_EQ(pf[it.first_part + 1] - pf[it.first_part], 3u);
  EXPECT_EQ(pf[it.first_part + 2] - pf[it.first_part + 1], 2u);
  EXPECT_DOUBLE_EQ(scene.points()[pf[it.first_part + 1]].lat, 3.0);
}

TEST(VectorScene, ItemsComeOutInDrawOrderAcrossFeatures) {
  auto src = std::make_shared<TinySource>();
  src->features.push_back(Line("7", {{0, 0}, {1, 1}}, 70));
  src->features.push_back(Line("1", {{0, 0}, {1, 1}}, 10));
  src->features.push_back(Line("7", {{0, 0}, {1, 1}}, 71));
  TinyStyle style;

  VectorScene scene;
  ASSERT_TRUE(scene.Build(src.get(), &style, MakeParams(kArea)).ok());

  ASSERT_EQ(scene.items().size(), 3u);
  EXPECT_EQ(scene.items()[0].priority, 1);
  EXPECT_EQ(scene.items()[1].priority, 7);
  EXPECT_EQ(scene.items()[2].priority, 7);
  // Stable within a priority: query order, not reversed and not sorted by ref.
  EXPECT_EQ(scene.items()[1].ref.feature, 70);
  EXPECT_EQ(scene.items()[2].ref.feature, 71);
  // Each item indexes its own style.
  EXPECT_EQ(scene.styles()[scene.items()[0].style].priority, 1);
}

TEST(VectorScene, AFeatureWithNoDrawableGeometryIsDropped) {
  auto src = std::make_shared<TinySource>();
  fv::VectorFeature empty = Line("0", {});
  empty.parts.clear();
  empty.parts.push_back({});  // one part, no vertices
  src->features.push_back(empty);
  src->features.push_back(Line("0", {{0, 0}, {1, 1}}));
  TinyStyle style;

  VectorScene scene;
  ASSERT_TRUE(scene.Build(src.get(), &style, MakeParams(kArea)).ok());
  EXPECT_EQ(scene.features(), 2u) << "the source still returned both";
  EXPECT_EQ(scene.items().size(), 1u) << "but only one can be drawn";
}

TEST(VectorScene, AFailedBuildLeavesNothingUsable) {
  auto src = std::make_shared<TinySource>();
  src->features.push_back(Line("0", {{0, 0}, {1, 1}}));
  TinyStyle style;

  VectorScene scene;
  ASSERT_TRUE(scene.Build(src.get(), &style, MakeParams(kArea)).ok());
  ASSERT_TRUE(scene.built());

  src->fail = true;
  EXPECT_FALSE(scene.Build(src.get(), &style, MakeParams(kArea)).ok());
  EXPECT_FALSE(scene.built());
  EXPECT_TRUE(scene.empty());
  EXPECT_FALSE(scene.CanServe(kArea, MakeParams(kArea).ctx, style.epoch))
      << "a half-built scene must never be served";
}

TEST(VectorScene, SimplificationRunsAtBuildTimeAndOnlyWhenAsked) {
  auto src = std::make_shared<TinySource>();
  std::vector<GeoPoint> zigzag;
  for (int i = 0; i < 101; ++i)
    zigzag.push_back(GeoPoint{(i % 2) * 0.0001, i * 0.01});
  src->features.push_back(Line("0", zigzag));
  TinyStyle style;

  VectorScene exact;
  ASSERT_TRUE(exact.Build(src.get(), &style, MakeParams(kArea)).ok());
  EXPECT_EQ(exact.vertices_kept(), 101u) << "exact is the default";

  fv::SceneBuildParams p = MakeParams(kArea);
  p.simplify_px = 1.0;
  p.dpp_x = 0.01;  // 1 px = 0.01 deg, so the 0.0001 zigzag is sub-pixel
  p.dpp_y = 0.01;
  VectorScene thin;
  ASSERT_TRUE(thin.Build(src.get(), &style, p).ok());
  EXPECT_LT(thin.vertices_kept(), 20u);
  EXPECT_EQ(thin.vertices_in(), 101u) << "the input count is still reported";
}

// --- the reuse predicate ---------------------------------------------------

class SceneReuse : public ::testing::Test {
 protected:
  void SetUp() override {
    src = std::make_shared<TinySource>();
    src->features.push_back(Line("0", {{0, 0}, {1, 1}}));
    ASSERT_TRUE(scene.Build(src.get(), &style, MakeParams(kArea)).ok());
  }
  std::shared_ptr<TinySource> src;
  TinyStyle style;
  VectorScene scene;
  StyleContext Ctx() { return MakeParams(kArea).ctx; }
};

TEST_F(SceneReuse, AViewInsideTheBuiltAreaIsServed) {
  EXPECT_TRUE(scene.CanServe(GeoRect{{-1, -1}, {1, 1}}, Ctx(), style.epoch));
  EXPECT_TRUE(scene.CanServe(kArea, Ctx(), style.epoch)) << "exactly the area";
}

TEST_F(SceneReuse, AViewThatLeftTheAreaIsNot) {
  EXPECT_FALSE(scene.CanServe(GeoRect{{-1, 9}, {1, 11}}, Ctx(), style.epoch));
  EXPECT_FALSE(scene.CanServe(GeoRect{{-11, -1}, {1, 1}}, Ctx(), style.epoch));
}

TEST_F(SceneReuse, EveryPieceOfTheStyleContextInvalidates) {
  // All three feed IStyleEngine::Style, so all three are baked in.
  StyleContext c = Ctx();
  c.scale_denominator *= 2.0;
  EXPECT_FALSE(scene.CanServe(kArea, c, style.epoch)) << "zoom rebuilds";

  c = Ctx();
  c.device_dpi = 192.0;
  EXPECT_FALSE(scene.CanServe(kArea, c, style.epoch));

  c = Ctx();
  c.symbol_scale = 2.0;
  EXPECT_FALSE(scene.CanServe(kArea, c, style.epoch));
}

TEST_F(SceneReuse, AMovedStyleEpochInvalidates) {
  EXPECT_TRUE(scene.CanServe(kArea, Ctx(), style.epoch));
  EXPECT_FALSE(scene.CanServe(kArea, Ctx(), style.epoch + 1));
}

TEST_F(SceneReuse, AnAntimeridianViewIsNeverServed) {
  // Deliberate: containment in wrapped longitude is a trap, and rebuilding is
  // correct. Pinned so a future "optimization" has to argue with a test.
  EXPECT_FALSE(scene.CanServe(GeoRect{{-1, 170.0}, {1, -170.0}}, Ctx(),
                              style.epoch));
}

// --- the renderer's use of it ----------------------------------------------

fv::MapProjection Proj(int w, int h, double lat, double lon, double dpp) {
  fv::MapProjection p;
  p.SetSurfaceSize(w, h);
  p.SetCenter(GeoPoint{lat, lon});
  p.SetResolution(dpp, dpp);
  return p;
}

uint64_t Hash(const fv::PixelBuffer& b) {
  uint64_t h = 1469598103934665603ull;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int x = 0; x < b.Width() * 4; ++x) {
      h ^= row[x];
      h *= 1099511628211ull;
    }
  }
  return h;
}

std::shared_ptr<TinySource> ManyLines() {
  auto src = std::make_shared<TinySource>();
  for (int i = 0; i < 20; ++i)
    src->features.push_back(
        Line(std::to_string(i % 3), {{-8.0 + i * 0.8, -8.0}, {8.0 - i * 0.8, 8.0}}, i));
  return src;
}

TEST(VectorRendererScene, APanInsideTheMarginSkipsQueryAndStyle) {
  auto src = ManyLines();
  auto style = std::make_shared<TinyStyle>();
  fv::CpuCanvas canvas(64, 64);

  fv::VectorRenderer r(src, style);
  r.SetSceneMargin(0.5);

  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 0.5), &canvas).ok());
  EXPECT_FALSE(r.scene_reused());
  EXPECT_EQ(src->queries, 1u);
  const size_t styled_after_first = style->styled;

  // A one-pixel pan: well inside a 50% margin.
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.5, 0.5, 0.5), &canvas).ok());
  EXPECT_TRUE(r.scene_reused());
  EXPECT_EQ(src->queries, 1u) << "the source was not asked again";
  EXPECT_EQ(style->styled, styled_after_first);
  EXPECT_EQ(r.query_ms(), 0.0);
  EXPECT_EQ(r.style_ms(), 0.0);
  EXPECT_GT(r.features_queried(), 0u) << "stats still describe the scene";
}

TEST(VectorRendererScene, TheDefaultMarginIsZeroSoAnyPanRebuilds) {
  auto src = ManyLines();
  auto style = std::make_shared<TinyStyle>();
  fv::CpuCanvas canvas(64, 64);

  fv::VectorRenderer r(src, style);
  EXPECT_EQ(r.scene_margin(), 0.0);
  EXPECT_EQ(r.simplify_pixels(), 0.0);

  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 0.5), &canvas).ok());
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.5, 0.0, 0.5), &canvas).ok());
  EXPECT_FALSE(r.scene_reused());
  EXPECT_EQ(src->queries, 2u);
  // The same viewport twice, though, is a hit even at margin 0.
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.5, 0.0, 0.5), &canvas).ok());
  EXPECT_TRUE(r.scene_reused());
}

TEST(VectorRendererScene, AReusedSceneDrawsTheSamePixelsAsARebuiltOne) {
  // The point of the whole session: retaining must not change the picture.
  auto src = ManyLines();
  auto style = std::make_shared<TinyStyle>();

  fv::CpuCanvas fresh(64, 64), reused(64, 64);
  fresh.Clear(fv::FvColor{0, 0, 0, 255});
  reused.Clear(fv::FvColor{0, 0, 0, 255});

  fv::VectorRenderer a(src, style);
  ASSERT_TRUE(a.Render(Proj(64, 64, 0.5, 0.5, 0.5), &fresh).ok());
  ASSERT_FALSE(a.scene_reused());

  fv::VectorRenderer b(src, style);
  b.SetSceneMargin(0.5);
  fv::CpuCanvas warmup(64, 64);
  ASSERT_TRUE(b.Render(Proj(64, 64, 0.0, 0.0, 0.5), &warmup).ok());
  ASSERT_TRUE(b.Render(Proj(64, 64, 0.5, 0.5, 0.5), &reused).ok());
  ASSERT_TRUE(b.scene_reused());

  EXPECT_EQ(Hash(fresh.Buffer()), Hash(reused.Buffer()));
}

TEST(VectorRendererScene, AStyleEngineThatMovedForcesARebuild) {
  auto src = ManyLines();
  auto style = std::make_shared<TinyStyle>();
  fv::CpuCanvas canvas(64, 64);

  fv::VectorRenderer r(src, style);
  r.SetSceneMargin(0.5);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 0.5), &canvas).ok());
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 0.5), &canvas).ok());
  ASSERT_TRUE(r.scene_reused());

  ++style->epoch;  // a rule, a viewing group, a mariner setting
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 0.5), &canvas).ok());
  EXPECT_FALSE(r.scene_reused());
  EXPECT_EQ(src->queries, 2u);
}

TEST(VectorRendererScene, InvalidateSceneForcesARebuild) {
  auto src = ManyLines();
  auto style = std::make_shared<TinyStyle>();
  fv::CpuCanvas canvas(64, 64);

  fv::VectorRenderer r(src, style);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 0.5), &canvas).ok());
  r.InvalidateScene();
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 0.5), &canvas).ok());
  EXPECT_FALSE(r.scene_reused());
  EXPECT_EQ(src->queries, 2u);
}

TEST(VectorRendererScene, SimplificationIsRenderOnlyAndKeepsEveryFeature) {
  // Vertices go, features do not: identify walks back to the source through
  // the FeatureRef, so a thinned scene must still name every feature it drew.
  auto src = std::make_shared<TinySource>();
  std::vector<GeoPoint> wiggle;
  for (int i = 0; i < 200; ++i)
    wiggle.push_back(GeoPoint{-5.0 + i * 0.05 + (i % 2) * 0.0001, -5.0 + i * 0.05});
  src->features.push_back(Line("0", wiggle, 42));
  auto style = std::make_shared<TinyStyle>();

  fv::CpuCanvas canvas(64, 64);
  fv::VectorRenderer r(src, style);
  r.SetSimplifyPixels(1.0);
  ASSERT_TRUE(r.Render(Proj(64, 64, 0.0, 0.0, 0.5), &canvas).ok());

  EXPECT_LT(r.scene().vertices_kept(), r.scene().vertices_in());
  ASSERT_EQ(r.scene().items().size(), 1u);
  EXPECT_EQ(r.scene().items()[0].ref.feature, 42);
  EXPECT_GT(r.pick_index().shape_count(), 0u) << "still tappable";
}

// --- the shared engine's epoch ---------------------------------------------
//
// LookupTableStyleEngine is what both real products inherit style_epoch()
// from, so the composition is pinned here rather than in either product.

class EpochEngine : public fv::LookupTableStyleEngine {
 public:
  EpochEngine() { set_open(true); }
  void Bump() { BumpStyleEpoch(); }

 protected:
  fv::Status StyleFeature(const fv::VectorFeature&, const StyleContext&,
                          const fv::StylePass&,
                          std::vector<fv::StyleResult>*) override {
    return fv::Status::Ok();
  }
};

TEST(LookupEngineEpoch, EveryConfigurationChangeMovesIt) {
  EpochEngine e;
  const uint64_t start = e.style_epoch();

  e.SetDrawLabels(true);
  const uint64_t after_labels = e.style_epoch();
  EXPECT_NE(after_labels, start);

  fv::Rule rule;
  rule.action = fv::RuleAction::kHide;
  rule.match.style_key = "X";
  e.rules().Add(rule);
  const uint64_t after_rule = e.style_epoch();
  EXPECT_NE(after_rule, after_labels);

  e.viewing_groups().Set(7, false);
  const uint64_t after_group = e.style_epoch();
  EXPECT_NE(after_group, after_rule);

  e.Bump();  // a product loader's own change (mariner setting, table reload)
  EXPECT_NE(e.style_epoch(), after_group);
}

TEST(LookupEngineEpoch, SettingTheSameValueDoesNotChurnLabels) {
  // Over-bumping only costs a rebuild, but the label switch is toggled by UI
  // and a no-op set should not throw away a scene.
  EpochEngine e;
  e.SetDrawLabels(false);
  const uint64_t before = e.style_epoch();
  e.SetDrawLabels(false);
  EXPECT_EQ(e.style_epoch(), before);
}

TEST(LookupEngineEpoch, ADefaultEngineReportsAConstantEpoch) {
  // IStyleEngine's default is 0 for an engine that cannot change — which is
  // what makes a scene over a fixed synthetic engine reusable at all.
  TinyStyle t;
  t.epoch = 5;
  EXPECT_EQ(t.style_epoch(), 5u);
}

}  // namespace
