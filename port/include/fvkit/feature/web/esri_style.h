// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/web/esri_style.h — an Esri layer's `drawingInfo` as a
/// `FeatureStyle` (port/web-features-plan.md WF-D11, WF4).
///
/// Renderers: simple → single, uniqueValue → categorised, classBreaks →
/// graduated. Symbols: simple marker, line and fill (sizes in points become
/// pixels at 96/72). The first label class sets the label field and text
/// symbol. What cannot be carried over (picture and CIM symbols, visual
/// variables, multi-field unique values, normalised class breaks, other
/// renderers) falls back to the default symbol or a single renderer and is
/// reported in `notes`.
#pragma once

#include <string>
#include <vector>

#include "fvkit/feature/schema.h"
#include "fvkit/feature/style.h"

namespace fv {
namespace web {

/// Reads the `drawingInfo` JSON object of an Esri layer into `*style`.
/// `schema` is the layer's (`EsriFeatureService::Describe`): it gives field
/// names their case and decides how category values are keyed. Each note is
/// one sentence on something not imported, also logged. kInvalidArg when
/// `drawing_info` is not a JSON object; a missing renderer is the default style.
Status EsriStyleFromDrawingInfo(const std::string& drawing_info, const FeatureSchema& schema,
                                FeatureStyle* style, std::vector<std::string>* notes = nullptr);

}  // namespace web
}  // namespace fv
