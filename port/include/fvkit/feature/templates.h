// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/templates.h — starting schemas for a new feature layer
/// (port/feature-overlay-plan.md §4, New layer).
#pragma once

#include <string>
#include <vector>

#include "fvkit/feature/schema.h"

namespace fv {

/// A named starting schema; its geometry type is the dialog's initial choice.
struct FeatureLayerTemplate {
  std::string id;     ///< "blank", "monuments", ...
  std::string title;  ///< "Historical monuments"
  FeatureSchema schema;
};

/// Blank (a "name" text field, any geometry) first, then the plan's examples:
/// historical monuments, restaurants and rivers.
const std::vector<FeatureLayerTemplate>& FeatureLayerTemplates();

/// The template with `id`; null when there is none.
const FeatureLayerTemplate* FindFeatureLayerTemplate(const std::string& id);

}  // namespace fv
