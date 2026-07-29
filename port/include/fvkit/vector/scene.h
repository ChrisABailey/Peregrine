// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::VectorScene — the retained, already-styled snapshot the plan calls a
// `TileDisplayList` (vpf-geosym plan §5.4, session R3a).
//
// The measured problem it exists for. One 900x650 DNC harbour render, the
// real Nantucket Sound library through GeoSym, splits like this:
//
//     query  38 ms   pull 5,037 features out of the VPF tables
//     style  17 ms   ask GeoSym what each one looks like
//     draw   78 ms   project, clip, rasterize
//     ----------
//     total 133 ms
//
// The first two thirds of that answer a question that did not change when the
// map panned by ten pixels: at a FIXED scale, with a FIXED style engine, the
// same features get the same styles. A VectorScene holds that answer.
//
// SHAPE. Geometry is flattened columnar — one coordinate array for the whole
// scene plus part offsets — instead of the `vector<vector<GeoPoint>>` a
// VectorFeature carries, which costs an allocation per part per feature. The
// items are pre-sorted into draw order, so a redraw is a linear walk.
//
// COORDINATES STAY GEOGRAPHIC, deliberately. The equal-arc projection is
// linear in both axes, so re-projecting is two multiplies per vertex; caching
// pixels instead would tie the scene to one viewport origin and buy almost
// nothing. Simplification is the part that is genuinely resolution-dependent,
// and it is applied at build time against the scene's own degrees-per-pixel.
//
// WHAT INVALIDATES A SCENE. Everything the styles were baked with:
//   * the geographic area — a viewport that has moved outside it;
//   * the StyleContext (scale, DPI, symbol scale) — all three feed Style();
//   * the style engine's epoch — a rule, a viewing group, a mariner setting.
// Scale is compared EXACTLY rather than by band. Both real engines style
// scale-dependently (S-52 SCAMIN thinning happens in the SOURCE, GeoSym's
// rules band on the denominator), so reusing a scene across a zoom would draw
// something the product did not ask for. Pan is the interaction this is for;
// zoom rebuilds, and says so.

#ifndef FVKIT_VECTOR_SCENE_H_
#define FVKIT_VECTOR_SCENE_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fvkit/geo.h"
#include "fvkit/vector/style.h"
#include "fvkit/vector/vector.h"

namespace fv {

// One styled draw pass over one feature, in draw order.
struct SceneItem {
  FeatureRef ref;
  VectorGeometryType type = VectorGeometryType::kLine;
  int priority = 0;
  uint32_t first_part = 0;  // into VectorScene::part_first()
  uint32_t part_count = 0;
  uint32_t style = 0;  // into VectorScene::styles()
};

// What a build is asked for. `ctx` is not a hint — it is exactly the context
// the styles are baked with, and the scene refuses to serve any other.
struct SceneBuildParams {
  GeoRect area;
  StyleContext ctx;
  size_t max_features = 0;

  // Douglas-Peucker tolerance in PIXELS. 0 = exact, and exact is the default:
  // a renderer that silently moved vertices would move every pinned golden in
  // the tree. dpp_x/dpp_y convert it to degrees and must be set with it.
  double simplify_px = 0.0;
  double dpp_x = 0.0;
  double dpp_y = 0.0;
};

class VectorScene {
 public:
  // Queries, styles, sorts and flattens. On failure the scene is left empty
  // (and unusable), never half-built.
  Status Build(IVectorSource* source, IStyleEngine* style,
               const SceneBuildParams& params);

  void Clear();
  bool empty() const { return items_.empty(); }
  bool built() const { return built_; }

  // True when this scene can be drawn for `view` at `ctx` without rebuilding.
  // A scene whose area crosses the antimeridian — or a view that does — is
  // never reused: containment in wrapped longitude is a trap worth more than
  // it saves, and the rebuild is correct.
  bool CanServe(const GeoRect& view, const StyleContext& ctx,
                uint64_t style_epoch) const;

  const std::vector<SceneItem>& items() const { return items_; }
  const std::vector<StyleResult>& styles() const { return styles_; }

  // Vertices of part p: points()[part_first()[p] .. part_first()[p+1]).
  const std::vector<GeoPoint>& points() const { return points_; }
  const std::vector<uint32_t>& part_first() const { return part_first_; }

  const GeoRect& area() const { return area_; }
  const StyleContext& ctx() const { return ctx_; }
  uint64_t style_epoch() const { return style_epoch_; }

  // Build accounting, for tests and the demo's status line.
  size_t features() const { return features_; }
  size_t vertices_in() const { return vertices_in_; }
  size_t vertices_kept() const { return points_.size(); }
  double query_ms() const { return query_ms_; }
  double style_ms() const { return style_ms_; }

 private:
  std::vector<SceneItem> items_;
  std::vector<StyleResult> styles_;
  std::vector<GeoPoint> points_;
  std::vector<uint32_t> part_first_;

  GeoRect area_;
  StyleContext ctx_;
  uint64_t style_epoch_ = 0;
  bool built_ = false;

  size_t features_ = 0;
  size_t vertices_in_ = 0;
  double query_ms_ = 0.0;
  double style_ms_ = 0.0;
};

// Douglas-Peucker over a run of geographic points, with the tolerance given
// per axis in degrees (the caller derives them from pixels x degrees-per-
// pixel, so the tolerance means the same thing north and south). Endpoints are
// always kept. Exposed because it is pure geometry and worth testing directly.
//
// `closed` keeps a ring drawable: a ring that simplifies below 3 distinct
// vertices comes back unchanged rather than as a degenerate sliver.
std::vector<GeoPoint> SimplifyPath(const std::vector<GeoPoint>& pts,
                                   double tol_lat, double tol_lon, bool closed);

}  // namespace fv

#endif  // FVKIT_VECTOR_SCENE_H_
