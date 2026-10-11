// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/geopackage.h — GeoPackage (OGC 12-128r18) feature layers,
/// read and write (port/feature-overlay-plan.md §1–2, FO2).
///
/// A `GeoPackage` is one open file holding any number of feature tables. Each
/// table is a `GeoPackageFeatureLayer`. Edits run inside one transaction per
/// file: the first edit begins it, `Save` commits it and `Revert` rolls it
/// back. Every table written here has an R-tree spatial index kept by the
/// spec's triggers. Field titles, descriptions, choices and bounds go in
/// `gpkg_data_columns` and `gpkg_data_column_constraints`, where QGIS reads
/// them; roles, units and date ranges go in the `fv_data_columns` extension
/// table. Not thread-safe: one thread uses a file and its layers.
#pragma once

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "fvkit/feature/layer.h"

namespace fv {

namespace detail {
class SqliteDb;
}

/// Decodes a GeoPackage geometry blob (the "GP" header, then WKB).
/// `srs_id`, when set, receives the header's SRS id. kUnsupported for the
/// extended-geometry flag; kInvalidArg for a malformed blob.
Status ReadGpkgGeometry(const uint8_t* data, size_t size, Geometry* out,
                        int32_t* srs_id = nullptr);
/// Encodes a GeoPackage geometry blob: little-endian header, an x/y envelope
/// except for points, the empty flag for an empty geometry. kInvalidArg for a
/// null geometry, which is stored as SQL NULL instead.
Status WriteGpkgGeometry(const Geometry& geometry, int32_t srs_id, std::vector<uint8_t>* out);

/// One row of `gpkg_contents` with data_type "features".
struct GeoPackageLayerInfo {
  std::string table;
  std::string identifier;  ///< display name; the table name when the file has none
  std::string description;
  GeometryType geometry_type = GeometryType::kGeometry;
};

struct GeoPackageLayerOptions {
  std::string identifier;  ///< empty = the table name
  std::string description;
  bool has_z = false;  ///< every geometry carries Z (geometry_columns z = 1)
  bool has_m = false;
};

class GeoPackageFeatureLayer;

class GeoPackage : public std::enable_shared_from_this<GeoPackage> {
 public:
  /// Creates a new, empty GeoPackage. kInvalidArg when `path` already exists.
  static Status Create(const std::string& path, std::shared_ptr<GeoPackage>* out);
  /// Opens an existing GeoPackage. kInvalidArg when the file lacks the core
  /// tables; kIoError when it cannot be opened.
  static Status Open(const std::string& path, bool writable, std::shared_ptr<GeoPackage>* out);

  ~GeoPackage();
  GeoPackage(const GeoPackage&) = delete;
  GeoPackage& operator=(const GeoPackage&) = delete;

  const std::string& Path() const { return path_; }
  bool Writable() const { return writable_; }

  /// The feature tables, in `gpkg_contents` order.
  Status Layers(std::vector<GeoPackageLayerInfo>* out) const;
  /// kNotFound for a table that is not a feature table; kUnsupported for an
  /// SRS other than EPSG:4326 or undefined geographic (FO-D9), or a curve type.
  Status OpenLayer(const std::string& table, std::shared_ptr<GeoPackageFeatureLayer>* out);
  /// Adds an empty feature table with its index and metadata, inside the edit
  /// transaction. kInvalidArg for an invalid schema, a reserved or taken table
  /// name; kUnsupported when the schema has a blob field with a choice list.
  Status CreateLayer(const std::string& table, const FeatureSchema& schema,
                     const GeoPackageLayerOptions& options,
                     std::shared_ptr<GeoPackageFeatureLayer>* out);
  /// Creates a table from `source`'s schema and copies every feature with its
  /// id. Z and M are set from the data.
  Status WriteLayer(const IFeatureLayer& source, const std::string& table,
                    std::shared_ptr<GeoPackageFeatureLayer>* out);

  /// The style JSON (`WriteFeatureStyleJson`) stored for feature table
  /// `table` in `layer_styles`. kNotFound when the file holds none.
  Status ReadLayerStyle(const std::string& table, std::string* json) const;
  /// Stores `json` as `table`'s style inside the edit transaction. The row
  /// goes in `layer_styles`, QGIS's table, in an `fv_style` column added to
  /// it; `useAsDefault` is 0 so QGIS does not apply a row it cannot read.
  /// kNotFound when `table` is not a feature table.
  Status WriteLayerStyle(const std::string& table, const std::string& json);

  /// True between the first edit and `Save` or `Revert`.
  bool HasUnsavedChanges() const;
  /// Brings `gpkg_contents` bounds and last_change up to date for every table
  /// edited, then commits.
  Status Save();
  /// Discards every edit since the last `Save`. A layer created since then
  /// no longer exists; its handle returns errors.
  Status Revert();

 private:
  friend class GeoPackageFeatureLayer;
  GeoPackage(std::string path, bool writable);

  Status RequireWritable() const;
  /// Begins the edit transaction if none is open.
  Status Begin();
  Status RegisterFunctions();

  std::string path_;
  bool writable_ = false;
  std::unique_ptr<detail::SqliteDb> db_;
  std::set<std::string> dirty_tables_;
};

/// One feature table of an open GeoPackage. Reads go to the file; ids are the
/// table's integer primary key.
class GeoPackageFeatureLayer : public IEditableFeatureLayer {
 public:
  std::string Name() const override { return identifier_; }
  const FeatureSchema& Schema() const override { return schema_; }
  size_t Count() const override;
  Status Extent(GeoRect* out) const override;
  Status All(std::vector<Feature>* out) const override;
  /// Uses the R-tree when the table has one, then checks each envelope.
  Status Query(const GeoRect& box, std::vector<Feature>* out) const override;
  Status Get(int64_t id, Feature* out) const override;
  /// Geometry must match the layer's type and Z/M; values the schema.
  Status Insert(Feature* feature) override;
  Status Update(const Feature& feature) override;
  Status Delete(int64_t id) override;

  Status InsertWithId(const Feature& feature) override;
  /// Rebuilds the table inside the edit transaction, keeping ids, geometry
  /// blobs, the R-tree and the table's other triggers and indexes; the field
  /// metadata is rewritten from the new schema. A new field may not take the
  /// id or geometry column's name.
  Status ChangeSchema(const SchemaChange& change) override;

  const std::string& Table() const { return table_; }
  const std::shared_ptr<GeoPackage>& Package() const { return package_; }

 private:
  friend class GeoPackage;
  GeoPackageFeatureLayer() = default;

  /// Reads `table_`'s schema, geometry column and metadata from the file.
  Status Load();
  Status Select(const std::string& where_sql, const std::vector<int64_t>& ids,
                std::vector<Feature>* out) const;
  Status CheckFeature(Feature* feature) const;
  Status Write(const Feature& feature, bool with_id, bool replace, int64_t* id_out);

  std::shared_ptr<GeoPackage> package_;
  std::string table_;
  std::string identifier_;
  std::string id_column_;
  std::string geometry_column_;
  int32_t srs_id_ = 4326;
  int z_ = 0;  ///< gpkg_geometry_columns z/m: 0 prohibited, 1 mandatory, 2 optional
  int m_ = 0;
  bool has_rtree_ = false;
  FeatureSchema schema_;
  std::string select_sql_;  ///< SELECT id, geom, fields... FROM table
};

}  // namespace fv
