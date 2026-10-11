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

class FeatureOverlay : public Overlay,
                       public app::Persistence,
                       public app::SearchProvider,
                       public app::EditTarget,
                       public app::HitTest,
                       public app::EditHint,
                       public app::SnapTo {
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
  /// hint is its title and the layer's name.
  void HitTestPoint(const MapProjection& proj, PixelPoint p, double tolerance_px,
                    std::vector<app::HitItem>& out) override;
  std::string EditHintAt(PixelPoint p) override { return edit_->HintAt(p); }

  /// The stack, for mouse capture and snapping to other overlays; null is legal.
  void SetManager(OverlayManager* manager) { manager_ = manager; }
  OverlayManager* manager() const { return manager_; }
  /// The projection of the last draw; hit testing uses it.
  bool has_projection() const { return have_proj_; }
  const MapProjection& last_projection() const { return last_proj_; }

  /// An empty in-memory layer with no fields and any geometry.
  Status FileNew() override;
  /// Opens a `.geojson` file, or the first feature table of a `.gpkg`, with
  /// the style stored in it; a file without one, or with one that does not
  /// read (logged), gets the default single style.
  Status FileOpen(const std::string& spec) override;
  /// Format 0 writes a GeoPackage (replacing any file at `spec`, except that
  /// saving to the open GeoPackage commits its pending edits); format 1
  /// writes GeoJSON. Either stores the style. The document is then reopened
  /// from `spec`.
  Status FileSaveAs(const std::string& spec, int format_index) override;
  bool SupportsRevert() const override { return true; }
  Status Revert(const std::string& spec) override;

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

  /// What a search result, a form title and a table row call `f`: the label
  /// field's text, else the first field named "name", else the first text
  /// field, else "Feature <id>".
  std::string FeatureTitle(const Feature& f) const;

  /// Matches `FeatureTitle` against `q.text`; `q.area` is met by the title's
  /// anchor point. Results' `feature` is the feature id.
  void Search(const app::SearchQuery& q, const std::atomic<bool>& cancel,
              std::vector<app::SearchResult>& out) override;

  /// Features drawn by the last `OnDraw`.
  size_t features_drawn() const { return features_drawn_; }

 private:
  Status Load(const std::string& spec);
  /// Replaces the style with the one read from `spec`'s `style_json`, or the default.
  void UseStoredStyle(const std::string& spec, const std::string& style_json);
  Status DrawFeatures(const MapProjection& proj, ICanvas& canvas);
  Status DrawLayer(const MapProjection& proj, ICanvas& canvas);

  std::shared_ptr<IEditableFeatureLayer> layer_;
  std::shared_ptr<class FeatureVectorSource> source_;
  std::shared_ptr<MapLibreStyleEngine> engine_;
  std::unique_ptr<VectorRenderer> renderer_;
  std::string engine_json_;  ///< what `engine_` was loaded from
  std::string engine_font_;
  std::shared_ptr<GeoPackage> package_;  ///< set when `layer_` lives in a GeoPackage
  FeatureStyle style_;
  std::string label_font_;
  double display_scale_ = 1.0;
  size_t features_drawn_ = 0;
  std::vector<int64_t> selection_;
  OverlayManager* manager_ = nullptr;
  MapProjection last_proj_;
  bool have_proj_ = false;
  std::unique_ptr<FeatureEditSession> edit_;
};

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

}  // namespace fv
