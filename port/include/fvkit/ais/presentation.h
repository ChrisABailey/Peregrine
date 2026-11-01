// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// @file
/// How AIS targets are named and drawn: ITU code texts and FalconView's ship
/// symbols, compiled into fvkit by port/tools/make_ais_sheet.py.
/// Plan: port/ais-plan.md (AIS3).

#ifndef FVKIT_AIS_PRESENTATION_H_
#define FVKIT_AIS_PRESENTATION_H_

#include <map>
#include <memory>
#include <string>

#include "fvkit/ais/vessel_table.h"
#include "fvkit/symbol/png_library.h"

namespace fv {
namespace ais {

/// ITU-R M.1371 ship and cargo type, e.g. "Cargo, hazardous category A".
std::string ShipTypeText(int ship_type);

/// ITU navigational status, e.g. "At anchor". Empty for 15 (not defined).
std::string NavStatusText(int nav_status);

/// Type of an aid to navigation (message 21), e.g. "Beacon, port hand".
std::string AidTypeText(int aid_type);

/// Display name of a target kind: "Vessel", "Aid to navigation", "AIS-SART", …
const char* KindText(TargetKind kind);

/// FalconView symbol name for a ship type (Symbollist.txt), e.g. "cargo";
/// "orange" for types the list does not name.
const char* HullSymbolName(int ship_type, bool has_ship_type);

/// Sheet id of the hull drawn for `v` ("ais-cargo", or "ais-cargo-stale"),
/// or empty for kinds drawn as shapes (AtoN, base station, beacons).
std::string HullSymbolId(const Vessel& v, bool stale);

/// The fill colour of the hull drawn for `v`: the most common opaque colour
/// of its sprite, so a trail matches its hull, stale variant included. Grey
/// for kinds drawn as shapes.
FvColor HullColor(const Vessel& v, bool stale);

/// Length of a hull tile at nominal size (pixelRatio divided out), pixels.
double HullNominalLengthPx();

/// The shared AIS sprite sheet, decoded on first use; read-only afterwards.
std::shared_ptr<PngSymbolLibrary> AisSymbolSheet();

}  // namespace ais
}  // namespace fv

#endif  // FVKIT_AIS_PRESENTATION_H_
