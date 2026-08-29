// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvpack — pre-render cataloged coverage into a GeoPackage tile pyramid
// (the plan's L5 TilePack: offline on the Mac, consumed as plain raster
// anywhere, iOS included).
//
//   fvpack --data TestData --series LFC --bounds "33.2,-85.5,34.3,-83.5" \
//          --levels 4 --out atlanta_lfc.gpkg [--table lfc] [--tile 256]

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/catalog/catalog.h"
#include "fvkit/engine.h"
#include "fvkit/formats/registry.h"
#include "fvkit/store/tile_pack.h"

namespace {
const char* Arg(int argc, char** argv, const char* name, const char* dflt) {
  for (int i = 1; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
  return dflt;
}
int Fail(const fv::Status& s, const char* what) {
  std::fprintf(stderr, "fvpack: %s: %s\n", what, s.message.c_str());
  return 1;
}
}  // namespace

int main(int argc, char** argv) {
  if (getenv("MSPCCS_DATA") == nullptr) setenv("MSPCCS_DATA", "fvw_core/PdfLib/sdk/lib", 0);
  std::string data = Arg(argc, argv, "--data", "TestData");
  std::string series = Arg(argc, argv, "--series", "");
  std::string bounds_s = Arg(argc, argv, "--bounds", "");
  std::string out = Arg(argc, argv, "--out", "pack.gpkg");
  std::string table = Arg(argc, argv, "--table", "tiles");
  int levels = std::atoi(Arg(argc, argv, "--levels", "3"));
  int tile = std::atoi(Arg(argc, argv, "--tile", "256"));

  double ll_lat, ll_lon, ur_lat, ur_lon;
  if (std::sscanf(bounds_s.c_str(), "%lf,%lf,%lf,%lf", &ll_lat, &ll_lon,
                  &ur_lat, &ur_lon) != 4) {
    std::fprintf(stderr, "fvpack: --bounds \"ll_lat,ll_lon,ur_lat,ur_lon\" required\n");
    return 1;
  }

  fv::RegisterBuiltinFormats();
  auto catalog = std::make_shared<fv::Catalog>();
  fv::Status s = catalog->Open(":memory:");
  if (!s.ok()) return Fail(s, "catalog");
  struct { const char* sub; const char* fmt; } srcs[] = {
      {"rpf", "cadrg"}, {"geotiff", "geotiff"}, {"tiros3", "tiros"}};
  for (auto& sc : srcs) {
    int64_t id;
    int n = 0;
    if (catalog->AddDataSource(data + "/" + sc.sub, sc.fmt, 0, &id).ok())
      catalog->Scan(id, &n);
  }

  int64_t series_id = 0;
  if (!series.empty()) {
    std::vector<fv::SeriesRow> rows;
    catalog->Series(&rows);
    // A series_key is no longer unique within a format (schema 2: "Color" at
    // 1 m and at 50 m are two series), so an exact display_name -- "Color 1
    // meter" -- wins, and a bare key that matches more than one row is named
    // as ambiguous rather than silently resolved to whichever came first.
    std::vector<const fv::SeriesRow*> hits;
    for (const auto& r : rows) {
      if (r.display_name == series) { hits.assign(1, &r); break; }
      if (r.series_key == series) hits.push_back(&r);
    }
    if (hits.size() == 1) series_id = hits[0]->id;
    if (hits.size() > 1) {
      std::fprintf(stderr, "%s: '%s' names %d series; use one of:\n",
                   "fvpack", series.c_str(), (int)hits.size());
      for (const auto* r : hits)
        std::fprintf(stderr, "    %s\n", r->display_name.c_str());
      return 1;
    }
    if (series_id == 0) {
      std::fprintf(stderr, "fvpack: unknown series '%s'\n", series.c_str());
      return 1;
    }
  }

  fv::MapEngine engine(catalog);
  fv::TilePackWriter writer;
  s = writer.Create(out, table, {{ll_lat, ll_lon}, {ur_lat, ur_lon}}, tile);
  if (!s.ok()) return Fail(s, "create");
  for (int z = 0; z < levels; ++z) {
    int n = 0;
    s = writer.WriteLevel(engine, z, series_id, &n);
    if (!s.ok()) return Fail(s, "level");
    std::fprintf(stderr, "fvpack: z%d: %d tile(s)\n", z, n);
  }
  s = writer.Close();
  if (!s.ok()) return Fail(s, "close");
  std::fprintf(stderr, "fvpack: wrote %s\n", out.c_str());
  return 0;
}
