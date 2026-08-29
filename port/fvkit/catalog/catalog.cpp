// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// FvKit L2 catalog implementation — see fvkit/catalog/catalog.h.

#include "fvkit/catalog/catalog.h"

#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <tuple>

#include "fv_map_enums.h"                 // MapScaleUnitsEnum (COM ABI)
#include "fv_map_scale_util.h"            // fv::MapScaleUtil (denominator calc)
#include "fv_map_series_string_converter.h"  // the map-type label, FalconView's
#include "fvkit/formats/registry.h"

namespace fv {

namespace {

const char kSchema[] =
    "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT);"
    "INSERT OR IGNORE INTO meta VALUES('schema_version','2');"
    "CREATE TABLE IF NOT EXISTS data_sources("
    "  id INTEGER PRIMARY KEY, path TEXT NOT NULL, format TEXT NOT NULL,"
    "  priority INTEGER DEFAULT 0, UNIQUE(path, format));"
    "CREATE TABLE IF NOT EXISTS map_series("
    "  id INTEGER PRIMARY KEY, format TEXT NOT NULL, series_key TEXT NOT NULL,"
    "  scale REAL, scale_units INTEGER, scale_denom REAL,"
    // Schema 2: the scale is PART of the identity, as it is in FalconView's
    // tblMapSeries (see catalog.h). Two GeoTIFF sheets both called "Color"
    // at 1 m and at 50 m are two map series, not one.
    "  UNIQUE(format, series_key, scale, scale_units));"
    "CREATE TABLE IF NOT EXISTS coverage("
    "  id INTEGER PRIMARY KEY, data_source_id INTEGER NOT NULL,"
    "  series_id INTEGER NOT NULL, path TEXT NOT NULL,"
    "  ll_lat REAL, ll_lon REAL, ur_lat REAL, ur_lon REAL,"
    "  size_bytes INTEGER);"
    "CREATE INDEX IF NOT EXISTS coverage_source ON coverage(data_source_id);"
    "CREATE VIRTUAL TABLE IF NOT EXISTS coverage_rtree"
    "  USING rtree(id, min_lon, max_lon, min_lat, max_lat);";

// Normalized scale denominator for BestSeriesForScale. DENOMINATOR passes
// through; ground-resolution units go through the ported MapScaleUtil (same
// math the Windows product used); everything else is "not comparable".
double NormalizedScaleDenom(double scale, int units) {
  if (scale <= 0) return 0;
  if (units == MAP_SCALE_DENOMINATOR) return scale;
  double meters;
  switch (units) {
    case MAP_SCALE_METERS:    meters = scale; break;
    case MAP_SCALE_KILOMETER: meters = scale * 1000.0; break;
    default:                  return 0;
  }
  double denom = 0;
  MapScaleUtil util;
  if (util.ResolutionToScale(meters, MAP_SCALE_METERS, &denom) != 0) return 0;
  return denom;
}

// The map-type label FalconView shows for a (scale, series) pair, through the
// ported CMapSeriesStringConverter: FORMAT_SERIES_SCALE is its "GNC 1:5 M"
// spelling, which keeps the series_key at the front of a menu. Products whose
// scale is not a number (DTED, VPF, OSM) name themselves and nothing else --
// asking the converter would get "Invalid Scale" back.
std::string SeriesDisplayName(const std::string& series_key, double scale,
                              int units) {
  if (scale <= 0) return series_key;
  std::wstring scale_str = MapSeriesStringConverter::ToString(
      scale, static_cast<MapScaleUnitsEnum>(units));
  std::string narrow;
  narrow.reserve(scale_str.size());
  for (wchar_t c : scale_str)
    narrow += (c >= 0 && c < 128) ? static_cast<char>(c) : '?';
  if (series_key.empty()) return narrow;
  return series_key + " " + narrow;
}

}  // namespace

Catalog::Catalog() = default;
Catalog::~Catalog() = default;

Status Catalog::Open(const std::string& db_path) {
  needs_rescan_ = false;
  Status s = db_.Open(db_path);
  if (!s.ok()) return s;

  // Migration. The catalog is a cache, so an older schema is REBUILT rather
  // than converted: schema 1 keyed a series on (format, series_key) alone and
  // the rows it produced cannot be split apart after the fact -- the frames'
  // own scales were never stored. Data sources survive; their coverage does
  // not, and NeedsRescan() tells the caller to run Scan() again.
  int version = 0;
  {
    detail::SqliteStmt q;
    Status qs = q.Prepare(
        db_, "SELECT value FROM meta WHERE key='schema_version'");
    // A brand-new file has no meta table at all: that is version 0, not an
    // error, so a failed prepare here is not reported.
    if (qs.ok() && q.Step(&qs)) version = atoi(q.ColText(0).c_str());
  }
  if (version > 0 && version < 2) {
    s = db_.Exec(
        "DROP TABLE IF EXISTS coverage_rtree;"
        "DROP TABLE IF EXISTS coverage;"
        "DROP TABLE IF EXISTS map_series;");
    if (!s.ok()) return s;
    needs_rescan_ = true;
  }

  s = db_.Exec(kSchema);
  if (!s.ok()) return s;
  if (needs_rescan_)
    s = db_.Exec("UPDATE meta SET value='2' WHERE key='schema_version'");
  return s;
}

Status Catalog::AddDataSource(const std::string& path,
                              const std::string& format_key, int priority,
                              int64_t* data_source_id) {
  if (data_source_id == nullptr)
    return Status::Error(kInvalidArg, "data_source_id is null");
  if (!db_.IsOpen()) return Status::Error(kInvalidArg, "catalog not open");

  detail::SqliteStmt ins;
  Status s = ins.Prepare(db_,
                         "INSERT OR IGNORE INTO data_sources(path, format, "
                         "priority) VALUES(?,?,?)");
  if (!s.ok()) return s;
  ins.BindText(1, path);
  ins.BindText(2, format_key);
  ins.BindInt64(3, priority);
  ins.Step(&s);
  if (!s.ok()) return s;

  detail::SqliteStmt sel;
  s = sel.Prepare(db_, "SELECT id FROM data_sources WHERE path=? AND format=?");
  if (!s.ok()) return s;
  sel.BindText(1, path);
  sel.BindText(2, format_key);
  if (!sel.Step(&s)) {
    return s.ok() ? Status::Error(kInternal, "data source not inserted") : s;
  }
  *data_source_id = sel.ColInt64(0);
  return Status::Ok();
}

Status Catalog::ResolveSeries(const std::string& format, const FrameInfo& f,
                              int64_t* series_id) {
  detail::SqliteStmt ins;
  Status s = ins.Prepare(db_,
                         "INSERT OR IGNORE INTO map_series(format, series_key, "
                         "scale, scale_units, scale_denom) VALUES(?,?,?,?,?)");
  if (!s.ok()) return s;
  ins.BindText(1, format);
  ins.BindText(2, f.series_key);
  ins.BindDouble(3, f.scale);
  ins.BindInt64(4, f.scale_units);
  ins.BindDouble(5, NormalizedScaleDenom(f.scale, f.scale_units));
  ins.Step(&s);
  if (!s.ok()) return s;

  detail::SqliteStmt sel;
  s = sel.Prepare(db_,
                  "SELECT id FROM map_series WHERE format=? AND series_key=? "
                  "AND scale=? AND scale_units=?");
  if (!s.ok()) return s;
  sel.BindText(1, format);
  sel.BindText(2, f.series_key);
  sel.BindDouble(3, f.scale);
  sel.BindInt64(4, f.scale_units);
  if (!sel.Step(&s))
    return s.ok() ? Status::Error(kInternal, "series not inserted") : s;
  *series_id = sel.ColInt64(0);
  return Status::Ok();
}

Status Catalog::InsertCoverage(int64_t data_source_id, int64_t series_id,
                               const FrameInfo& f) {
  detail::SqliteStmt ins;
  Status s = ins.Prepare(
      db_,
      "INSERT INTO coverage(data_source_id, series_id, path, ll_lat, ll_lon, "
      "ur_lat, ur_lon, size_bytes) VALUES(?,?,?,?,?,?,?,?)");
  if (!s.ok()) return s;
  ins.BindInt64(1, data_source_id);
  ins.BindInt64(2, series_id);
  ins.BindText(3, f.path);
  ins.BindDouble(4, f.bounds.ll.lat);
  ins.BindDouble(5, f.bounds.ll.lon);
  ins.BindDouble(6, f.bounds.ur.lat);
  ins.BindDouble(7, f.bounds.ur.lon);
  ins.BindInt64(8, f.size_bytes);
  ins.Step(&s);
  if (!s.ok()) return s;
  int64_t cov_id = db_.LastInsertRowId();

  // R-tree entries: 1 box normally, 2 for antimeridian crossers (D2).
  detail::SqliteStmt rt;
  s = rt.Prepare(db_, "INSERT INTO coverage_rtree VALUES(?,?,?,?,?)");
  if (!s.ok()) return s;
  auto pieces = f.bounds.SplitAtAntimeridian();
  for (size_t i = 0; i < pieces.size(); ++i) {
    rt.Reset();
    rt.BindInt64(1, cov_id * 2 + (int64_t)i);
    rt.BindDouble(2, pieces[i].ll.lon);
    rt.BindDouble(3, pieces[i].ur.lon);
    rt.BindDouble(4, pieces[i].ll.lat);
    rt.BindDouble(5, pieces[i].ur.lat);
    rt.Step(&s);
    if (!s.ok()) return s;
  }
  return Status::Ok();
}

Status Catalog::Scan(int64_t data_source_id, int* frames_added) {
  if (frames_added != nullptr) *frames_added = 0;
  if (!db_.IsOpen()) return Status::Error(kInvalidArg, "catalog not open");

  detail::SqliteStmt sel;
  Status s = sel.Prepare(db_,
                         "SELECT path, format FROM data_sources WHERE id=?");
  if (!s.ok()) return s;
  sel.BindInt64(1, data_source_id);
  if (!sel.Step(&s))
    return s.ok() ? Status::Error(kNotFound, "unknown data source id") : s;
  std::string src_path = sel.ColText(0);
  std::string format = sel.ColText(1);

  const FormatFactories* fmt = FindFormat(format);
  if (fmt == nullptr || !fmt->make_enumerator)
    return Status::Error(kUnsupported, "no enumerator registered: " + format);

  auto e = fmt->make_enumerator();
  s = e->Begin(src_path);
  if (!s.ok()) return s;

  // replace this source's coverage atomically
  s = db_.Exec("BEGIN");
  if (!s.ok()) return s;
  {
    detail::SqliteStmt del;
    s = del.Prepare(db_,
                    "DELETE FROM coverage_rtree WHERE id/2 IN "
                    "(SELECT id FROM coverage WHERE data_source_id=?)");
    if (s.ok()) {
      del.BindInt64(1, data_source_id);
      del.Step(&s);
    }
  }
  if (s.ok()) {
    detail::SqliteStmt del2;
    s = del2.Prepare(db_, "DELETE FROM coverage WHERE data_source_id=?");
    if (s.ok()) {
      del2.BindInt64(1, data_source_id);
      del2.Step(&s);
    }
  }

  // Keyed on the SERIES IDENTITY and not on the key alone -- a cache keyed
  // more loosely than the table it fronts would put the second scale's frames
  // in the first scale's series however the schema is written.
  std::map<std::tuple<std::string, double, int>, int64_t> series_cache;
  FrameInfo f;
  int added = 0;
  while (s.ok() && e->Next(&f)) {
    auto key = std::make_tuple(f.series_key, f.scale, f.scale_units);
    auto it = series_cache.find(key);
    int64_t series_id;
    if (it != series_cache.end()) {
      series_id = it->second;
    } else {
      s = ResolveSeries(format, f, &series_id);
      if (!s.ok()) break;
      series_cache[key] = series_id;
    }
    s = InsertCoverage(data_source_id, series_id, f);
    if (s.ok()) ++added;
  }

  if (!s.ok()) {
    db_.Exec("ROLLBACK");
    return s;
  }
  s = db_.Exec("COMMIT");
  if (s.ok() && frames_added != nullptr) *frames_added = added;
  return s;
}

Status Catalog::Series(std::vector<SeriesRow>* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "out is null");
  out->clear();
  detail::SqliteStmt sel;
  Status s = sel.Prepare(db_,
                         "SELECT id, format, series_key, scale, scale_units, "
                         "scale_denom FROM map_series ORDER BY id");
  if (!s.ok()) return s;
  while (sel.Step(&s)) {
    SeriesRow r;
    r.id = sel.ColInt64(0);
    r.format = sel.ColText(1);
    r.series_key = sel.ColText(2);
    r.scale = sel.ColDouble(3);
    r.scale_units = (int)sel.ColInt64(4);
    r.scale_denom = sel.ColDouble(5);
    r.display_name = SeriesDisplayName(r.series_key, r.scale, r.scale_units);
    out->push_back(std::move(r));
  }
  return s;
}

Status Catalog::SelectByGeoRect(const GeoRect& rect,
                                std::vector<CoverageRow>* out,
                                int64_t series_id) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "out is null");
  out->clear();
  if (!db_.IsOpen()) return Status::Error(kInvalidArg, "catalog not open");

  // query R-tree per non-crossing piece, de-dupe by coverage id (D2)
  std::set<int64_t> ids;
  Status s;
  for (const GeoRect& piece : rect.SplitAtAntimeridian()) {
    detail::SqliteStmt q;
    s = q.Prepare(db_,
                  "SELECT id FROM coverage_rtree WHERE min_lon<=? AND "
                  "max_lon>=? AND min_lat<=? AND max_lat>=?");
    if (!s.ok()) return s;
    q.BindDouble(1, piece.ur.lon);
    q.BindDouble(2, piece.ll.lon);
    q.BindDouble(3, piece.ur.lat);
    q.BindDouble(4, piece.ll.lat);
    while (q.Step(&s)) ids.insert(q.ColInt64(0) / 2);
    if (!s.ok()) return s;
  }

  detail::SqliteStmt row;
  s = row.Prepare(db_,
                  "SELECT c.id, c.series_id, m.series_key, c.path, c.ll_lat, "
                  "c.ll_lon, c.ur_lat, c.ur_lon, c.size_bytes, m.format "
                  "FROM coverage c "
                  "JOIN map_series m ON m.id=c.series_id WHERE c.id=?");
  if (!s.ok()) return s;
  for (int64_t id : ids) {  // std::set: ascending id order
    row.Reset();
    row.BindInt64(1, id);
    if (!row.Step(&s)) {
      if (!s.ok()) return s;
      continue;
    }
    if (series_id != 0 && row.ColInt64(1) != series_id) continue;
    CoverageRow r;
    r.id = row.ColInt64(0);
    r.series_id = row.ColInt64(1);
    r.series_key = row.ColText(2);
    r.path = row.ColText(3);
    r.bounds = GeoRect{{row.ColDouble(4), row.ColDouble(5)},
                       {row.ColDouble(6), row.ColDouble(7)}};
    r.size_bytes = row.ColInt64(8);
    r.format = row.ColText(9);
    out->push_back(std::move(r));
  }
  return Status::Ok();
}

Status Catalog::BestSeriesForScale(double target_scale_denom,
                                   SeriesRow* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "out is null");
  if (target_scale_denom <= 0)
    return Status::Error(kInvalidArg, "target scale must be positive");
  std::vector<SeriesRow> all;
  Status s = Series(&all);
  if (!s.ok()) return s;

  const SeriesRow* best = nullptr;
  double best_dist = 0;
  for (const SeriesRow& r : all) {
    if (r.scale_denom <= 0) continue;  // not comparable (e.g. DTED)
    double d = std::fabs(std::log(r.scale_denom / target_scale_denom));
    if (best == nullptr || d < best_dist) {
      best = &r;
      best_dist = d;
    }
  }
  if (best == nullptr)
    return Status::Error(kNotFound, "no scale-comparable series in catalog");
  *out = *best;
  return Status::Ok();
}

Status Catalog::RemoveDataSource(int64_t data_source_id) {
  if (!db_.IsOpen()) return Status::Error(kInvalidArg, "catalog not open");
  Status s = db_.Exec("BEGIN");
  if (!s.ok()) return s;
  detail::SqliteStmt del_rt;
  s = del_rt.Prepare(db_,
                     "DELETE FROM coverage_rtree WHERE id/2 IN "
                     "(SELECT id FROM coverage WHERE data_source_id=?)");
  if (s.ok()) {
    del_rt.BindInt64(1, data_source_id);
    del_rt.Step(&s);
  }
  if (s.ok()) {
    detail::SqliteStmt del_cov;
    s = del_cov.Prepare(db_, "DELETE FROM coverage WHERE data_source_id=?");
    if (s.ok()) {
      del_cov.BindInt64(1, data_source_id);
      del_cov.Step(&s);
    }
  }
  if (s.ok()) {
    detail::SqliteStmt del_src;
    s = del_src.Prepare(db_, "DELETE FROM data_sources WHERE id=?");
    if (s.ok()) {
      del_src.BindInt64(1, data_source_id);
      del_src.Step(&s);
    }
  }
  if (!s.ok()) {
    db_.Exec("ROLLBACK");
    return s;
  }
  return db_.Exec("COMMIT");
}

}  // namespace fv
