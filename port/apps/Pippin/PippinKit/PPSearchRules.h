// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// PPSearchRules.h — Pippin's names for the shared search rules in
/// `fvkit/app/search_rules.h` (kind word, duplicate test).
#pragma once

#include "fvkit/app/search_rules.h"

namespace pippin {

using fv::app::ChartDetailIsRoad;
using fv::app::ClassifySearchResult;
using fv::app::DedupeSearchRows;
using fv::app::FoldSearchTitle;
using fv::app::SearchBoundsOverlap;
using fv::app::SearchKind;
using fv::app::SearchKindWord;
using fv::app::SearchRow;
using fv::app::SearchRowDistanceMeters;
using fv::app::SearchRowsAreDuplicates;
using fv::app::SearchSource;

}  // namespace pippin
