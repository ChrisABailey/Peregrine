// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_layer_fields.h — the Layer Fields and New Layer dialogs'
/// view-models (port/feature-overlay-plan.md §4).
///
/// Layer Fields drafts a feature overlay's schema: fields are added, renamed,
/// retyped, reordered and deleted, and each remembers the field it came from
/// so `Apply` carries the values across (`SchemaChange`). `Assess` says what
/// would be lost, so the shell can ask first. New Layer picks a template,
/// geometry, name, file and format, and writes the empty layer.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fvkit/feature/schema_change.h"

namespace fv {
class FeatureOverlay;
class Overlay;

namespace desk {

/// The storage types and roles a field editor offers, in pop-up order.
const std::vector<FieldType>& EditableFieldTypes();
const std::vector<FieldRole>& EditableFieldRoles();
/// "Text", "Whole number", ... and "None", "Phone", ...
std::string FieldTypeTitle(FieldType type);
std::string FieldRoleTitle(FieldRole role);

/// An enumeration's choices as lines of "value" or "value = Label".
std::string FormatChoices(const std::vector<FieldChoice>& choices);
/// Blank lines are skipped; kInvalidArg for a repeated value.
Status ParseChoices(const std::string& text, std::vector<FieldChoice>* out);

class LayerFieldsModel {
 public:
  /// Drafts `overlay`'s schema. kNotFound without a layer.
  static Status Open(const std::shared_ptr<FeatureOverlay>& overlay, std::string title,
                     std::shared_ptr<LayerFieldsModel>* out);

  const std::string& title() const { return title_; }
  const Overlay* overlay() const { return overlay_id_; }
  GeometryType geometry() const { return draft_.schema.geometry_type; }
  bool dirty() const;

  size_t FieldCount() const { return draft_.schema.fields.size(); }
  /// A default field past the end.
  const FieldDef& Field(size_t i) const;
  /// The name the field had when the dialog opened; "" for a new field.
  std::string OriginalName(size_t i) const;

  /// Appends a text field named "field", "field_1", ...; returns its index.
  size_t AddField();
  Status RemoveField(size_t i);
  /// Moves field `from` to position `to`.
  Status MoveField(size_t from, size_t to);
  /// Replaces field `i`. kInvalidArg for an empty name or one another field
  /// has (case-insensitively). A retyped field's default, bounds and choices
  /// are dropped when they no longer fit its type.
  Status SetField(size_t i, const FieldDef& field);

  /// The draft as a change to the layer's current schema.
  SchemaChange Change() const { return draft_; }
  /// kInvalidArg when the draft schema does not validate.
  Status Validate() const;
  /// What applying would lose; kNotFound when the overlay has closed.
  Status Assess(SchemaChangeImpact* out) const;
  /// Changes the overlay's layer to the draft; the draft then restarts from
  /// the new schema. kNotFound when the overlay has closed.
  Status Apply();
  /// Back to the layer's current schema.
  Status Revert();

 private:
  LayerFieldsModel() = default;
  void Reset(const FeatureSchema& schema);

  std::weak_ptr<FeatureOverlay> overlay_;
  const Overlay* overlay_id_ = nullptr;
  std::string title_;
  FeatureSchema original_;
  SchemaChange draft_;
};

/// The geometry types a new layer may have, in pop-up order: point, line,
/// area, their Multi- forms, then any.
const std::vector<GeometryType>& NewLayerGeometries();
/// "Point", "Line", "Area", "Multipoint", ..., "Any geometry".
std::string GeometryTitle(GeometryType type);

class NewLayerModel {
 public:
  /// Starts on the first template.
  NewLayerModel();

  size_t TemplateCount() const;
  std::string TemplateTitle(size_t i) const;
  size_t template_index() const { return template_; }
  /// Takes the template's fields and geometry; the name follows the template
  /// until it is set by hand.
  Status SetTemplate(size_t i);

  GeometryType geometry() const { return schema_.geometry_type; }
  Status SetGeometry(GeometryType type);
  const FeatureSchema& schema() const { return schema_; }

  const std::string& name() const { return name_; }
  void SetName(std::string name);

  /// 0 GeoPackage, 1 GeoJSON (`FeatureOverlay::kFormat*`).
  int format() const { return format_; }
  Status SetFormat(int format);
  const std::string& path() const { return path_; }
  /// A path ending in .geojson or .json selects GeoJSON, .gpkg GeoPackage;
  /// any other gets the current format's extension.
  void SetPath(std::string path);

  /// kInvalidArg for an empty name or path.
  Status Validate() const;
  /// Writes the empty layer to `path()` (`CreateFeatureLayerFile`).
  Status Create() const;

 private:
  size_t template_ = 0;
  FeatureSchema schema_;
  std::string name_;
  bool name_by_hand_ = false;
  int format_ = 0;
  std::string path_;
};

}  // namespace desk
}  // namespace fv
