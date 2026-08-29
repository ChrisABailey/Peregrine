// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/vector/feature_rows.h — "one named thing, one row", extracted so that
// the live scan and the staged index cannot disagree about what one road is
// (port/search-plan-COMPLETE.md, S3).
//
// S2 discovered the rule and implemented it inside VectorMapOverlay: a road is
// cut at every tile seam it crosses and OSM had already split it at every
// junction where a tag changed, so a raw scan answers "Ruddy Turnstone" with a
// dozen identical rows. Pieces sharing a title, a layer and a style key are
// therefore joined when their boxes come within a gap.
//
// S3 builds an INDEX of those rows at staging time, and an index that merged
// its pieces by a different rule than the live scan would be a search box that
// answers one way for the pack it has an index for and another way for the
// pack it does not. Hence this file: the merge is one function, tier 1 calls
// it per query and the index builder calls it over a whole pyramid.
//
// WHAT THE FUNCTION GUARANTEES, because two callers now depend on it:
//
//   * A piece that BRIDGES two clusters folds them together, so the answer
//     never depends on the order the tiles were read.
//   * The merged bounds is the union; the anchor is the BIGGEST piece's own
//     label point (the union's centre can be off the road entirely); the
//     quality and the rank are the best (smallest) among the pieces; and the
//     ref is the FIRST piece in input order — never whichever one the
//     internal sweep happened to reach first.
//   * Output order is the order the surviving rows first appeared in the
//     input, so a stable input gives a stable answer.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "fvkit/geo.h"
#include "fvkit/vector/vector.h"

namespace fv {

// One named thing, before or after merging — the shape both the live scan and
// the staged index carry a row in.
struct FeatureRow {
  // The identity a merge is keyed on. Two pieces join only when all three
  // match: a "Main Street" that is residential in one place and a track in
  // another is two answers, not one long one.
  std::string title;
  std::string layer;
  std::string style_key;

  GeoRect bounds{};   // the piece's own box; the union after merging
  GeoPoint anchor{};  // where a label would sit (see the header)

  // Match quality as fvkit/app/search.h defines it (0 exact … 2 token), or 0
  // when nothing was matched. The best of the pieces survives a merge.
  int quality = 0;

  // THE SOURCE'S OWN PROMINENCE, smaller = more prominent, 0 when the source
  // has no opinion. A tile pyramid puts the shallowest zoom the name survives
  // to here, which is a relevance ranking the cutter already paid for: a name
  // that is still drawn at z6 is a city, one that first appears at z14 is a
  // cul-de-sac. The best (smallest) of the pieces survives a merge.
  int rank = 0;

  FeatureRef ref{};
};

// Join the pieces of each named thing, in place. `gap_m` is how far apart two
// pieces' boxes may lie and still be one row (0 merges only pieces that
// actually touch — the tile-seam case and nothing else); NEGATIVE disables
// merging entirely and leaves the input untouched, which is what a caller
// building its own index out of raw pieces wants.
void MergeFeatureRows(std::vector<FeatureRow>* rows, double gap_m);

}  // namespace fv
