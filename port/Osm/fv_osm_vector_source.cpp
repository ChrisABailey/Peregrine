// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fv_osm_vector_source.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <unordered_map>

#include "fv_mvt.h"

namespace fv {
namespace {

// The port's reference display pitch, same constant MapProjection uses for
// SetPhysicalScale. Repeated rather than included because fvkit/proj.h drags
// in the raster engine, and this file needs one number.
constexpr double kDefaultMmPerPixel = 0.25;

// A small alias table so identify shows "Highway class" rather than "class".
// Plan section 5.3's "OSM: tag passthrough plus a small alias table" — OSM tag
// VALUES are already words ("motorway", "residential"), so unlike VPF's
// INT.VDT or S-57's Appendix A there is nothing to decode; only the keys need
// naming, and only the ones the OpenMapTiles schema actually emits.
struct KeyAlias {
  const char* key;
  const char* name;
};
const KeyAlias kKeyAliases[] = {
    {"class", "Class"},
    {"subclass", "Subclass"},
    {"name", "Name"},
    {"name:latin", "Name (latin)"},
    {"name_int", "Name (international)"},
    {"ref", "Reference"},
    {"network", "Route network"},
    {"brunnel", "Bridge/tunnel/ford"},
    {"oneway", "One way"},
    {"surface", "Surface"},
    {"access", "Access"},
    {"service", "Service road type"},
    {"expressway", "Expressway"},
    {"toll", "Toll"},
    {"layer", "Vertical layer"},
    {"level", "Building level"},
    {"indoor", "Indoor"},
    {"housenumber", "House number"},
    {"ele", "Elevation (m)"},
    {"ele_ft", "Elevation (ft)"},
    {"iata", "IATA code"},
    {"icao", "ICAO code"},
    {"intermittent", "Intermittent"},
    {"render_height", "Render height"},
    {"render_min_height", "Render minimum height"},
    {"capital", "Capital"},
    {"rank", "Rank"},
    {"admin_level", "Administrative level"},
    {"disputed", "Disputed"},
    {"maritime", "Maritime boundary"},
};

const char* AliasFor(const std::string& key) {
  for (const KeyAlias& a : kKeyAliases)
    if (key == a.key) return a.name;
  return nullptr;
}

std::string BaseName(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Best one-line title for a popup: the feature's own name if it has one, else
// what it is.
std::string TitleFor(const MvtFeature& f, const std::string& style_key,
                     const std::string& layer) {
  for (const char* key : {"name", "name:latin", "name_int", "ref"}) {
    const std::string* v = f.Tag(key);
    if (v != nullptr && !v->empty()) return *v;
  }
  if (!style_key.empty()) return style_key;
  return layer;
}

}  // namespace

// ---------------------------------------------------------------------------

struct OsmVectorSource::Impl {
  MbtilesFile file;

  double mm_per_pixel = kDefaultMmPerPixel;
  int zoom_override = -1;
  size_t max_tiles = 64;
  size_t cache_capacity = 64;
  bool clip_to_tile = true;
  std::vector<std::string> style_key_tags{"class"};

  // Layer names in a STABLE order: the file's own `json` inventory first (so
  // an index means the same thing across runs and across viewports), then any
  // name a tile turned out to hold that the inventory did not list.
  std::vector<std::string> layer_names;
  std::unordered_map<std::string, int> layer_index;
  std::vector<std::string> undeclared_layers;

  // FeatureRef::tile is an index into this, assigned on first use, because
  // z/x/y does not fit in one int32 beyond z13 and a source that silently
  // aliased two tiles would mis-identify features.
  std::vector<webmerc::TileId> tile_ids;
  std::map<webmerc::TileId, int> tile_index;

  // LRU of decoded tiles.
  std::list<webmerc::TileId> lru;
  struct CacheEntry {
    MvtTile tile;
    std::list<webmerc::TileId>::iterator pos;
  };
  std::map<webmerc::TileId, CacheEntry> cache;

  int last_zoom = -1;
  size_t last_read = 0;
  size_t last_missing = 0;
  bool last_capped = false;
  bool last_truncated = false;
  size_t last_buffer_dropped = 0;
  size_t last_clipped = 0;
  size_t last_clipped_away = 0;
  double last_overzoom = 0.0;

  int LayerIndex(const std::string& name) {
    auto it = layer_index.find(name);
    if (it != layer_index.end()) return it->second;
    const int idx = static_cast<int>(layer_names.size());
    layer_names.push_back(name);
    layer_index[name] = idx;
    undeclared_layers.push_back(name);
    return idx;
  }

  int TileIndex(const webmerc::TileId& t) {
    auto it = tile_index.find(t);
    if (it != tile_index.end()) return it->second;
    const int idx = static_cast<int>(tile_ids.size());
    tile_ids.push_back(t);
    tile_index[t] = idx;
    return idx;
  }

  // nullptr + kNotFound when the pyramid has no such tile.
  Status GetTile(const webmerc::TileId& t, const MvtTile** out);
  void Trim();

  std::string StyleKeyFor(const MvtFeature& f, const std::string& layer) const {
    for (const std::string& key : style_key_tags) {
      const std::string* v = f.Tag(key);
      if (v != nullptr && !v->empty()) return *v;
    }
    return layer;
  }
};

void OsmVectorSource::Impl::Trim() {
  while (cache.size() > cache_capacity && !lru.empty()) {
    const webmerc::TileId victim = lru.back();
    lru.pop_back();
    cache.erase(victim);
  }
}

Status OsmVectorSource::Impl::GetTile(const webmerc::TileId& t,
                                      const MvtTile** out) {
  *out = nullptr;
  auto it = cache.find(t);
  if (it != cache.end()) {
    lru.splice(lru.begin(), lru, it->second.pos);
    it->second.pos = lru.begin();
    *out = &it->second.tile;
    return Status::Ok();
  }

  std::string blob;
  Status s = file.ReadTile(t, &blob);
  if (!s.ok()) return s;  // kNotFound for a hole in the pyramid

  CacheEntry entry;
  s = entry.tile.Decode(blob.data(), blob.size(), t);
  if (!s.ok()) return s;

  lru.push_front(t);
  entry.pos = lru.begin();
  auto ins = cache.emplace(t, std::move(entry));
  *out = &ins.first->second.tile;
  Trim();
  // Trim may have evicted the entry we just inserted if the capacity is 0;
  // re-look it up rather than hand back a dangling pointer.
  if (cache.find(t) == cache.end()) *out = nullptr;
  return Status::Ok();
}

// ---------------------------------------------------------------------------

OsmVectorSource::OsmVectorSource() : impl_(new Impl) {}
OsmVectorSource::~OsmVectorSource() = default;

Status OsmVectorSource::Open(const std::string& path) {
  std::unique_ptr<Impl> fresh(new Impl);
  // Carry the knobs across a reopen — they are application settings, not
  // properties of the file (E3c's stated rule for RuleSet/ViewingGroupSet).
  fresh->mm_per_pixel = impl_->mm_per_pixel;
  fresh->zoom_override = impl_->zoom_override;
  fresh->max_tiles = impl_->max_tiles;
  fresh->cache_capacity = impl_->cache_capacity;
  fresh->clip_to_tile = impl_->clip_to_tile;
  fresh->style_key_tags = impl_->style_key_tags;

  Status s = fresh->file.Open(path);
  if (!s.ok()) return s;

  const std::string& fmt = fresh->file.format();
  if (!fmt.empty() && fmt != "pbf" && fmt != "mvt") {
    return Status::Error(kUnsupported,
                         path + ": format '" + fmt +
                             "' is a raster pyramid, not vector tiles");
  }

  for (const MbtilesLayerInfo& l : fresh->file.layers()) {
    if (fresh->layer_index.count(l.id) != 0) continue;
    fresh->layer_index[l.id] = static_cast<int>(fresh->layer_names.size());
    fresh->layer_names.push_back(l.id);
  }

  impl_ = std::move(fresh);
  return Status::Ok();
}

bool OsmVectorSource::IsOpen() const { return impl_->file.IsOpen(); }
GeoRect OsmVectorSource::Bounds() const { return impl_->file.Bounds(); }

std::vector<std::string> OsmVectorSource::Layers() const {
  return impl_->layer_names;
}

void OsmVectorSource::SetDisplayMmPerPixel(double mm_per_pixel) {
  if (mm_per_pixel > 0.0) impl_->mm_per_pixel = mm_per_pixel;
}
double OsmVectorSource::display_mm_per_pixel() const {
  return impl_->mm_per_pixel;
}
void OsmVectorSource::SetZoomOverride(int z) { impl_->zoom_override = z; }
int OsmVectorSource::zoom_override() const { return impl_->zoom_override; }
void OsmVectorSource::SetMaxTilesPerQuery(size_t n) {
  impl_->max_tiles = n > 0 ? n : 1;
}
void OsmVectorSource::SetTileCacheCapacity(size_t n) {
  // Never 0: a zero-capacity cache would evict each tile as it was decoded
  // and hand Query a dangling entry to skip, which reads as empty coverage
  // rather than as a bad setting. 1 is "no reuse", which is what a caller
  // asking for 0 actually wants.
  impl_->cache_capacity = n > 0 ? n : 1;
  impl_->Trim();
}
void OsmVectorSource::SetClipToTile(bool on) { impl_->clip_to_tile = on; }
bool OsmVectorSource::clip_to_tile() const { return impl_->clip_to_tile; }
void OsmVectorSource::SetStyleKeyTags(std::vector<std::string> tags) {
  impl_->style_key_tags = std::move(tags);
}

int OsmVectorSource::last_query_zoom() const { return impl_->last_zoom; }
size_t OsmVectorSource::last_query_tiles_read() const {
  return impl_->last_read;
}
size_t OsmVectorSource::last_query_tiles_missing() const {
  return impl_->last_missing;
}
bool OsmVectorSource::last_query_zoom_capped() const {
  return impl_->last_capped;
}
bool OsmVectorSource::last_query_truncated() const {
  return impl_->last_truncated;
}
size_t OsmVectorSource::last_query_buffer_dropped() const {
  return impl_->last_buffer_dropped;
}
size_t OsmVectorSource::last_query_clipped() const { return impl_->last_clipped; }
size_t OsmVectorSource::last_query_clipped_away() const {
  return impl_->last_clipped_away;
}
double OsmVectorSource::last_query_overzoom() const {
  return impl_->last_overzoom;
}
const std::vector<std::string>& OsmVectorSource::undeclared_layers() const {
  return impl_->undeclared_layers;
}
const MbtilesFile& OsmVectorSource::file() const { return impl_->file; }

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

namespace {

// Inclusive XYZ tile range covering one non-crossing box at one zoom.
struct TileRange {
  int min_x = 0, max_x = -1, min_y = 0, max_y = -1;
  int64_t count() const {
    if (max_x < min_x || max_y < min_y) return 0;
    return int64_t(max_x - min_x + 1) * int64_t(max_y - min_y + 1);
  }
};

TileRange RangeFor(const GeoRect& box, int z) {
  const int64_t n = webmerc::TilesPerAxis(z);
  TileRange r;
  double x0 = webmerc::LonToTileX(box.ll.lon, z);
  double x1 = webmerc::LonToTileX(box.ur.lon, z);
  double y0 = webmerc::LatToTileY(box.ur.lat, z);  // north edge -> smaller y
  double y1 = webmerc::LatToTileY(box.ll.lat, z);
  if (x1 < x0) std::swap(x0, x1);
  if (y1 < y0) std::swap(y0, y1);
  r.min_x = static_cast<int>(std::floor(x0));
  r.max_x = static_cast<int>(std::ceil(x1)) - 1;
  r.min_y = static_cast<int>(std::floor(y0));
  r.max_y = static_cast<int>(std::ceil(y1)) - 1;
  // A zero-width box (a click point) still names the tile it fell in.
  if (r.max_x < r.min_x) r.max_x = r.min_x;
  if (r.max_y < r.min_y) r.max_y = r.min_y;
  r.min_x = static_cast<int>(std::max<int64_t>(0, r.min_x));
  r.min_y = static_cast<int>(std::max<int64_t>(0, r.min_y));
  r.max_x = static_cast<int>(std::min<int64_t>(n - 1, r.max_x));
  r.max_y = static_cast<int>(std::min<int64_t>(n - 1, r.max_y));
  return r;
}

int64_t TotalTiles(const std::vector<GeoRect>& boxes, int z) {
  int64_t n = 0;
  for (const GeoRect& b : boxes) n += RangeFor(b, z).count();
  return n;
}

// --- per-tile clipping -----------------------------------------------------
//
// Plain rectangle clipping in lon/lat. That is exact here and nowhere near
// exact in general: a tile box IS an axis-aligned lon/lat rectangle by
// construction (webmerc::TileBounds), and no tile box crosses the
// antimeridian, so the two things that usually make geographic clipping hard
// are both absent. A clipped vertex lands on the seam, and the neighbouring
// tile's clip puts its own vertex on the same seam from the other side, which
// is what makes the two halves meet.

bool Contains(const GeoRect& box, const GeoRect& inner) {
  return inner.ll.lat >= box.ll.lat && inner.ur.lat <= box.ur.lat &&
         inner.ll.lon >= box.ll.lon && inner.ur.lon <= box.ur.lon;
}

// Liang-Barsky. Returns false when the segment misses the box entirely;
// otherwise (*a, *b) are moved to the surviving portion.
bool ClipSegment(const GeoRect& box, GeoPoint* a, GeoPoint* b) {
  double t0 = 0.0, t1 = 1.0;
  const double dx = b->lon - a->lon;
  const double dy = b->lat - a->lat;
  const double p[4] = {-dx, dx, -dy, dy};
  const double q[4] = {a->lon - box.ll.lon, box.ur.lon - a->lon,
                       a->lat - box.ll.lat, box.ur.lat - a->lat};
  for (int i = 0; i < 4; ++i) {
    if (p[i] == 0.0) {
      if (q[i] < 0.0) return false;  // parallel and outside
      continue;
    }
    const double r = q[i] / p[i];
    if (p[i] < 0.0) {
      if (r > t1) return false;
      if (r > t0) t0 = r;
    } else {
      if (r < t0) return false;
      if (r < t1) t1 = r;
    }
  }
  const GeoPoint a0 = *a;
  a->lon = a0.lon + t0 * dx;
  a->lat = a0.lat + t0 * dy;
  b->lon = a0.lon + t1 * dx;
  b->lat = a0.lat + t1 * dy;
  return true;
}

// One polyline in, zero or more in: a line that leaves the box and comes back
// is two runs, and joining them would draw a chord across the gap.
void ClipPolyline(const GeoRect& box, const std::vector<GeoPoint>& in,
                  std::vector<std::vector<GeoPoint>>* out) {
  if (in.size() < 2) {
    if (in.size() == 1 && box.Contains(in[0])) out->push_back(in);
    return;
  }
  std::vector<GeoPoint> run;
  for (size_t i = 0; i + 1 < in.size(); ++i) {
    GeoPoint a = in[i], b = in[i + 1];
    if (!ClipSegment(box, &a, &b)) {
      if (run.size() >= 2) out->push_back(std::move(run));
      run.clear();
      continue;
    }
    if (run.empty()) {
      run.push_back(a);
    } else if (run.back().lat != a.lat || run.back().lon != a.lon) {
      // The previous segment was clipped short of where this one starts:
      // a gap outside the box, so the run ends here.
      if (run.size() >= 2) out->push_back(std::move(run));
      run.clear();
      run.push_back(a);
    }
    run.push_back(b);
  }
  if (run.size() >= 2) out->push_back(std::move(run));
}

// Sutherland-Hodgman against the four edges. A ring survives as one ring (a
// convex box cannot split a ring into pieces that need separate outer/hole
// bookkeeping, which is exactly why this is the right algorithm here and not
// a general polygon-polygon clip).
void ClipRing(const GeoRect& box, const std::vector<GeoPoint>& in,
              std::vector<GeoPoint>* out) {
  auto inside = [&box](const GeoPoint& p, int edge) {
    switch (edge) {
      case 0: return p.lon >= box.ll.lon;
      case 1: return p.lon <= box.ur.lon;
      case 2: return p.lat >= box.ll.lat;
      default: return p.lat <= box.ur.lat;
    }
  };
  auto cross = [&box](const GeoPoint& a, const GeoPoint& b, int edge) {
    GeoPoint r;
    if (edge < 2) {
      const double x = edge == 0 ? box.ll.lon : box.ur.lon;
      const double t = (b.lon == a.lon) ? 0.0 : (x - a.lon) / (b.lon - a.lon);
      r.lon = x;
      r.lat = a.lat + t * (b.lat - a.lat);
    } else {
      const double y = edge == 2 ? box.ll.lat : box.ur.lat;
      const double t = (b.lat == a.lat) ? 0.0 : (y - a.lat) / (b.lat - a.lat);
      r.lat = y;
      r.lon = a.lon + t * (b.lon - a.lon);
    }
    return r;
  };

  std::vector<GeoPoint> cur = in;
  std::vector<GeoPoint> next;
  for (int edge = 0; edge < 4 && !cur.empty(); ++edge) {
    next.clear();
    for (size_t i = 0; i < cur.size(); ++i) {
      const GeoPoint& a = cur[i];
      const GeoPoint& b = cur[(i + 1) % cur.size()];
      const bool ain = inside(a, edge), bin = inside(b, edge);
      if (ain) next.push_back(a);
      if (ain != bin) next.push_back(cross(a, b, edge));
    }
    cur.swap(next);
  }
  if (cur.size() < 3) return;
  // The seam's contract is an EXPLICITLY closed ring (front == back), which
  // the MVT reader guarantees on the way in; Sutherland-Hodgman's output is
  // implicitly closed, so close it.
  if (cur.front().lat != cur.back().lat || cur.front().lon != cur.back().lon)
    cur.push_back(cur.front());
  *out = std::move(cur);
}

}  // namespace

Status OsmVectorSource::Query(const VectorQuery& q,
                              std::vector<VectorFeature>* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "Query: null out");
  if (!IsOpen()) return Status::Error(kIoError, "Query: source not open");

  Impl& im = *impl_;
  im.last_read = 0;
  im.last_missing = 0;
  im.last_capped = false;
  im.last_truncated = false;
  im.last_buffer_dropped = 0;
  im.last_clipped = 0;
  im.last_clipped_away = 0;
  im.last_overzoom = 0.0;

  const int min_z = im.file.min_zoom();
  const int max_z = im.file.max_zoom();
  const std::vector<GeoRect> boxes = q.area.SplitAtAntimeridian();

  // Representative latitude for the ground-resolution term: the middle of the
  // query, since Mercator's metres-per-pixel varies with it.
  double lat = 0.0;
  for (const GeoRect& b : boxes) lat += 0.5 * (b.ll.lat + b.ur.lat);
  if (!boxes.empty()) lat /= static_cast<double>(boxes.size());

  int z;
  if (im.zoom_override >= 0) {
    z = std::max(min_z, std::min(max_z, im.zoom_override));
  } else if (q.scale_denominator > 0.0) {
    z = webmerc::ZoomForScale(q.scale_denominator, lat, im.mm_per_pixel, min_z,
                              max_z);
  } else {
    // No scale at all: start at the deepest level and step coarser until the
    // range is affordable. Without this a bulk query is the whole pyramid.
    z = max_z;
  }
  if (im.zoom_override < 0) {
    while (z > min_z &&
           TotalTiles(boxes, z) > static_cast<int64_t>(im.max_tiles)) {
      --z;
      im.last_capped = true;
    }
  }
  im.last_zoom = z;

  // How far past the pyramid the display is (see the header). Reported, never
  // acted on: the clamp above already read the deepest tiles there are, and
  // the style engine is the half that must NOT be clamped.
  // Only past the BOTTOM of the pyramid counts. Landing a third of a level
  // above the tiles that were read is ordinary rounding — ZoomForScale picks
  // the nearest level, because a pyramid level is generalized for a scale
  // range centred on itself — and reporting that as overzoom would make the
  // number non-zero almost always and therefore worth nothing.
  if (im.zoom_override < 0 && q.scale_denominator > 0.0 && z >= max_z) {
    const double want =
        webmerc::ZoomForScaleExact(q.scale_denominator, lat, im.mm_per_pixel);
    if (std::isfinite(want) && want > z) im.last_overzoom = want - z;
  }

  const size_t limit = q.max_features;
  for (const GeoRect& box : boxes) {
    const TileRange r = RangeFor(box, z);
    for (int tx = r.min_x; tx <= r.max_x; ++tx) {
      for (int ty = r.min_y; ty <= r.max_y; ++ty) {
        if (limit != 0 && out->size() >= limit) {
          im.last_truncated = true;
          return Status::Ok();
        }
        const webmerc::TileId id{z, tx, ty};
        const MvtTile* tile = nullptr;
        const Status s = im.GetTile(id, &tile);
        if (s.code == kNotFound) {
          ++im.last_missing;
          continue;
        }
        if (!s.ok()) return s;
        if (tile == nullptr) continue;
        ++im.last_read;

        const GeoRect tile_box = webmerc::TileBounds(id);
        const int tile_ref = im.TileIndex(id);

        for (const MvtLayer& layer : tile->layers()) {
          const int layer_ref = im.LayerIndex(layer.name);
          for (size_t fi = 0; fi < layer.features.size(); ++fi) {
            const MvtFeature& f = layer.features[fi];
            if (!f.bounds.Intersects(q.area)) continue;
            // Buffer geometry: this feature belongs to a neighbouring tile
            // and, if that neighbour is in range, is about to arrive from it.
            if (!f.bounds.Intersects(tile_box)) {
              ++im.last_buffer_dropped;
              continue;
            }
            if (limit != 0 && out->size() >= limit) {
              im.last_truncated = true;
              return Status::Ok();
            }

            FeatureRef ref;
            ref.layer = layer_ref;
            ref.tile = tile_ref;
            ref.feature = static_cast<int32_t>(fi);

            const std::string style_key = im.StyleKeyFor(f, layer.name);

            // Clip the buffer geometry away, so the neighbouring tile's copy
            // of this feature and this one meet on the seam instead of
            // overlapping. Only geometry that actually leaves the box is
            // touched — the common case costs one rectangle comparison.
            const bool needs_clip =
                im.clip_to_tile && f.type != VectorGeometryType::kPoint &&
                !Contains(tile_box, f.bounds);
            std::vector<std::vector<GeoPoint>> clipped;
            std::vector<bool> clipped_outer;
            if (needs_clip) {
              for (size_t ri = 0; ri < f.parts.size(); ++ri) {
                const bool outer =
                    ri >= f.ring_outer.size() || f.ring_outer[ri];
                if (f.type == VectorGeometryType::kArea) {
                  std::vector<GeoPoint> ring;
                  ClipRing(tile_box, f.parts[ri], &ring);
                  if (ring.empty()) {
                    // An OUTER ring that clips away is one polygon fewer out
                    // of this multipolygon, so it is counted here rather than
                    // by the whole-feature test below; a hole that clips away
                    // is simply not a hole in this tile.
                    if (outer) ++im.last_clipped_away;
                    continue;
                  }
                  clipped.push_back(std::move(ring));
                  clipped_outer.push_back(outer);
                } else {
                  // A line may come back as several runs, so the ring flags
                  // are re-sized to match rather than pushed one per part.
                  ClipPolyline(tile_box, f.parts[ri], &clipped);
                  clipped_outer.resize(clipped.size(), true);
                }
              }
              if (clipped.empty()) {
                // Areas already counted their lost outer rings above.
                if (f.type != VectorGeometryType::kArea)
                  ++im.last_clipped_away;
                continue;
              }
              ++im.last_clipped;
            }
            const std::vector<std::vector<GeoPoint>>& parts =
                needs_clip ? clipped : f.parts;
            const std::vector<bool>& ring_outer =
                needs_clip ? clipped_outer : f.ring_outer;

            if (f.type != VectorGeometryType::kArea) {
              VectorFeature v;
              v.type = f.type;
              v.style_key = style_key;
              v.layer = layer.name;
              v.parts = parts;
              v.bounds = f.bounds;
              v.attributes = f.tags;
              v.ref = ref;
              if (needs_clip) {
                // The stored bounds describe the UNclipped geometry, and a
                // pick index built from a box that extends past the ink hits
                // on empty map. Recompute from what is actually emitted.
                bool first = true;
                for (const auto& part : v.parts)
                  for (const GeoPoint& p : part) {
                    if (first) {
                      v.bounds.ll = v.bounds.ur = p;
                      first = false;
                      continue;
                    }
                    v.bounds.ll.lat = std::min(v.bounds.ll.lat, p.lat);
                    v.bounds.ur.lat = std::max(v.bounds.ur.lat, p.lat);
                    v.bounds.ll.lon = std::min(v.bounds.ll.lon, p.lon);
                    v.bounds.ur.lon = std::max(v.bounds.ur.lon, p.lon);
                  }
                // The whole feature met the query box; the part of it that is
                // THIS tile's may not. Re-test, or the seam contract "every
                // feature returned overlaps the query" breaks for exactly the
                // features clipping touched.
                if (!v.bounds.Intersects(q.area)) {
                  --im.last_clipped;
                  ++im.last_clipped_away;
                  continue;
                }
              }
              out->push_back(std::move(v));
              continue;
            }

            // A POLYGON geometry may be a MULTIpolygon; the seam's contract
            // is one outer ring plus its holes, so emit one VectorFeature per
            // outer ring. They share a FeatureRef on purpose — identify names
            // the feature, not the ring.
            //
            // Bounds are computed PER EMITTED POLYGON, not once for the whole
            // multipolygon: two islands would otherwise each claim a box
            // covering the water between them, and the pick index would hit
            // on open sea.
            VectorFeature v;
            bool open = false;
            auto emit = [&out, &q, &im, needs_clip](VectorFeature* poly) {
              bool first = true;
              for (const auto& part : poly->parts)
                for (const GeoPoint& p : part) {
                  if (first) {
                    poly->bounds.ll = poly->bounds.ur = p;
                    first = false;
                    continue;
                  }
                  poly->bounds.ll.lat = std::min(poly->bounds.ll.lat, p.lat);
                  poly->bounds.ur.lat = std::max(poly->bounds.ur.lat, p.lat);
                  poly->bounds.ll.lon = std::min(poly->bounds.ll.lon, p.lon);
                  poly->bounds.ur.lon = std::max(poly->bounds.ur.lon, p.lon);
                }
              if (first) return;
              // Same re-test as the line branch above: what survived clipping
              // has to still meet the query box.
              if (needs_clip && !poly->bounds.Intersects(q.area)) {
                ++im.last_clipped_away;
                return;
              }
              out->push_back(std::move(*poly));
            };
            for (size_t ri = 0; ri < parts.size(); ++ri) {
              const bool outer = ri < ring_outer.size() && ring_outer[ri];
              if (outer) {
                if (open) emit(&v);
                v = VectorFeature{};
                v.type = VectorGeometryType::kArea;
                v.style_key = style_key;
                v.layer = layer.name;
                v.attributes = f.tags;
                v.ref = ref;
                open = true;
              } else if (!open) {
                continue;  // hole with no outer ring; MvtTile counted it
              }
              v.parts.push_back(parts[ri]);
            }
            if (open) emit(&v);
          }
        }
      }
    }
  }
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Describe
// ---------------------------------------------------------------------------

Status OsmVectorSource::Describe(const FeatureRef& ref,
                                 FeatureDescription* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "Describe: null out");
  if (!IsOpen()) return Status::Error(kIoError, "Describe: source not open");
  *out = FeatureDescription{};
  out->ref = ref;

  Impl& im = *impl_;
  if (ref.layer < 0 || ref.layer >= static_cast<int32_t>(im.layer_names.size()))
    return Status::Error(kNotFound, "Describe: no such layer");
  if (ref.tile < 0 || ref.tile >= static_cast<int32_t>(im.tile_ids.size()))
    return Status::Error(kNotFound, "Describe: no such tile");

  const webmerc::TileId id = im.tile_ids[ref.tile];
  const std::string& layer_name = im.layer_names[ref.layer];

  const MvtTile* tile = nullptr;
  const Status s = im.GetTile(id, &tile);
  if (!s.ok()) return s;
  if (tile == nullptr) return Status::Error(kNotFound, "Describe: tile gone");

  const MvtLayer* layer = tile->Layer(layer_name);
  if (layer == nullptr)
    return Status::Error(kNotFound, "Describe: layer not in this tile");
  if (ref.feature < 0 ||
      ref.feature >= static_cast<int32_t>(layer->features.size()))
    return Status::Error(kNotFound, "Describe: no such feature");

  const MvtFeature& f = layer->features[ref.feature];
  const std::string style_key = im.StyleKeyFor(f, layer_name);

  out->title = TitleFor(f, style_key, layer_name);
  out->class_name = style_key;
  out->layer_name = layer_name;
  out->source_note = BaseName(im.file.path()) + " " + std::to_string(id.z) +
                     "/" + std::to_string(id.x) + "/" + std::to_string(id.y);

  out->attributes.reserve(f.tags.size());
  for (const auto& kv : f.tags) {
    FeatureAttribute a;
    a.code = kv.first;
    const char* alias = AliasFor(kv.first);
    a.name = alias != nullptr ? alias : kv.first;
    a.raw = kv.second;
    // OSM tag values are already words; there is no coded-value dictionary to
    // decode through, so display IS raw. Documented rather than left to look
    // like an oversight.
    a.display = kv.second;
    out->attributes.push_back(std::move(a));
  }
  return Status::Ok();
}

}  // namespace fv
