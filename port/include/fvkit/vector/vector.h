// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// FvKit vector seam (vpf-geosym plan phase V5).
//
// The plan's key architectural move: DON'T build a VPF-shaped renderer. Split
// vector charting into three replaceable parts and let every vector product
// ride the same middle and right-hand side:
//
//   IVectorSource   product-specific: features with WGS-84 geometry,
//                   attributes, and a style key
//        │
//   IStyleEngine    product-specific: (feature, scale) -> draw ops
//        │
//   VectorRenderer  SHARED: projection, clipping, batching -> ICanvas
//
// VPF/DNC + GeoSym is the first source/style pair. OSM (MVT + a MapLibre
// style subset) and ENC (S-57 + S-52) are siblings, not forks — that is the
// whole reason this header is generic and lives in fvkit rather than next to
// the VPF reader.
//
// This file defines the LEFT-hand side only. IStyleEngine and VectorRenderer
// arrive with V5b.

#ifndef FVKIT_VECTOR_VECTOR_H_
#define FVKIT_VECTOR_VECTOR_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fvkit/geo.h"

namespace fv {

enum class VectorGeometryType {
  kPoint = 0,
  kLine,
  kArea,
};

// ---------------------------------------------------------------------------
// Identify (plan §5.3)
// ---------------------------------------------------------------------------

// A small, copyable handle naming ONE feature in ONE source — what the render
// path and the pick index carry instead of a bag of attributes. Four 32-bit
// ids, so it costs the same as a pointer pair and can be stored per drawn
// primitive without thought.
//
// `source` is assigned by whoever owns more than one source (a future overlay
// or multi-library manager); a source never sets it, and a single-source
// renderer leaves it 0.
struct FeatureRef {
  int32_t source = 0;
  int32_t layer = -1;   // index into IVectorSource::Layers(); <0 = invalid
  int32_t tile = 0;     // product tile id (VPF tile_id, MVT tile index, …)
  int32_t feature = 0;  // row id within (layer, tile)

  bool valid() const { return layer >= 0; }
  bool operator==(const FeatureRef& o) const {
    return source == o.source && layer == o.layer && tile == o.tile &&
           feature == o.feature;
  }
  bool operator!=(const FeatureRef& o) const { return !(*this == o); }
};

// One attribute as a click popup wants to show it.
//
//   code    the product's column name, verbatim ("hdp", "SCAMIN", "maxspeed")
//   name    what the product's own schema calls it ("Hydrographic Depth")
//   raw     the stored value as text ("3", "BE010", "")
//   display raw decoded to human-readable text ("Approximate", "Depth Curve").
//           Falls back to `raw` when the product has no dictionary entry —
//           never empty just because a decode failed, but legitimately empty
//           when the value itself is null (VPF distinguishes the two).
struct FeatureAttribute {
  std::string code;
  std::string name;
  std::string raw;
  std::string display;
};

// The answer to "what did I just click on?", fetched on demand — never carried
// on the render path. Products fill what they have; empty strings are fine.
struct FeatureDescription {
  FeatureRef ref;
  std::string title;       // best one-line name ("Depth Curve", "Buoy")
  std::string class_name;  // the feature class ("Hydrography Line Features")
  std::string layer_name;  // the layer/table it came from ("hydline")
  std::vector<FeatureAttribute> attributes;
  std::string source_note;  // provenance ("dnc17 h1707300 hyd tile 7")
};

// One drawable feature in WGS-84 degrees.
//
// GEOMETRY REPRESENTATION: `parts` is a list of coordinate runs.
//   kPoint — one part, one point per part (a multi-point has several parts).
//   kLine  — one part per connected run; DNC edges are one part each.
//   kArea  — part[0] is the outer ring, parts[1..] are holes (V5b; areas need
//            the face/ring/edge topology that is still Windows-only).
// Rings are NOT implicitly closed: if the first and last points differ, a
// renderer that needs closure must close it.
struct VectorFeature {
  VectorGeometryType type = VectorGeometryType::kLine;

  // The key a style engine dispatches on. Deliberately a string so the seam
  // stays product-neutral: VPF/DNC puts the 5-character FACC code here
  // ("BH140" = river/stream), OSM would put a tag pair, ENC an S-57 acronym.
  std::string style_key;

  // Which layer/table the feature came from ("hydline"). GeoSym rules select
  // on FACC *and* on the feature class, so both travel with the feature.
  std::string layer;

  std::vector<std::vector<GeoPoint>> parts;
  GeoRect bounds;

  // Product-specific attributes, verbatim, for style rules and identify/tap.
  // Kept as a small sorted vector, not a map: a handful of entries per
  // feature and hundreds of thousands of features.
  std::vector<std::pair<std::string, std::string>> attributes;

  // Provenance, so a hit-test can go back to the source row. This is what the
  // pick index stores and what Describe() takes; it is the ONLY part of a
  // feature the render path needs to keep once drawing is done.
  FeatureRef ref;

  const std::string* Attribute(const std::string& key) const {
    for (const auto& kv : attributes)
      if (kv.first == key) return &kv.second;
    return nullptr;
  }
};

// What a caller asks for. Scale is carried because vector products are
// scale-thinned (DNC's library = scale band; OSM's zoom; S-52's SCAMIN).
struct VectorQuery {
  GeoRect area = GeoRect::World();
  // Map scale denominator (e.g. 50000.0 for 1:50k). 0 = "no scale filter".
  double scale_denominator = 0.0;
  // 0 = unlimited. A guard for interactive callers over dense coverages.
  size_t max_features = 0;
};

// ---------------------------------------------------------------------------
// The name index (search-plan-COMPLETE.md, S3)
// ---------------------------------------------------------------------------

// What a global text lookup asks of a source that has an index. There is no
// scale and no zoom here on purpose: an index is not a level of the pyramid,
// it is what the whole pyramid had to say about names, collapsed once.
struct VectorNameQuery {
  // Token-prefix text, as fvkit/app/search.h defines matching. EMPTY means
  // every indexed row, which is what an export or an "info" listing wants.
  std::string text;
  // Optional cut. A source may answer with a superset (a box that crosses the
  // antimeridian is commonly handed straight back); the caller cuts exactly.
  std::optional<GeoRect> area;
  // 0 = no cap. A capped answer keeps the MOST PROMINENT rows (see rank
  // below), never an arbitrary page of them.
  size_t max_results = 0;
};

// One row of that index: a named thing, already merged out of the pieces the
// tiles hold it in (fvkit/vector/feature_rows.h is the merge, and the index
// builder runs the same one the live scan does).
struct VectorNameHit {
  std::string name;
  std::string layer;
  std::string style_key;
  GeoPoint position{};  // the label anchor
  GeoRect bounds{};     // what "go there" frames; degenerate for a point
  // The source's own prominence, smaller = more prominent. A pyramid puts the
  // shallowest zoom the name survives to here; 0 when the source has no
  // opinion. See FeatureRow::rank.
  int rank = 0;
  // LIVE for this open source — a FeatureRef is only meaningful inside the
  // process that minted it (OSM's `tile` is a per-open index), so an index
  // stores something durable and the source turns it back into a ref on the
  // way out. Describe() works on it like any other.
  FeatureRef ref{};
};

// A product-specific reader of drawable features.
//
// Implementations are NOT required to be thread-safe; the renderer owns one
// per worker if it ever parallelizes.
class IVectorSource {
 public:
  virtual ~IVectorSource() = default;

  // `path` meaning is implementation-defined (for VPF: a DNC library dir).
  virtual Status Open(const std::string& path) = 0;
  virtual bool IsOpen() const = 0;

  // Full extent of everything this source can return.
  virtual GeoRect Bounds() const = 0;

  // Layers available ("hydline", "hydarea", …). Stable order.
  virtual std::vector<std::string> Layers() const = 0;

  // Append matching features to *out (does NOT clear it).
  virtual Status Query(const VectorQuery& q,
                       std::vector<VectorFeature>* out) = 0;

  // Full, human-readable metadata for one feature, fetched on demand (plan
  // §5.3). This is the identify/tap path: it re-reads the row from the source
  // and decodes coded values through the product's own dictionary (VPF's
  // INT.VDT/CHAR.VDT, S-57's Appendix A, an OSM alias table), which is what
  // makes a popup worth having.
  //
  // Default: kUnsupported, so a source that has nothing to add (or is not
  // there yet) does not have to implement it and a caller can tell the
  // difference between "no such feature" and "this product cannot describe".
  virtual Status Describe(const FeatureRef& ref, FeatureDescription* out) {
    (void)ref;
    (void)out;
    return Status::Error(kUnsupported, "source does not implement Describe");
  }

  // --- the name index (search-plan-COMPLETE.md, S3) ------------------------
  //
  // "Where is X" over the WHOLE source, with no area to narrow it. A tile
  // pyramid cannot answer that by reading tiles — the honest cost of a
  // pack-wide scan is a walk nobody would wait for — so the answer comes from
  // an index built once, at staging time, and stored beside the data (for
  // MBTiles, extra SQLite tables inside the pack itself; port/Osm/tools).
  //
  // A source WITHOUT one returns kUnsupported, which is not a failure: it is
  // what "degrades to area-only search" means in code, and the caller
  // (VectorMapOverlay) then falls back to the tier-1 tile scan.
  //
  // NARROWING IS BEST EFFORT, MATCHING IS THE CALLER'S. An implementation may
  // return more rows than the text strictly matches — an index tokenises the
  // way its own engine does, and FTS5's notion of a word boundary is not
  // fvkit/app/search.h's. The caller runs the shared rule over what comes
  // back, so an index can only ever be a candidate generator: it may not
  // decide that something matches when the port's rule says it does not.
  virtual bool HasNameIndex() const { return false; }
  virtual Status SearchNames(const VectorNameQuery& q,
                             std::vector<VectorNameHit>* out) {
    (void)q;
    (void)out;
    return Status::Error(kUnsupported, "source has no name index");
  }
};

using VectorSourcePtr = std::shared_ptr<IVectorSource>;

}  // namespace fv

#endif  // FVKIT_VECTOR_VECTOR_H_
