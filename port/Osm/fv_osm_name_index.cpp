// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_osm_name_index.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <memory>

#include "fv_osm_vector_source.h"
#include "fvkit/overlay/vector_map_overlay.h"

#include "fvkit/app/search.h"  // TextMatchQuality — the port's one match rule
#include "fvkit/detail/sqlite.h"

namespace fv {
namespace osm {
namespace {

// The columns, in one place, because three statements select them and one
// reader parses them.
const char* const kSelectColumns =
    "n.name, n.layer, n.class, n.lat, n.lon, n.min_lat, n.min_lon, "
    "n.max_lat, n.max_lon, n.min_zoom, n.tile_z, n.tile_x, n.tile_y, "
    "n.feature";

void ReadRow(const detail::SqliteStmt& st, NameRow* r) {
  r->name = st.ColText(0);
  r->layer = st.ColText(1);
  r->style_key = st.ColText(2);
  r->position.lat = st.ColDouble(3);
  r->position.lon = st.ColDouble(4);
  r->bounds.ll.lat = st.ColDouble(5);
  r->bounds.ll.lon = st.ColDouble(6);
  r->bounds.ur.lat = st.ColDouble(7);
  r->bounds.ur.lon = st.ColDouble(8);
  r->min_zoom = static_cast<int>(st.ColInt64(9));
  r->tile.z = static_cast<int>(st.ColInt64(10));
  r->tile.x = static_cast<int>(st.ColInt64(11));
  r->tile.y = static_cast<int>(st.ColInt64(12));
  r->feature = static_cast<int>(st.ColInt64(13));
}

bool TableExists(detail::SqliteDb& db, const char* name) {
  detail::SqliteStmt st;
  if (!st.Prepare(db, "SELECT 1 FROM sqlite_master WHERE type IN "
                      "('table','view') AND name = ?").ok()) {
    return false;
  }
  st.BindText(1, name);
  return st.Step();
}

// Something an FTS5 tokeniser would produce at least one token from. A query
// word of pure punctuation ("-", "&") indexes to nothing, and a MATCH
// expression containing an empty phrase is a syntax error rather than an empty
// answer — so such a word sends the whole query down the scan path, where the
// port's own rule (which does NOT split on punctuation) is applied directly.
bool HasIndexableCharacter(const std::string& token) {
  for (unsigned char c : token) {
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') || c >= 0x80) {
      return true;
    }
  }
  return false;
}

// The MATCH expression for `text`: one prefix PHRASE per whitespace word, all
// of them required. `"ruddy"* "tur"*` is FTS5 for "a word starting ruddy AND a
// word starting tur, in any order", which is exactly what the port's quality-2
// rule accepts — and a superset of its quality-0 and quality-1 rules, since a
// whole-string prefix is also a token prefix. A SUPERSET is the only safe
// direction: the caller re-tests every candidate against the real rule.
//
// Empty when no word survives, which the caller reads as "scan instead".
std::string MatchExpression(const std::string& text) {
  std::string out;
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && isspace(static_cast<unsigned char>(text[i]))) ++i;
    const size_t start = i;
    while (i < text.size() && !isspace(static_cast<unsigned char>(text[i]))) ++i;
    if (i == start) break;
    const std::string word = text.substr(start, i - start);
    if (!HasIndexableCharacter(word)) return {};
    if (!out.empty()) out.push_back(' ');
    out.push_back('"');
    for (char c : word) {
      if (c == '"') out.push_back('"');  // doubled, as SQL quotes are
      out.push_back(c);
    }
    out += "\"*";
  }
  return out;
}

std::string BboxPredicate(const GeoRect* area) {
  if (area == nullptr || area->CrossesAntimeridian()) return {};
  return " AND n.max_lat >= ? AND n.min_lat <= ? AND n.max_lon >= ?"
         " AND n.min_lon <= ?";
}

int BindBbox(detail::SqliteStmt& st, int next, const GeoRect* area) {
  if (area == nullptr || area->CrossesAntimeridian()) return next;
  st.BindDouble(next++, area->ll.lat);
  st.BindDouble(next++, area->ur.lat);
  st.BindDouble(next++, area->ll.lon);
  st.BindDouble(next++, area->ur.lon);
  return next;
}

}  // namespace

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

struct NameIndexWriter::Impl {
  detail::SqliteDb db;
  detail::SqliteStmt insert;
  bool fts = false;
  bool in_transaction = false;
  int64_t rows = 0;
};

NameIndexWriter::NameIndexWriter() : impl_(new Impl) {}
NameIndexWriter::~NameIndexWriter() { Close(); }

Status NameIndexWriter::Open(const std::string& path) {
  Close();
  const Status s = impl_->db.Open(path);
  if (!s.ok()) return s;
  // A pack is a pack, not an empty database sqlite3_open would have created
  // for a typo'd path.
  if (!TableExists(impl_->db, "tiles")) {
    impl_->db.Close();
    return Status::Error(kInvalidArg, path + ": not an MBTiles pack");
  }
  return Status::Ok();
}

void NameIndexWriter::Close() {
  if (impl_->in_transaction) {
    impl_->insert.Finalize();
    impl_->db.Exec("ROLLBACK");
    impl_->in_transaction = false;
  }
  impl_->insert.Finalize();
  impl_->db.Close();
  impl_->rows = 0;
}

bool NameIndexWriter::IsOpen() const { return impl_->db.IsOpen(); }
bool NameIndexWriter::fts_built() const { return impl_->fts; }
int64_t NameIndexWriter::rows_written() const { return impl_->rows; }

Status NameIndexWriter::DropIndex() {
  if (!IsOpen()) return Status::Error(kIoError, "index writer not open");
  // The FTS mirror first: dropping the content table under it leaves an
  // external-content index that errors on every read.
  impl_->db.Exec("DROP TABLE IF EXISTS search_names_fts");
  Status s = impl_->db.Exec("DROP TABLE IF EXISTS search_names");
  if (!s.ok()) return s;
  return impl_->db.Exec("DROP TABLE IF EXISTS search_names_meta");
}

Status NameIndexWriter::Begin(bool fts) {
  if (!IsOpen()) return Status::Error(kIoError, "index writer not open");
  Status s = DropIndex();
  if (!s.ok()) return s;

  s = impl_->db.Exec(
      "CREATE TABLE search_names ("
      " id INTEGER PRIMARY KEY,"
      " name TEXT NOT NULL,"
      " layer TEXT NOT NULL,"
      " class TEXT NOT NULL,"
      " lat REAL NOT NULL, lon REAL NOT NULL,"
      " min_lat REAL NOT NULL, min_lon REAL NOT NULL,"
      " max_lat REAL NOT NULL, max_lon REAL NOT NULL,"
      " min_zoom INTEGER NOT NULL,"
      " tile_z INTEGER NOT NULL, tile_x INTEGER NOT NULL,"
      " tile_y INTEGER NOT NULL, feature INTEGER NOT NULL)");
  if (!s.ok()) return s;
  s = impl_->db.Exec(
      "CREATE TABLE search_names_meta (key TEXT PRIMARY KEY, value TEXT)");
  if (!s.ok()) return s;

  // FTS5 IS AN OPTIMISATION. A SQLite without the module fails this one
  // statement and the index is built anyway — the reader then scans, which is
  // slower and gives the same answers.
  impl_->fts = impl_->db
                   .Exec("CREATE VIRTUAL TABLE search_names_fts USING fts5("
                         "name, content='search_names', content_rowid='id')")
                   .ok();

  s = impl_->db.Exec("BEGIN");
  if (!s.ok()) return s;
  impl_->in_transaction = true;
  impl_->rows = 0;
  return impl_->insert.Prepare(
      impl_->db,
      "INSERT INTO search_names (name, layer, class, lat, lon, min_lat,"
      " min_lon, max_lat, max_lon, min_zoom, tile_z, tile_x, tile_y, feature)"
      " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
}

Status NameIndexWriter::Add(const NameRow& row) {
  if (!impl_->in_transaction) {
    return Status::Error(kIoError, "index writer: Begin() first");
  }
  detail::SqliteStmt& st = impl_->insert;
  st.Reset();
  st.BindText(1, row.name);
  st.BindText(2, row.layer);
  st.BindText(3, row.style_key);
  st.BindDouble(4, row.position.lat);
  st.BindDouble(5, row.position.lon);
  st.BindDouble(6, row.bounds.ll.lat);
  st.BindDouble(7, row.bounds.ll.lon);
  st.BindDouble(8, row.bounds.ur.lat);
  st.BindDouble(9, row.bounds.ur.lon);
  st.BindInt64(10, row.min_zoom);
  st.BindInt64(11, row.tile.z);
  st.BindInt64(12, row.tile.x);
  st.BindInt64(13, row.tile.y);
  st.BindInt64(14, row.feature);
  Status s = Status::Ok();
  st.Step(&s);
  if (!s.ok()) return s;
  ++impl_->rows;
  return Status::Ok();
}

Status NameIndexWriter::Commit(
    const std::vector<std::pair<std::string, std::string>>& meta) {
  if (!impl_->in_transaction) {
    return Status::Error(kIoError, "index writer: nothing to commit");
  }
  impl_->insert.Finalize();

  // The FTS mirror is filled ONCE, here, rather than by triggers: the index is
  // written in a single pass and never updated in place, so a trigger would be
  // a row-by-row cost for a guarantee nothing needs.
  if (impl_->fts) {
    const Status s = impl_->db.Exec(
        "INSERT INTO search_names_fts (rowid, name) SELECT id, name FROM "
        "search_names");
    if (!s.ok()) {
      impl_->db.Exec("ROLLBACK");
      impl_->in_transaction = false;
      return s;
    }
  }
  // The zoom index is what makes "most prominent first" free rather than a
  // sort of the whole gazetteer on every keystroke.
  impl_->db.Exec("CREATE INDEX search_names_by_zoom ON search_names(min_zoom)");

  detail::SqliteStmt st;
  Status s = st.Prepare(impl_->db,
                        "INSERT OR REPLACE INTO search_names_meta (key, value)"
                        " VALUES (?,?)");
  if (!s.ok()) {
    impl_->db.Exec("ROLLBACK");
    impl_->in_transaction = false;
    return s;
  }
  auto put = [&](const std::string& k, const std::string& v) {
    st.Reset();
    st.BindText(1, k);
    st.BindText(2, v);
    Status step = Status::Ok();
    st.Step(&step);
    return step;
  };
  s = put("version", std::to_string(kNameIndexVersion));
  if (s.ok()) s = put("rows", std::to_string(impl_->rows));
  if (s.ok()) s = put("fts", impl_->fts ? "1" : "0");
  for (const auto& kv : meta) {
    if (!s.ok()) break;
    s = put(kv.first, kv.second);
  }
  st.Finalize();
  if (!s.ok()) {
    impl_->db.Exec("ROLLBACK");
    impl_->in_transaction = false;
    return s;
  }
  s = impl_->db.Exec("COMMIT");
  impl_->in_transaction = false;
  return s;
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

struct NameIndexReader::Impl {
  mutable detail::SqliteDb db;
  bool fts = false;
  int version = 0;
  int64_t rows = 0;
  std::vector<std::pair<std::string, std::string>> meta;
};

NameIndexReader::NameIndexReader() : impl_(new Impl) {}
NameIndexReader::~NameIndexReader() { Close(); }

void NameIndexReader::Close() {
  impl_->db.Close();
  impl_->fts = false;
  impl_->version = 0;
  impl_->rows = 0;
  impl_->meta.clear();
}

bool NameIndexReader::IsOpen() const { return impl_->db.IsOpen(); }
bool NameIndexReader::uses_fts() const { return impl_->fts; }
int NameIndexReader::version() const { return impl_->version; }
int64_t NameIndexReader::row_count() const { return impl_->rows; }

std::string NameIndexReader::Meta(const std::string& key) const {
  for (const auto& kv : impl_->meta) {
    if (kv.first == key) return kv.second;
  }
  return {};
}

Status NameIndexReader::Open(const std::string& path) {
  Close();
  Status s = impl_->db.OpenReadOnly(path);
  if (!s.ok()) return s;
  if (!TableExists(impl_->db, "search_names")) {
    impl_->db.Close();
    // NOT AN ERROR, and the caller must not treat it as one: every pack cut
    // before this existed is in this state, and the answer is the tile scan.
    return Status::Error(kNotFound, path + ": no name index");
  }

  detail::SqliteStmt st;
  if (st.Prepare(impl_->db, "SELECT key, value FROM search_names_meta").ok()) {
    while (st.Step()) impl_->meta.emplace_back(st.ColText(0), st.ColText(1));
  }
  st.Finalize();
  impl_->version = std::atoi(Meta("version").c_str());
  if (impl_->version > kNameIndexVersion) {
    const std::string got = std::to_string(impl_->version);
    Close();
    return Status::Error(kUnsupported,
                         path + ": name index version " + got +
                             " is newer than this reader understands");
  }

  if (st.Prepare(impl_->db, "SELECT COUNT(*) FROM search_names").ok() &&
      st.Step()) {
    impl_->rows = st.ColInt64(0);
  }
  st.Finalize();

  // PRESENT IS NOT USABLE. An FTS5 table in the file is inert to a SQLite
  // built without the module: the table row is in sqlite_master either way and
  // it is PREPARING a statement against it that fails. So the probe is a
  // prepare, and it is done once, here, rather than discovered per query.
  impl_->fts =
      TableExists(impl_->db, "search_names_fts") &&
      st.Prepare(impl_->db, "SELECT rowid FROM search_names_fts LIMIT 1").ok();
  st.Finalize();
  return Status::Ok();
}

Status NameIndexReader::Query(const std::string& text, const GeoRect* area,
                              size_t max, std::vector<NameRow>* out) const {
  if (out == nullptr) return Status::Error(kInvalidArg, "Query: null out");
  if (!IsOpen()) return Status::Error(kIoError, "name index not open");

  const std::string match = text.empty() ? std::string() : MatchExpression(text);
  const bool use_fts = impl_->fts && !match.empty();
  const std::string bbox = BboxPredicate(area);
  detail::SqliteStmt st;

  if (use_fts) {
    std::string sql = std::string("SELECT ") + kSelectColumns +
                      " FROM search_names_fts f JOIN search_names n"
                      " ON n.id = f.rowid WHERE search_names_fts MATCH ?" +
                      bbox + " ORDER BY n.min_zoom, n.name";
    if (max > 0) sql += " LIMIT " + std::to_string(max);
    const Status s = st.Prepare(impl_->db, sql.c_str());
    if (!s.ok()) return s;
    st.BindText(1, match);
    BindBbox(st, 2, area);
    Status step = Status::Ok();
    while (st.Step(&step)) {
      NameRow r;
      ReadRow(st, &r);
      out->push_back(std::move(r));
    }
    return step;
  }

  // THE SCAN PATH: no FTS module, no mirror, or a query FTS5 cannot express.
  // The rows come back in prominence order and the port's own rule cuts them,
  // so the ANSWER is identical to the indexed path — only the work differs.
  std::string sql = std::string("SELECT ") + kSelectColumns +
                    " FROM search_names n WHERE 1=1" + bbox +
                    " ORDER BY n.min_zoom, n.name";
  const Status s = st.Prepare(impl_->db, sql.c_str());
  if (!s.ok()) return s;
  BindBbox(st, 1, area);
  Status step = Status::Ok();
  while (st.Step(&step)) {
    if (!text.empty() && app::TextMatchQuality(text, st.ColText(0)) < 0) {
      continue;
    }
    NameRow r;
    ReadRow(st, &r);
    out->push_back(std::move(r));
    if (max > 0 && out->size() >= max) break;
  }
  return step;
}

// ---------------------------------------------------------------------------
// Building one
// ---------------------------------------------------------------------------

namespace {

GeoRect UnionOf(const GeoRect& a, const GeoRect& b) {
  GeoRect r;
  r.ll.lat = std::min(a.ll.lat, b.ll.lat);
  r.ll.lon = std::min(a.ll.lon, b.ll.lon);
  r.ur.lat = std::max(a.ur.lat, b.ur.lat);
  r.ur.lon = std::max(a.ur.lon, b.ur.lon);
  return r;
}

std::string NowUtc() {
  const std::time_t t = std::time(nullptr);
  char buf[32] = {0};
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buf;
}

std::string Join(const std::vector<std::string>& v, char sep) {
  std::string out;
  for (const std::string& s : v) {
    if (!out.empty()) out.push_back(sep);
    out += s;
  }
  return out;
}

}  // namespace

Status BuildNameIndex(const std::string& path,
                      const NameIndexBuildOptions& options,
                      NameIndexBuildStats* stats,
                      const std::function<void(int, int64_t, size_t)>& on_zoom) {
  NameIndexBuildStats local;
  NameIndexBuildStats& st = stats != nullptr ? *stats : local;
  st = NameIndexBuildStats();

  const int window = options.window > 0 ? options.window : 8;

  auto source = std::make_shared<OsmVectorSource>();
  Status s = source->Open(path);
  if (!s.ok()) return s;
  // A forced zoom is honoured exactly and is NOT tile-capped — the budget that
  // makes a live search cheap is the wrong instinct here, since the whole
  // point of an index is to have read everything once.
  source->SetMaxTilesPerQuery(static_cast<size_t>(window) * window * 4);
  source->SetTileCacheCapacity(static_cast<size_t>(window) * window + 8);

  VectorMapOverlay overlay("pack", source);
  if (!options.label_tags.empty()) overlay.SetLabelTags(options.label_tags);
  overlay.SetMergeGapMeters(options.merge_gap_m);
  // THE BUILDER IS TIER 1 BY DEFINITION. A pack being re-indexed already has
  // an index, and asking it about itself would copy the old answers forward.
  overlay.SetUseNameIndex(false);
  const std::vector<std::string> used_tags = overlay.label_tags();

  std::vector<int> zooms;
  s = source->file().ZoomLevels(&zooms);
  if (!s.ok()) return s;
  if (zooms.empty()) return Status::Error(kNotFound, path + ": no tiles");

  const std::atomic<bool> never{false};
  std::vector<FeatureRow> all;

  for (int z : zooms) {
    if (options.min_zoom >= 0 && z < options.min_zoom) continue;
    if (options.max_zoom >= 0 && z > options.max_zoom) continue;
    MbtilesFile::ZoomExtent ext;
    if (!source->file().ZoomExtentOf(z, &ext).ok() || ext.empty()) continue;
    source->SetZoomOverride(z);

    std::vector<FeatureRow> at_zoom;
    for (int x = ext.min_x; x <= ext.max_x; x += window) {
      for (int y = ext.min_y; y <= ext.max_y; y += window) {
        const int x1 = std::min(x + window - 1, ext.max_x);
        const int y1 = std::min(y + window - 1, ext.max_y);
        app::SearchQuery q;
        q.area = UnionOf(webmerc::TileBounds({z, x, y}),
                         webmerc::TileBounds({z, x1, y1}));
        q.max_results = 0;  // an export, not a search box
        std::vector<FeatureRow> rows;
        overlay.ScanRows(q, never, &rows);
        st.tiles_scanned += static_cast<int64_t>(x1 - x + 1) * (y1 - y + 1);
        for (FeatureRow& r : rows) {
          // THE PROMINENCE, and the only field a builder decides: the level
          // the cutter was still drawing this name at.
          r.rank = z;
          at_zoom.push_back(std::move(r));
        }
      }
    }
    // Merge WITHIN the level first: a road crosses the windows this loop cut
    // the level into, and joining those now keeps the whole-pack merge below
    // working on rows rather than on pieces.
    MergeFeatureRows(&at_zoom, options.merge_gap_m);
    if (on_zoom) on_zoom(z, ext.count, at_zoom.size());
    all.insert(all.end(), std::make_move_iterator(at_zoom.begin()),
               std::make_move_iterator(at_zoom.end()));
  }

  // ONE ROW PER NAMED THING ACROSS THE WHOLE PYRAMID. The levels were walked
  // shallowest first, so a surviving row keeps the shallowest rank it was seen
  // with and the ref from the level where the name still mattered.
  st.pieces = static_cast<int64_t>(all.size());
  MergeFeatureRows(&all, options.merge_gap_m);

  // RESOLVE FIRST, THEN LET GO OF THE PACK — a FeatureRef is live only inside
  // the process that minted it, and a pack being read cannot be written to.
  std::vector<NameRow> index_rows;
  index_rows.reserve(all.size());
  for (const FeatureRow& row : all) {
    NameRow out;
    std::string layer;
    if (!source->ResolveRef(row.ref, &out.tile, &layer)) {
      ++st.unresolved;
      continue;
    }
    out.name = row.title;
    out.layer = row.layer.empty() ? layer : row.layer;
    out.style_key = row.style_key;
    out.position = row.anchor;
    out.bounds = row.bounds;
    out.min_zoom = row.rank;
    out.feature = row.ref.feature;
    index_rows.push_back(std::move(out));
  }
  all.clear();
  overlay.SetSource(nullptr);
  source.reset();

  NameIndexWriter writer;
  s = writer.Open(path);
  if (!s.ok()) return s;
  s = writer.Begin(options.fts);
  if (!s.ok()) return s;
  for (const NameRow& row : index_rows) {
    s = writer.Add(row);
    if (!s.ok()) return s;
  }

  std::vector<std::pair<std::string, std::string>> meta;
  meta.emplace_back("built", NowUtc());
  meta.emplace_back("builder", "fvnames");
  meta.emplace_back("label_tags", Join(used_tags, ','));
  meta.emplace_back("merge_gap_m", std::to_string(options.merge_gap_m));
  meta.emplace_back("zoom_min", std::to_string(zooms.front()));
  meta.emplace_back("zoom_max", std::to_string(zooms.back()));
  meta.emplace_back("tiles_scanned", std::to_string(st.tiles_scanned));
  s = writer.Commit(meta);
  if (!s.ok()) return s;
  st.names = writer.rows_written();
  st.fts = writer.fts_built();
  return Status::Ok();
}

}  // namespace osm
}  // namespace fv