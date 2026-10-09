// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_vector_map.h — draws a DNC, ENC or OSM series as the base map.
///
/// One source and `VectorRenderer` per series and one style engine per
/// product (GeoSym, S-52, GL style), as PythonView keeps them. The engines
/// read `VectorMapConfig` when they are opened, so a changed configuration
/// takes effect after `Reset`.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "fv_view_ladder.h"
#include "fv_view_viewport.h"
#include "fvkit/geo.h"

namespace fv {
class Catalog;
class GeoSymStyleEngine;
class ICanvas;
class IVectorSource;
class OsmStyleEngine;
class S52StyleEngine;
class VectorRenderer;
struct MarinerSettings;

namespace desk {

/// The mariner's depth settings; an unset value keeps the product's own
/// default (DNC and ENC differ, see peregrine.ini.sample [mariner]).
struct MarinerOverrides {
  std::optional<double> safety_contour, shallow_contour, deep_contour, safety_depth;
  std::optional<bool> two_shades, shallow_pattern;

  void ApplyTo(MarinerSettings* m) const;
  bool operator==(const MarinerOverrides& o) const;
};

/// Where the vector products' assets are and how they are drawn. Settings
/// keys: `geosym.*`, `enc.*`, `osm.style`, `mariner.*`.
struct VectorMapConfig {
  std::string geosym_dir;  ///< holds SymAssign/ and Graphics/
  int geosym_brightness = 0, geosym_contrast = 0;  ///< -100..100
  std::string enc_dir;     ///< chartsymbols.xml and the S-57 catalogue CSVs
  bool enc_show_meta = false;
  std::string osm_style;   ///< MapLibre GL style sheet
  MarinerOverrides mariner;
};

/// The process-wide configuration, as Map ▸ Options last applied it.
VectorMapConfig CurrentVectorMapConfig();
/// Replaces the process-wide configuration. Thread-safe.
void SetVectorMapConfig(const VectorMapConfig& config);

class VectorBaseMap {
 public:
  VectorBaseMap();
  ~VectorBaseMap();
  VectorBaseMap(const VectorBaseMap&) = delete;
  VectorBaseMap& operator=(const VectorBaseMap&) = delete;

  /// Whether `format` is a vector product: "vpf", "enc" or "osm".
  static bool CanDraw(const std::string& format);

  /// Drops every open source and style engine; the next render reopens them
  /// from `catalog` and the current `VectorMapConfig`.
  void Reset(std::shared_ptr<Catalog> catalog);

  /// Clears `canvas` to the product's ground and draws `product` at `view`.
  /// A missing asset directory or style sheet is an error naming the
  /// settings key.
  Status Render(const view::Viewport& view, const view::LadderProduct& product,
                ICanvas& canvas);

 private:
  struct Series {
    std::shared_ptr<IVectorSource> source;
    std::unique_ptr<VectorRenderer> renderer;
  };

  Status OpenSeries(const view::LadderProduct& product, Series* out);
  Status OpenSource(const view::LadderProduct& product, std::shared_ptr<IVectorSource>* out);

  std::shared_ptr<Catalog> catalog_;
  VectorMapConfig config_;
  std::map<int64_t, Series> series_;
  std::shared_ptr<GeoSymStyleEngine> geosym_;
  std::shared_ptr<S52StyleEngine> s52_;
  std::shared_ptr<OsmStyleEngine> osm_;
  std::optional<double> osm_ref_lat_;
};

}  // namespace desk
}  // namespace fv
