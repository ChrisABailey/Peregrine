// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/value_text.h — field values as the text a form edits and a
/// table shows (port/feature-overlay-plan.md §4).
#pragma once

#include <string>

#include "fvkit/feature/schema.h"

namespace fv {

/// The text shown for `value`: an enumeration's choice label, "Yes"/"No",
/// numbers without trailing zeros, dates as stored. Null and blobs are "".
std::string FormatFieldValue(const FieldDef& field, const FieldValue& value);

/// Parses typed text into a value of `field`'s storage type. Blank text is
/// null. An enumeration accepts a choice's value or label; a boolean accepts
/// yes/no, true/false and 1/0; a date is YYYY-MM-DD; a datetime is
/// YYYY-MM-DDThh:mm[:ss[.fff]] with an optional Z or ±hh:mm (a space for the T
/// is accepted and written as T). kInvalidArg naming the field otherwise.
/// Bounds and `required` are `ValidateValue`'s.
Status ParseFieldValue(const FieldDef& field, const std::string& text, FieldValue* out);

/// The URL a form's action button opens for `text`: tel: for a phone (digits
/// and a leading + only), mailto: for an email, the text for a URL (https://
/// added when it has no scheme). "" for any other role or blank text.
std::string FieldActionUrl(const FieldDef& field, const std::string& text);

}  // namespace fv
