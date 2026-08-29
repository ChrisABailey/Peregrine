// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_mbtiles.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include <nlohmann/json.hpp>

#include "fvkit/detail/sqlite.h"

namespace fv {
namespace {

// "-106.6494,24.02672,0.0,40.64636" -> a GeoRect. TileJSON writes
// west,south,east,north.
bool ParseBoundsValue(const std::string& text, GeoRect* out) {
  double v[4];
  size_t pos = 0;
  for (int i = 0; i < 4; ++i) {
    if (pos > text.size()) return false;
    size_t comma = text.find(',', pos);
    const std::string field =
        text.substr(pos, comma == std::string::npos ? std::string::npos
                                                    : comma - pos);
    if (field.empty()) return false;
    char* end = nullptr;
    v[i] = std::strtod(field.c_str(), &end);
    if (end == field.c_str()) return false;
    if (comma == std::string::npos) {
      if (i != 3) return false;
      pos = text.size() + 1;
    } else {
      pos = comma + 1;
    }
  }
  out->ll.lon = v[0];
  out->ll.lat = v[1];
  out->ur.lon = v[2];
  out->ur.lat = v[3];
  return true;
}

int ParseInt(const std::string& s, int fallback) {
  char* end = nullptr;
  const long v = std::strtol(s.c_str(), &end, 10);
  if (end == s.c_str()) return fallback;
  return static_cast<int>(v);
}

}  // namespace

// ---------------------------------------------------------------------------

struct MbtilesFile::Impl {
  std::string path;
  // mutable: the const accessors below (Bounds, ReadTile, ZoomExtentOf) read
  // through it, and sqlite's own API takes a non-const handle for a SELECT.
  mutable detail::SqliteDb db;
  std::map<std::string, std::string> metadata;

  std::string name, format, description, attribution, version, type;
  int min_zoom = 0;
  int max_zoom = 0;
  GeoPoint center;
  int center_zoom = 0;

  bool has_declared_bounds = false;
  GeoRect declared_bounds;

  std::vector<MbtilesLayerInfo> layers;
  std::vector<std::string> warnings;

  // Lazily measured from the pyramid; see the header on why the declared
  // bounds are not trusted.
  mutable bool pyramid_measured = false;
  mutable GeoRect pyramid_bounds;
  mutable int pyramid_zoom = -1;

  // Prepared once; ReadTile is on the interactive path.
  mutable detail::SqliteStmt tile_stmt;

  const std::string* Get(const std::string& key) const {
    auto it = metadata.find(key);
    return it == metadata.end() ? nullptr : &it->second;
  }
  std::string GetOr(const std::string& key, const char* fallback) const {
    const std::string* v = Get(key);
    return v != nullptr ? *v : std::string(fallback);
  }

  void MeasurePyramid() const;
};

void MbtilesFile::Impl::MeasurePyramid() const {
  pyramid_measured = true;
  pyramid_bounds = GeoRect{};
  pyramid_zoom = -1;

  detail::SqliteStmt st;
  if (!st.Prepare(db, "SELECT MAX(zoom_level) FROM tiles").ok())
    return;
  if (!st.Step()) return;
  const int z = static_cast<int>(st.ColInt64(0));
  st.Finalize();

  detail::SqliteStmt ex;
  if (!ex.Prepare(db,
                  "SELECT MIN(tile_column),MAX(tile_column),"
                  "MIN(tile_row),MAX(tile_row),COUNT(*) "
                  "FROM tiles WHERE zoom_level=?")
           .ok())
    return;
  ex.BindInt64(1, z);
  if (!ex.Step()) return;
  if (ex.ColInt64(4) == 0) return;

  const int min_x = static_cast<int>(ex.ColInt64(0));
  const int max_x = static_cast<int>(ex.ColInt64(1));
  const int min_row = static_cast<int>(ex.ColInt64(2));
  const int max_row = static_cast<int>(ex.ColInt64(3));

  // TMS rows count northward, so the LARGEST row is the NORTHERNMOST tile.
  const int north_y = webmerc::TmsRow(z, max_row);
  const int south_y = webmerc::TmsRow(z, min_row);

  pyramid_zoom = z;
  pyramid_bounds.ll.lon = webmerc::TileToLon(min_x, z);
  pyramid_bounds.ur.lon = webmerc::TileToLon(max_x + 1, z);
  pyramid_bounds.ur.lat = webmerc::TileToLat(north_y, z);
  pyramid_bounds.ll.lat = webmerc::TileToLat(south_y + 1, z);
}

// ---------------------------------------------------------------------------

MbtilesFile::MbtilesFile() : impl_(new Impl) {}
MbtilesFile::~MbtilesFile() = default;

Status MbtilesFile::Open(const std::string& path) {
  Close();
  Status s = impl_->db.OpenReadOnly(path);
  if (!s.ok()) return s;
  impl_->path = path;

  detail::SqliteStmt st;
  s = st.Prepare(impl_->db, "SELECT name,value FROM metadata");
  if (!s.ok()) {
    Close();
    return Status::Error(kIoError,
                         path + ": not an MBTiles file (no metadata table)");
  }
  while (st.Step(&s)) impl_->metadata[st.ColText(0)] = st.ColText(1);
  st.Finalize();
  if (!s.ok()) {
    Close();
    return s;
  }

  // The tiles table is what makes it an MBTiles rather than any old SQLite
  // file, so probe it before declaring success.
  s = impl_->tile_stmt.Prepare(
      impl_->db,
      "SELECT tile_data FROM tiles "
      "WHERE zoom_level=? AND tile_column=? AND tile_row=?");
  if (!s.ok()) {
    Close();
    return Status::Error(kIoError,
                         path + ": not an MBTiles file (no tiles table)");
  }

  impl_->name = impl_->GetOr("name", "");
  impl_->format = impl_->GetOr("format", "");
  impl_->description = impl_->GetOr("description", "");
  impl_->attribution = impl_->GetOr("attribution", "");
  impl_->version = impl_->GetOr("version", "");
  impl_->type = impl_->GetOr("type", "");
  impl_->min_zoom = ParseInt(impl_->GetOr("minzoom", "0"), 0);
  impl_->max_zoom = ParseInt(impl_->GetOr("maxzoom", "0"), 0);
  if (impl_->max_zoom < impl_->min_zoom) {
    impl_->warnings.push_back("maxzoom < minzoom; using minzoom for both");
    impl_->max_zoom = impl_->min_zoom;
  }

  if (const std::string* b = impl_->Get("bounds")) {
    if (ParseBoundsValue(*b, &impl_->declared_bounds)) {
      impl_->has_declared_bounds = true;
    } else {
      impl_->warnings.push_back("bounds metadata is not 4 numbers: " + *b);
    }
  }

  if (const std::string* c = impl_->Get("center")) {
    // "lon,lat,zoom"
    double lon = 0, lat = 0;
    int zoom = impl_->min_zoom;
    if (std::sscanf(c->c_str(), "%lf,%lf,%d", &lon, &lat, &zoom) >= 2) {
      impl_->center.lon = lon;
      impl_->center.lat = lat;
      impl_->center_zoom = zoom;
    } else {
      impl_->warnings.push_back("center metadata is not lon,lat[,zoom]: " + *c);
    }
  }

  // vector_layers, from the TileJSON `json` value.
  if (const std::string* j = impl_->Get("json")) {
    try {
      const nlohmann::json doc = nlohmann::json::parse(*j);
      const auto it = doc.find("vector_layers");
      if (it != doc.end() && it->is_array()) {
        for (const auto& l : *it) {
          MbtilesLayerInfo info;
          if (l.contains("id") && l["id"].is_string())
            info.id = l["id"].get<std::string>();
          if (l.contains("description") && l["description"].is_string())
            info.description = l["description"].get<std::string>();
          info.minzoom = l.value("minzoom", impl_->min_zoom);
          info.maxzoom = l.value("maxzoom", impl_->max_zoom);
          const auto f = l.find("fields");
          if (f != l.end() && f->is_object()) {
            for (auto kv = f->begin(); kv != f->end(); ++kv) {
              info.fields.emplace_back(
                  kv.key(),
                  kv.value().is_string() ? kv.value().get<std::string>() : "");
            }
          }
          if (!info.id.empty()) impl_->layers.push_back(std::move(info));
        }
      }
    } catch (const std::exception& e) {
      // A file with no usable `json` is still readable — the layer list can
      // be recovered from the tiles. Say so and carry on (fv::Settings' rule:
      // a wrong VALUE warns, only a broken FILE refuses).
      impl_->warnings.push_back(std::string("json metadata unparseable: ") +
                                e.what());
    }
  } else {
    impl_->warnings.push_back("no json metadata: layer inventory unavailable");
  }

  return Status::Ok();
}

void MbtilesFile::Close() {
  impl_.reset(new Impl);
}

bool MbtilesFile::IsOpen() const { return impl_->db.IsOpen(); }
const std::string& MbtilesFile::path() const { return impl_->path; }

const std::string& MbtilesFile::name() const { return impl_->name; }
const std::string& MbtilesFile::format() const { return impl_->format; }
const std::string& MbtilesFile::description() const {
  return impl_->description;
}
const std::string& MbtilesFile::attribution() const {
  return impl_->attribution;
}
const std::string& MbtilesFile::version() const { return impl_->version; }
const std::string& MbtilesFile::type() const { return impl_->type; }
int MbtilesFile::min_zoom() const { return impl_->min_zoom; }
int MbtilesFile::max_zoom() const { return impl_->max_zoom; }
GeoPoint MbtilesFile::center() const { return impl_->center; }
int MbtilesFile::center_zoom() const { return impl_->center_zoom; }

const std::string* MbtilesFile::Metadata(const std::string& key) const {
  return impl_->Get(key);
}

bool MbtilesFile::has_declared_bounds() const {
  return impl_->has_declared_bounds;
}
GeoRect MbtilesFile::declared_bounds() const { return impl_->declared_bounds; }

const std::vector<MbtilesLayerInfo>& MbtilesFile::layers() const {
  return impl_->layers;
}
const std::vector<std::string>& MbtilesFile::warnings() const {
  return impl_->warnings;
}

GeoRect MbtilesFile::Bounds() const {
  if (!IsOpen()) return GeoRect{};
  if (!impl_->pyramid_measured) impl_->MeasurePyramid();
  if (impl_->pyramid_zoom < 0) {
    // No tiles at all: fall back to whatever the file claims.
    return impl_->has_declared_bounds ? impl_->declared_bounds : GeoRect{};
  }
  return impl_->pyramid_bounds;
}

bool MbtilesFile::declared_bounds_disagree() const {
  if (!impl_->has_declared_bounds) return false;
  const GeoRect derived = Bounds();
  if (impl_->pyramid_zoom < 0) return false;
  // One tile's worth of slack at the zoom the pyramid was measured at: a
  // writer that rounds its declared box out to tile edges is not disagreeing.
  const double lon_tol = 360.0 / static_cast<double>(
                                    webmerc::TilesPerAxis(impl_->pyramid_zoom));
  const double lat_tol = lon_tol;  // generous near the equator, tighter is not
                                   // worth the Mercator arithmetic here
  const GeoRect& d = impl_->declared_bounds;
  return std::abs(d.ll.lon - derived.ll.lon) > lon_tol ||
         std::abs(d.ur.lon - derived.ur.lon) > lon_tol ||
         std::abs(d.ll.lat - derived.ll.lat) > lat_tol ||
         std::abs(d.ur.lat - derived.ur.lat) > lat_tol;
}

Status MbtilesFile::ReadTile(const webmerc::TileId& t,
                             std::string* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "ReadTile: null out");
  out->clear();
  if (!IsOpen()) return Status::Error(kIoError, "ReadTile: not open");
  if (t.z < 0 || t.z > 30)
    return Status::Error(kInvalidArg, "ReadTile: zoom out of range");
  const int64_t n = webmerc::TilesPerAxis(t.z);
  if (t.x < 0 || t.x >= n || t.y < 0 || t.y >= n)
    return Status::Error(kInvalidArg, "ReadTile: tile outside the grid");

  detail::SqliteStmt& st = impl_->tile_stmt;
  st.Reset();
  st.BindInt64(1, t.z);
  st.BindInt64(2, t.x);
  st.BindInt64(3, webmerc::TmsRow(t.z, t.y));

  Status s;
  if (!st.Step(&s)) {
    if (!s.ok()) return s;
    return Status::Error(kNotFound, "no tile " + std::to_string(t.z) + "/" +
                                        std::to_string(t.x) + "/" +
                                        std::to_string(t.y));
  }
  const void* blob = st.ColBlob(0);
  const int bytes = st.ColBytes(0);
  if (blob != nullptr && bytes > 0)
    out->assign(static_cast<const char*>(blob), static_cast<size_t>(bytes));
  // RESET ONCE THE BLOB IS COPIED, and not merely on the next call. A stepped
  // statement holds a READ TRANSACTION open for the life of the handle, and a
  // pack that has ever served one tile then refuses to be written to by
  // anything else — which is how fvnames (S3) discovered this, since building
  // an index means writing to the pack the scan is reading. The error a write
  // gets in that state is SQLITE_IOERR ("disk I/O error"), not SQLITE_BUSY,
  // so it does not look like a lock at all.
  st.Reset();
  return Status::Ok();
}

bool MbtilesFile::HasTile(const webmerc::TileId& t) const {
  std::string ignored;
  return ReadTile(t, &ignored).ok();
}

Status MbtilesFile::ZoomExtentOf(int z, ZoomExtent* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "ZoomExtentOf: null");
  *out = ZoomExtent{};
  if (!IsOpen()) return Status::Error(kIoError, "ZoomExtentOf: not open");

  detail::SqliteStmt st;
  Status s = st.Prepare(impl_->db,
                        "SELECT MIN(tile_column),MAX(tile_column),"
                        "MIN(tile_row),MAX(tile_row),COUNT(*) "
                        "FROM tiles WHERE zoom_level=?");
  if (!s.ok()) return s;
  st.BindInt64(1, z);
  if (!st.Step(&s)) return s.ok() ? Status::Ok() : s;
  out->count = st.ColInt64(4);
  if (out->count == 0) return Status::Ok();
  out->min_x = static_cast<int>(st.ColInt64(0));
  out->max_x = static_cast<int>(st.ColInt64(1));
  // TMS in, XYZ out: the flip also swaps min and max.
  out->min_y = webmerc::TmsRow(z, static_cast<int>(st.ColInt64(3)));
  out->max_y = webmerc::TmsRow(z, static_cast<int>(st.ColInt64(2)));
  return Status::Ok();
}

Status MbtilesFile::ZoomLevels(std::vector<int>* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "ZoomLevels: null");
  out->clear();
  if (!IsOpen()) return Status::Error(kIoError, "ZoomLevels: not open");
  detail::SqliteStmt st;
  Status s = st.Prepare(impl_->db,
                        "SELECT DISTINCT zoom_level FROM tiles "
                        "ORDER BY zoom_level");
  if (!s.ok()) return s;
  while (st.Step(&s)) out->push_back(static_cast<int>(st.ColInt64(0)));
  return s;
}

}  // namespace fv
