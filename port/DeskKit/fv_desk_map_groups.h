// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_map_groups.h — which catalog formats make up each base-map choice.
///
/// The UI calls a group a "Map family" (Raster, Elevation, OpenStreetMap, ENC,
/// DNC). In code it is `MapGroup`, because "family" already names a group of
/// features within one product (`fvkit/vector/families.h`). The table is data,
/// read from `map-groups.json` (port/desktop-plan.md §3b).
#pragma once

#include <set>
#include <string>
#include <vector>

#include "fv_view_ladder.h"
#include "fvkit/geo.h"

namespace fv {
class Catalog;

namespace desk {

/// A display scale for series the catalog stores without one (a DTED level,
/// a DNC library, an OSM pyramid). Matches `series` exactly or as a prefix,
/// ignoring case; an entry with only a `format` matches every series of it.
/// An empty `format` matches any of the group's formats.
struct NominalScale {
  std::string format;
  std::string series;
  bool prefix = false;
  double scale_denom = 0;  // 1:N, > 0
};

/// One base-map choice: a title, the catalog formats it draws, and its ladder.
struct MapGroup {
  std::string id;
  std::string title;
  std::vector<std::string> formats;
  view::LadderKind ladder = view::LadderKind::kUniform;
  double uniform_factor = 2.0;
  /// Checked in order; the first match wins.
  std::vector<NominalScale> nominal_scales;

  /// The nominal 1:N for a series, or 0 when no entry matches.
  double NominalScaleOf(const std::string& format, const std::string& series_key) const;
};

/// The ordered set of map groups.
class MapGroups {
 public:
  /// The table compiled from `port/DeskKit/map-groups.json`.
  static MapGroups Builtin();

  /// Parses a map-groups document. All or nothing: on error the table is
  /// left as it was. A format claimed by two groups is an error.
  Status LoadJson(const std::string& text, const std::string& name = "<string>");
  Status LoadFile(const std::string& path);

  const std::vector<MapGroup>& All() const { return groups_; }
  const MapGroup* Find(const std::string& id) const;
  /// The group that draws `format`, or null.
  const MapGroup* ForFormat(const std::string& format) const;

  /// The groups with at least one series among `formats_present`, in table
  /// order.
  std::vector<const MapGroup*> WithData(const std::set<std::string>& formats_present) const;

 private:
  std::vector<MapGroup> groups_;
};

/// The formats with coverage in `catalog`. Empty on a read error.
std::set<std::string> CatalogFormats(const Catalog& catalog);

}  // namespace desk
}  // namespace fv
