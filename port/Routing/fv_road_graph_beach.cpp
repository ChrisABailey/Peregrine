// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * @file fv_road_graph_beach.cpp
 * Coastline runs along beach polygons, and the access arcs that join path
 * ends to them. See fv_road_graph_beach.h.
 */

#include "fv_road_graph_beach.h"

#include "fvkit/nav/road_snap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace fv {
namespace routing {
namespace beach {
namespace {

constexpr double kMetersPerDegLat = 6371008.8 * 3.14159265358979323846 / 180.0;

/// Splits and access points closer than this share one vertex.
constexpr double kMergeMeters = 1.0;

/// True when `p` is within `margin_m` of `box`.
bool NearBox(const GeoRect& box, const GeoPoint& p, double margin_m) {
  const double dlat = margin_m / kMetersPerDegLat;
  const double c = std::cos(p.lat * 3.14159265358979323846 / 180.0);
  const double dlon = margin_m / (kMetersPerDegLat * (c > 1e-6 ? c : 1e-6));
  return p.lat >= box.ll.lat - dlat && p.lat <= box.ur.lat + dlat &&
         p.lon >= box.ll.lon - dlon && p.lon <= box.ur.lon + dlon;
}

GeoRect BoxOf(const std::vector<GeoPoint>& pts) {
  GeoRect r{{90.0, 180.0}, {-90.0, -180.0}};
  for (const GeoPoint& p : pts) {
    r.ll.lat = std::min(r.ll.lat, p.lat);
    r.ll.lon = std::min(r.ll.lon, p.lon);
    r.ur.lat = std::max(r.ur.lat, p.lat);
    r.ur.lon = std::max(r.ur.lon, p.lon);
  }
  return r;
}

double Meters(const GeoPoint& a, const GeoPoint& b) {
  return GreatCircleMeters(a.lat, a.lon, b.lat, b.lon);
}

GeoPoint Lerp(const GeoPoint& a, const GeoPoint& b, double t) {
  return GeoPoint{a.lat + (b.lat - a.lat) * t, a.lon + (b.lon - a.lon) * t};
}

/// A contiguous stretch of coastline that borders a beach.
struct Run {
  std::vector<int64_t> ids;
  std::vector<GeoPoint> pts;
  uint32_t name = 0;
  GeoRect box;
};

/// Where an access candidate meets a run.
struct Split {
  size_t candidate = 0;
  size_t seg = 0;  // segment [seg, seg + 1] of the run
  double t = 0.0;
};

/// One point along a run once splits are inserted.
struct Station {
  GeoPoint p;
  int64_t coast_id = 0;  // 0 for a split point
  bool vertex = false;
  uint32_t node = kNoArc;
  std::vector<size_t> candidates;
};

/// Finds the runs of `line` that border a beach, bridging short gaps.
void RunsOf(const Line& line, const std::vector<Ring>& rings,
            const std::unordered_map<int64_t, size_t>& ring_of_id, const Params& params,
            uint32_t beach_name, std::vector<Run>* out) {
  const size_t n = line.pts.size();
  std::vector<int> ring_at(n, -1);
  for (size_t k = 0; k < n; ++k) {
    auto it = ring_of_id.find(line.ids[k]);
    if (it != ring_of_id.end()) {
      ring_at[k] = static_cast<int>(it->second);
      continue;
    }
    for (size_t r = 0; r < rings.size(); ++r) {
      if (!NearBox(rings[r].box, line.pts[k], params.line_snap_m)) continue;
      if (Inside(rings[r], line.pts[k]) ||
          BoundaryDistance(rings[r], line.pts[k]) <= params.line_snap_m) {
        ring_at[k] = static_cast<int>(r);
        break;
      }
    }
  }

  std::vector<double> cum(n, 0.0);
  for (size_t k = 1; k < n; ++k) cum[k] = cum[k - 1] + Meters(line.pts[k - 1], line.pts[k]);

  // [begin, end] index ranges of on-beach nodes, gaps under gap_m bridged.
  std::vector<std::pair<size_t, size_t>> ranges;
  for (size_t k = 0; k < n; ++k) {
    if (ring_at[k] < 0) continue;
    if (!ranges.empty() && (ranges.back().second + 1 == k ||
                            cum[k] - cum[ranges.back().second] < params.gap_m)) {
      ranges.back().second = k;
    } else {
      ranges.emplace_back(k, k);
    }
  }

  for (const auto& range : ranges) {
    if (range.second == range.first) continue;
    Run run;
    std::unordered_map<uint32_t, int> votes;
    for (size_t k = range.first; k <= range.second; ++k) {
      run.ids.push_back(line.ids[k]);
      run.pts.push_back(line.pts[k]);
      if (ring_at[k] >= 0) ++votes[rings[static_cast<size_t>(ring_at[k])].name];
    }
    // The name most of the run borders; unnamed polygons vote too, so a long
    // run does not take the name of a park it passes.
    uint32_t best = 0;
    int best_votes = 0;
    for (const auto& v : votes) {
      if (v.second > best_votes || (v.second == best_votes && v.first < best)) {
        best = v.first;
        best_votes = v.second;
      }
    }
    run.name = best != 0 ? best : beach_name;
    run.box = BoxOf(run.pts);
    out->push_back(std::move(run));
  }
}

}  // namespace

bool Inside(const Ring& ring, const GeoPoint& p) {
  bool in = false;
  const size_t n = ring.pts.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const GeoPoint& a = ring.pts[i];
    const GeoPoint& b = ring.pts[j];
    if ((a.lat > p.lat) != (b.lat > p.lat) &&
        p.lon < (b.lon - a.lon) * (p.lat - a.lat) / (b.lat - a.lat) + a.lon) {
      in = !in;
    }
  }
  return in;
}

double BoundaryDistance(const Ring& ring, const GeoPoint& p) {
  double best = std::numeric_limits<double>::infinity();
  SegmentProjection proj;
  for (size_t i = 1; i < ring.pts.size(); ++i) {
    ProjectOntoSegment(p, ring.pts[i - 1], ring.pts[i], &proj);
    best = std::min(best, proj.distance_m);
  }
  return best;
}

bool NearAnyRing(const std::vector<Ring>& rings, const GeoPoint& p, double snap_m) {
  for (const Ring& r : rings) {
    if (!NearBox(r.box, p, snap_m)) continue;
    if (Inside(r, p) || BoundaryDistance(r, p) <= snap_m) return true;
  }
  return false;
}

void Synthesize(const std::vector<Ring>& rings, const std::vector<Line>& lines,
                const std::vector<AccessCandidate>& candidates,
                const std::function<uint32_t(int64_t)>& graph_node_of,
                uint32_t first_new_node, uint32_t beach_name, const Params& params,
                Result* out) {
  *out = Result{};

  std::unordered_map<int64_t, size_t> ring_of_id;
  for (size_t r = 0; r < rings.size(); ++r) {
    for (int64_t id : rings[r].ids) ring_of_id.emplace(id, r);
  }

  std::vector<Run> runs;
  for (const Line& line : lines) RunsOf(line, rings, ring_of_id, params, beach_name, &runs);

  // Each candidate joins the run nearest to it.
  std::vector<std::vector<Split>> splits(runs.size());
  SegmentProjection proj;
  for (size_t c = 0; c < candidates.size(); ++c) {
    const GeoPoint& p = candidates[c].p;
    double best = std::numeric_limits<double>::infinity();
    Split best_split;
    size_t best_run = 0;
    for (size_t r = 0; r < runs.size(); ++r) {
      if (!NearBox(runs[r].box, p, std::min(best, params.access_max_m))) continue;
      for (size_t s = 0; s + 1 < runs[r].pts.size(); ++s) {
        ProjectOntoSegment(p, runs[r].pts[s], runs[r].pts[s + 1], &proj);
        if (proj.distance_m < best) {
          best = proj.distance_m;
          best_run = r;
          best_split = Split{c, s, proj.length_m > 0.0 ? proj.along_m / proj.length_m : 0.0};
        }
      }
    }
    if (best > params.access_max_m) {
      ++out->access_unreached;
      continue;
    }
    splits[best_run].push_back(best_split);
  }

  uint32_t next_node = first_new_node;
  int64_t next_split_id = -1;
  std::unordered_map<int64_t, uint32_t> coast_node;  // coastline id -> new node

  auto add_node = [&](const GeoPoint& p, int64_t osm_id) {
    out->nodes.push_back(NewNode{p, osm_id});
    return next_node++;
  };

  for (size_t r = 0; r < runs.size(); ++r) {
    const Run& run = runs[r];
    std::vector<Split>& rs = splits[r];
    std::sort(rs.begin(), rs.end(), [](const Split& a, const Split& b) {
      return a.seg != b.seg ? a.seg < b.seg : a.t < b.t;
    });

    // A split within kMergeMeters of a coastline node attaches to that node;
    // any other is inserted as a station of its own, in order along the run.
    std::vector<std::vector<size_t>> at_node(run.pts.size());
    std::vector<std::vector<const Split*>> inserted(run.pts.size());
    for (const Split& sp : rs) {
      const GeoPoint q = Lerp(run.pts[sp.seg], run.pts[sp.seg + 1], sp.t);
      if (Meters(q, run.pts[sp.seg]) < kMergeMeters) {
        at_node[sp.seg].push_back(sp.candidate);
      } else if (Meters(q, run.pts[sp.seg + 1]) < kMergeMeters) {
        at_node[sp.seg + 1].push_back(sp.candidate);
      } else {
        inserted[sp.seg].push_back(&sp);
      }
    }

    std::vector<Station> st;
    for (size_t k = 0; k < run.pts.size(); ++k) {
      Station s;
      s.p = run.pts[k];
      s.coast_id = run.ids[k];
      s.candidates = at_node[k];
      s.vertex = k == 0 || k + 1 == run.pts.size() || !s.candidates.empty() ||
                 graph_node_of(s.coast_id) != kNoArc;
      st.push_back(std::move(s));
      for (const Split* sp : inserted[k]) {
        const GeoPoint q = Lerp(run.pts[k], run.pts[k + 1], sp->t);
        if (st.back().coast_id == 0 && Meters(q, st.back().p) < kMergeMeters) {
          st.back().candidates.push_back(sp->candidate);
          continue;
        }
        Station split;
        split.p = q;
        split.vertex = true;
        split.candidates.push_back(sp->candidate);
        st.push_back(std::move(split));
      }
    }

    for (Station& s : st) {
      if (!s.vertex) continue;
      if (s.coast_id == 0) {
        s.node = add_node(s.p, next_split_id--);
        continue;
      }
      const uint32_t existing = graph_node_of(s.coast_id);
      if (existing != kNoArc) {
        s.node = existing;
        continue;
      }
      auto it = coast_node.find(s.coast_id);
      if (it == coast_node.end()) it = coast_node.emplace(s.coast_id, add_node(s.p, s.coast_id)).first;
      s.node = it->second;
    }

    // Cut the run at its vertices.
    size_t start = 0;
    NewEdge edge;
    for (size_t k = 1; k < st.size(); ++k) {
      edge.length_m += Meters(st[k - 1].p, st[k].p);
      if (!st[k].vertex) {
        edge.interior.push_back(st[k].p);
        continue;
      }
      if (st[start].node != st[k].node) {
        edge.u = st[start].node;
        edge.v = st[k].node;
        edge.klass = RoadClass::kBeach;
        edge.name = run.name;
        out->run_m += edge.length_m;
        out->edges.push_back(edge);
      }
      edge = NewEdge{};
      start = k;
    }
    ++out->runs;

    for (const Station& s : st) {
      for (size_t c : s.candidates) {
        const AccessCandidate& cand = candidates[c];
        if (cand.node == s.node) continue;
        NewEdge access;
        access.u = cand.node;
        access.v = s.node;
        access.length_m = Meters(cand.p, s.p);
        access.klass = RoadClass::kBeachAccess;
        access.access_flags = cand.access_flags;
        out->edges.push_back(access);
        ++out->access_arcs;
      }
    }
  }
}

}  // namespace beach
}  // namespace routing
}  // namespace fv
