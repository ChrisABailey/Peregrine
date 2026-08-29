// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/overlay/vector_map_overlay.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "fvkit/vector/feature_rows.h"

namespace fv {
namespace {

GeoPoint CenterOf(const GeoRect& r) {
  return GeoPoint{0.5 * (r.ll.lat + r.ur.lat), 0.5 * (r.ll.lon + r.ur.lon)};
}

// The feature's box, DERIVED rather than trusted. `VectorFeature::bounds` is
// part of the seam's contract and every source in the tree fills it, but this
// walk is over the vertices anyway (the line anchor below needs them) and a
// box that disagreed with its own geometry would put a result somewhere the
// map does not draw it.
GeoRect BoundsOf(const VectorFeature& f) {
  bool any = false;
  GeoRect r{};
  for (const auto& part : f.parts) {
    for (const GeoPoint& p : part) {
      if (!any) {
        r.ll = r.ur = p;
        any = true;
        continue;
      }
      r.ll.lat = std::min(r.ll.lat, p.lat);
      r.ll.lon = std::min(r.ll.lon, p.lon);
      r.ur.lat = std::max(r.ur.lat, p.lat);
      r.ur.lon = std::max(r.ur.lon, p.lon);
    }
  }
  return any ? r : f.bounds;
}

// Where a label would be anchored, which is what a result's `position` means
// and what the session's distance ordering and radius cut measure to.
//
//   point  the point itself;
//   line   the halfway point ALONG the longest run, not the centre of its box
//          — a road bent round a marsh has a box centre out in the water;
//   area   the centre of its box, which is the honest cheap answer. A proper
//          pole of inaccessibility belongs with the label-placement work the
//          ledger already carries, not here.
GeoPoint AnchorOf(const VectorFeature& f, const GeoRect& bounds) {
  if (f.parts.empty() || f.parts[0].empty()) return CenterOf(bounds);
  if (f.type == VectorGeometryType::kPoint) return f.parts[0][0];
  if (f.type == VectorGeometryType::kArea) return CenterOf(bounds);

  const std::vector<GeoPoint>* longest = nullptr;
  double longest_len = -1.0;
  for (const auto& part : f.parts) {
    if (part.size() < 2) continue;
    double len = 0.0;
    for (size_t i = 1; i < part.size(); ++i) {
      len += app::SearchDistanceMeters(part[i - 1], part[i]);
    }
    if (len > longest_len) {
      longest_len = len;
      longest = &part;
    }
  }
  if (longest == nullptr) return f.parts[0][0];

  double half = 0.5 * longest_len;
  for (size_t i = 1; i < longest->size(); ++i) {
    const double seg = app::SearchDistanceMeters((*longest)[i - 1], (*longest)[i]);
    if (seg <= 0.0) continue;
    if (half > seg) {
      half -= seg;
      continue;
    }
    const double t = half / seg;
    const GeoPoint& a = (*longest)[i - 1];
    const GeoPoint& b = (*longest)[i];
    return GeoPoint{a.lat + t * (b.lat - a.lat), a.lon + t * (b.lon - a.lon)};
  }
  return longest->back();
}

std::string DetailFor(const std::string& layer,
                      const std::string& style_key) {
  // WHAT KIND OF THING THIS IS, in the source's own words. The layer is the
  // grouping every vector product actually has ("transportation_name",
  // "hydline", "LNDARE"), and the style key is its sub-type when the product
  // carries one ("residential", "BH140"). No prettier English is invented
  // here: this class wraps any IVectorSource, a table of display names for one
  // product's layers would be a second copy of what port/families already
  // groups, and `detail` is documented as the string a caller greps.
  if (style_key.empty() || style_key == layer) return layer;
  return layer + " \xc2\xb7 " + style_key;  // a middle dot
}

}  // namespace

bool VectorMapOverlay::RefKey::operator<(const RefKey& o) const {
  if (source != o.source) return source < o.source;
  if (layer != o.layer) return layer < o.layer;
  if (tile != o.tile) return tile < o.tile;
  return feature < o.feature;
}

VectorMapOverlay::VectorMapOverlay(std::string name, VectorSourcePtr source)
    : Overlay(std::move(name)),
      source_(std::move(source)),
      label_tags_{"name", "name:latin", "name:en"} {}

VectorMapOverlay::~VectorMapOverlay() = default;

void VectorMapOverlay::SetSource(VectorSourcePtr source) {
  source_ = std::move(source);
  by_ref_.clear();
  by_id_.clear();
  last_features_ = 0;
  last_results_ = 0;
  last_used_index_ = false;
}

void VectorMapOverlay::SetLabelTags(std::vector<std::string> tags) {
  label_tags_ = std::move(tags);
}

void VectorMapOverlay::SetSearchScaleDenominator(double denominator) {
  scale_denominator_ = denominator > 0.0 ? denominator : 0.0;
}

void VectorMapOverlay::SetMaxFeaturesPerSearch(size_t n) {
  max_features_ = n;
}

void VectorMapOverlay::SetMergeGapMeters(double m) { merge_gap_m_ = m; }

uint64_t VectorMapOverlay::FeatureIdFor(const FeatureRef& ref) {
  const RefKey key{ref.source, ref.layer, ref.tile, ref.feature};
  auto it = by_ref_.find(key);
  if (it != by_ref_.end()) return it->second;
  by_id_.push_back(ref);
  const uint64_t id = static_cast<uint64_t>(by_id_.size());  // 1-based
  by_ref_.emplace(key, id);
  return id;
}

bool VectorMapOverlay::FeatureRefFor(uint64_t id, FeatureRef* out) const {
  if (id == 0 || id > by_id_.size()) return false;
  if (out != nullptr) *out = by_id_[static_cast<size_t>(id - 1)];
  return true;
}

Status VectorMapOverlay::DescribeFeature(uint64_t id,
                                         FeatureDescription* out) const {
  FeatureRef ref;
  if (!FeatureRefFor(id, &ref)) {
    return Status::Error(kNotFound, "no such feature id in this overlay");
  }
  if (!source_) return Status::Error(kNotFound, "overlay has no source");
  return source_->Describe(ref, out);
}

void VectorMapOverlay::SetUseNameIndex(bool on) { use_name_index_ = on; }

void VectorMapOverlay::Search(const app::SearchQuery& q,
                              const std::atomic<bool>& cancel,
                              std::vector<app::SearchResult>& out) {
  last_features_ = 0;
  last_results_ = 0;
  last_used_index_ = false;
  if (!source_ || !source_->IsOpen()) return;

  // TIER 2 FIRST, AND ONLY FOR TEXT. An index is what makes "where is X"
  // answerable with no area at all, and it is cheaper than the scan when there
  // IS one; but "what is in this box" is a question about the tiles, and the
  // tiles are the thing that knows. A source with no index says so and the
  // scan runs — that is the whole of "degrades gracefully".
  if (!q.text.empty() && use_name_index_ && source_->HasNameIndex()) {
    if (SearchIndex(q, cancel, out)) return;
  }
  SearchTiles(q, cancel, out);
}

bool VectorMapOverlay::SearchIndex(const app::SearchQuery& q,
                                   const std::atomic<bool>& cancel,
                                   std::vector<app::SearchResult>& out) {
  if (cancel.load()) return true;  // answered with nothing; do not then scan

  VectorNameQuery nq;
  nq.text = q.text;
  nq.area = q.area;
  // OVER-FETCH, MODESTLY. The index narrows by its own tokeniser and the
  // SHARED RULE below is what actually decides, so a handful of rows can fall
  // out between the two; asking for a few more than the cap keeps a full page
  // full without turning a capped query into an uncapped one.
  nq.max_results = q.max_results > 0 ? q.max_results * 2 + 16 : 0;

  std::vector<VectorNameHit> hits;
  const Status s = source_->SearchNames(nq, &hits);
  // kUnsupported (no index after all) or a failed read: the scan is a better
  // answer than no answer, and with no area it will honestly find nothing.
  if (!s.ok()) return false;

  last_used_index_ = true;
  last_features_ = hits.size();

  for (const VectorNameHit& h : hits) {
    if (cancel.load()) return true;
    if (q.max_results > 0 && last_results_ >= q.max_results) break;

    // THE INDEX NARROWS, THE PORT'S RULE DECIDES. An FTS tokeniser splits on
    // punctuation the port's does not, and folds letters the port's leaves
    // alone, so a candidate it hands back is re-tested here — which is what
    // keeps one search box from meaning two different things depending on
    // which pack answered.
    int quality = 0;
    if (!app::SearchTextAccepts(q, h.name, &quality)) continue;
    // The same area rule as the scan, over the box and not the anchor: a road
    // running through the query box is in it. Degenerate boxes (points) make
    // this exactly the containment test.
    if (q.area && !q.area->Intersects(h.bounds)) continue;

    app::SearchResult r;
    r.overlay = this;
    r.feature = FeatureIdFor(h.ref);
    r.match_quality = quality;
    r.position = h.position;
    r.bounds = h.bounds;
    r.title = h.name;
    r.detail = DetailFor(h.layer, h.style_key);
    out.push_back(std::move(r));
    ++last_results_;
  }
  return true;
}

void VectorMapOverlay::SearchTiles(const app::SearchQuery& q,
                                   const std::atomic<bool>& cancel,
                                   std::vector<app::SearchResult>& out) {
  std::vector<FeatureRow> rows;
  ScanRows(q, cancel, &rows);
  for (FeatureRow& row : rows) {
    if (q.max_results > 0 && last_results_ >= q.max_results) break;
    app::SearchResult r;
    r.overlay = this;
    // THE ID A MERGED ROW CARRIES is the first piece the scan met. Identify
    // then goes back to a real feature rather than to a synthetic union that
    // no source could describe, and because the mint is kept, re-running the
    // same query over the same pack gives the row the same number again.
    r.feature = FeatureIdFor(row.ref);
    r.match_quality = row.quality;
    r.position = row.anchor;
    r.bounds = row.bounds;
    r.title = std::move(row.title);
    r.detail = DetailFor(row.layer, row.style_key);
    out.push_back(std::move(r));
    ++last_results_;
  }
}

void VectorMapOverlay::ScanRows(const app::SearchQuery& q,
                                const std::atomic<bool>& cancel,
                                std::vector<FeatureRow>* out) {
  if (out == nullptr) return;
  last_features_ = 0;
  last_results_ = 0;
  last_used_index_ = false;
  if (!source_ || !source_->IsOpen()) return;
  // TIER 1 IS AREA-CONSTRAINED, and a query without one is answered with
  // nothing rather than with the pack. See the header: the source's own name
  // index is the global path, and this is what a pack without one does.
  if (!q.area) return;

  VectorQuery vq;
  vq.area = *q.area;
  vq.scale_denominator = scale_denominator_;
  vq.max_features = max_features_;

  if (cancel.load()) return;
  std::vector<VectorFeature> features;
  const Status s = source_->Query(vq, &features);
  // A failed read is not an empty answer, but nothing at this seam can report
  // it: SearchProvider::Search returns void because a search over four
  // overlays cannot fail as a whole. The rows are simply absent, which is what
  // a caller sees for a pack that has nothing there either.
  if (!s.ok()) return;
  last_features_ = features.size();
  // ONE SOURCE QUERY IS THE UNIT OF WORK, and that is the limit of how
  // cancellable this provider is: IVectorSource::Query is a single call with
  // no cancel of its own, so a flag raised while it is decoding tiles is
  // honoured when it returns. What keeps that unit bounded is the source's own
  // tile budget (see SetSearchScaleDenominator) rather than anything here.
  if (cancel.load()) return;

  std::vector<FeatureRow> rows;
  for (const VectorFeature& f : features) {
    if (cancel.load()) return;

    // THE LABEL, and the whole of this provider's product knowledge.
    const std::string* label = nullptr;
    for (const std::string& tag : label_tags_) {
      const std::string* v = f.Attribute(tag);
      if (v != nullptr && !v->empty()) {
        label = v;
        break;
      }
    }
    // Unnamed geometry is never a result, spatial-only queries included --
    // see the header. Most of a vector tile is unnamed.
    if (label == nullptr) continue;

    int quality = 0;
    if (!app::SearchTextAccepts(q, *label, &quality)) continue;

    FeatureRow row;
    row.bounds = BoundsOf(f);
    // THE AREA TEST FOR EXTENDED GEOMETRY IS THE BOX, not the anchor -- a road
    // running straight through the search area whose midpoint lies outside it
    // is in the area by any reading a user would recognise. RouteOverlay tests
    // a route the same way and a waypoint as the point it is.
    if (f.type == VectorGeometryType::kPoint) {
      if (!q.area->Contains(row.bounds.ll)) continue;
    } else if (!q.area->Intersects(row.bounds)) {
      continue;
    }
    row.title = *label;
    row.layer = f.layer;
    row.style_key = f.style_key;
    row.anchor = AnchorOf(f, row.bounds);
    row.quality = quality;
    row.ref = f.ref;
    rows.push_back(std::move(row));
  }

  // ONE ROAD, ONE ROW — fvkit/vector/feature_rows.h, which is also what the
  // index builder runs, so a pack with an index and a pack without one cannot
  // disagree about what counts as one named thing.
  MergeFeatureRows(&rows, merge_gap_m_);
  *out = std::move(rows);
}

}  // namespace fv
