// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/feature/geopackage.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <utility>

#include "fvkit/detail/sqlite.h"
#include "fvkit/feature/schema_change.h"
#include "fvkit/log.h"

// Older SQLite headers lack the flag; it only lets SQLite cache results.
#ifndef SQLITE_DETERMINISTIC
#define SQLITE_DETERMINISTIC 0
#endif

namespace fv {

namespace {

using detail::SqliteDb;
using detail::SqliteStmt;

constexpr int32_t kWgs84 = 4326;
constexpr int32_t kUndefinedGeographic = 0;
constexpr const char* kRtreeDefinition = "http://www.geopackage.org/spec120/#extension_rtree";

Status Bad(const std::string& why) { return Status::Error(kInvalidArg, "GeoPackage: " + why); }

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

/// An SQL identifier in double quotes.
std::string Quote(const std::string& name) {
  std::string out = "\"";
  for (char c : name) {
    if (c == '"') out += '"';
    out += c;
  }
  return out + "\"";
}

/// An SQL string literal.
std::string Literal(const std::string& text) {
  std::string out = "'";
  for (char c : text) {
    if (c == '\'') out += '\'';
    out += c;
  }
  return out + "'";
}

std::string Number(double v) {
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.17g", v);
  return buf;
}

// MARK: Geometry blob

constexpr uint8_t kFlagLittleEndian = 0x01;
constexpr uint8_t kFlagEmpty = 0x10;
constexpr uint8_t kFlagExtended = 0x20;

/// The parsed "GP" header. `envelope` is minx, maxx, miny, maxy when present.
struct BlobHeader {
  uint8_t flags = 0;
  int32_t srs_id = 0;
  bool has_envelope = false;
  double envelope[4] = {0, 0, 0, 0};
  size_t wkb_offset = 0;
};

Status ParseHeader(const uint8_t* data, size_t size, BlobHeader* h) {
  if (data == nullptr || size < 8 || data[0] != 'G' || data[1] != 'P')
    return Bad("geometry blob has no GP header");
  if (data[2] != 0) return Bad("geometry blob version " + std::to_string(data[2]));
  h->flags = data[3];
  if (h->flags & kFlagExtended)
    return Status::Error(kUnsupported, "GeoPackage: extended geometry types are not supported");
  const bool little = (h->flags & kFlagLittleEndian) != 0;
  auto get = [&](size_t at, size_t n, uint8_t* dst) {
    for (size_t i = 0; i < n; ++i) dst[i] = little ? data[at + i] : data[at + n - 1 - i];
  };
  // The host is assumed little-endian, as everywhere this port builds.
  get(4, 4, reinterpret_cast<uint8_t*>(&h->srs_id));
  const int indicator = (h->flags >> 1) & 0x07;
  static const size_t kEnvelopeDoubles[] = {0, 4, 6, 6, 8};
  if (indicator > 4) return Bad("geometry blob envelope code " + std::to_string(indicator));
  const size_t doubles = kEnvelopeDoubles[indicator];
  if (size < 8 + doubles * 8) return Bad("geometry blob is truncated");
  h->has_envelope = doubles > 0;
  for (size_t i = 0; i < 4 && i < doubles; ++i)
    get(8 + i * 8, 8, reinterpret_cast<uint8_t*>(&h->envelope[i]));
  h->wkb_offset = 8 + doubles * 8;
  return Status::Ok();
}

/// The x/y box of a blob, from its header when it has one.
bool BlobEnvelope(const uint8_t* data, size_t size, double env[4], bool* empty) {
  BlobHeader h;
  if (!ParseHeader(data, size, &h).ok()) return false;
  if (h.flags & kFlagEmpty) {
    *empty = true;
    return true;
  }
  if (h.has_envelope) {
    std::copy(h.envelope, h.envelope + 4, env);
    *empty = false;
    return true;
  }
  Geometry g;
  if (!ReadWkb(data + h.wkb_offset, size - h.wkb_offset, &g).ok()) return false;
  GeoRect box;
  *empty = !g.Envelope(&box);
  if (!*empty) {
    env[0] = box.ll.lon;
    env[1] = box.ur.lon;
    env[2] = box.ll.lat;
    env[3] = box.ur.lat;
  }
  return true;
}

// MARK: SQL functions the R-tree triggers call

void BlobArgs(sqlite3_context* ctx, sqlite3_value* arg, double env[4], bool* empty, bool* ok) {
  *ok = false;
  if (sqlite3_value_type(arg) == SQLITE_NULL) {
    sqlite3_result_null(ctx);
    return;
  }
  const auto* data = static_cast<const uint8_t*>(sqlite3_value_blob(arg));
  const int n = sqlite3_value_bytes(arg);
  if (!BlobEnvelope(data, static_cast<size_t>(n), env, empty)) {
    sqlite3_result_null(ctx);
    return;
  }
  *ok = true;
}

template <int kIndex>
void StBound(sqlite3_context* ctx, int, sqlite3_value** argv) {
  double env[4];
  bool empty = false;
  bool ok = false;
  BlobArgs(ctx, argv[0], env, &empty, &ok);
  if (!ok) return;
  if (empty) {
    sqlite3_result_null(ctx);
    return;
  }
  sqlite3_result_double(ctx, env[kIndex]);
}

void StIsEmpty(sqlite3_context* ctx, int, sqlite3_value** argv) {
  double env[4];
  bool empty = false;
  bool ok = false;
  BlobArgs(ctx, argv[0], env, &empty, &ok);
  if (ok) sqlite3_result_int(ctx, empty ? 1 : 0);
}

// MARK: Core tables

const char kCoreTables[] =
    "PRAGMA application_id = 0x47504B47;"  // 'GPKG'
    "PRAGMA user_version = 10200;"
    "CREATE TABLE IF NOT EXISTS gpkg_spatial_ref_sys("
    "  srs_name TEXT NOT NULL, srs_id INTEGER NOT NULL PRIMARY KEY,"
    "  organization TEXT NOT NULL, organization_coordsys_id INTEGER NOT NULL,"
    "  definition TEXT NOT NULL, description TEXT);"
    "INSERT OR IGNORE INTO gpkg_spatial_ref_sys VALUES"
    "  ('WGS 84 geodetic', 4326, 'EPSG', 4326,"
    "   'GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
    "SPHEROID[\"WGS 84\",6378137,298.257223563,AUTHORITY[\"EPSG\",\"7030\"]],"
    "AUTHORITY[\"EPSG\",\"6326\"]],PRIMEM[\"Greenwich\",0,AUTHORITY[\"EPSG\",\"8901\"]],"
    "UNIT[\"degree\",0.0174532925199433,AUTHORITY[\"EPSG\",\"9122\"]],"
    "AUTHORITY[\"EPSG\",\"4326\"]]', 'longitude/latitude coordinates in decimal degrees on the WGS 84 spheroid'),"
    "  ('Undefined cartesian SRS', -1, 'NONE', -1, 'undefined', 'undefined cartesian coordinate reference system'),"
    "  ('Undefined geographic SRS', 0, 'NONE', 0, 'undefined', 'undefined geographic coordinate reference system');"
    "CREATE TABLE IF NOT EXISTS gpkg_contents("
    "  table_name TEXT NOT NULL PRIMARY KEY, data_type TEXT NOT NULL,"
    "  identifier TEXT UNIQUE, description TEXT DEFAULT '',"
    "  last_change DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "  min_x DOUBLE, min_y DOUBLE, max_x DOUBLE, max_y DOUBLE, srs_id INTEGER,"
    "  CONSTRAINT fk_gc_r_srs_id FOREIGN KEY (srs_id) REFERENCES gpkg_spatial_ref_sys(srs_id));";

const char kFeatureTables[] =
    "CREATE TABLE IF NOT EXISTS gpkg_geometry_columns("
    "  table_name TEXT NOT NULL, column_name TEXT NOT NULL,"
    "  geometry_type_name TEXT NOT NULL, srs_id INTEGER NOT NULL,"
    "  z TINYINT NOT NULL, m TINYINT NOT NULL,"
    "  CONSTRAINT pk_geom_cols PRIMARY KEY (table_name, column_name),"
    "  CONSTRAINT uk_gc_table_name UNIQUE (table_name),"
    "  CONSTRAINT fk_gc_tn FOREIGN KEY (table_name) REFERENCES gpkg_contents(table_name),"
    "  CONSTRAINT fk_gc_srs FOREIGN KEY (srs_id) REFERENCES gpkg_spatial_ref_sys(srs_id));"
    "CREATE TABLE IF NOT EXISTS gpkg_extensions("
    "  table_name TEXT, column_name TEXT, extension_name TEXT NOT NULL,"
    "  definition TEXT NOT NULL, scope TEXT NOT NULL,"
    "  CONSTRAINT ge_tce UNIQUE (table_name, column_name, extension_name));";

const char kSchemaTables[] =
    "CREATE TABLE IF NOT EXISTS gpkg_data_columns("
    "  table_name TEXT NOT NULL, column_name TEXT NOT NULL,"
    "  name TEXT, title TEXT, description TEXT, mime_type TEXT, constraint_name TEXT,"
    "  CONSTRAINT pk_gdc PRIMARY KEY (table_name, column_name));"
    "CREATE TABLE IF NOT EXISTS gpkg_data_column_constraints("
    "  constraint_name TEXT NOT NULL, constraint_type TEXT NOT NULL, value TEXT,"
    "  min NUMERIC, min_is_inclusive BOOLEAN, max NUMERIC, max_is_inclusive BOOLEAN,"
    "  description TEXT,"
    "  CONSTRAINT gdcc_ntv UNIQUE (constraint_name, constraint_type, value));"
    "CREATE TABLE IF NOT EXISTS fv_data_columns("
    "  table_name TEXT NOT NULL, column_name TEXT NOT NULL,"
    "  role TEXT, unit TEXT, range_end TEXT,"
    "  PRIMARY KEY (table_name, column_name));"
    // The UNIQUE constraint does not see these rows as duplicates: column_name is NULL.
    "INSERT INTO gpkg_extensions SELECT 'gpkg_data_columns', NULL, 'gpkg_schema',"
    "  'http://www.geopackage.org/spec120/#extension_schema', 'read-write'"
    "  WHERE NOT EXISTS (SELECT 1 FROM gpkg_extensions WHERE table_name = 'gpkg_data_columns'"
    "  AND extension_name = 'gpkg_schema');"
    "INSERT INTO gpkg_extensions SELECT 'gpkg_data_column_constraints', NULL, 'gpkg_schema',"
    "  'http://www.geopackage.org/spec120/#extension_schema', 'read-write'"
    "  WHERE NOT EXISTS (SELECT 1 FROM gpkg_extensions"
    "  WHERE table_name = 'gpkg_data_column_constraints' AND extension_name = 'gpkg_schema');"
    "INSERT INTO gpkg_extensions SELECT 'fv_data_columns', NULL, 'fv_data_columns',"
    "  'Peregrine field roles: port/feature-overlay-plan.md', 'read-write'"
    "  WHERE NOT EXISTS (SELECT 1 FROM gpkg_extensions WHERE table_name = 'fv_data_columns'"
    "  AND extension_name = 'fv_data_columns');";

bool TableExists(SqliteDb& db, const std::string& name) {
  SqliteStmt st;
  if (!st.Prepare(db, "SELECT 1 FROM sqlite_master WHERE type IN ('table','view') AND "
                      "lower(name) = lower(?)").ok())
    return false;
  st.BindText(1, name);
  return st.Step();
}

/// The SQL type GeoPackage spells for a storage type.
const char* SqlType(FieldType t) {
  switch (t) {
    case FieldType::kText: return "TEXT";
    case FieldType::kInteger: return "INTEGER";
    case FieldType::kReal: return "REAL";
    case FieldType::kBoolean: return "BOOLEAN";
    case FieldType::kDate: return "DATE";
    case FieldType::kDateTime: return "DATETIME";
    case FieldType::kBlob: return "BLOB";
  }
  return "TEXT";
}

/// The storage type for a declared SQL type; unknown types read as text.
FieldType FromSqlType(const std::string& declared) {
  const std::string t = Lower(declared.substr(0, declared.find('(')));
  if (t == "boolean") return FieldType::kBoolean;
  if (t == "integer" || t == "int" || t == "tinyint" || t == "smallint" || t == "mediumint" ||
      t == "bigint")
    return FieldType::kInteger;
  if (t == "real" || t == "double" || t == "float") return FieldType::kReal;
  if (t == "date") return FieldType::kDate;
  if (t == "datetime") return FieldType::kDateTime;
  if (t == "blob") return FieldType::kBlob;
  return FieldType::kText;
}

/// A field default as an SQL literal; empty for none.
std::string DefaultLiteral(const FieldDef& f) {
  const FieldValue& v = f.default_value;
  if (IsNull(v)) return "";
  if (const auto* b = std::get_if<bool>(&v)) return *b ? "1" : "0";
  if (const auto* i = std::get_if<int64_t>(&v)) return std::to_string(*i);
  if (const auto* d = std::get_if<double>(&v)) return Number(*d);
  if (const auto* s = std::get_if<std::string>(&v)) return Literal(*s);
  return "";
}

/// Parses `PRAGMA table_info`'s dflt_value back into a value. Expressions
/// (CURRENT_TIMESTAMP, strftime(...)) are not values and give null.
FieldValue ParseDefault(const std::string& sql, FieldType type) {
  if (sql.empty() || Lower(sql) == "null") return {};
  if (sql.size() >= 2 && sql.front() == '\'' && sql.back() == '\'') {
    std::string text;
    for (size_t i = 1; i + 1 < sql.size(); ++i) {
      text += sql[i];
      if (sql[i] == '\'' && sql[i + 1] == '\'') ++i;
    }
    if (type == FieldType::kText || type == FieldType::kDate || type == FieldType::kDateTime)
      return text;
    return {};
  }
  char* end = nullptr;
  const double d = std::strtod(sql.c_str(), &end);
  if (end == sql.c_str() || *end != '\0') return {};
  switch (type) {
    case FieldType::kBoolean: return d != 0.0;
    case FieldType::kInteger: return static_cast<int64_t>(d);
    case FieldType::kReal: return d;
    default: return {};
  }
}

void Bind(SqliteStmt& st, int i, const FieldValue& v) {
  if (const auto* b = std::get_if<bool>(&v)) st.BindInt64(i, *b ? 1 : 0);
  else if (const auto* n = std::get_if<int64_t>(&v)) st.BindInt64(i, *n);
  else if (const auto* d = std::get_if<double>(&v)) st.BindDouble(i, *d);
  else if (const auto* s = std::get_if<std::string>(&v)) st.BindText(i, *s);
  else if (const auto* blob = std::get_if<std::vector<uint8_t>>(&v))
    st.BindBlob(i, blob->data(), blob->size());
  // monostate: left unbound, which is NULL.
}

FieldValue Column(const SqliteStmt& st, int i, FieldType type) {
  if (st.ColType(i) == SQLITE_NULL) return {};
  switch (type) {
    case FieldType::kBoolean: return st.ColInt64(i) != 0;
    case FieldType::kInteger: return st.ColInt64(i);
    case FieldType::kReal: return st.ColDouble(i);
    case FieldType::kBlob: {
      const auto* p = static_cast<const uint8_t*>(st.ColBlob(i));
      return std::vector<uint8_t>(p, p + st.ColBytes(i));
    }
    default: return st.ColText(i);
  }
}

/// `base`, or `base_1`, `base_2`, ... — the first not used by a field.
std::string FreeName(const FeatureSchema& schema, const std::string& base) {
  std::string name = base;
  for (int n = 1; schema.Find(name) >= 0; ++n) name = base + "_" + std::to_string(n);
  return name;
}

bool ReservedTableName(const std::string& table) {
  const std::string t = Lower(table);
  return t.rfind("gpkg_", 0) == 0 || t.rfind("rtree_", 0) == 0 || t.rfind("sqlite_", 0) == 0 ||
         t == "fv_data_columns" || t == "layer_styles";
}

/// Runs `body` inside a savepoint so a failure undoes all of it.
template <typename Body>
Status Atomically(SqliteDb& db, Body body) {
  Status s = db.Exec("SAVEPOINT fv_op");
  if (!s.ok()) return s;
  s = body();
  if (!s.ok()) {
    db.Exec("ROLLBACK TO fv_op");
    db.Exec("RELEASE fv_op");
    return s;
  }
  return db.Exec("RELEASE fv_op");
}

/// The R-tree table and the spec's six triggers (§F.3, 1.2.1 form).
std::string RtreeSql(const std::string& t, const std::string& c, const std::string& i) {
  const std::string rt = Quote("rtree_" + t + "_" + c);
  const std::string qt = Quote(t);
  const std::string qc = Quote(c);
  const std::string qi = Quote(i);
  auto trig = [&](const char* suffix) { return Quote("rtree_" + t + "_" + c + "_" + suffix); };
  const std::string values = "VALUES (NEW." + qi + ", ST_MinX(NEW." + qc + "), ST_MaxX(NEW." + qc +
                             "), ST_MinY(NEW." + qc + "), ST_MaxY(NEW." + qc + "));";
  const std::string nonempty = "(NEW." + qc + " NOT NULL AND NOT ST_IsEmpty(NEW." + qc + "))";
  const std::string empty = "(NEW." + qc + " IS NULL OR ST_IsEmpty(NEW." + qc + "))";
  std::string sql;
  sql += "CREATE VIRTUAL TABLE " + rt + " USING rtree(id, minx, maxx, miny, maxy);";
  sql += "CREATE TRIGGER " + trig("insert") + " AFTER INSERT ON " + qt + " WHEN " + nonempty +
         " BEGIN INSERT OR REPLACE INTO " + rt + " " + values + " END;";
  sql += "CREATE TRIGGER " + trig("update1") + " AFTER UPDATE OF " + qc + " ON " + qt +
         " WHEN OLD." + qi + " = NEW." + qi + " AND " + nonempty + " BEGIN INSERT OR REPLACE INTO " +
         rt + " " + values + " END;";
  sql += "CREATE TRIGGER " + trig("update2") + " AFTER UPDATE OF " + qc + " ON " + qt +
         " WHEN OLD." + qi + " = NEW." + qi + " AND " + empty + " BEGIN DELETE FROM " + rt +
         " WHERE id = OLD." + qi + "; END;";
  sql += "CREATE TRIGGER " + trig("update3") + " AFTER UPDATE ON " + qt + " WHEN OLD." + qi +
         " != NEW." + qi + " AND " + nonempty + " BEGIN DELETE FROM " + rt + " WHERE id = OLD." + qi +
         "; INSERT OR REPLACE INTO " + rt + " " + values + " END;";
  sql += "CREATE TRIGGER " + trig("update4") + " AFTER UPDATE ON " + qt + " WHEN OLD." + qi +
         " != NEW." + qi + " AND " + empty + " BEGIN DELETE FROM " + rt + " WHERE id IN (OLD." + qi +
         ", NEW." + qi + "); END;";
  sql += "CREATE TRIGGER " + trig("delete") + " AFTER DELETE ON " + qt + " WHEN OLD." + qc +
         " NOT NULL BEGIN DELETE FROM " + rt + " WHERE id = OLD." + qi + "; END;";
  return sql;
}

/// CREATE TABLE for a feature table: the id, the geometry column, then the fields.
std::string TableDdl(const std::string& table, const std::string& id_column,
                     const std::string& geom_column, const FeatureSchema& schema) {
  std::string ddl = "CREATE TABLE " + Quote(table) + " (" + Quote(id_column) +
                    " INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL, " + Quote(geom_column) + " " +
                    GeometryTypeName(schema.geometry_type);
  for (const FieldDef& f : schema.fields) {
    ddl += ", " + Quote(f.name) + " " + SqlType(f.type);
    if (f.required) ddl += " NOT NULL";
    const std::string def = DefaultLiteral(f);
    if (!def.empty()) ddl += " DEFAULT " + def;
  }
  return ddl + ")";
}

/// Removes `table`'s field metadata: its gpkg_data_columns and fv_data_columns
/// rows, and the constraints only those rows used.
Status DropFieldMetadata(SqliteDb& db, const std::string& table) {
  Status s = db.Exec(kFeatureTables);
  if (s.ok()) s = db.Exec(kSchemaTables);
  if (!s.ok()) return s;
  SqliteStmt del;
  s = del.Prepare(db, "DELETE FROM gpkg_data_column_constraints WHERE constraint_name IN "
                      "(SELECT constraint_name FROM gpkg_data_columns WHERE table_name = ?1) "
                      "AND constraint_name NOT IN (SELECT constraint_name FROM gpkg_data_columns "
                      "WHERE table_name != ?1 AND constraint_name IS NOT NULL)");
  if (!s.ok()) return s;
  del.BindText(1, table);
  del.Step(&s);
  if (!s.ok()) return s;
  for (const char* sql : {"DELETE FROM gpkg_data_columns WHERE table_name = ?",
                          "DELETE FROM fv_data_columns WHERE table_name = ?"}) {
    SqliteStmt st;
    s = st.Prepare(db, sql);
    if (!s.ok()) return s;
    st.BindText(1, table);
    st.Step(&s);
    if (!s.ok()) return s;
  }
  return Status::Ok();
}

/// Writes one field's rows in gpkg_data_columns, its constraint and fv_data_columns.
Status WriteFieldMetadata(SqliteDb& db, const std::string& table, const FieldDef& f) {
  std::string constraint;
  const bool has_choices = f.role == FieldRole::kEnumeration && !f.choices.empty();
  const bool has_range = !has_choices && (f.min || f.max);
  if (has_choices || has_range) constraint = table + "_" + f.name;

  SqliteStmt st;
  Status s = st.Prepare(db, "INSERT OR REPLACE INTO gpkg_data_columns(table_name, column_name, "
                            "name, title, description, mime_type, constraint_name) "
                            "VALUES(?,?,?,?,?,NULL,?)");
  if (!s.ok()) return s;
  st.BindText(1, table);
  st.BindText(2, f.name);
  // GDAL reads `name` as the field's alias; the spec's `title` carries the same text.
  if (!f.title.empty()) {
    st.BindText(3, f.title);
    st.BindText(4, f.title);
  }
  if (!f.description.empty()) st.BindText(5, f.description);
  if (!constraint.empty()) st.BindText(6, constraint);
  st.Step(&s);
  if (!s.ok()) return s;

  if (!constraint.empty()) {
    SqliteStmt del;
    s = del.Prepare(db, "DELETE FROM gpkg_data_column_constraints WHERE constraint_name = ?");
    if (!s.ok()) return s;
    del.BindText(1, constraint);
    del.Step(&s);
    if (!s.ok()) return s;
    SqliteStmt ins;
    s = ins.Prepare(db, "INSERT INTO gpkg_data_column_constraints(constraint_name, "
                        "constraint_type, value, min, min_is_inclusive, max, max_is_inclusive, "
                        "description) VALUES(?,?,?,?,?,?,?,?)");
    if (!s.ok()) return s;
    if (has_choices) {
      for (const FieldChoice& c : f.choices) {
        ins.Reset();
        ins.BindText(1, constraint);
        ins.BindText(2, "enum");
        ins.BindText(3, c.value);
        if (!c.label.empty()) ins.BindText(8, c.label);
        ins.Step(&s);
        if (!s.ok()) return s;
      }
    } else {
      ins.BindText(1, constraint);
      ins.BindText(2, "range");
      if (f.min) {
        ins.BindDouble(4, *f.min);
        ins.BindInt64(5, 1);
      }
      if (f.max) {
        ins.BindDouble(6, *f.max);
        ins.BindInt64(7, 1);
      }
      ins.Step(&s);
      if (!s.ok()) return s;
    }
  }

  if (f.role != FieldRole::kNone || !f.unit.empty() || !f.range_end.empty()) {
    SqliteStmt fv;
    s = fv.Prepare(db, "INSERT OR REPLACE INTO fv_data_columns VALUES(?,?,?,?,?)");
    if (!s.ok()) return s;
    fv.BindText(1, table);
    fv.BindText(2, f.name);
    if (f.role != FieldRole::kNone) fv.BindText(3, ToString(f.role));
    if (!f.unit.empty()) fv.BindText(4, f.unit);
    if (!f.range_end.empty()) fv.BindText(5, f.range_end);
    fv.Step(&s);
    if (!s.ok()) return s;
  }
  return Status::Ok();
}

}  // namespace

// MARK: Blob codec

Status ReadGpkgGeometry(const uint8_t* data, size_t size, Geometry* out, int32_t* srs_id) {
  if (out == nullptr) return Bad("ReadGpkgGeometry: null out");
  BlobHeader h;
  Status s = ParseHeader(data, size, &h);
  if (!s.ok()) return s;
  if (srs_id != nullptr) *srs_id = h.srs_id;
  return ReadWkb(data + h.wkb_offset, size - h.wkb_offset, out);
}

Status WriteGpkgGeometry(const Geometry& geometry, int32_t srs_id, std::vector<uint8_t>* out) {
  if (out == nullptr) return Bad("WriteGpkgGeometry: null out");
  if (geometry.IsNull()) return Bad("a null geometry has no blob");
  std::vector<uint8_t> wkb;
  Status s = WriteWkb(geometry, &wkb);
  if (!s.ok()) return s;
  GeoRect box;
  const bool empty = !geometry.Envelope(&box);
  const bool envelope = !empty && geometry.type != GeometryType::kPoint;
  uint8_t flags = kFlagLittleEndian;
  if (empty) flags |= kFlagEmpty;
  if (envelope) flags |= 1 << 1;
  out->assign({'G', 'P', 0, flags});
  auto put = [&](const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    out->insert(out->end(), b, b + n);
  };
  put(&srs_id, 4);
  if (envelope) {
    const double env[4] = {box.ll.lon, box.ur.lon, box.ll.lat, box.ur.lat};
    put(env, sizeof env);
  }
  out->insert(out->end(), wkb.begin(), wkb.end());
  return Status::Ok();
}

// MARK: GeoPackage

GeoPackage::GeoPackage(std::string path, bool writable)
    : path_(std::move(path)), writable_(writable), db_(std::make_unique<SqliteDb>()) {}

GeoPackage::~GeoPackage() = default;

Status GeoPackage::RegisterFunctions() {
  struct Fn {
    const char* name;
    void (*fn)(sqlite3_context*, int, sqlite3_value**);
  };
  const Fn fns[] = {{"ST_MinX", StBound<0>}, {"ST_MaxX", StBound<1>}, {"ST_MinY", StBound<2>},
                    {"ST_MaxY", StBound<3>}, {"ST_IsEmpty", StIsEmpty}};
  for (const Fn& f : fns) {
    if (sqlite3_create_function(db_->get(), f.name, 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                nullptr, f.fn, nullptr, nullptr) != SQLITE_OK)
      return Status::Error(kInternal, std::string("GeoPackage: cannot register ") + f.name);
  }
  return Status::Ok();
}

Status GeoPackage::Create(const std::string& path, std::shared_ptr<GeoPackage>* out) {
  if (out == nullptr) return Bad("Create: null out");
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) return Bad(path + " already exists");
  std::shared_ptr<GeoPackage> gp(new GeoPackage(path, true));
  Status s = gp->db_->Open(path);
  if (!s.ok()) return s;
  s = gp->RegisterFunctions();
  if (s.ok()) s = gp->db_->Exec(kCoreTables);
  if (s.ok()) s = gp->db_->Exec(kFeatureTables);
  if (!s.ok()) {
    gp->db_->Close();
    std::filesystem::remove(path, ec);
    return s;
  }
  *out = std::move(gp);
  return Status::Ok();
}

Status GeoPackage::Open(const std::string& path, bool writable, std::shared_ptr<GeoPackage>* out) {
  if (out == nullptr) return Bad("Open: null out");
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return Status::Error(kNotFound, "GeoPackage: no file " + path);
  std::shared_ptr<GeoPackage> gp(new GeoPackage(path, writable));
  Status s;
  s = writable ? gp->db_->Open(path) : gp->db_->OpenReadOnly(path);
  if (!s.ok()) return s;
  s = gp->RegisterFunctions();
  if (!s.ok()) return s;
  if (!TableExists(*gp->db_, "gpkg_contents") || !TableExists(*gp->db_, "gpkg_spatial_ref_sys"))
    return Bad(path + " is not a GeoPackage");
  *out = std::move(gp);
  return Status::Ok();
}

Status GeoPackage::RequireWritable() const {
  if (!writable_) return Status::Error(kUnsupported, "GeoPackage: " + path_ + " is open read-only");
  return Status::Ok();
}

Status GeoPackage::Begin() {
  Status s = RequireWritable();
  if (!s.ok()) return s;
  if (sqlite3_get_autocommit(db_->get()) == 0) return Status::Ok();
  return db_->Exec("BEGIN");
}

// A failed change can leave the transaction open with nothing in it; only a
// successful one marks a table dirty.
bool GeoPackage::HasUnsavedChanges() const {
  if (db_->IsOpen() && sqlite3_get_autocommit(db_->get()) == 0 && !dirty_tables_.empty()) return true;
  for (const auto& layer : OpenLayers())
    if (layer->HasEdits()) return true;
  return false;
}

std::vector<std::shared_ptr<GeoPackageFeatureLayer>> GeoPackage::OpenLayers() const {
  std::vector<std::shared_ptr<GeoPackageFeatureLayer>> out;
  for (const auto& weak : layers_)
    if (auto layer = weak.lock()) out.push_back(std::move(layer));
  return out;
}

Status GeoPackage::Layers(std::vector<GeoPackageLayerInfo>* out) const {
  if (out == nullptr) return Bad("Layers: null out");
  out->clear();
  const bool has_columns = TableExists(*db_, "gpkg_geometry_columns");
  SqliteStmt st;
  const std::string sql =
      has_columns ? "SELECT c.table_name, c.identifier, c.description, g.geometry_type_name "
                    "FROM gpkg_contents c LEFT JOIN gpkg_geometry_columns g "
                    "ON g.table_name = c.table_name WHERE c.data_type = 'features' ORDER BY c.rowid"
                  : "SELECT table_name, identifier, description, NULL FROM gpkg_contents "
                    "WHERE data_type = 'features' ORDER BY rowid";
  Status s = st.Prepare(*db_, sql.c_str());
  if (!s.ok()) return s;
  while (st.Step(&s)) {
    GeoPackageLayerInfo info;
    info.table = st.ColText(0);
    info.identifier = st.ColText(1);
    if (info.identifier.empty()) info.identifier = info.table;
    info.description = st.ColText(2);
    GeometryTypeFromName(st.ColText(3).c_str(), &info.geometry_type);
    out->push_back(std::move(info));
  }
  return s;
}

Status GeoPackage::OpenLayer(const std::string& table,
                             std::shared_ptr<GeoPackageFeatureLayer>* out) {
  if (out == nullptr) return Bad("OpenLayer: null out");
  std::shared_ptr<GeoPackageFeatureLayer> layer(new GeoPackageFeatureLayer());
  layer->package_ = shared_from_this();
  layer->table_ = table;
  Status s = layer->Load();
  if (!s.ok()) return s;
  layers_.erase(std::remove_if(layers_.begin(), layers_.end(),
                               [](const std::weak_ptr<GeoPackageFeatureLayer>& w) { return w.expired(); }),
                layers_.end());
  layers_.push_back(layer);
  *out = std::move(layer);
  return Status::Ok();
}

Status GeoPackage::CreateLayer(const std::string& table, const FeatureSchema& schema,
                               const GeoPackageLayerOptions& options,
                               std::shared_ptr<GeoPackageFeatureLayer>* out) {
  Status s = RequireWritable();
  if (!s.ok()) return s;
  if (table.empty()) return Bad("a table needs a name");
  if (ReservedTableName(table)) return Bad("\"" + table + "\" is a reserved table name");
  s = schema.Validate();
  if (!s.ok()) return s;
  if (TableExists(*db_, table)) return Bad("table \"" + table + "\" already exists");
  for (const FieldDef& f : schema.fields)
    if (f.type == FieldType::kBlob && !f.choices.empty())
      return Status::Error(kUnsupported, "GeoPackage: blob field \"" + f.name + "\" has choices");

  const std::string id_column = FreeName(schema, "fid");
  const std::string geom_column = FreeName(schema, "geom");
  const char* type_name = GeometryTypeName(schema.geometry_type);

  s = Begin();
  if (!s.ok()) return s;
  s = Atomically(*db_, [&]() -> Status {
    Status r = db_->Exec(kFeatureTables);
    if (!r.ok()) return r;
    r = db_->Exec(kSchemaTables);
    if (!r.ok()) return r;

    r = db_->Exec(TableDdl(table, id_column, geom_column, schema).c_str());
    if (!r.ok()) return r;

    SqliteStmt st;
    r = st.Prepare(*db_, "INSERT INTO gpkg_contents(table_name, data_type, identifier, "
                         "description, srs_id) VALUES(?, 'features', ?, ?, 4326)");
    if (!r.ok()) return r;
    st.BindText(1, table);
    st.BindText(2, options.identifier.empty() ? table : options.identifier);
    st.BindText(3, options.description);
    st.Step(&r);
    if (!r.ok()) return r;

    SqliteStmt gc;
    r = gc.Prepare(*db_, "INSERT INTO gpkg_geometry_columns VALUES(?,?,?,4326,?,?)");
    if (!r.ok()) return r;
    gc.BindText(1, table);
    gc.BindText(2, geom_column);
    gc.BindText(3, type_name);
    gc.BindInt64(4, options.has_z ? 1 : 0);
    gc.BindInt64(5, options.has_m ? 1 : 0);
    gc.Step(&r);
    if (!r.ok()) return r;

    r = db_->Exec(RtreeSql(table, geom_column, id_column).c_str());
    if (!r.ok()) return r;
    SqliteStmt ext;
    r = ext.Prepare(*db_, "INSERT INTO gpkg_extensions VALUES(?,?,'gpkg_rtree_index',?,'write-only')");
    if (!r.ok()) return r;
    ext.BindText(1, table);
    ext.BindText(2, geom_column);
    ext.BindText(3, kRtreeDefinition);
    ext.Step(&r);
    if (!r.ok()) return r;

    for (const FieldDef& f : schema.fields) {
      r = WriteFieldMetadata(*db_, table, f);
      if (!r.ok()) return r;
    }
    return Status::Ok();
  });
  if (!s.ok()) return s;
  dirty_tables_.insert(table);

  std::shared_ptr<GeoPackageFeatureLayer> layer;
  s = OpenLayer(table, &layer);
  if (!s.ok()) return s;
  if (out != nullptr) *out = std::move(layer);
  return Status::Ok();
}

Status GeoPackage::WriteLayer(const IFeatureLayer& source, const std::string& table,
                              std::shared_ptr<GeoPackageFeatureLayer>* out) {
  std::vector<Feature> features;
  Status s = source.All(&features);
  if (!s.ok()) return s;
  GeoPackageLayerOptions options;
  options.identifier = source.Name();
  bool any = false;
  bool all_z = true;
  bool all_m = true;
  for (const Feature& f : features) {
    if (f.geometry.IsNull()) continue;
    any = true;
    all_z = all_z && f.geometry.has_z;
    all_m = all_m && f.geometry.has_m;
  }
  options.has_z = any && all_z;
  options.has_m = any && all_m;

  std::shared_ptr<GeoPackageFeatureLayer> layer;
  s = Begin();
  if (!s.ok()) return s;
  s = Atomically(*db_, [&]() -> Status {
    Status r = CreateLayer(table, source.Schema(), options, &layer);
    if (!r.ok()) return r;
    for (Feature& f : features) {
      // Z or M on only some features is dropped so the column's z/m holds.
      if (!options.has_z) f.geometry.has_z = false;
      if (!options.has_m) f.geometry.has_m = false;
      r = layer->InsertWithId(f);
      if (!r.ok()) return r;
    }
    // The copy is part of the table-level change, not an edit of the layer.
    r = layer->WriteEdits();
    if (r.ok()) layer->ClearEdits(/*saved=*/true);
    return r;
  });
  if (!s.ok()) return s;
  if (out != nullptr) *out = std::move(layer);
  return Status::Ok();
}

Status GeoPackage::Save() {
  std::vector<GeoPackageFeatureLayer*> edited;
  const auto open = OpenLayers();
  for (const auto& layer : open)
    if (layer->HasEdits()) edited.push_back(layer.get());
  return Commit(edited);
}

Status GeoPackage::Commit(const std::vector<GeoPackageFeatureLayer*>& layers) {
  if (!layers.empty()) {
    Status s = Begin();
    if (!s.ok()) return s;
    s = Atomically(*db_, [&]() -> Status {
      for (GeoPackageFeatureLayer* layer : layers) {
        Status r = layer->WriteEdits();
        if (!r.ok()) return r;
      }
      return Status::Ok();
    });
    if (!s.ok()) return s;
  }
  if (sqlite3_get_autocommit(db_->get()) != 0) return Status::Ok();
  Status s = Status::Ok();
  for (const std::string& table : dirty_tables_) {
    std::shared_ptr<GeoPackageFeatureLayer> layer;
    if (!OpenLayer(table, &layer).ok()) continue;
    SqliteStmt st;
    s = st.Prepare(*db_, "UPDATE gpkg_contents SET min_x = ?, min_y = ?, max_x = ?, "
                         "max_y = ?, last_change = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                         "WHERE table_name = ?");
    if (!s.ok()) break;
    GeoRect box;
    if (layer->Extent(&box).ok()) {
      st.BindDouble(1, box.ll.lon);
      st.BindDouble(2, box.ll.lat);
      st.BindDouble(3, box.ur.lon);
      st.BindDouble(4, box.ur.lat);
    }
    st.BindText(5, table);
    st.Step(&s);
    if (!s.ok()) break;
  }
  if (s.ok()) s = db_->Exec("COMMIT");
  if (!s.ok()) {
    // The layers' rows are in the transaction and still in their edits; the
    // rollback keeps the two from both reaching the file on the next save.
    if (!layers.empty()) {
      db_->Exec("ROLLBACK");
      dirty_tables_.clear();
    }
    return s;
  }
  dirty_tables_.clear();
  for (GeoPackageFeatureLayer* layer : layers) layer->ClearEdits(/*saved=*/true);
  return Status::Ok();
}

Status GeoPackage::Revert() {
  if (sqlite3_get_autocommit(db_->get()) == 0) {
    Status s = db_->Exec("ROLLBACK");
    if (!s.ok()) return s;
    dirty_tables_.clear();
  }
  for (const auto& layer : OpenLayers()) layer->ClearEdits(/*saved=*/false);
  return Status::Ok();
}

Status GeoPackage::SaveTableChanges() { return Commit({}); }

Status GeoPackage::RevertTableChanges() {
  if (sqlite3_get_autocommit(db_->get()) != 0) return Status::Ok();
  Status s = db_->Exec("ROLLBACK");
  if (!s.ok()) return s;
  dirty_tables_.clear();
  return Status::Ok();
}

namespace {

/// QGIS's layer_styles columns, plus ours.
const char kLayerStylesTable[] =
    "CREATE TABLE IF NOT EXISTS layer_styles (id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "f_table_catalog TEXT(256), f_table_schema TEXT(256), f_table_name TEXT(256), "
    "f_geometry_column TEXT(256), styleName TEXT(30), styleQML TEXT, styleSLD TEXT, "
    "useAsDefault BOOLEAN, description TEXT, owner TEXT(30), ui TEXT(30), "
    "update_time DATETIME DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')), fv_style TEXT);"
    "INSERT INTO gpkg_contents(table_name, data_type, identifier, last_change) "
    "  SELECT 'layer_styles', 'attributes', 'layer_styles', strftime('%Y-%m-%dT%H:%M:%fZ','now')"
    "  WHERE NOT EXISTS (SELECT 1 FROM gpkg_contents WHERE table_name = 'layer_styles');";

bool HasColumn(SqliteDb& db, const std::string& table, const std::string& column) {
  SqliteStmt st;
  if (!st.Prepare(db, ("PRAGMA table_info(" + Quote(table) + ")").c_str()).ok()) return false;
  while (st.Step())
    if (Lower(st.ColText(1)) == Lower(column)) return true;
  return false;
}

}  // namespace

Status GeoPackage::ReadLayerStyle(const std::string& table, std::string* json) const {
  if (json == nullptr) return Bad("ReadLayerStyle: null out");
  if (!TableExists(*db_, "layer_styles") || !HasColumn(*db_, "layer_styles", "fv_style"))
    return Status::Error(kNotFound, "no style for " + table);
  SqliteStmt st;
  Status s = st.Prepare(*db_, "SELECT fv_style FROM layer_styles WHERE lower(f_table_name) = lower(?) "
                              "AND fv_style IS NOT NULL ORDER BY id DESC LIMIT 1");
  if (!s.ok()) return s;
  st.BindText(1, table);
  if (!st.Step(&s)) return s.ok() ? Status::Error(kNotFound, "no style for " + table) : s;
  *json = st.ColText(0);
  return Status::Ok();
}

Status GeoPackage::WriteLayerStyle(const std::string& table, const std::string& json) {
  Status s = RequireWritable();
  if (!s.ok()) return s;
  std::string geom_column;
  {
    SqliteStmt st;
    s = st.Prepare(*db_, "SELECT column_name FROM gpkg_geometry_columns WHERE lower(table_name) = lower(?)");
    if (!s.ok()) return s;
    st.BindText(1, table);
    if (!st.Step(&s)) return s.ok() ? Status::Error(kNotFound, "\"" + table + "\" is not a feature table") : s;
    geom_column = st.ColText(0);
  }
  s = Begin();
  if (!s.ok()) return s;
  s = Atomically(*db_, [&]() -> Status {
    Status r = db_->Exec(kLayerStylesTable);
    if (!r.ok()) return r;
    if (!HasColumn(*db_, "layer_styles", "fv_style")) {
      r = db_->Exec("ALTER TABLE layer_styles ADD COLUMN fv_style TEXT");
      if (!r.ok()) return r;
    }
    SqliteStmt up;
    r = up.Prepare(*db_, "UPDATE layer_styles SET fv_style = ?, "
                         "update_time = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                         "WHERE lower(f_table_name) = lower(?) AND fv_style IS NOT NULL");
    if (!r.ok()) return r;
    up.BindText(1, json);
    up.BindText(2, table);
    up.Step(&r);
    if (!r.ok()) return r;
    if (sqlite3_changes(db_->get()) > 0) return Status::Ok();
    SqliteStmt ins;
    r = ins.Prepare(*db_, "INSERT INTO layer_styles(f_table_catalog, f_table_schema, f_table_name, "
                          "f_geometry_column, styleName, useAsDefault, description, fv_style) "
                          "VALUES('', '', ?, ?, 'Peregrine', 0, 'Peregrine feature style', ?)");
    if (!r.ok()) return r;
    ins.BindText(1, table);
    ins.BindText(2, geom_column);
    ins.BindText(3, json);
    ins.Step(&r);
    return r;
  });
  if (!s.ok()) return s;
  dirty_tables_.insert(table);
  return Status::Ok();
}

// MARK: GeoPackageFeatureLayer

Status GeoPackageFeatureLayer::Load() {
  SqliteDb& db = *package_->db_;
  if (!TableExists(db, "gpkg_geometry_columns"))
    return Status::Error(kNotFound, "GeoPackage: no feature table \"" + table_ + "\"");

  SqliteStmt st;
  Status s = st.Prepare(db, "SELECT c.identifier, g.column_name, g.geometry_type_name, g.srs_id, "
                            "g.z, g.m, s.organization, s.organization_coordsys_id "
                            "FROM gpkg_contents c JOIN gpkg_geometry_columns g "
                            "ON g.table_name = c.table_name "
                            "LEFT JOIN gpkg_spatial_ref_sys s ON s.srs_id = g.srs_id "
                            "WHERE c.table_name = ? AND c.data_type = 'features'");
  if (!s.ok()) return s;
  st.BindText(1, table_);
  if (!st.Step(&s)) {
    if (!s.ok()) return s;
    return Status::Error(kNotFound, "GeoPackage: no feature table \"" + table_ + "\"");
  }
  identifier_ = st.ColText(0);
  if (identifier_.empty()) identifier_ = table_;
  geometry_column_ = st.ColText(1);
  if (!GeometryTypeFromName(st.ColText(2).c_str(), &schema_.geometry_type))
    return Status::Error(kUnsupported,
                         "GeoPackage: geometry type " + st.ColText(2) + " is not supported");
  srs_id_ = static_cast<int32_t>(st.ColInt64(3));
  z_ = static_cast<int>(st.ColInt64(4));
  m_ = static_cast<int>(st.ColInt64(5));
  const std::string org = Lower(st.ColText(6));
  const int64_t org_id = st.ColInt64(7);
  // Undefined geographic is read as WGS-84 degrees; anything else needs a conversion (FO-D9).
  const bool wgs84 = (org == "epsg" && org_id == kWgs84) || srs_id_ == kUndefinedGeographic;
  if (!wgs84)
    return Status::Error(kUnsupported, "GeoPackage: \"" + table_ + "\" uses SRS " +
                                           st.ColText(6) + ":" + std::to_string(org_id) +
                                           "; only EPSG:4326 is supported");

  SqliteStmt info;
  s = info.Prepare(db, ("PRAGMA table_info(" + Quote(table_) + ")").c_str());
  if (!s.ok()) return s;
  schema_.fields.clear();
  while (info.Step(&s)) {
    const std::string name = info.ColText(1);
    const bool pk = info.ColInt64(5) != 0;
    if (pk && id_column_.empty() && FromSqlType(info.ColText(2)) == FieldType::kInteger) {
      id_column_ = name;
      continue;
    }
    if (Lower(name) == Lower(geometry_column_)) continue;
    FieldDef f;
    f.name = name;
    f.type = FromSqlType(info.ColText(2));
    f.required = info.ColInt64(3) != 0;
    if (info.ColType(4) != SQLITE_NULL) f.default_value = ParseDefault(info.ColText(4), f.type);
    schema_.fields.push_back(std::move(f));
  }
  if (!s.ok()) return s;
  if (id_column_.empty())
    return Status::Error(kUnsupported, "GeoPackage: \"" + table_ + "\" has no integer primary key");

  if (TableExists(db, "gpkg_data_columns")) {
    SqliteStmt dc;
    s = dc.Prepare(db, "SELECT column_name, name, title, description, constraint_name "
                       "FROM gpkg_data_columns WHERE table_name = ?");
    if (!s.ok()) return s;
    dc.BindText(1, table_);
    const bool has_constraints = TableExists(db, "gpkg_data_column_constraints");
    while (dc.Step(&s)) {
      const int i = schema_.Find(dc.ColText(0));
      if (i < 0) continue;
      FieldDef& f = schema_.fields[i];
      f.title = dc.ColText(2).empty() ? dc.ColText(1) : dc.ColText(2);
      if (f.title == f.name) f.title.clear();
      f.description = dc.ColText(3);
      const std::string constraint = dc.ColText(4);
      if (constraint.empty() || !has_constraints) continue;
      SqliteStmt cc;
      Status cs = cc.Prepare(db, "SELECT constraint_type, value, min, max, description "
                                 "FROM gpkg_data_column_constraints WHERE constraint_name = ? "
                                 "ORDER BY rowid");
      if (!cs.ok()) return cs;
      cc.BindText(1, constraint);
      while (cc.Step(&cs)) {
        const std::string type = Lower(cc.ColText(0));
        if (type == "enum") {
          f.role = FieldRole::kEnumeration;
          f.choices.push_back(FieldChoice{cc.ColText(1), cc.ColText(4)});
        } else if (type == "range") {
          // Exclusive bounds are read as inclusive: the schema has no other kind.
          if (cc.ColType(2) != SQLITE_NULL) f.min = cc.ColDouble(2);
          if (cc.ColType(3) != SQLITE_NULL) f.max = cc.ColDouble(3);
        }
      }
      if (!cs.ok()) return cs;
    }
    if (!s.ok()) return s;
  }

  if (TableExists(db, "fv_data_columns")) {
    SqliteStmt fv;
    s = fv.Prepare(db, "SELECT column_name, role, unit, range_end FROM fv_data_columns "
                       "WHERE table_name = ?");
    if (!s.ok()) return s;
    fv.BindText(1, table_);
    while (fv.Step(&s)) {
      const int i = schema_.Find(fv.ColText(0));
      if (i < 0) continue;
      FieldDef& f = schema_.fields[i];
      FieldRole role;
      if (fv.ColType(1) != SQLITE_NULL && FieldRoleFromString(fv.ColText(1), &role)) f.role = role;
      f.unit = fv.ColText(2);
      f.range_end = fv.ColText(3);
    }
    if (!s.ok()) return s;
  }

  has_rtree_ = TableExists(db, "rtree_" + table_ + "_" + geometry_column_);
  select_sql_ = "SELECT " + Quote(id_column_) + ", " + Quote(geometry_column_);
  for (const FieldDef& f : schema_.fields) select_sql_ += ", " + Quote(f.name);
  select_sql_ += " FROM " + Quote(table_);
  return Status::Ok();
}

size_t GeoPackageFeatureLayer::Count() const {
  if (rebuild_) {
    size_t n = 0;
    for (const auto& entry : edits_)
      if (entry.second.feature) ++n;
    return n;
  }
  SqliteStmt st;
  if (!st.Prepare(*package_->db_, ("SELECT COUNT(*) FROM " + Quote(table_)).c_str()).ok()) return 0;
  int64_t n = st.Step() ? st.ColInt64(0) : 0;
  for (const auto& entry : edits_) {
    const Edit& e = entry.second;
    if (e.feature && !e.stored) ++n;
    if (!e.feature && e.stored) --n;
  }
  return n > 0 ? static_cast<size_t>(n) : 0;
}

Status GeoPackageFeatureLayer::Extent(GeoRect* out) const {
  bool any = false;
  double box[4] = {0, 0, 0, 0};
  auto take = [&](const double env[4]) {
    if (!any) {
      std::copy(env, env + 4, box);
      any = true;
      return;
    }
    box[0] = std::min(box[0], env[0]);
    box[1] = std::max(box[1], env[1]);
    box[2] = std::min(box[2], env[2]);
    box[3] = std::max(box[3], env[3]);
  };
  if (!HasEdits()) {
    SqliteStmt st;
    Status s = st.Prepare(*package_->db_, ("SELECT " + Quote(geometry_column_) + " FROM " +
                                           Quote(table_) + " WHERE " + Quote(geometry_column_) +
                                           " IS NOT NULL").c_str());
    if (!s.ok()) return s;
    while (st.Step(&s)) {
      double env[4];
      bool empty = false;
      if (BlobEnvelope(static_cast<const uint8_t*>(st.ColBlob(0)),
                       static_cast<size_t>(st.ColBytes(0)), env, &empty) &&
          !empty)
        take(env);
    }
    if (!s.ok()) return s;
  } else {
    std::vector<Feature> all;
    Status s = All(&all);
    if (!s.ok()) return s;
    for (const Feature& f : all) {
      GeoRect b;
      if (!f.geometry.Envelope(&b)) continue;
      const double env[4] = {b.ll.lon, b.ur.lon, b.ll.lat, b.ur.lat};
      take(env);
    }
  }
  if (!any) return Status::Error(kNotFound, "layer \"" + identifier_ + "\" has no geometry");
  if (out != nullptr) *out = GeoRect{{box[2], box[0]}, {box[3], box[1]}};
  return Status::Ok();
}

Status GeoPackageFeatureLayer::Select(const std::string& where_sql, const std::vector<int64_t>& ids,
                                      std::vector<Feature>* out) const {
  SqliteStmt st;
  const std::string sql =
      select_sql_ + (where_sql.empty() ? "" : " WHERE " + where_sql) + " ORDER BY " +
      Quote(id_column_);
  Status s = st.Prepare(*package_->db_, sql.c_str());
  if (!s.ok()) return s;
  for (size_t i = 0; i < ids.size(); ++i) st.BindInt64(static_cast<int>(i) + 1, ids[i]);
  while (st.Step(&s)) {
    Feature f;
    f.id = st.ColInt64(0);
    if (st.ColType(1) != SQLITE_NULL) {
      Status g = ReadGpkgGeometry(static_cast<const uint8_t*>(st.ColBlob(1)),
                                  static_cast<size_t>(st.ColBytes(1)), &f.geometry);
      if (!g.ok())
        return Status::Error(g.code, g.message + " (feature " + std::to_string(f.id) + " of \"" +
                                         identifier_ + "\")");
    }
    f.values.reserve(schema_.fields.size());
    for (size_t i = 0; i < schema_.fields.size(); ++i)
      f.values.push_back(Column(st, static_cast<int>(i) + 2, schema_.fields[i].type));
    out->push_back(std::move(f));
  }
  return s;
}

void GeoPackageFeatureLayer::Merge(std::vector<Feature>* stored, const GeoRect* box) const {
  if (edits_.empty()) return;
  stored->erase(std::remove_if(stored->begin(), stored->end(),
                               [this](const Feature& f) { return edits_.count(f.id) != 0; }),
                stored->end());
  for (const auto& entry : edits_) {
    const std::optional<Feature>& f = entry.second.feature;
    if (!f) continue;
    if (box != nullptr) {
      GeoRect b;
      if (!f->geometry.Envelope(&b) || !box->Intersects(b)) continue;
    }
    stored->push_back(*f);
  }
  std::sort(stored->begin(), stored->end(), [](const Feature& a, const Feature& b) { return a.id < b.id; });
}

Status GeoPackageFeatureLayer::All(std::vector<Feature>* out) const {
  if (out == nullptr) return Bad("All: null out");
  out->clear();
  if (!rebuild_) {
    Status s = Select("", {}, out);
    if (!s.ok()) return s;
  }
  Merge(out, nullptr);
  return Status::Ok();
}

Status GeoPackageFeatureLayer::Get(int64_t id, Feature* out) const {
  const auto edit = edits_.find(id);
  if (edit != edits_.end() || rebuild_) {
    if (edit == edits_.end() || !edit->second.feature)
      return Status::Error(kNotFound, "no feature " + std::to_string(id) + " in \"" + identifier_ + "\"");
    if (out != nullptr) *out = *edit->second.feature;
    return Status::Ok();
  }
  std::vector<Feature> found;
  Status s = Select(Quote(id_column_) + " = ?", {id}, &found);
  if (!s.ok()) return s;
  if (found.empty())
    return Status::Error(kNotFound, "no feature " + std::to_string(id) + " in \"" + identifier_ + "\"");
  if (out != nullptr) *out = std::move(found.front());
  return Status::Ok();
}

Status GeoPackageFeatureLayer::Query(const GeoRect& box, std::vector<Feature>* out) const {
  if (out == nullptr) return Bad("Query: null out");
  out->clear();
  std::vector<Feature> candidates;
  Status s;
  if (rebuild_) {
    // Every row is in the edits.
  } else if (has_rtree_) {
    const std::string rtree = Quote("rtree_" + table_ + "_" + geometry_column_);
    std::string where;
    std::vector<int64_t> unused;
    const std::vector<GeoRect> parts = box.SplitAtAntimeridian();
    // The R-tree stores 32-bit bounds rounded outward, so its hits are a superset.
    for (size_t i = 0; i < parts.size(); ++i) {
      const GeoRect& p = parts[i];
      if (i > 0) where += " OR ";
      where += "(minx <= " + Number(p.ur.lon) + " AND maxx >= " + Number(p.ll.lon) +
               " AND miny <= " + Number(p.ur.lat) + " AND maxy >= " + Number(p.ll.lat) + ")";
    }
    s = Select(Quote(id_column_) + " IN (SELECT id FROM " + rtree + " WHERE " + where + ")",
               unused, &candidates);
  } else {
    s = Select(Quote(geometry_column_) + " IS NOT NULL", {}, &candidates);
  }
  if (!s.ok()) return s;
  Merge(&candidates, &box);
  for (Feature& f : candidates) {
    GeoRect b;
    if (f.geometry.Envelope(&b) && box.Intersects(b)) out->push_back(std::move(f));
  }
  return Status::Ok();
}

Status GeoPackageFeatureLayer::CheckFeature(Feature* f) const {
  if (f->values.size() > schema_.fields.size())
    return Status::Error(kInvalidArg, "feature has " + std::to_string(f->values.size()) +
                                          " values for " + std::to_string(schema_.fields.size()) +
                                          " fields");
  f->values.resize(schema_.fields.size());
  for (size_t i = 0; i < f->values.size(); ++i) {
    Status s = ValidateValue(schema_.fields[i], f->values[i]);
    if (!s.ok()) return s;
  }
  const Geometry& g = f->geometry;
  if (g.IsNull()) return Status::Ok();
  if (schema_.geometry_type != GeometryType::kGeometry && g.type != schema_.geometry_type)
    return Bad(std::string("a ") + GeometryTypeName(g.type) + " in a " +
               GeometryTypeName(schema_.geometry_type) + " layer");
  if ((g.has_z && z_ == 0) || (!g.has_z && z_ == 1))
    return Bad(std::string("layer \"") + identifier_ + (z_ == 0 ? "\" has no Z" : "\" requires Z"));
  if ((g.has_m && m_ == 0) || (!g.has_m && m_ == 1))
    return Bad(std::string("layer \"") + identifier_ + (m_ == 0 ? "\" has no M" : "\" requires M"));
  return Status::Ok();
}

Status GeoPackageFeatureLayer::NextId(int64_t* out) {
  if (next_id_ == 0) {
    SqliteDb& db = *package_->db_;
    int64_t used = 0;
    SqliteStmt max;
    Status s = max.Prepare(db, ("SELECT MAX(" + Quote(id_column_) + ") FROM " + Quote(table_)).c_str());
    if (!s.ok()) return s;
    if (max.Step(&s)) used = max.ColInt64(0);
    if (!s.ok()) return s;
    // AUTOINCREMENT never reuses an id, even one whose row was deleted.
    if (TableExists(db, "sqlite_sequence")) {
      SqliteStmt seq;
      s = seq.Prepare(db, "SELECT seq FROM sqlite_sequence WHERE name = ?");
      if (!s.ok()) return s;
      seq.BindText(1, table_);
      if (seq.Step(&s)) used = std::max(used, seq.ColInt64(0));
      if (!s.ok()) return s;
    }
    if (!edits_.empty()) used = std::max(used, edits_.rbegin()->first);
    next_id_ = used + 1;
  }
  *out = next_id_;
  return Status::Ok();
}

Status GeoPackageFeatureLayer::Insert(Feature* feature) {
  if (feature == nullptr) return Bad("Insert: null feature");
  Status s = package_->RequireWritable();
  if (!s.ok()) return s;
  Feature f = *feature;
  s = CheckFeature(&f);
  if (!s.ok()) return s;
  int64_t id = 0;
  s = NextId(&id);
  if (!s.ok()) return s;
  f.id = id;
  next_id_ = id + 1;
  feature->id = id;
  feature->values = f.values;
  edits_[id] = Edit{std::move(f), /*stored=*/false, /*geometry_changed=*/true};
  return Status::Ok();
}

Status GeoPackageFeatureLayer::InsertWithId(const Feature& feature) {
  Status s = package_->RequireWritable();
  if (!s.ok()) return s;
  if (feature.id <= 0) return Bad("feature id must be positive");
  if (Get(feature.id, nullptr).ok())
    return Bad("feature id " + std::to_string(feature.id) + " is in use");
  Feature f = feature;
  s = CheckFeature(&f);
  if (!s.ok()) return s;
  int64_t next = 0;
  s = NextId(&next);
  if (!s.ok()) return s;
  next_id_ = std::max(next, f.id + 1);
  // A row deleted and restored (an undo) is still in the file.
  const auto deleted = edits_.find(f.id);
  const bool stored = deleted != edits_.end() && deleted->second.stored;
  edits_[f.id] = Edit{std::move(f), stored, /*geometry_changed=*/true};
  return Status::Ok();
}

Status GeoPackageFeatureLayer::Update(const Feature& feature) {
  Status s = package_->RequireWritable();
  if (!s.ok()) return s;
  Feature old;
  s = Get(feature.id, &old);
  if (!s.ok()) return s;
  Feature f = feature;
  s = CheckFeature(&f);
  if (!s.ok()) return s;
  const auto it = edits_.find(f.id);
  Edit e;
  e.stored = it == edits_.end() ? true : it->second.stored;
  e.geometry_changed = (it != edits_.end() && it->second.geometry_changed) || !(old.geometry == f.geometry);
  e.feature = std::move(f);
  edits_[feature.id] = std::move(e);
  return Status::Ok();
}

Status GeoPackageFeatureLayer::Delete(int64_t id) {
  Status s = package_->RequireWritable();
  if (!s.ok()) return s;
  s = Get(id, nullptr);
  if (!s.ok()) return s;
  const auto it = edits_.find(id);
  const bool stored = it == edits_.end() ? true : it->second.stored;
  if (!stored || rebuild_) {
    edits_.erase(id);
  } else {
    edits_[id] = Edit{std::nullopt, /*stored=*/true, /*geometry_changed=*/false};
  }
  return Status::Ok();
}

Status GeoPackageFeatureLayer::ChangeSchema(const SchemaChange& change) {
  Status s = package_->RequireWritable();
  if (!s.ok()) return s;
  s = ValidateSchemaChange(schema_, change);
  if (!s.ok()) return s;
  for (const FieldDef& f : change.schema.fields) {
    if (Lower(f.name) == Lower(id_column_) || Lower(f.name) == Lower(geometry_column_))
      return Bad("\"" + f.name + "\" is the table's id or geometry column");
    if (f.type == FieldType::kBlob && !f.choices.empty())
      return Status::Error(kUnsupported, "GeoPackage: blob field \"" + f.name + "\" has choices");
  }

  std::vector<Feature> features;
  s = All(&features);
  if (!s.ok()) return s;
  std::map<int64_t, Edit> next;
  for (const Feature& f : features) {
    Feature mapped;
    RemapFeature(f, schema_, change, &mapped);
    for (size_t i = 0; i < mapped.values.size(); ++i) {
      s = ValidateValue(change.schema.fields[i], mapped.values[i]);
      if (!s.ok()) return Status::Error(s.code, s.message + " (feature " + std::to_string(f.id) + ")");
    }
    const auto old = edits_.find(f.id);
    Edit e;
    e.stored = old == edits_.end() ? !rebuild_ : old->second.stored;
    e.geometry_changed = old != edits_.end() && old->second.geometry_changed;
    e.feature = std::move(mapped);
    next.emplace(f.id, std::move(e));
  }
  edits_ = std::move(next);
  rebuild_ = true;
  schema_ = change.schema;
  return Status::Ok();
}

Status GeoPackageFeatureLayer::WriteRow(const Feature& f, bool replace) {
  SqliteDb& db = *package_->db_;
  std::vector<uint8_t> blob;
  if (!f.geometry.IsNull()) {
    Status s = WriteGpkgGeometry(f.geometry, srs_id_, &blob);
    if (!s.ok()) return s;
  }
  std::string sql;
  if (replace) {
    sql = "UPDATE " + Quote(table_) + " SET " + Quote(geometry_column_) + " = ?";
    for (const FieldDef& d : schema_.fields) sql += ", " + Quote(d.name) + " = ?";
    sql += " WHERE " + Quote(id_column_) + " = ?";
  } else {
    sql = "INSERT INTO " + Quote(table_) + " (" + Quote(geometry_column_);
    for (const FieldDef& d : schema_.fields) sql += ", " + Quote(d.name);
    sql += ", " + Quote(id_column_) + ") VALUES (?";
    for (size_t i = 0; i < schema_.fields.size(); ++i) sql += ", ?";
    sql += ", ?)";
  }
  SqliteStmt st;
  Status s = st.Prepare(db, sql.c_str());
  if (!s.ok()) return s;
  if (!blob.empty()) st.BindBlob(1, blob.data(), blob.size());
  for (size_t i = 0; i < f.values.size(); ++i) Bind(st, static_cast<int>(i) + 2, f.values[i]);
  st.BindInt64(static_cast<int>(f.values.size()) + 2, f.id);
  st.Step(&s);
  if (!s.ok()) return Status::Error(s.code, s.message + ": " + sqlite3_errmsg(db.get()));
  return Status::Ok();
}

Status GeoPackageFeatureLayer::WriteEdits() {
  if (!HasEdits()) return Status::Ok();
  if (rebuild_) {
    Status s = RebuildTable();
    if (!s.ok()) return s;
  } else {
    for (const auto& entry : edits_) {
      const Edit& e = entry.second;
      Status s = Status::Ok();
      if (e.feature) {
        s = WriteRow(*e.feature, e.stored);
      } else if (e.stored) {
        SqliteStmt del;
        s = del.Prepare(*package_->db_,
                        ("DELETE FROM " + Quote(table_) + " WHERE " + Quote(id_column_) + " = ?").c_str());
        if (s.ok()) {
          del.BindInt64(1, entry.first);
          del.Step(&s);
        }
      }
      if (!s.ok()) return s;
    }
  }
  // An id inserted and deleted before the save never reached the table; the
  // sequence still records it so AUTOINCREMENT does not hand it out again.
  SqliteDb& db = *package_->db_;
  if (next_id_ > 1 && TableExists(db, "sqlite_sequence")) {
    SqliteStmt up;
    Status s = up.Prepare(db, "UPDATE sqlite_sequence SET seq = MAX(seq, ?) WHERE name = ?");
    if (!s.ok()) return s;
    up.BindInt64(1, next_id_ - 1);
    up.BindText(2, table_);
    up.Step(&s);
    if (!s.ok()) return s;
  }
  package_->dirty_tables_.insert(table_);
  return Status::Ok();
}

// The table is rebuilt rather than altered column by column: retyping and
// reordering need a new table anyway, and one path keeps every case alike.
Status GeoPackageFeatureLayer::RebuildTable() {
  SqliteDb& db = *package_->db_;
  // Geometry the edits did not touch is copied as stored.
  std::map<int64_t, std::vector<uint8_t>> blobs;
  SqliteStmt geo;
  Status r = geo.Prepare(db, ("SELECT " + Quote(id_column_) + ", " + Quote(geometry_column_) +
                              " FROM " + Quote(table_)).c_str());
  if (!r.ok()) return r;
  while (geo.Step(&r)) {
    const int64_t id = geo.ColInt64(0);
    const auto e = edits_.find(id);
    if (geo.ColType(1) == SQLITE_NULL || e == edits_.end() || e->second.geometry_changed) continue;
    const auto* p = static_cast<const uint8_t*>(geo.ColBlob(1));
    blobs[id].assign(p, p + geo.ColBytes(1));
  }
  if (!r.ok()) return r;

  // Triggers and indexes go with the old table and are recreated on the new one.
  std::vector<std::pair<std::string, std::string>> dependents;  // type, sql
  SqliteStmt dep;
  r = dep.Prepare(db, "SELECT type, sql FROM sqlite_master WHERE tbl_name = ? "
                      "AND type IN ('trigger', 'index') AND sql IS NOT NULL");
  if (!r.ok()) return r;
  dep.BindText(1, table_);
  while (dep.Step(&r)) dependents.emplace_back(dep.ColText(0), dep.ColText(1));
  if (!r.ok()) return r;
  int64_t sequence = -1;
  if (TableExists(db, "sqlite_sequence")) {
    SqliteStmt seq;
    r = seq.Prepare(db, "SELECT seq FROM sqlite_sequence WHERE name = ?");
    if (!r.ok()) return r;
    seq.BindText(1, table_);
    if (seq.Step(&r)) sequence = seq.ColInt64(0);
    if (!r.ok()) return r;
  }

  const std::string temp = "fv_rebuild_" + table_;
  r = db.Exec(("DROP TABLE IF EXISTS " + Quote(temp)).c_str());
  if (r.ok()) r = db.Exec(TableDdl(temp, id_column_, geometry_column_, schema_).c_str());
  if (!r.ok()) return r;
  std::string sql = "INSERT INTO " + Quote(temp) + " (" + Quote(id_column_) + ", " + Quote(geometry_column_);
  for (const FieldDef& f : schema_.fields) sql += ", " + Quote(f.name);
  sql += ") VALUES (?, ?";
  for (size_t i = 0; i < schema_.fields.size(); ++i) sql += ", ?";
  sql += ")";
  SqliteStmt ins;
  r = ins.Prepare(db, sql.c_str());
  if (!r.ok()) return r;
  for (const auto& entry : edits_) {
    if (!entry.second.feature) continue;
    const Feature& f = *entry.second.feature;
    ins.Reset();
    ins.BindInt64(1, f.id);
    std::vector<uint8_t> encoded;
    const auto blob = blobs.find(f.id);
    if (blob != blobs.end()) {
      ins.BindBlob(2, blob->second.data(), blob->second.size());
    } else if (!f.geometry.IsNull()) {
      r = WriteGpkgGeometry(f.geometry, srs_id_, &encoded);
      if (!r.ok()) return r;
      ins.BindBlob(2, encoded.data(), encoded.size());
    }
    for (size_t i = 0; i < f.values.size(); ++i) Bind(ins, static_cast<int>(i) + 3, f.values[i]);
    ins.Step(&r);
    if (!r.ok()) return Status::Error(r.code, r.message + ": " + sqlite3_errmsg(db.get()));
  }

  r = db.Exec(("DROP TABLE " + Quote(table_)).c_str());
  if (r.ok()) r = db.Exec(("ALTER TABLE " + Quote(temp) + " RENAME TO " + Quote(table_)).c_str());
  if (!r.ok()) return r;
  for (const auto& d : dependents) {
    r = db.Exec(d.second.c_str());
    if (r.ok()) continue;
    // An index on a deleted column cannot come back; a trigger is part of the file's integrity.
    if (d.first == "trigger") return r;
    FV_LOG_WARNING(package_->Path() << ": index dropped with its column: " << r.message);
  }
  // The R-tree outlives the old table and the new rows went in without its
  // triggers, so it is refilled from them.
  if (has_rtree_) {
    const std::string rtree = Quote("rtree_" + table_ + "_" + geometry_column_);
    const std::string g = Quote(geometry_column_);
    r = db.Exec(("DELETE FROM " + rtree + "; INSERT INTO " + rtree + " SELECT " + Quote(id_column_) +
                 ", ST_MinX(" + g + "), ST_MaxX(" + g + "), ST_MinY(" + g + "), ST_MaxY(" + g + ") FROM " +
                 Quote(table_) + " WHERE " + g + " NOT NULL AND NOT ST_IsEmpty(" + g + ")")
                    .c_str());
    if (!r.ok()) return r;
  }
  if (sequence >= 0) {
    SqliteStmt seq;
    r = seq.Prepare(db, "UPDATE sqlite_sequence SET seq = MAX(seq, ?) WHERE name = ?");
    if (!r.ok()) return r;
    seq.BindInt64(1, sequence);
    seq.BindText(2, table_);
    seq.Step(&r);
    if (!r.ok()) return r;
  }

  r = DropFieldMetadata(db, table_);
  if (!r.ok()) return r;
  for (const FieldDef& f : schema_.fields) {
    r = WriteFieldMetadata(db, table_, f);
    if (!r.ok()) return r;
  }
  return Status::Ok();
}

void GeoPackageFeatureLayer::ClearEdits(bool saved) {
  const bool reload = rebuild_;
  edits_.clear();
  rebuild_ = false;
  if (!saved) next_id_ = 0;
  if (!reload) return;
  // The schema is read back: the rebuilt table's after a save, the old one's after a discard.
  id_column_.clear();
  schema_ = FeatureSchema{};
  Status s = Load();
  if (!s.ok()) FV_LOG_WARNING(package_->Path() << ": " << table_ << ": " << s.message);
}

Status GeoPackageFeatureLayer::Save(const std::string* style_json) {
  Status s = package_->RequireWritable();
  if (!s.ok()) return s;
  sqlite3* db = package_->db_->get();
  const bool began = sqlite3_get_autocommit(db) != 0;
  if (style_json != nullptr) s = package_->WriteLayerStyle(table_, *style_json);
  if (s.ok()) s = package_->Commit({this});
  // A transaction this call opened holds only its own writes, so it goes.
  if (!s.ok() && began && sqlite3_get_autocommit(db) == 0) {
    package_->db_->Exec("ROLLBACK");
    package_->dirty_tables_.clear();
  }
  return s;
}

}  // namespace fv
