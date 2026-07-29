// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// TilePack — see fvkit/store/tile_pack.h.

#include "fvkit/store/tile_pack.h"

#include <png.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/tools/png_io.h"

namespace fv {

namespace {

// ---- PNG blob encode/decode over the ported libpng ------------------------

void PngWriteToVec(png_structp png, png_bytep data, png_size_t len) {
  auto* v = (std::vector<unsigned char>*)png_get_io_ptr(png);
  v->insert(v->end(), data, data + len);
}
void PngFlushNoop(png_structp) {}

Status EncodePng(const PixelBuffer& b, std::vector<unsigned char>* out) {
  out->clear();
  png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info = png_create_info_struct(png);
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    return Status::Error(kInternal, "libpng encode failed");
  }
  png_set_write_fn(png, out, PngWriteToVec, PngFlushNoop);
  png_set_IHDR(png, info, b.Width(), b.Height(), 8, PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  for (int y = 0; y < b.Height(); ++y)
    png_write_row(png, const_cast<unsigned char*>(b.Row(y)));
  png_write_end(png, nullptr);
  png_destroy_write_struct(&png, &info);
  return Status::Ok();
}

// The decoder moved to fvkit/tools/png_io.h on its third consumer (here, the
// canvas golden reader, and the S-52 raster symbol sheet).

// ---- GeoPackage boilerplate ------------------------------------------------

const char kGpkgSchema[] =
    "PRAGMA application_id = 0x47504B47;"  // 'GPKG'
    "PRAGMA user_version = 10200;"
    "CREATE TABLE IF NOT EXISTS gpkg_spatial_ref_sys("
    "  srs_name TEXT NOT NULL, srs_id INTEGER PRIMARY KEY,"
    "  organization TEXT NOT NULL, organization_coordsys_id INTEGER NOT NULL,"
    "  definition TEXT NOT NULL, description TEXT);"
    "INSERT OR IGNORE INTO gpkg_spatial_ref_sys VALUES"
    "  ('WGS 84', 4326, 'EPSG', 4326,"
    "   'GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
    "SPHEROID[\"WGS 84\",6378137,298.257223563]],"
    "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]', NULL),"
    "  ('undefined cartesian', -1, 'NONE', -1, 'undefined', NULL),"
    "  ('undefined geographic', 0, 'NONE', 0, 'undefined', NULL);"
    "CREATE TABLE IF NOT EXISTS gpkg_contents("
    "  table_name TEXT PRIMARY KEY, data_type TEXT NOT NULL,"
    "  identifier TEXT UNIQUE, description TEXT DEFAULT '',"
    "  last_change DATETIME DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "  min_x DOUBLE, min_y DOUBLE, max_x DOUBLE, max_y DOUBLE,"
    "  srs_id INTEGER);"
    "CREATE TABLE IF NOT EXISTS gpkg_tile_matrix_set("
    "  table_name TEXT PRIMARY KEY, srs_id INTEGER NOT NULL,"
    "  min_x DOUBLE NOT NULL, min_y DOUBLE NOT NULL,"
    "  max_x DOUBLE NOT NULL, max_y DOUBLE NOT NULL);"
    "CREATE TABLE IF NOT EXISTS gpkg_tile_matrix("
    "  table_name TEXT NOT NULL, zoom_level INTEGER NOT NULL,"
    "  matrix_width INTEGER NOT NULL, matrix_height INTEGER NOT NULL,"
    "  tile_width INTEGER NOT NULL, tile_height INTEGER NOT NULL,"
    "  pixel_x_size DOUBLE NOT NULL, pixel_y_size DOUBLE NOT NULL,"
    "  PRIMARY KEY (table_name, zoom_level));";

}  // namespace

// ---------------------------------------------------------------------------
// TilePackWriter
// ---------------------------------------------------------------------------

Status TilePackWriter::Create(const std::string& path,
                              const std::string& table_name,
                              const GeoRect& bounds, int tile_size) {
  if (bounds.CrossesAntimeridian())
    return Status::Error(kUnsupported, "regional packs only (no AM crossing)");
  if (bounds.ll.lat >= bounds.ur.lat || bounds.ll.lon >= bounds.ur.lon)
    return Status::Error(kInvalidArg, "empty bounds");
  if (tile_size < 64 || tile_size > 4096)
    return Status::Error(kInvalidArg, "tile_size out of range");

  std::remove(path.c_str());
  Status s = db_.Open(path);
  if (!s.ok()) return s;
  s = db_.Exec(kGpkgSchema);
  if (!s.ok()) return s;

  table_ = table_name;
  bounds_ = bounds;
  tile_size_ = tile_size;
  // zoom 0: whole pack in one tile (larger geographic axis governs)
  double span_lat = bounds.ur.lat - bounds.ll.lat;
  double span_lon = bounds.ur.lon - bounds.ll.lon;
  base_dpp_ = std::max(span_lat, span_lon) / tile_size_;

  std::string mk = "CREATE TABLE \"" + table_ +
                   "\"(id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   " zoom_level INTEGER NOT NULL, tile_column INTEGER NOT NULL,"
                   " tile_row INTEGER NOT NULL, tile_data BLOB NOT NULL,"
                   " UNIQUE (zoom_level, tile_column, tile_row));";
  s = db_.Exec(mk.c_str());
  if (!s.ok()) return s;

  detail::SqliteStmt tms;
  s = tms.Prepare(db_, "INSERT INTO gpkg_tile_matrix_set VALUES(?,4326,?,?,?,?)");
  if (!s.ok()) return s;
  tms.BindText(1, table_);
  tms.BindDouble(2, bounds.ll.lon);
  tms.BindDouble(3, bounds.ll.lat);
  tms.BindDouble(4, bounds.ur.lon);
  tms.BindDouble(5, bounds.ur.lat);
  tms.Step(&s);
  return s;
}

double TilePackWriter::LevelDpp(int zoom) const {
  return base_dpp_ / std::pow(2.0, zoom);
}

Status TilePackWriter::WriteLevel(MapEngine& engine, int zoom,
                                  int64_t series_id, int* tiles_written) {
  if (tiles_written != nullptr) *tiles_written = 0;
  if (!db_.IsOpen()) return Status::Error(kInvalidArg, "Create first");
  if (zoom < 0 || zoom > 24) return Status::Error(kInvalidArg, "bad zoom");

  const double dpp = LevelDpp(zoom);
  const double span_lat = bounds_.ur.lat - bounds_.ll.lat;
  const double span_lon = bounds_.ur.lon - bounds_.ll.lon;
  const int mw = (int)std::ceil(span_lon / (dpp * tile_size_));
  const int mh = (int)std::ceil(span_lat / (dpp * tile_size_));

  Status s = engine.SetSurfaceDimensions(tile_size_, tile_size_);
  if (!s.ok()) return s;
  s = engine.SetResolution(dpp, dpp);
  if (!s.ok()) return s;

  detail::SqliteStmt ins;
  s = ins.Prepare(db_, ("INSERT OR REPLACE INTO \"" + table_ +
                        "\"(zoom_level, tile_column, tile_row, tile_data) "
                        "VALUES(?,?,?,?)")
                           .c_str());
  if (!s.ok()) return s;
  s = db_.Exec("BEGIN");
  if (!s.ok()) return s;

  const FvColor bg{0, 0, 0, 0};  // transparent background -> skippable tiles
  CpuCanvas canvas(tile_size_, tile_size_);
  int written = 0;
  for (int row = 0; row < mh && s.ok(); ++row) {
    for (int col = 0; col < mw && s.ok(); ++col) {
      // tile (0,0) = pack NW corner (GeoPackage convention)
      double top = bounds_.ur.lat - row * tile_size_ * dpp;
      double left = bounds_.ll.lon + col * tile_size_ * dpp;
      GeoPoint center{top - (tile_size_ / 2.0) * dpp,
                      left + (tile_size_ / 2.0) * dpp};
      s = engine.SetCenter(center);
      if (!s.ok()) break;
      canvas.Clear(bg);
      int drawn = 0;
      s = engine.RenderBaseMap(canvas, series_id, {}, &drawn);
      if (!s.ok()) break;
      if (drawn == 0) continue;  // nothing intersected: skip empty tile

      std::vector<unsigned char> blob;
      s = EncodePng(canvas.Buffer(), &blob);
      if (!s.ok()) break;
      ins.Reset();
      ins.BindInt64(1, zoom);
      ins.BindInt64(2, col);
      ins.BindInt64(3, row);
      ins.BindBlob(4, blob.data(), blob.size());
      ins.Step(&s);
      if (s.ok()) ++written;
    }
  }
  if (!s.ok()) {
    db_.Exec("ROLLBACK");
    return s;
  }

  // per-level tile-matrix row
  detail::SqliteStmt tm;
  s = tm.Prepare(db_,
                 "INSERT OR REPLACE INTO gpkg_tile_matrix VALUES(?,?,?,?,?,?,?,?)");
  if (s.ok()) {
    tm.BindText(1, table_);
    tm.BindInt64(2, zoom);
    tm.BindInt64(3, mw);
    tm.BindInt64(4, mh);
    tm.BindInt64(5, tile_size_);
    tm.BindInt64(6, tile_size_);
    tm.BindDouble(7, dpp);
    tm.BindDouble(8, dpp);
    tm.Step(&s);
  }
  if (!s.ok()) {
    db_.Exec("ROLLBACK");
    return s;
  }
  s = db_.Exec("COMMIT");
  if (s.ok() && tiles_written != nullptr) *tiles_written = written;
  return s;
}

// ---------------------------------------------------------------------------
// Level metadata (shared)
// ---------------------------------------------------------------------------

Status ReadTilePackLevels(const std::string& gpkg_path, std::string* table,
                          GeoRect* bounds, std::vector<TileLevelInfo>* levels) {
  detail::SqliteDb db;
  Status s = db.Open(gpkg_path);
  if (!s.ok()) return s;

  detail::SqliteStmt c;
  s = c.Prepare(db,
                "SELECT table_name, min_x, min_y, max_x, max_y FROM "
                "gpkg_contents WHERE data_type='tiles' LIMIT 1");
  if (!s.ok()) return s;
  if (!c.Step(&s))
    return s.ok() ? Status::Error(kUnsupported, "no tiles table in " + gpkg_path)
                  : s;
  *table = c.ColText(0);
  bounds->ll = {c.ColDouble(2), c.ColDouble(1)};
  bounds->ur = {c.ColDouble(4), c.ColDouble(3)};

  levels->clear();
  detail::SqliteStmt m;
  s = m.Prepare(db,
                "SELECT zoom_level, matrix_width, matrix_height, tile_width, "
                "pixel_x_size, pixel_y_size FROM gpkg_tile_matrix WHERE "
                "table_name=? ORDER BY zoom_level");
  if (!s.ok()) return s;
  m.BindText(1, *table);
  while (m.Step(&s)) {
    TileLevelInfo li;
    li.zoom = (int)m.ColInt64(0);
    li.matrix_width = (int)m.ColInt64(1);
    li.matrix_height = (int)m.ColInt64(2);
    // tile_width read for validation; dpp from pixel sizes
    li.dpp_lon = m.ColDouble(4);
    li.dpp_lat = m.ColDouble(5);
    levels->push_back(li);
  }
  if (s.ok() && levels->empty())
    return Status::Error(kUnsupported, "no tile matrix rows in " + gpkg_path);
  return s;
}

// ---------------------------------------------------------------------------
// TilePackRasterSource — one level as a flat raster
// ---------------------------------------------------------------------------

struct TilePackRasterSource::Impl {
  detail::SqliteDb db;
  detail::SqliteStmt fetch;  // prepared tile fetch
  std::string table;
  GeoRect bounds;
  TileLevelInfo level;
  int tile_size = 256;
  bool is_open = false;
};

TilePackRasterSource::TilePackRasterSource() : impl_(new Impl) {}
TilePackRasterSource::~TilePackRasterSource() = default;

Status TilePackRasterSource::Open(const std::string& path) {
  if (impl_->is_open) return Status::Error(kInvalidArg, "already open");
  std::string file = path;
  int want_zoom = -1;  // -1 = deepest
  size_t hash = path.rfind("#z=");
  if (hash != std::string::npos) {
    file = path.substr(0, hash);
    want_zoom = std::atoi(path.c_str() + hash + 3);
  }

  std::vector<TileLevelInfo> levels;
  Status s = ReadTilePackLevels(file, &impl_->table, &impl_->bounds, &levels);
  if (!s.ok()) return s;
  impl_->level = levels.back();  // deepest
  if (want_zoom >= 0) {
    bool found = false;
    for (const auto& li : levels)
      if (li.zoom == want_zoom) {
        impl_->level = li;
        found = true;
      }
    if (!found)
      return Status::Error(kNotFound,
                           "zoom " + std::to_string(want_zoom) + " not in pack");
  }

  s = impl_->db.Open(file);
  if (!s.ok()) return s;
  // tile size from the matrix table (validated constant per pack)
  detail::SqliteStmt ts;
  s = ts.Prepare(impl_->db,
                 "SELECT tile_width FROM gpkg_tile_matrix WHERE table_name=? "
                 "AND zoom_level=?");
  if (!s.ok()) return s;
  ts.BindText(1, impl_->table);
  ts.BindInt64(2, impl_->level.zoom);
  if (ts.Step(&s)) impl_->tile_size = (int)ts.ColInt64(0);

  s = impl_->fetch.Prepare(
      impl_->db, ("SELECT tile_data FROM \"" + impl_->table +
                  "\" WHERE zoom_level=? AND tile_column=? AND tile_row=?")
                     .c_str());
  if (!s.ok()) return s;
  impl_->is_open = true;
  return Status::Ok();
}

GeoRect TilePackRasterSource::Bounds() const {
  return impl_->is_open ? impl_->bounds : GeoRect{};
}

Status TilePackRasterSource::Info(ImageInfo* info) const {
  if (info == nullptr) return Status::Error(kInvalidArg, "info is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  info->size = {impl_->level.matrix_width * impl_->tile_size,
                impl_->level.matrix_height * impl_->tile_size};
  info->bounds = impl_->bounds;
  return Status::Ok();
}

Status TilePackRasterSource::ReadBlock(const PixelRect& r, PixelBuffer* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "out is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  ImageInfo info;
  Info(&info);
  if (r.width <= 0 || r.height <= 0 || r.x < 0 || r.y < 0 ||
      r.x + r.width > info.size.width || r.y + r.height > info.size.height)
    return Status::Error(kInvalidArg, "block rect outside level");

  *out = PixelBuffer(r.width, r.height);  // zero-filled = transparent
  const int T = impl_->tile_size;
  Status s;
  for (int tr = r.y / T; tr <= (r.y + r.height - 1) / T; ++tr) {
    for (int tc = r.x / T; tc <= (r.x + r.width - 1) / T; ++tc) {
      impl_->fetch.Reset();
      impl_->fetch.BindInt64(1, impl_->level.zoom);
      impl_->fetch.BindInt64(2, tc);
      impl_->fetch.BindInt64(3, tr);
      if (!impl_->fetch.Step(&s)) {
        if (!s.ok()) return s;
        continue;  // absent tile = transparent
      }
      PixelBuffer tile;
      s = DecodePng(impl_->fetch.ColBlob(0), (size_t)impl_->fetch.ColBytes(0),
                    &tile);
      if (!s.ok()) return s;
      // copy the intersecting window
      int x0 = std::max(r.x, tc * T), x1 = std::min(r.x + r.width, (tc + 1) * T);
      int y0 = std::max(r.y, tr * T), y1 = std::min(r.y + r.height, (tr + 1) * T);
      for (int y = y0; y < y1; ++y)
        std::memcpy(out->Row(y - r.y) + 4 * (x0 - r.x),
                    tile.Row(y - tr * T) + 4 * (x0 - tc * T),
                    (size_t)(x1 - x0) * 4);
    }
  }
  return Status::Ok();
}

Status TilePackRasterSource::PixelToGeo(double px, double py,
                                        GeoPoint* p) const {
  if (p == nullptr) return Status::Error(kInvalidArg, "p is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  p->lon = impl_->bounds.ll.lon + px * impl_->level.dpp_lon;
  p->lat = impl_->bounds.ur.lat - py * impl_->level.dpp_lat;
  return Status::Ok();
}

Status TilePackRasterSource::GeoToPixel(const GeoPoint& p, double* px,
                                        double* py) const {
  if (px == nullptr || py == nullptr)
    return Status::Error(kInvalidArg, "px/py is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  *px = (p.lon - impl_->bounds.ll.lon) / impl_->level.dpp_lon;
  *py = (impl_->bounds.ur.lat - p.lat) / impl_->level.dpp_lat;
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// TilePackEnumerator — one FrameInfo per pyramid level ("<file>#z=<k>")
// ---------------------------------------------------------------------------

Status TilePackEnumerator::Begin(const std::string& dir) {
  frames_.clear();
  next_ = 0;
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::exists(dir, ec) || ec)
    return Status::Error(kNotFound, "pack dir not found: " + dir);
  fs::recursive_directory_iterator it(
      dir, fs::directory_options::skip_permission_denied, ec), end;
  if (ec) return Status::Error(kNotFound, "pack dir not readable: " + dir);
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec) || ec) continue;
    if (it->path().extension() != ".gpkg") continue;

    std::string table;
    GeoRect bounds;
    std::vector<TileLevelInfo> levels;
    if (!ReadTilePackLevels(it->path().string(), &table, &bounds, &levels).ok())
      continue;  // not a tile pack; skip
    for (const auto& li : levels) {
      FrameInfo f;
      f.path = it->path().string() + "#z=" + std::to_string(li.zoom);
      f.bounds = bounds;
      f.series_key = table + "/z" + std::to_string(li.zoom);
      // meters/pixel -> normalized scale denominator via the catalog's rule
      f.scale = li.dpp_lat * 111319.49;  // deg -> meters at equator
      f.scale_units = 4;                 // MAP_SCALE_METERS
      f.size_bytes = (int64_t)it->file_size(ec);
      if (ec) f.size_bytes = 0;
      frames_.push_back(std::move(f));
    }
  }
  std::sort(frames_.begin(), frames_.end(),
            [](const FrameInfo& a, const FrameInfo& b) { return a.path < b.path; });
  return Status::Ok();
}

bool TilePackEnumerator::Next(FrameInfo* info) {
  if (next_ >= frames_.size()) return false;
  *info = frames_[next_++];
  return true;
}

Status TilePackWriter::Close() {
  if (!db_.IsOpen()) return Status::Ok();
  detail::SqliteStmt c;
  Status s = c.Prepare(db_,
                       "INSERT OR REPLACE INTO gpkg_contents(table_name, "
                       "data_type, identifier, min_x, min_y, max_x, max_y, "
                       "srs_id) VALUES(?, 'tiles', ?, ?, ?, ?, ?, 4326)");
  if (!s.ok()) return s;
  c.BindText(1, table_);
  c.BindText(2, table_);
  c.BindDouble(3, bounds_.ll.lon);
  c.BindDouble(4, bounds_.ll.lat);
  c.BindDouble(5, bounds_.ur.lon);
  c.BindDouble(6, bounds_.ur.lat);
  c.Step(&s);
  db_.Close();
  return s;
}

}  // namespace fv
