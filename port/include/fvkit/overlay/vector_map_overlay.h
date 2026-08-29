// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/overlay/vector_map_overlay.h — a vector MAP source, wearing an
// overlay's clothes so that "where is X" has ONE discovery path
// (port/search-plan-COMPLETE.md, S2).
//
// WHY A MAP BECOMES AN OVERLAY. S1 built the aggregating session over
// `OverlayManager`, and the alternative for map data was a second registry of
// `SearchProvider`s that only search knows about — a parallel stack, with its
// own ordering, its own lifetime rules and nothing else ever riding it.
// Chris's call (2026-08-25) was to wrap instead: an `IVectorSource` held by an
// overlay is discovered by the same walk as a point set, ranked against it by
// the same rules, and named in a result by the same flat `Overlay*` that every
// other capability already carries. It is also the ledger's "draw OSM over
// DTED hillshade" layering feature arriving early — which is why this class is
// named for what it IS rather than for the one capability it starts with.
//
// WHAT IT DOES NOT DO YET, and the omission is deliberate: it does not DRAW.
// `OnDraw` is the base class's, which draws nothing. Owning a `VectorScene`,
// rebuilding it when the camera moves and choosing which band it renders in is
// the layering feature's work, and none of it touches the search seam — a
// shell today can hold a not-visible VectorMapOverlay purely as the search
// provider while it keeps drawing the base map its own way (Pippin's two-layer
// economy, P18, is untouched by this file existing).
//
// TWO TIERS, AND THE SOURCE DECIDES WHICH ONE ANSWERS.
//
// TIER 1 is the area-constrained scan: the query's box goes to the source, the
// source picks a zoom that fits its own tile budget, and the features that
// come back are matched on their label tag. No index, nothing to build, works
// on any `IVectorSource` the day it exists.
//
// TIER 2 (S3) is the source's own NAME INDEX — `IVectorSource::SearchNames`,
// which for a tile pyramid is a table built once at staging time and stored
// inside the pack (port/Osm/tools/fvnames.cpp). It is asked when the query
// carries TEXT and the source says it has one, because that is the question
// tier 1 cannot answer well: an index has read the whole pyramid already, so
// "Ruddy Turnstone" needs no area and costs one SELECT. A spatial-only query
// still goes to tier 1 — "what is here" is a question about the tiles
// themselves, and the tiles are right there.
//
// That has two consequences worth stating plainly:
//
//   * A QUERY WITH NO AREA AND NO INDEX FINDS NOTHING. Not an error, not the
//     whole pack — nothing, because the honest answer to "search everywhere
//     with no index" is a pyramid walk nobody would wait for. The session's
//     `near` + `radius_m` becomes an area before a provider ever sees it, so
//     "within 5 km of me" is already an area query, and a pack with an index
//     answers with no area at all.
//   * THE INDEX BAKED IN ITS LABEL CHOICE. `SetLabelTags` below steers tier 1
//     only: which tag became a row's `name` was decided when the index was
//     built. The two agree by construction when the same tags built it, and
//     the builder writes what it used into the pack so a mismatch is at least
//     discoverable.
//   * THE PYRAMID IS THE RELEVANCE FUNCTION, for free. Ask over a whole state
//     and the tile budget steps the zoom coarser until the range fits — and a
//     coarse level holds only what the cutter kept there, so a state-wide
//     search finds Charleston and not every Charleston Court. Ask over a
//     harbour and you get its POIs. Nominatim buys importance ranking with
//     enormous effort; this is a crude, honest version that costs nothing.
//
// UNNAMED FEATURES ARE NEVER RESULTS, including for a spatial-only query. Most
// of what a vector tile holds has no name at all — landcover, building
// footprints, every unnamed driveway — and a row with an empty title is not
// something any UI can render or any user can choose between. "What is in this
// area" is answered here by the named things in it.

#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/app/search.h"
#include "fvkit/overlay/overlay.h"
#include "fvkit/vector/feature_rows.h"
#include "fvkit/vector/vector.h"

namespace fv {

class VectorMapOverlay : public Overlay, public app::SearchProvider {
 public:
  // `source` may be null; the overlay is then simply a search provider that
  // never finds anything, which is what a shell holding a slot for a pack the
  // user has not chosen yet needs.
  explicit VectorMapOverlay(std::string name = "Vector map",
                            VectorSourcePtr source = nullptr);
  ~VectorMapOverlay() override;

  // Capability accessors (R2 — never dynamic_cast). Search ONLY, for now.
  app::SearchProvider* AsSearch() override { return this; }

  const VectorSourcePtr& source() const { return source_; }
  // Replacing the source drops the minted-id table: an id names a row in the
  // pack that minted it, and handing the same number to a different pack is
  // the one way a stale bookmark could point at the wrong thing silently.
  void SetSource(VectorSourcePtr source);

  // --- what this provider calls a label ------------------------------------

  // The tags consulted, in order, for a feature's title; the first one present
  // and non-empty wins. THIS IS THE ONE PIECE OF PRODUCT KNOWLEDGE the search
  // seam lets a provider keep (search-plan-COMPLETE.md §4), and it is a knob
  // than a constant because this class wraps any `IVectorSource`: OSM spells
  // it `name`, an OpenMapTiles cut made with `name:latin` spells it that, ENC
  // spells it `OBJNAM` and DNC `nam`. The caller sees only `title`.
  //
  // Default {"name", "name:latin", "name:en"} — OpenMapTiles, which is the
  // schema every pyramid this port cuts uses.
  void SetLabelTags(std::vector<std::string> tags);
  const std::vector<std::string>& label_tags() const { return label_tags_; }

  // --- how much of the pyramid a search reads ------------------------------

  // The map scale the tier-1 scan reads at, as a denominator (50000.0 for
  // 1:50k). 0 — the default — means "let the source decide", which for a tile
  // pyramid is its own tile-count budget stepping the zoom coarser until the
  // range fits. That is the arrangement the plan asks for: ONE number owns the
  // budget (the source's), rather than two that can disagree. A shell that
  // wants a search to see exactly what the user is looking at passes the
  // viewport's own scale.
  void SetSearchScaleDenominator(double denominator);
  double search_scale_denominator() const { return scale_denominator_; }

  // A hard cap on features read from the source per search. 0 — the default —
  // is no cap here, again because the source's tile budget is already the
  // bound. Set it when wrapping a source that has no budget of its own.
  void SetMaxFeaturesPerSearch(size_t n);
  size_t max_features_per_search() const { return max_features_; }

  // --- one road, one row ---------------------------------------------------

  // How far apart two pieces of the same named feature may lie and still be
  // reported as one result, in metres. Default 100.
  //
  // THIS IS NOT AN OPTIONAL POLISH, it is what makes tier 1 usable: a road is
  // cut at every tile seam it crosses (and OSM had already split it at every
  // junction where a tag changed), so an un-merged scan answers "Ruddy
  // Turnstone" with a dozen identical rows. Pieces sharing a title, a layer
  // and a style key are joined when their boxes come within this distance;
  // the merged row's bounds is the union, its position the centre of that,
  // and its feature id the first piece the scan met.
  //
  // Negative disables merging entirely — the raw pieces, which is what a
  // caller building its own index wants. 0 merges only pieces that actually
  // touch, which is exactly the tile-seam case and nothing else.
  void SetMergeGapMeters(double m);
  double merge_gap_meters() const { return merge_gap_m_; }

  // --- which tier answers --------------------------------------------------

  // Ask the source's name index for text queries when it has one. On by
  // default; turning it off pins this overlay to the tile scan, which is what
  // a caller comparing the two wants (and what the index BUILDER needs, since
  // it is producing the rows the index will hold).
  void SetUseNameIndex(bool on);
  bool use_name_index() const { return use_name_index_; }

  // --- SearchProvider ------------------------------------------------------

  // See the header note for the contract this honours and the one query it
  // deliberately answers with nothing (no area, no index).
  void Search(const app::SearchQuery& q, const std::atomic<bool>& cancel,
              std::vector<app::SearchResult>& out) override;

  // --- the rows behind the results -----------------------------------------

  // THE TIER-1 SCAN, stopping one step short of a `SearchResult`: the named
  // things in `q.area`, merged, with their layer and style key still as
  // FIELDS rather than joined into the `detail` line a UI reads.
  //
  // This exists for the INDEX BUILDER (port/Osm/tools/fvnames.cpp), and the
  // reason it is the same call rather than a similar one is the whole point of
  // S3: what the index holds must be what the live scan would have said. A
  // builder that re-implemented "which tag is the label", "unnamed geometry is
  // never a row" and the merge would be a second opinion, and the two would
  // drift the first time either was fixed.
  //
  // Refs are live for the overlay's current source, so a builder that wants
  // something durable resolves them before it lets go of that source.
  void ScanRows(const app::SearchQuery& q, const std::atomic<bool>& cancel,
                std::vector<FeatureRow>* out);

  // --- provenance ----------------------------------------------------------

  // THE MINT (search-plan-COMPLETE.md §3). A `SearchResult` names its feature
  // with one
  // uint64 because `HitItem` does, and nothing crossing this seam — no
  // binding, no shell, no serialised bookmark — should have to hold a variant
  // or a 128-bit `FeatureRef`. The mapping is therefore private to this class
  // and kept: the same feature minted twice gets the same number, so a result
  // held across two runs of the same query still names the same road.
  //
  // Ids start at 1. 0 is "no feature" and is never minted.
  uint64_t FeatureIdFor(const FeatureRef& ref);
  // The inverse. False for 0 and for any id this overlay did not mint.
  bool FeatureRefFor(uint64_t id, FeatureRef* out) const;
  size_t minted_count() const { return by_id_.size(); }

  // The identify path: the source's own `Describe` for a result's feature.
  // Fails (kNotFound) for an id this overlay never minted, and passes the
  // source's own failure through otherwise — including the `kUnsupported` a
  // source that cannot describe anything returns.
  Status DescribeFeature(uint64_t id, FeatureDescription* out) const;

  // --- diagnostics ---------------------------------------------------------
  // Features the source returned for the last search, before the label and
  // area tests; and the rows that survived merging. The pair is what a tile
  // budget test reads, and what tells "the pack has nothing there" apart from
  // "the scan never got that deep".
  size_t last_search_features() const { return last_features_; }
  size_t last_search_results() const { return last_results_; }
  // Which tier answered the last search: true = the source's name index,
  // false = the tile scan (including a scan that read nothing).
  bool last_search_used_index() const { return last_used_index_; }

 private:
  struct RefKey {
    int32_t source = 0, layer = 0, tile = 0, feature = 0;
    bool operator<(const RefKey& o) const;
  };

  VectorSourcePtr source_;
  std::vector<std::string> label_tags_;
  double scale_denominator_ = 0.0;
  size_t max_features_ = 0;
  double merge_gap_m_ = 100.0;
  bool use_name_index_ = true;

  std::map<RefKey, uint64_t> by_ref_;
  std::vector<FeatureRef> by_id_;  // index = id - 1

  size_t last_features_ = 0;
  size_t last_results_ = 0;
  bool last_used_index_ = false;

  // The two tiers, each appending to `out`. `SearchIndex` answers false when
  // the source turned out to have no index (or reading it failed), which is
  // when the scan runs.
  bool SearchIndex(const app::SearchQuery& q, const std::atomic<bool>& cancel,
                   std::vector<app::SearchResult>& out);
  void SearchTiles(const app::SearchQuery& q, const std::atomic<bool>& cancel,
                   std::vector<app::SearchResult>& out);
};

}  // namespace fv
