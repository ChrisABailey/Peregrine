// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// geojson_detail.h — the GeoJSON reader's parsing pieces, shared with the
/// web feature clients. Private to fvkit: it names nlohmann types.
#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "fvkit/feature/layer.h"

namespace fv {
namespace geojson_detail {

/// Ordered, so fields keep the order the document gives them.
using json = nlohmann::ordered_json;

/// A GeoJSON geometry object (or null) as a `Geometry`. kInvalidArg when malformed.
Status ReadGeometry(const json& j, Geometry* out, int depth);

/// A JSON value as the field's storage type. False when it cannot be one.
bool JsonToValue(const json& j, FieldType type, FieldValue* out);

/// What the values of one property have been seen to be.
struct Seen {
  bool any = false;  ///< a non-null value
  bool boolean = false, integer = false, real = false, other = false;
  bool text = false;  ///< a string that is not a date or datetime
  bool date = false, datetime = false;
  bool all_url = true, all_email = true;
};

/// Adds one property value to what has been seen of it.
void Observe(const json& v, Seen* s);
/// The field a property's seen values call for.
FieldDef Infer(const std::string& name, const Seen& s);

}  // namespace geojson_detail
}  // namespace fv
