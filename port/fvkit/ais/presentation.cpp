// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// @file
/// AIS code texts and ship symbols. See fvkit/ais/presentation.h.

#include "fvkit/ais/presentation.h"

#include <cstdint>

namespace fv {
namespace ais {
namespace {
#include "ais_sheet.inc"

constexpr const char* kDefaultHull = "orange";

/// Suffix for the second digit of WIG, HSC, passenger, cargo, tanker and other types.
std::string HazardSuffix(int digit) {
  switch (digit) {
    case 1: return ", hazardous category A";
    case 2: return ", hazardous category B";
    case 3: return ", hazardous category C";
    case 4: return ", hazardous category D";
    default: return "";
  }
}

/// The most common colour among a tile's opaque pixels.
FvColor DominantColor(const PixelBuffer& tile) {
  std::map<std::uint32_t, int> counts;
  for (int y = 0; y < tile.Height(); ++y) {
    const unsigned char* p = tile.Row(y);
    for (int x = 0; x < tile.Width(); ++x, p += 4) {
      if (p[3] < 200) continue;
      ++counts[(std::uint32_t{p[0]} << 16) | (std::uint32_t{p[1]} << 8) | p[2]];
    }
  }
  std::uint32_t best = 0x808080;
  int best_n = 0;
  for (const auto& [rgb, n] : counts)
    if (n > best_n) best = rgb, best_n = n;
  return FvColor{static_cast<unsigned char>(best >> 16), static_cast<unsigned char>(best >> 8),
                 static_cast<unsigned char>(best), 255};
}

}  // namespace

std::string ShipTypeText(int t) {
  const int tens = t / 10;
  const int unit = t % 10;
  switch (tens) {
    case 2: return "Wing in ground" + HazardSuffix(unit);
    case 4: return "High-speed craft" + HazardSuffix(unit);
    case 6: return "Passenger" + HazardSuffix(unit);
    case 7: return "Cargo" + HazardSuffix(unit);
    case 8: return "Tanker" + HazardSuffix(unit);
    case 9: return "Other type" + HazardSuffix(unit);
    default: break;
  }
  switch (t) {
    case 0: return "Not available";
    case 30: return "Fishing";
    case 31: return "Towing";
    case 32: return "Towing, over 200 m long or 25 m wide";
    case 33: return "Dredging or underwater operations";
    case 34: return "Diving operations";
    case 35: return "Military operations";
    case 36: return "Sailing";
    case 37: return "Pleasure craft";
    case 50: return "Pilot vessel";
    case 51: return "Search and rescue vessel";
    case 52: return "Tug";
    case 53: return "Port tender";
    case 54: return "Anti-pollution";
    case 55: return "Law enforcement";
    case 56: case 57: return "Local vessel";
    case 58: return "Medical transport";
    case 59: return "Noncombatant ship";
    default: break;
  }
  return "Unknown (" + std::to_string(t) + ")";
}

std::string NavStatusText(int s) {
  static const char* const kText[] = {
      "Under way using engine",
      "At anchor",
      "Not under command",
      "Restricted manoeuvrability",
      "Constrained by draught",
      "Moored",
      "Aground",
      "Engaged in fishing",
      "Under way sailing",
      "Reserved (HSC)",
      "Reserved (WIG)",
      "Towing astern",
      "Pushing ahead or towing alongside",
      "Reserved",
      "AIS-SART, MOB or EPIRB active",
  };
  if (s >= 0 && s < static_cast<int>(sizeof kText / sizeof kText[0])) return kText[s];
  return "";
}

std::string AidTypeText(int a) {
  static const char* const kText[] = {
      "Not specified",
      "Reference point",
      "RACON",
      "Fixed structure off shore",
      "Spare",
      "Light, without sectors",
      "Light, with sectors",
      "Leading light, front",
      "Leading light, rear",
      "Beacon, cardinal N",
      "Beacon, cardinal E",
      "Beacon, cardinal S",
      "Beacon, cardinal W",
      "Beacon, port hand",
      "Beacon, starboard hand",
      "Beacon, preferred channel port hand",
      "Beacon, preferred channel starboard hand",
      "Beacon, isolated danger",
      "Beacon, safe water",
      "Beacon, special mark",
      "Cardinal mark N",
      "Cardinal mark E",
      "Cardinal mark S",
      "Cardinal mark W",
      "Port hand mark",
      "Starboard hand mark",
      "Preferred channel port hand",
      "Preferred channel starboard hand",
      "Isolated danger",
      "Safe water",
      "Special mark",
      "Light vessel, LANBY or rig",
  };
  if (a >= 0 && a < static_cast<int>(sizeof kText / sizeof kText[0])) return kText[a];
  return "Unknown (" + std::to_string(a) + ")";
}

const char* KindText(TargetKind kind) {
  switch (kind) {
    case TargetKind::kVessel: return "Vessel";
    case TargetKind::kBaseStation: return "Base station";
    case TargetKind::kAidToNavigation: return "Aid to navigation";
    case TargetKind::kSarAircraft: return "SAR aircraft";
    case TargetKind::kSart: return "AIS-SART";
    case TargetKind::kMob: return "Man overboard";
    case TargetKind::kEpirb: return "EPIRB";
  }
  return "";
}

const char* HullSymbolName(int ship_type, bool has_ship_type) {
  if (has_ship_type && ship_type >= 0 && ship_type < 100 && kAisShipTypeSymbol[ship_type] != nullptr)
    return kAisShipTypeSymbol[ship_type];
  return kDefaultHull;
}

std::string HullSymbolId(const Vessel& v, bool stale) {
  std::string id;
  if (v.kind == TargetKind::kVessel) {
    id = std::string("ais-") + HullSymbolName(v.ship_type, v.has_ship_type);
  } else if (v.kind == TargetKind::kSarAircraft) {
    id = "ais-air";
  } else {
    return id;
  }
  if (stale) id += "-stale";
  return id;
}

FvColor HullColor(const Vessel& v, bool stale) {
  static const std::map<std::string, FvColor> colors = [] {
    std::map<std::string, FvColor> out;
    std::shared_ptr<PngSymbolLibrary> sheet = AisSymbolSheet();
    for (const std::string& id : sheet->ids())
      if (const SymbolPixmap* pix = sheet->Pixmap(id)) out[id] = DominantColor(pix->tile);
    return out;
  }();
  const auto it = colors.find(HullSymbolId(v, stale));
  return it != colors.end() ? it->second : FvColor{128, 128, 128, 255};
}

double HullNominalLengthPx() { return kAisHullLengthTilePx / 2.0; }

std::shared_ptr<PngSymbolLibrary> AisSymbolSheet() {
  static const std::shared_ptr<PngSymbolLibrary> sheet = [] {
    auto lib = std::make_shared<PngSymbolLibrary>();
    if (lib->OpenSheetData(kAisSheetPng, sizeof kAisSheetPng, kAisSheetJson).ok()) {
      // Pixmap caches on first lookup; filling the cache now leaves no writes.
      for (const std::string& id : lib->ids()) lib->Pixmap(id);
    }
    return lib;
  }();
  return sheet;
}

}  // namespace ais
}  // namespace fv
