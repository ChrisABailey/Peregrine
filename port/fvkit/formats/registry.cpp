// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Format registry — see fvkit/formats/registry.h.

#include "fvkit/formats/registry.h"

#include <map>

#include "fvkit/formats/cadrg.h"
#include "fvkit/formats/dted.h"
#include "fvkit/formats/dted_shaded.h"
#include "fvkit/formats/geotiff.h"
#include "fvkit/formats/tiros.h"
#include "fvkit/formats/vpf.h"
#include "fvkit/store/tile_pack.h"

namespace fv {

namespace {

// std::map: node stability keeps FindFormat pointers valid across later
// registrations (a vector would reallocate).
std::map<std::string, FormatFactories>& Registry() {
  static std::map<std::string, FormatFactories> r;
  return r;
}

}  // namespace

Status RegisterFormat(const FormatFactories& factories) {
  if (factories.format_key.empty())
    return Status::Error(kInvalidArg, "format_key is empty");
  auto inserted = Registry().emplace(factories.format_key, factories);
  if (!inserted.second)
    return Status::Error(kInvalidArg,
                         "format already registered: " + factories.format_key);
  return Status::Ok();
}

const FormatFactories* FindFormat(const std::string& format_key) {
  auto it = Registry().find(format_key);
  return it == Registry().end() ? nullptr : &it->second;
}

std::vector<std::string> RegisteredFormatKeys() {
  std::vector<std::string> keys;
  keys.reserve(Registry().size());
  for (const auto& kv : Registry()) keys.push_back(kv.first);
  return keys;
}

void RegisterBuiltinFormats() {
  if (FindFormat("dted") == nullptr) {
    FormatFactories dted;
    dted.format_key = "dted";
    dted.make_enumerator = [] { return std::make_shared<DtedFrameEnumerator>(); };
    dted.make_elevation_source = [](const std::string& root_dir) {
      return std::make_shared<DtedElevationSource>(root_dir);
    };
    RegisterFormat(dted);
  }
  if (FindFormat("geotiff") == nullptr) {
    FormatFactories gtif;
    gtif.format_key = "geotiff";
    gtif.make_enumerator = [] {
      return std::make_shared<GeoTiffFrameEnumerator>();
    };
    gtif.make_raster_source = [] {
      return std::make_shared<GeoTiffRasterSource>();
    };
    RegisterFormat(gtif);
  }
  if (FindFormat("cadrg") == nullptr) {
    FormatFactories cadrg;
    cadrg.format_key = "cadrg";
    cadrg.make_enumerator = [] {
      return std::make_shared<CadrgFrameEnumerator>();
    };
    cadrg.make_raster_source = [] {
      return std::make_shared<CadrgRasterSource>();
    };
    RegisterFormat(cadrg);
  }
  if (FindFormat("gpkg") == nullptr) {
    FormatFactories gpkg;
    gpkg.format_key = "gpkg";
    gpkg.make_enumerator = [] { return std::make_shared<TilePackEnumerator>(); };
    gpkg.make_raster_source = [] {
      return std::make_shared<TilePackRasterSource>();
    };
    RegisterFormat(gpkg);
  }
  if (FindFormat("dted-shaded") == nullptr) {
    FormatFactories shaded;
    shaded.format_key = "dted-shaded";
    // Same cell tree as "dted" (the elevation-query format); this format is
    // the rendering path over those cells.
    shaded.make_enumerator = [] {
      return std::make_shared<DtedFrameEnumerator>();
    };
    shaded.make_raster_source = [] {
      return std::make_shared<DtedShadedRasterSource>();
    };
    RegisterFormat(shaded);
  }
  if (FindFormat("vpf") == nullptr) {
    FormatFactories vpf;
    vpf.format_key = "vpf";
    vpf.make_enumerator = [] { return std::make_shared<VpfFrameEnumerator>(); };
    RegisterFormat(vpf);
  }
  if (FindFormat("tiros") == nullptr) {
    FormatFactories tiros;
    tiros.format_key = "tiros";
    tiros.make_enumerator = [] {
      return std::make_shared<TirosFrameEnumerator>();
    };
    tiros.make_raster_source = [] {
      return std::make_shared<TirosRasterSource>();
    };
    RegisterFormat(tiros);
  }
}

void ClearFormatRegistryForTest() { Registry().clear(); }

}  // namespace fv
