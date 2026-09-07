// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// pippin_link_check — P1's acceptance, made into a binary.
//
// "The libraries link" is a claim a merged archive does NOT establish:
// `libtool -static` will happily produce an archive with an unresolved
// symbol in it, and the first thing to notice would be P2's Xcode target
// failing at 11 pm. So P1 links an actual iOS executable against
// libpippin_core.a and calls the seams Pippin will call, on the phone's
// architecture, with the phone's SDK.
//
// It is NOT a test — the tests are on the mac, which is where every line of
// this C++ is exercised. It is a link, plus (when handed a staged data pack)
// a run: opening the pack and drawing a frame of Kiawah is the cheapest
// possible check that P1's data and P1's cross-build fit each other.
//
//   pippin_link_check [/path/to/Data] [out.png]
//
// On the SIMULATOR the build is runnable — `xcrun simctl spawn booted` — so
// the run half of this is real. For the device the link is the whole of it.

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "fv_osm_style.h"
#include "fv_osm_vector_source.h"
#include "fv_road_graph.h"
#include "fv_route_rules.h"
#include "fv_router.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/canvas/geo_draw.h"
#include "fvkit/engine.h"
#include "fvkit/nav/position.h"
#include "fvkit/overlay/overlay.h"
#include "fvkit/proj.h"
#include "fvkit/settings.h"
#include "fvkit/tools/png_write.h"
#include "fvkit/vector/renderer.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("  %-34s %s\n", what, ok ? "ok" : "FAILED");
  if (!ok) ++failures;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string pack = argc > 1 ? argv[1] : std::string();
  const std::string out = argc > 2 ? argv[2] : std::string("pippin_kiawah.png");

  std::printf("pippin_link_check: core linked and running\n");

  // The seams with no data behind them: constructing them is what forces the
  // linker to resolve them.
  fv::MapProjection proj;
  Check(proj.SetSurfaceSize(390, 750).ok(), "MapProjection (a phone screen)");
  Check(proj.SetRotation(0.0).ok(), "MapProjection::SetRotation (PR1)");
  fv::CpuCanvas canvas(390, 750);
  canvas.Clear(fv::FvColor{255, 255, 255, 255});
  Check(canvas.Buffer().Width() == 390, "CpuCanvas");
  fv::FixQueue queue(64);
  fv::PositionFix fix;
  fix.lat = 32.6045;
  fix.lon = -80.0870;
  fix.has_position = true;
  queue.Push(fix);
  std::vector<fv::PositionFix> drained;
  queue.Drain(&drained);
  Check(drained.size() == 1, "nav FixQueue (MM1)");

  if (pack.empty()) {
    std::printf("pippin_link_check: no data pack given, link only\n");
    return failures == 0 ? 0 : 1;
  }

  // The pack, read exactly the way PippinKit will read it: one settings file
  // whose every path is relative to the pack directory.
  fv::Settings settings;
  const fv::Status ss = settings.Load(pack + "/pippin.ini");
  Check(ss.ok(), "pippin.ini loads");
  auto pack_path = [&](const std::string& key, const std::string& def) {
    return pack + "/" + settings.GetString(key, def);
  };

  auto source = std::make_shared<fv::OsmVectorSource>();
  Check(source->Open(pack_path("pippin.mbtiles", "kiawah.mbtiles")).ok(),
        "kiawah.mbtiles opens");
  auto style = std::make_shared<fv::OsmStyleEngine>();
  Check(style->LoadFile(pack_path("osm.style", "peregrine-osm.json")).ok(),
        "peregrine-osm.json loads");

  fv::routing::RoadGraph graph;
  Check(fv::routing::RoadGraph::Load(pack_path("routing.graph", "kiawah.fvroad"),
                                     &graph).ok(),
        "kiawah.fvroad loads");

  // The routing seam, on the profiles Pippin actually offers. The rule file
  // is polled on the mac and read once here, which is all a read-only bundle
  // can ever give it.
  fv::routing::RouteRulesFile rules(pack_path("routing.rules",
                                              "route-weights.json"));
  for (const char* profile : {"foot", "bicycle"}) {
    fv::routing::RouteOptions options;
    const fv::Status ps =
        fv::routing::SelectProfile(rules.rules(), profile, &options);
    fv::routing::Router router(graph);
    fv::routing::Route route;
    // Ruddy Turnstone to the beach club, the ledger's own fixture.
    const fv::Status rs = router.Route({32.6044007, -80.1083007},
                                       {32.59303776115624, -80.11886651782085},
                                       options, &route);
    char label[64];
    std::snprintf(label, sizeof(label), "route: %s", profile);
    Check(ps.ok() && rs.ok() && route.found, label);
    if (route.found)
      std::printf("      %.2f km, %.0f s, %zu points\n", route.length_m / 1000.0,
                  route.seconds, route.geometry.size());
  }

  // And the picture: the same frame the mac renders, drawn on the phone.
  Check(proj.SetCenter(fv::GeoPoint{32.6045, -80.0870}).ok(), "centre on Kiawah");
  Check(proj.SetPhysicalScale(25000.0, 0.25).ok(), "1:25,000");
  style->SetReferenceLatitude(32.6045);
  style->SetDisplayMmPerPixel(source->display_mm_per_pixel());
  fv::FvColor bg{255, 255, 255, 255};
  style->background(proj.Scale(), &bg);
  canvas.Clear(bg);
  fv::VectorRenderer renderer(source, style);
  const fv::Status rs = renderer.Render(proj, &canvas);
  Check(rs.ok() && renderer.draws_emitted() > 100, "Kiawah frame drawn");
  std::printf("      %zu features, %zu draws, z%d\n", renderer.features_queried(),
              renderer.draws_emitted(), source->last_query_zoom());
  Check(fv::WritePng(canvas.Buffer(), out).ok(), "frame written as PNG");

  std::printf("pippin_link_check: %s\n", failures == 0 ? "ALL OK" : "FAILURES");
  return failures == 0 ? 0 : 1;
}
