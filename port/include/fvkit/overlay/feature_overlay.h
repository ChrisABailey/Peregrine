// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/overlay/feature_overlay.h — the `fv.features` file overlay: one
/// feature layer from a GeoPackage or GeoJSON file, drawn through a
/// `FeatureStyle` (port/feature-overlay-plan.md §1–3). The style compiles to
/// MapLibre JSON and renders through `MapLibreStyleEngine` and
/// `VectorRenderer`, like any other vector product. The style travels with
/// the file: in `layer_styles` in a GeoPackage, in `fv_style` in GeoJSON.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fvkit/app/capabilities.h"
#include "fvkit/app/search.h"
#include "fvkit/feature/feature_filter.h"
#include "fvkit/feature/geopackage.h"
#include "fvkit/feature/layer.h"
#include "fvkit/feature/style.h"
#include "fvkit/overlay/feature_edit.h"
#include "fvkit/overlay/overlay.h"
#include "fvkit/proj.h"
#include "fvkit/style/maplibre_style.h"
#include "fvkit/vector/renderer.h"

namespace fv {

class OverlayManager;

/// Where `FeatureOverlay::ExportSelection` writes the selected features.
struct FeatureExport {
  enum class Mode {
    kNewFile,   ///< a new GeoPackage at `path`, replacing any file there
    kNewTable,  ///< a new table in the existing GeoPackage at `path`
    kAppend,    ///< appended to `table` of the existing GeoPackage at `path`
  };
  Mode mode = Mode::kNewFile;
  std::string path;
  std::string table;  ///< kAppend only
};

/// True when features of schema `from` append to a table of schema `to`
/// unchanged: the same geometry type, and the same fields by name
/// (case-insensitively) and type, in any order.
bool SchemasMatchForAppend(const FeatureSchema& from, const FeatureSchema& to);

/// True while an overlay holds the GeoPackage at `path` open.
bool PackageIsOpen(const std::string& path);

class FeatureOverlay : public Overlay,
                       public app::Persistence,
                       public app::SearchProvider,
                       public app::EditTarget,
                       public app::HitTest,
                       public app::EditHint,
                       public app::SnapTo,
                       public app::ItemInfo {
 public:
  explicit FeatureOverlay(std::string name = "Features");
  ~FeatureOverlay() override;

  static const char kTypeId[];           ///< "fv.features"
  static const char kExtension[];        ///< "gpkg"
  static const char kGeoJsonExtension[]; ///< "geojson"
  /// `FileTypeDesc::save_filters` indexes.
  static constexpr int kFormatGeoPackage = 0;
  static constexpr int kFormatGeoJson = 1;

  app::Persistence* AsPersistence() override { return this; }
  app::SearchProvider* AsSearch() override { return this; }
  app::EditTarget* AsEditTarget() override { return this; }
  app::SnapTo* AsSnapTo() override { return this; }
  app::HitTest* AsHitTest() override { return this; }
  app::EditHint* AsEditHint() override { return this; }
  app::ItemInfo* AsItemInfo() override { return this; }
  bool SupportsOpacity() const override { return true; }

  /// Draws the features whose envelope meets the view at `opacity()`, then,
  /// while edited, the editing handles at full opacity.
  Status OnDraw(const MapProjection& proj, ICanvas& canvas) override;

  /// The geometry editor; it takes the input SPI below.
  FeatureEditSession& edit() { return *edit_; }
  const FeatureEditSession& edit() const { return *edit_; }

  void EnterEditFocus() override { edit_->EnterEditFocus(); }
  void ReleaseEditFocus() override { edit_->ReleaseEditFocus(); }
  bool CanUndo() const override { return edit_->CanUndo(); }
  void Undo() override { edit_->Undo(); }
  bool CanRedo() const override { return edit_->CanRedo(); }
  void Redo() override { edit_->Redo(); }

  bool OnMouseDown(const MouseEvent& e) override { return edit_->OnMouseDown(e); }
  bool OnMouseMove(const MouseEvent& e) override { return edit_->OnMouseMove(e); }
  bool OnMouseUp(const MouseEvent& e) override { return edit_->OnMouseUp(e); }
  bool OnKeyDown(const KeyEvent& e) override { return edit_->OnKeyDown(e); }

  /// Every vertex drawn within `tolerance_px`, nearest first, except those
  /// the edit session is dragging.
  void SnapToPoint(const MapProjection& proj, PixelPoint p, double tolerance_px,
                   std::vector<app::SnapToItem>& out) override;

  /// The topmost feature drawn under `p`, by the last drawn projection; the
  /// hint is its map tip and the layer's name.
  void HitTestPoint(const MapProjection& proj, PixelPoint p, double tolerance_px,
                    std::vector<app::HitItem>& out) override;
  /// The topmost feature's attributes, as the form shows them.
  std::optional<app::ItemInfoSheet> InfoAt(const MapProjection& proj, PixelPoint p,
                                           double tolerance_px) override;
  std::string EditHintAt(PixelPoint p) override { return edit_->HintAt(p); }

  /// The stack, for mouse capture and snapping to other overlays; null is legal.
  void SetManager(OverlayManager* manager) { manager_ = manager; }
  OverlayManager* manager() const { return manager_; }
  /// The projection of the last draw; hit testing uses it.
  bool has_projection() const { return have_proj_; }
  const MapProjection& last_projection() const { return last_proj_; }

  /// An empty in-memory layer with no fields and any geometry.
  Status FileNew() override;
  /// Opens a `.geojson` file, or a feature table of a `.gpkg` (the one the
  /// spec names, `fvkit/app/source_spec.h`, else the first), with the style
  /// stored in it; a file without one, or with one that does not read
  /// (logged), gets the default single style. Overlays of one GeoPackage share
  /// its open package, so their pending edits commit and revert together.
  Status FileOpen(const std::string& spec) override;
  /// Format 0 writes a GeoPackage (replacing any file at `spec`, except that
  /// saving to the open GeoPackage commits its pending edits); format 1
  /// writes GeoJSON. Either stores the style. The document is then reopened
  /// from `spec`. kInvalidArg for a layer of a file of several saved over
  /// that file under its plain path, which would replace the other layers.
  Status FileSaveAs(const std::string& spec, int format_index) override;
  /// The identifiers of the other tables of the GeoPackage at `spec` that a
  /// format-0 save to its plain path would replace, when it holds two or more.
  /// Empty for a spec naming a table, one table, a missing file, GeoJSON, and
  /// this layer's own file opened by its plain path (that save commits it alone).
  std::vector<std::string> SaveAsWouldReplace(const std::string& spec, int format_index) override;
  /// Writes every feature and the style as a new table of the GeoPackage at
  /// `path` (named as `AddFeatureLayerToPackage` names one) and reopens it.
  Status FileSaveAsAddedLayer(const std::string& path, std::string* spec) override;
  bool SupportsRevert() const override { return true; }
  /// Discards the package's pending edits, every layer's, and reopens `spec`.
  Status Revert(const std::string& spec) override;

  /// The feature tables of the GeoPackage at `path`, in file order; empty for
  /// any other file. `FileTypeDesc::list_layers` for this type.
  static Status ListLayers(const std::string& path, std::vector<std::string>* layers);
  /// A layer opened by a GeoPackage's bare path names its table in its file
  /// spec (`path|layername=table`), so the spec still means this one table
  /// once the file holds others. No change for any other overlay.
  void NameTableInSpec();
  /// The GeoPackage the layer lives in; null for any other layer.
  const std::shared_ptr<GeoPackage>& package() const { return package_; }

  /// The layer drawn; null before a file is opened or created.
  const std::shared_ptr<IEditableFeatureLayer>& layer() const { return layer_; }
  /// Shows `layer` in place of the current one; the overlay takes its name.
  void SetLayer(std::shared_ptr<IEditableFeatureLayer> layer);

  /// Changes the layer's fields (`IEditableFeatureLayer::ChangeSchema`) and
  /// follows them in the style. The edit history of this layer is dropped:
  /// its entries hold values laid out for the old fields. Does not dirty the
  /// document; the caller does.
  Status ChangeSchema(const SchemaChange& change);

  const FeatureStyle& style() const { return style_; }
  /// Does not dirty the document; the caller does.
  void SetStyle(FeatureStyle style) { style_ = std::move(style); }

  /// The label font; empty uses the canvas default. Needed below 100%
  /// opacity, where labels go through an offscreen canvas with no default.
  void SetLabelFont(std::string path) { label_font_ = std::move(path); }
  const std::string& label_font() const { return label_font_; }

  /// Device pixels per style pixel: the style's sizes are authored pixels, and
  /// a 2x display draws them twice as wide. 1 by default.
  void SetDisplayScale(double scale) { display_scale_ = scale > 0 ? scale : 1.0; }
  double display_scale() const { return display_scale_; }

  /// The most recently selected feature's id; 0 = none. Every selected
  /// feature is drawn with a highlight under it. Selecting does not dirty the
  /// document.
  int64_t selected() const { return selection_.empty() ? 0 : selection_.back(); }
  /// Makes `id` the whole selection; 0 clears it.
  void SetSelected(int64_t id);
  const std::vector<int64_t>& selection() const { return selection_; }
  /// Duplicates and zeros are dropped; the last id is `selected()`.
  void SetSelection(std::vector<int64_t> ids);
  bool IsSelected(int64_t id) const;

  /// The tables of the GeoPackage at `path` the selection can be appended to
  /// (`SchemasMatchForAppend`), leaving out this layer's own table.
  Status AppendTargets(const std::string& path, std::vector<GeoPackageLayerInfo>* out) const;
  /// Writes the selected features with the layer's schema and style as `to`
  /// says; an append keeps the target table's style and gives the features
  /// new ids. A file an overlay holds open is written through its shared
  /// connection, so its layers keep their unsaved edits. `spec` receives the
  /// table written (`path|layername=table`). kInvalidArg for an empty
  /// selection, a new file over a file held open, and an append to a table
  /// with unsaved edits or another schema.
  Status ExportSelection(const FeatureExport& to, std::string* spec);

  /// What a search result, a form title and a table row call `f`: the label
  /// field's text, else the first field named "name", else the first text
  /// field, else "Feature <id>".
  std::string FeatureTitle(const Feature& f) const;

  /// The field whose text a hover over a feature shows; empty shows `FeatureTitle`.
  const std::string& map_tip_field() const { return map_tip_field_; }
  void SetMapTipField(std::string field) { map_tip_field_ = std::move(field); }
  /// The map tip field's text for `f`, else `FeatureTitle` when that field is
  /// unset, missing or empty for `f`.
  std::string FeatureMapTip(const Feature& f) const;
  /// The field holding each feature's link (a URL or a file path); empty for none.
  const std::string& hyperlink_field() const { return hyperlink_field_; }
  void SetHyperlinkField(std::string field) { hyperlink_field_ = std::move(field); }
  /// The hyperlink field's text for `f`; "" when it has none.
  std::string FeatureHyperlink(const Feature& f) const;

  /// How this layer draws its selection. Without a style of its own the
  /// layer uses the default `FeatureSelectionStyle`.
  bool has_selection_style() const { return has_selection_style_; }
  const FeatureSelectionStyle& selection_style() const { return selection_style_; }
  void SetSelectionStyle(const FeatureSelectionStyle& style);
  void ClearSelectionStyle();

  /// The definition query: only features it accepts are drawn, hit, snapped
  /// to, searched and listed. The layer itself keeps every feature, and a
  /// save writes them all. Empty accepts every feature.
  const FeatureFilter& definition_query() const { return definition_query_; }
  /// Parses `json` as a `FeatureFilter`; "" clears the query. Selected
  /// features the query rejects are deselected. On a parse error the query is
  /// unchanged. Does not dirty the document; the caller does.
  Status SetDefinitionQuery(const std::string& json);
  /// True when the definition query accepts `f`.
  bool Shows(const Feature& f) const;
  /// The layer's features whose envelope meets `box`, or all of them, that the
  /// definition query accepts.
  Status QueryShown(const GeoRect& box, std::vector<Feature>* out) const;
  Status AllShown(std::vector<Feature>* out) const;

  /// True when the map draws `f` at `scale_denominator`: the definition query
  /// shows it and, under a rules style, some shown rule draws its geometry
  /// at that scale, as the style engine decides. Hits, snapping and
  /// rubber-band selection take only drawn features; search and the
  /// attribute table take every shown one (layer-manager-plan LM-D8).
  bool Draws(const Feature& f, double scale_denominator) const;
  /// `QueryShown` narrowed to the features `Draws` at `scale_denominator`.
  Status QueryDrawn(const GeoRect& box, double scale_denominator, std::vector<Feature>* out) const;

  /// Matches `FeatureTitle` against `q.text`; `q.area` is met by the title's
  /// anchor point. Results' `feature` is the feature id.
  void Search(const app::SearchQuery& q, const std::atomic<bool>& cancel,
              std::vector<app::SearchResult>& out) override;

  /// Features in the scene the last `OnDraw` drew from: those in view, or
  /// after a pan those in a margin around it as well.
  size_t features_drawn() const { return features_drawn_; }

 private:
  Status Load(const std::string& spec);
  /// Replaces the style with the one read from `spec`'s `style_json`, or the default.
  void UseStoredStyle(const std::string& spec, const std::string& style_json);
  /// The style JSON a save stores: the style and the layer's settings. The
  /// display name goes in only when `keep_name` and it differs from the layer's.
  Status StyleDocument(bool keep_name, std::string* json) const;
  Status DrawFeatures(const MapProjection& proj, ICanvas& canvas);
  Status DrawLayer(const MapProjection& proj, ICanvas& canvas);

  std::shared_ptr<IEditableFeatureLayer> layer_;
  std::shared_ptr<class FeatureVectorSource> source_;
  std::shared_ptr<MapLibreStyleEngine> engine_;
  std::unique_ptr<VectorRenderer> renderer_;
  std::string engine_json_;  ///< what `engine_` was loaded from
  std::string engine_font_;
  std::vector<bool> source_fields_;  ///< the fields `source_` turns into attributes
  uint64_t scene_revision_ = 0;      ///< the layer revision the renderer's scene was built from
  double scene_scale_ = 0.0;         ///< the scale of the last draw
  uint64_t definition_generation_ = 0;
  uint64_t scene_definition_generation_ = 0;
  std::shared_ptr<GeoPackage> package_;  ///< set when `layer_` lives in a GeoPackage
  std::string spec_layer_;               ///< the table the open spec named; empty for a whole file
  FeatureStyle style_;
  std::string label_font_;
  double display_scale_ = 1.0;
  size_t features_drawn_ = 0;
  std::vector<int64_t> selection_;
  std::string map_tip_field_;
  std::string hyperlink_field_;
  bool has_selection_style_ = false;
  FeatureSelectionStyle selection_style_;
  FeatureFilter definition_query_;
  /// The rules style `Draws` asks, without selection halos; rebuilt when the style changes.
  mutable std::unique_ptr<MapLibreStyleEngine> draws_engine_;
  mutable FeatureStyle draws_style_;
  OverlayManager* manager_ = nullptr;
  MapProjection last_proj_;
  bool have_proj_ = false;
  std::unique_ptr<FeatureEditSession> edit_;
};

/// Adds an empty feature table for a layer named `name` with `schema` and the
/// default style to the GeoPackage at `path`, and commits it. The table is
/// `name` reduced to letters, digits and underscores, made unique in the
/// file; a name the file already shows is numbered ("Rivers 2"). `spec`
/// receives `path|layername=table`. Open layers of the file keep their
/// unsaved edits. kInvalidArg for a read-only file.
Status AddFeatureLayerToPackage(const std::string& path, const std::string& name, const FeatureSchema& schema,
                                std::string* spec);

/// Writes an empty layer named `name` with `schema` and the default style to
/// `spec`, as `FeatureOverlay::FileSaveAs` format `format` (a GeoPackage
/// replaces any file there; its table is named after the file).
Status CreateFeatureLayerFile(const std::string& spec, int format, const std::string& name,
                              const FeatureSchema& schema);

/// Draws `symbol` as a legend swatch filling `canvas`, through the same
/// engine as the map: a marker for point geometry, a line for lines, a filled
/// box for areas and for any geometry. Sizes are multiplied by `scale`.
Status DrawFeatureSwatch(ICanvas& canvas, const FeatureSymbol& symbol, GeometryType geometry,
                         double scale = 1.0);

/// Draws `rules` together as one legend swatch filling `canvas`: each rule
/// without its filter, beyond a geometry-type test, or its zoom range, and a
/// label as "Aa" in `font` (a font file; empty: the canvas default).
/// `geometry` kGeometry takes the geometry from the first rule that is not a
/// symbol rule.
Status DrawRulesSwatch(ICanvas& canvas, const std::vector<StyleRule>& rules, GeometryType geometry,
                       double scale = 1.0, const std::string& font = std::string());

}  // namespace fv
