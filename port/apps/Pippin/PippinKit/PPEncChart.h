// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * PPEncChart.h — an S-57 ENC exchange set as the app's base map.
 *
 * Plain C++ (no Foundation), so band choice and drawing are gtests on the mac.
 * Cells are grouped by IHO usage band; each band is opened on first use as its
 * own source, and a draw shows the one band nearest the display scale.
 */
#ifndef PIPPIN_PPENCCHART_H_
#define PIPPIN_PPENCCHART_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/canvas/canvas.h"
#include "fvkit/geo.h"
#include "fvkit/proj.h"

#include "fv_enc_names.h"
#include "fv_enc_vector_source.h"

namespace fv {
class S52StyleEngine;
class VectorRenderer;
}  // namespace fv

namespace pippin {

/** Which of a chart's two renderers a draw uses; each keeps its own scene. */
enum class EncChartPass { kBase, kUnderlay };

/** Which S-52 display categories are drawn; each includes the ones before it. */
enum class EncDisplayCategory { kBase, kStandard, kAll };

/** The S-52 colour tables: day (paper white ground), dusk and night. */
enum class EncPalette { kDay, kDusk, kNight };

/** Depth units for feature summaries. Chart depths are metres in the data. */
enum class EncDepthUnits { kMetres, kFeet };

/** The mariner's chart settings. Depths are metres. The defaults are S-52's
 *  own and draw the chart exactly as an `EncChart` with no settings applied. */
struct EncChartSettings {
  double safety_contour = 30.0;  ///< the bold contour; deeper water is unshaded
  double safety_depth = 30.0;    ///< soundings this shallow or shallower print black
  double shallow_contour = 2.0;  ///< inner edge of the shallow-water shade
  double deep_contour = 30.0;    ///< outer edge of the deep-water shade
  EncDisplayCategory category = EncDisplayCategory::kAll;
  EncPalette palette = EncPalette::kDay;

  bool operator==(const EncChartSettings& o) const;
  bool operator!=(const EncChartSettings& o) const { return !(*this == o); }
};

/** A feature under a tap, described for an info sheet. */
struct EncFeatureInfo {
  std::string title;    ///< the object name (OBJNAM), else the class name
  std::string kind;     ///< the S-57 class name ("Light", "Depth area")
  std::string acronym;  ///< the S-57 class acronym ("LIGHTS")
  std::string summary;  ///< chart-style one-liner ("Fl(2) G 6s 12m 5M"); may be empty
  /// Every attribute as (catalogue name, decoded value), in record order.
  std::vector<std::pair<std::string, std::string>> attributes;
  std::string source;   ///< cell, edition and record id
};

/** The chart-notation line for `d`: a light's characteristic, a depth area's
 *  range, a contour's or a hazard's depth. Empty for other classes. */
std::string EncFeatureSummary(const fv::FeatureDescription& d, EncDepthUnits units);

/** Picks the band whose nominal scale is nearest `scale_denominator` in log
 *  terms; ties go to the larger-scale band. 0 when `bands` is empty. */
int EncBandForScale(const std::vector<int>& bands, double scale_denominator);

/** An ENC exchange set drawn with S-52 symbology, one usage band at a time.
 *  Only cells that meet the view are parsed; a band change drops the cells
 *  of every other band. */
class EncDrawnBandSource;

class EncChart {
 public:
  EncChart();
  ~EncChart();
  EncChart(const EncChart&) = delete;
  EncChart& operator=(const EncChart&) = delete;

  /** Indexes the base cells under `cell_root` and loads the S-52 library and
   *  S-57 catalogue from `s52_dir`. A `chart-index.tsv` in `cell_root`
   *  (fv_enc_index.h) is used instead of scanning the tree, and rewritten
   *  when it is an older version; cells it lists as cancelled are left out.
   *  No catalogued cell is parsed until a draw reaches it. */
  fv::Status Open(const std::string& cell_root, const std::string& s52_dir);

  /** The usage bands present, ascending (1 overview … 6 berthing). */
  const std::vector<int>& bands() const { return bands_; }
  /** Number of base cells found. */
  size_t cell_count() const;
  /** Cells drawn without some of their update files (CH-D5), from the
   *  index; 0 for a set opened without one. */
  size_t outdated_cell_count() const { return outdated_cells_; }

  /** The area the chart is drawn for: the union of the bounds of its cells in
   *  band 3 (coastal) and finer, or of all cells when it has none. Overview and
   *  general cells are left out because they span far past the set's region.
   *  Inverted (south of north) before Open. */
  fv::GeoRect coverage() const;

  /** Parsed cells currently held, across all bands. */
  size_t resident_cell_count() const;
  /** Cap on parsed cells a band keeps between draws (see
   *  EncVectorSource::SetMaxResidentCells); applies to bands opened after. */
  void SetMaxResidentCells(size_t n) { max_resident_cells_ = n; }

  void SetDrawLabels(bool on);

  /** Applies the mariner's settings; contours out of order are clamped so
   *  shallow <= safety <= deep. Takes effect on the next draw. */
  void SetSettings(const EncChartSettings& settings);
  const EncChartSettings& settings() const { return settings_; }

  /** The colour the chart ground is cleared to for the current palette. */
  fv::FvColor ground_color() const;

  /** Features the last kBase draw put under canvas pixel (x, y), topmost
   *  first, at most `max_items`. Area fills that only surround the point
   *  (land, depth areas) are included, so a tap in open water names its depth. */
  std::vector<EncFeatureInfo> FeaturesAt(double x, double y, double tolerance,
                                         EncDepthUnits units, size_t max_items = 8);
  void SetSceneMargin(double fraction) { scene_margin_ = fraction; }
  void SetSimplifyPixels(double px) { simplify_px_ = px; }

  /** Clears `canvas` to the chart ground and draws the band for `proj`'s scale.
   *  `device_dpi` is the true pixel pitch, which sizes pens and dashes;
   *  `symbol_scale` is device pixels per authored symbol pixel (symbols, area
   *  patterns, text). */
  fv::Status Render(const fv::MapProjection& proj, double device_dpi,
                    double symbol_scale, EncChartPass pass, fv::ICanvas* canvas);

  /** A source for search. Text queries go to the set's named-object table
   *  (`names`), so they answer over every cell, nearest the last draw first;
   *  area queries go to the band of the last kBase draw. Empty after this
   *  chart is destroyed. Same thread as Render. */
  std::shared_ptr<fv::IVectorSource> search_source();

  /** Every named object in the set, from `chart-names.tsv` beside the cells,
   *  or built from the cells on first use (and written there for a set opened
   *  from its chart-index.tsv). */
  const std::vector<fv::EncNameEntry>& names();

  /** Centre of the last kBase draw; (0, 0) before one. */
  fv::GeoPoint last_center() const { return last_center_; }

  /** The band and renderer of the last kBase draw; 0 / nullptr before one. */
  int last_band() const { return last_band_; }
  fv::VectorRenderer* last_renderer() const { return last_renderer_; }
  fv::EncVectorSource* last_source() const { return last_source_; }

 private:
  struct Band;
  fv::Status OpenBand(int band, Band** out);
  void ReleaseOtherBands(int keep);
  void ApplySettings();

  std::shared_ptr<fv::S52StyleEngine> style_;
  EncChartSettings settings_;
  std::string s52_dir_;
  std::vector<int> bands_;
  std::map<int, std::vector<fv::EncCellEntry>> cells_;
  std::map<int, std::unique_ptr<Band>> open_;
  double scene_margin_ = 0.0;
  double simplify_px_ = 0.0;
  size_t max_resident_cells_ = 32;
  int last_band_ = 0;
  fv::VectorRenderer* last_renderer_ = nullptr;
  fv::EncVectorSource* last_source_ = nullptr;
  std::shared_ptr<EncDrawnBandSource> search_source_;
  std::string cell_root_;
  std::vector<fv::EncNameEntry> names_;
  bool names_loaded_ = false;
  bool indexed_ = false;  // opened from chart-index.tsv
  size_t outdated_cells_ = 0;
  fv::GeoPoint last_center_;
};

}  // namespace pippin

#endif  // PIPPIN_PPENCCHART_H_
