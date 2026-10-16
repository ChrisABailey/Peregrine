// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Legend swatches for a MapLibre style. Each legend entry is drawn from its
/// specimens, synthetic features pushed through the style's own filters and
/// paint and rendered by VectorRenderer, so a swatch is what the map draws at
/// the same scale.
///
/// Every call styles features through `engine`, which marks its per-render
/// diagnostics (layers_that_drew, empty_labels). Use an engine dedicated to
/// the legend, loaded from the same file as the map's. Set its pitch and
/// reference latitude before loading it: layer minzooms become scale bands
/// at load.

#ifndef FVKIT_STYLE_LEGEND_H_
#define FVKIT_STYLE_LEGEND_H_

#include <string>
#include <vector>

#include "fvkit/canvas/canvas.h"
#include "fvkit/style/maplibre_style.h"

namespace fv {

/// The ids of the style layers that draw `entry`'s specimens at
/// `scale_denominator`, in style order. Empty means the row is not drawn at
/// that scale.
std::vector<std::string> LegendEntryLayers(MapLibreStyleEngine& engine,
                                           const MapLibreLegendEntry& entry,
                                           double scale_denominator);

/// True when every one of `entry_layers` (from LegendEntryLayers or
/// RenderLegendSwatch) is among `map_layers`, the map engine's
/// layer_ids_that_drew() for the last render. False for an empty row. All,
/// not any: a bike lane's specimen draws the road under it too.
bool LegendEntryOnMap(const std::vector<std::string>& entry_layers,
                      const std::vector<std::string>& map_layers);

/// The zoom from which `entry` is drawn: the smallest `minzoom` among the
/// layers that draw its specimens at z22. Negative when nothing draws it.
double LegendEntryMinZoom(MapLibreStyleEngine& engine, const MapLibreLegendEntry& entry);

/// Draws one legend entry's specimens through `engine` at `scale_denominator`,
/// onto a `width_pt` x `height_pt` canvas (`pixels_per_point` device pixels per
/// point) cleared to the style's background. A line runs across the full
/// width at mid-height, a point sits at the centre, an area fills an inset
/// rectangle. Returns the layer ids that drew; empty means the row is not
/// shown at this scale.
std::vector<std::string> RenderLegendSwatch(MapLibreStyleEngine& engine,
                                            const MapLibreLegendEntry& entry,
                                            double scale_denominator,
                                            double pixels_per_point, int width_pt,
                                            int height_pt, ICanvas* canvas);

}  // namespace fv

#endif  // FVKIT_STYLE_LEGEND_H_
