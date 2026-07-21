// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvrender — the plan's L3b milestone: a headless map renderer.
//
//   fvrender --data TestData --center "33.7488 -84.3882" --scale 500000 \
//            --size 900x650 --out map.png [--series LFC] [--format cadrg ...]
//
// Scans the given data directory's known format subdirs (rpf -> cadrg,
// geotiff, dted, tiros3 -> tiros) into an in-memory catalog, configures the
// engine, renders, writes a PNG. The location string goes through
// GEO_string_to_lat_lon, so DMS and MGRS work too.

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/engine.h"
#include "fvkit/formats/registry.h"
#include "fvkit/tools/png_write.h"

#include "geo_tool.h"  // GEO_string_to_lat_lon (fv_geo_tool)

namespace {

int Fail(const fv::Status& s, const char* what) {
  std::fprintf(stderr, "fvrender: %s: %s (code %d)\n", what, s.message.c_str(),
               s.code);
  return 1;
}

const char* Arg(int argc, char** argv, const char* name, const char* dflt) {
  for (int i = 1; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
  return dflt;
}

}  // namespace

int main(int argc, char** argv) {
  // GEOTRANS data for datum shifts (GeoTIFF scan, MGRS parsing); same
  // default the pan-viewer demo uses.
  if (getenv("MSPCCS_DATA") == nullptr) {
    const char* dflt = "fvw_core/PdfLib/sdk/lib";
    if (FILE* f = std::fopen((std::string(dflt) + "/ellips.dat").c_str(), "rb")) {
      std::fclose(f);
      setenv("MSPCCS_DATA", dflt, 0);
    } else {
      std::fprintf(stderr,
                   "fvrender: warning: MSPCCS_DATA unset (datum shifts and "
                   "MGRS will fail)\n");
    }
  }

  std::string data = Arg(argc, argv, "--data", "TestData");
  std::string center_str = Arg(argc, argv, "--center", "33.7488 -84.3882");
  double scale = std::atof(Arg(argc, argv, "--scale", "500000"));
  std::string size = Arg(argc, argv, "--size", "900x650");
  std::string out = Arg(argc, argv, "--out", "map.png");
  std::string series = Arg(argc, argv, "--series", "");

  int w = 0, h = 0;
  if (std::sscanf(size.c_str(), "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) {
    std::fprintf(stderr, "fvrender: bad --size '%s'\n", size.c_str());
    return 1;
  }
  double lat = 0, lon = 0;
  if (GEO_string_to_lat_lon(center_str.c_str(), "WGE", &lat, &lon) != 0) {
    std::fprintf(stderr, "fvrender: cannot parse --center '%s'\n",
                 center_str.c_str());
    return 1;
  }

  fv::RegisterBuiltinFormats();
  auto catalog = std::make_shared<fv::Catalog>();
  fv::Status s = catalog->Open(":memory:");
  if (!s.ok()) return Fail(s, "catalog");

  struct { const char* subdir; const char* format; } sources[] = {
      {"rpf", "cadrg"}, {"geotiff", "geotiff"}, {"tiros3", "tiros"},
  };
  int total = 0;
  for (const auto& src : sources) {
    std::string path = data + "/" + src.subdir;
    int64_t id = 0;
    if (!catalog->AddDataSource(path, src.format, 0, &id).ok()) continue;
    int n = 0;
    if (catalog->Scan(id, &n).ok()) {
      std::fprintf(stderr, "fvrender: %s: %d frames\n", src.format, n);
      total += n;
    }
  }
  if (total == 0) {
    std::fprintf(stderr, "fvrender: no frames under %s\n", data.c_str());
    return 1;
  }

  int64_t series_id = 0;
  if (!series.empty()) {
    std::vector<fv::SeriesRow> rows;
    catalog->Series(&rows);
    for (const auto& r : rows)
      if (r.series_key == series) series_id = r.id;
    if (series_id == 0) {
      std::fprintf(stderr, "fvrender: unknown --series '%s'\n", series.c_str());
      return 1;
    }
  }

  fv::MapEngine engine(catalog);
  if (!(s = engine.SetSurfaceDimensions(w, h)).ok()) return Fail(s, "size");
  if (!(s = engine.SetCenter({lat, lon})).ok()) return Fail(s, "center");
  if (!(s = engine.SetScale(scale)).ok()) return Fail(s, "scale");

  fv::CpuCanvas canvas(w, h);
  canvas.Clear(fv::FvColor{24, 24, 24, 255});
  int drawn = 0;
  s = engine.RenderBaseMap(canvas, series_id, {}, &drawn);
  if (!s.ok()) return Fail(s, "render");
  std::fprintf(stderr, "fvrender: %d frame(s) composited\n", drawn);

  s = fv::WritePng(canvas.Buffer(), out);
  if (!s.ok()) return Fail(s, "png");
  std::fprintf(stderr, "fvrender: wrote %s (%dx%d at 1:%g, %.5f %.5f)\n",
               out.c_str(), w, h, scale, lat, lon);
  return 0;
}
