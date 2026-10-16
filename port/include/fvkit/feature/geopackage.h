// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/geopackage.h — GeoPackage (OGC 12-128r18) feature layers,
/// read and write (port/feature-overlay-plan.md §1–2, FO2).
///
/// A `GeoPackage` is one open file holding any number of feature tables. Each
/// table is a `GeoPackageFeatureLayer`. A layer holds its feature and field
/// edits in memory until `GeoPackageFeatureLayer::Save` writes them, so the
/// layers of one file save and revert independently. Table-level changes
/// (`CreateLayer`, `WriteLayer`, `WriteLayerStyle`) run inside one transaction
/// per file, which `GeoPackage::Save` commits together with every open
/// layer's edits. Every table written here has an R-tree spatial index kept by the
/// spec's triggers. Field titles, descriptions, choices and bounds go in
/// `gpkg_data_columns` and `gpkg_data_column_constraints`, where QGIS reads
/// them; roles, units and date ranges go in the `fv_data_columns` extension
/// table. Not thread-safe: one thread uses a file and its layers.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
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

  /// True while a table-level change is uncommitted or an open layer holds edits.
  bool HasUnsavedChanges() const;
  /// Writes every open layer's edits, brings `gpkg_contents` bounds and
  /// last_change up to date for every table written, then commits.
  Status Save();
  /// Discards every table-level change and every open layer's edits since the
  /// last save. A layer created since then no longer exists; its handle
  /// returns errors.
  Status Revert();
  /// Commits the table-level changes alone; open layers keep their edits.
  Status SaveTableChanges();
  /// Rolls back the table-level changes alone; open layers keep their edits.
  Status RevertTableChanges();

 private:
  friend class GeoPackageFeatureLayer;
  GeoPackage(std::string path, bool writable);

  Status RequireWritable() const;
  /// Begins the edit transaction if none is open.
  Status Begin();
  Status RegisterFunctions();
  /// The layers opened from this file that are still held somewhere.
  std::vector<std::shared_ptr<GeoPackageFeatureLayer>> OpenLayers() const;
  /// Writes `layers`' edits into the transaction and commits it. On failure
  /// the layers keep their edits and the file is as before.
  Status Commit(const std::vector<GeoPackageFeatureLayer*>& layers);

  std::string path_;
  bool writable_ = false;
  std::unique_ptr<detail::SqliteDb> db_;
  std::set<std::string> dirty_tables_;
  std::vector<std::weak_ptr<GeoPackageFeatureLayer>> layers_;
};

/// One feature table of an open GeoPackage. Reads merge the file with this
/// layer's unsaved edits; ids are the table's integer primary key.
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
  /// Remaps every feature to the new schema now; `Save` rebuilds the table,
  /// keeping ids, unedited geometry blobs, the R-tree and the table's other
  /// triggers and indexes, and rewrites the field metadata. A new field may
  /// not take the id or geometry column's name.
  Status ChangeSchema(const SchemaChange& change) override;

  /// True when the layer holds edits not yet written to the file.
  bool HasEdits() const { return rebuild_ || !edits_.empty(); }
  /// Writes this layer's edits, and `style_json` as its style when given, and
  /// commits them with any table-level change already pending in the file.
  /// Other layers' edits stay unsaved. On failure nothing this call wrote
  /// stays in the file's transaction.
  Status Save(const std::string* style_json = nullptr);

  const std::string& Table() const { return table_; }
  const std::shared_ptr<GeoPackage>& Package() const { return package_; }

 private:
  friend class GeoPackage;
  GeoPackageFeatureLayer() = default;

  /// An unsaved change to one feature. `feature` is empty for a deletion.
  struct Edit {
    std::optional<Feature> feature;
    bool stored = false;            ///< the row exists in the file
    bool geometry_changed = false;  ///< the stored geometry blob is stale
  };

  /// Reads `table_`'s schema, geometry column and metadata from the file.
  Status Load();
  Status Select(const std::string& where_sql, const std::vector<int64_t>& ids,
                std::vector<Feature>* out) const;
  /// `stored` with this layer's edits applied, in id order.
  void Merge(std::vector<Feature>* stored, const GeoRect* box) const;
  Status CheckFeature(Feature* feature) const;
  /// The id the next `Insert` takes: past every id the file and the edits have used.
  Status NextId(int64_t* out);
  /// Writes the edits into the open transaction; the caller commits.
  Status WriteEdits();
  /// Writes one feature row, as an INSERT with its id or an UPDATE.
  Status WriteRow(const Feature& f, bool replace);
  /// Rebuilds the table under `schema_` from the edits, which hold every row.
  Status RebuildTable();
  /// Forgets the edits after they were committed (`saved`) or discarded.
  void ClearEdits(bool saved);

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

  std::map<int64_t, Edit> edits_;
  /// The schema changed: `edits_` holds every row and `Save` rebuilds the table.
  bool rebuild_ = false;
  int64_t next_id_ = 0;  ///< 0 until the first insert reads it from the file
};

}  // namespace fv
