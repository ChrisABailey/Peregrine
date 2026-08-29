// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/vector/feature_rows.h"

#include <algorithm>
#include <unordered_map>

#include "fvkit/app/search.h"  // SearchDistanceMeters — the port's one metre

namespace fv {
namespace {

// The closest pair of points on two boxes, so that the gap between them can be
// measured in the port's ONE metre (app::SearchDistanceMeters) rather than in
// a metres-per-degree constant written out for the third time. Overlapping
// boxes give the same point twice, hence a gap of zero.
//
// The antimeridian is not handled, and the consequence is bounded: two pieces
// of one named feature that straddle +/-180 are reported as two rows instead
// of one. Merging is a presentation nicety; inventing wrap-around arithmetic
// for it would cost more than the rows it saves.
void ClosestPoints(const GeoRect& a, const GeoRect& b, GeoPoint* pa,
                   GeoPoint* pb) {
  if (a.ur.lat < b.ll.lat) {
    pa->lat = a.ur.lat;
    pb->lat = b.ll.lat;
  } else if (b.ur.lat < a.ll.lat) {
    pa->lat = a.ll.lat;
    pb->lat = b.ur.lat;
  } else {
    pa->lat = pb->lat = std::max(a.ll.lat, b.ll.lat);
  }
  if (a.ur.lon < b.ll.lon) {
    pa->lon = a.ur.lon;
    pb->lon = b.ll.lon;
  } else if (b.ur.lon < a.ll.lon) {
    pa->lon = a.ll.lon;
    pb->lon = b.ur.lon;
  } else {
    pa->lon = pb->lon = std::max(a.ll.lon, b.ll.lon);
  }
}

double BoxGapMeters(const GeoRect& a, const GeoRect& b) {
  GeoPoint pa, pb;
  ClosestPoints(a, b, &pa, &pb);
  return app::SearchDistanceMeters(pa, pb);
}

GeoRect UnionOf(const GeoRect& a, const GeoRect& b) {
  GeoRect r;
  r.ll.lat = std::min(a.ll.lat, b.ll.lat);
  r.ll.lon = std::min(a.ll.lon, b.ll.lon);
  r.ur.lat = std::max(a.ur.lat, b.ur.lat);
  r.ur.lon = std::max(a.ur.lon, b.ur.lon);
  return r;
}

// How big a piece is, for choosing which one carries the merged row's label
// anchor. Degrees, and deliberately not metres: it is only ever compared with
// another box at the same latitude.
double Extent(const GeoRect& r) {
  return (r.ur.lat - r.ll.lat) + (r.ur.lon - r.ll.lon);
}

// How far apart in LATITUDE two boxes can be and still be within `gap_m`,
// over-estimated on purpose. The sweep below retires a cluster once the
// pieces have moved this far north of it, and an over-estimate only ever
// retires one LATER than it had to — the answer cannot change, which is what
// lets a whole-pyramid merge stop being quadratic without becoming a
// different merge from the one tier 1 runs per query.
double LatitudeReachDegrees(double gap_m) {
  constexpr double kConservativeMetersPerDegLat = 110000.0;  // < the real one
  return gap_m / kConservativeMetersPerDegLat;
}

struct Cluster {
  FeatureRow row;
  double anchor_extent = -1.0;
  size_t first = 0;  // the input position of the earliest piece in it
  bool dead = false;
};

std::string KeyOf(const FeatureRow& r) {
  return r.title + '\x1f' + r.layer + '\x1f' + r.style_key;
}

}  // namespace

void MergeFeatureRows(std::vector<FeatureRow>* rows, double gap_m) {
  if (rows == nullptr || gap_m < 0.0 || rows->size() < 2) return;

  // SAME TITLE, SAME LAYER, SAME STYLE KEY is the only pairing that can ever
  // merge, so bucket on it first: the sweep below then runs over the pieces of
  // ONE named thing rather than over everything the scan found.
  std::unordered_map<std::string, std::vector<size_t>> buckets;
  std::vector<std::string> bucket_order;  // only to keep the walk deterministic
  buckets.reserve(rows->size());
  for (size_t i = 0; i < rows->size(); ++i) {
    const std::string key = KeyOf((*rows)[i]);
    auto it = buckets.find(key);
    if (it == buckets.end()) {
      bucket_order.push_back(key);
      buckets.emplace(key, std::vector<size_t>{i});
    } else {
      it->second.push_back(i);
    }
  }

  const double reach = LatitudeReachDegrees(gap_m);
  std::vector<Cluster> clusters;
  clusters.reserve(bucket_order.size());

  for (const std::string& key : bucket_order) {
    std::vector<size_t>& members = buckets[key];
    // Sorted south to north so the sweep can retire clusters the remaining
    // pieces can no longer reach. Ties keep input order.
    std::sort(members.begin(), members.end(), [&](size_t a, size_t b) {
      const double la = (*rows)[a].bounds.ll.lat;
      const double lb = (*rows)[b].bounds.ll.lat;
      if (la != lb) return la < lb;
      return a < b;
    });

    std::vector<size_t> active;  // indices into `clusters`
    for (size_t idx : members) {
      const FeatureRow& piece = (*rows)[idx];
      active.erase(std::remove_if(active.begin(), active.end(),
                                  [&](size_t ci) {
                                    return clusters[ci].row.bounds.ur.lat <
                                           piece.bounds.ll.lat - reach;
                                  }),
                   active.end());

      // A PIECE CAN BRIDGE two clusters that were not touching each other, so
      // every cluster it reaches is folded into the first — otherwise the
      // answer would depend on the order the tiles were read, which is the one
      // thing a stable result list cannot afford.
      size_t into = clusters.size();
      for (size_t ai = 0; ai < active.size();) {
        Cluster& cl = clusters[active[ai]];
        if (BoxGapMeters(cl.row.bounds, piece.bounds) > gap_m) {
          ++ai;
          continue;
        }
        if (into == clusters.size()) {
          into = active[ai];
          ++ai;
          continue;
        }
        Cluster& first = clusters[into];
        first.row.bounds = UnionOf(first.row.bounds, cl.row.bounds);
        if (cl.anchor_extent > first.anchor_extent) {
          first.row.anchor = cl.row.anchor;
          first.anchor_extent = cl.anchor_extent;
        }
        first.row.quality = std::min(first.row.quality, cl.row.quality);
        first.row.rank = std::min(first.row.rank, cl.row.rank);
        if (cl.first < first.first) {
          first.first = cl.first;
          first.row.ref = cl.row.ref;
        }
        cl.dead = true;
        active.erase(active.begin() + static_cast<ptrdiff_t>(ai));
      }

      if (into == clusters.size()) {
        Cluster cl;
        cl.row = piece;
        cl.anchor_extent = Extent(piece.bounds);
        cl.first = idx;
        clusters.push_back(std::move(cl));
        active.push_back(clusters.size() - 1);
        continue;
      }
      Cluster& cl = clusters[into];
      cl.row.bounds = UnionOf(cl.row.bounds, piece.bounds);
      const double extent = Extent(piece.bounds);
      if (extent > cl.anchor_extent) {
        // The label anchor stays on the BIGGEST piece, which is a point of the
        // thing itself; the centre of the union can be off the road entirely.
        cl.row.anchor = piece.anchor;
        cl.anchor_extent = extent;
      }
      cl.row.quality = std::min(cl.row.quality, piece.quality);
      cl.row.rank = std::min(cl.row.rank, piece.rank);
      if (idx < cl.first) {
        // THE ID A MERGED ROW CARRIES is the first piece in INPUT order — the
        // one the caller's own scan met first — and not whichever piece this
        // sweep reached first. The two were the same before the sweep existed
        // and the pinned tier-1 results say they must stay the same.
        cl.first = idx;
        cl.row.ref = piece.ref;
      }
    }
  }

  std::vector<Cluster*> alive;
  alive.reserve(clusters.size());
  for (Cluster& cl : clusters) {
    if (!cl.dead) alive.push_back(&cl);
  }
  std::sort(alive.begin(), alive.end(), [](const Cluster* a, const Cluster* b) {
    return a->first < b->first;
  });

  std::vector<FeatureRow> merged;
  merged.reserve(alive.size());
  for (Cluster* cl : alive) merged.push_back(std::move(cl->row));
  *rows = std::move(merged);
}

}  // namespace fv
