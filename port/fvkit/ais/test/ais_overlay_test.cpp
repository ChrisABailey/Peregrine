// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for the `fv.ais` overlay and AIS presentation, driven by the
/// synthetic Charleston recording (port/tools/make_ais_sample.py).

#include "fvkit/overlay/ais_overlay.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "fvkit/ais/presentation.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/tools/png_write.h"

namespace fv {
namespace {

constexpr double kT0 = 1793016000.0;  // 2026-10-26 12:00 UTC, the recording's start
constexpr double kKnot = 1852.0 / 3600.0;

uint64_t Fnv1a(const PixelBuffer& b) {
  uint64_t h = 1469598103934665603ull;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int i = 0; i < b.Width() * 4; ++i) {
      h ^= row[i];
      h *= 1099511628211ull;
    }
  }
  return h;
}

/// Replays the recording through a file transport, each line at its TAG time.
void Replay(AisOverlay* overlay) {
  FileLineTransport file(FV_AIS_SAMPLE);
  ASSERT_TRUE(file.Open().ok());
  std::string line;
  double t = kT0;
  while (file.ReadLine(&line) == LineResult::kLine) {
    ais::AisSentence s;
    if (ais::ParseAisSentence(line, &s, nullptr) && s.tag.has_time) t = static_cast<double>(s.tag.unix_s);
    overlay->FeedLine(line, t);
  }
}

MapProjection Harbour() {
  MapProjection p;
  p.SetSurfaceSize(800, 600);
  p.SetCenter({32.74, -79.88});
  p.SetScale(50000.0);
  return p;
}

PixelPoint PixelOf(const MapProjection& proj, const ais::Vessel& v) {
  double x = 0, y = 0;
  EXPECT_TRUE(proj.GeoToSurface({v.lat_deg, v.lon_deg}, &x, &y).ok());
  return PixelPoint{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))};
}

/// A system TrueType font, or empty.
std::string SystemFont() {
  for (const char* f : {"/System/Library/Fonts/Supplemental/Arial.ttf",
                        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"}) {
    if (FILE* fp = std::fopen(f, "rb")) {
      std::fclose(fp);
      return f;
    }
  }
  return {};
}

const app::InfoRow* FindRow(const app::ItemInfoSheet& sheet, const std::string& key) {
  for (const app::InfoRow& r : sheet.rows)
    if (r.key == key) return &r;
  return nullptr;
}

TEST(AisPresentation, TextsAndSymbols) {
  EXPECT_EQ(ais::ShipTypeText(70), "Cargo");
  EXPECT_EQ(ais::ShipTypeText(81), "Tanker, hazardous category A");
  EXPECT_EQ(ais::ShipTypeText(52), "Tug");
  EXPECT_EQ(ais::ShipTypeText(5), "Unknown (5)");
  EXPECT_EQ(ais::NavStatusText(1), "At anchor");
  EXPECT_EQ(ais::NavStatusText(15), "");
  EXPECT_EQ(ais::AidTypeText(30), "Special mark");

  EXPECT_STREQ(ais::HullSymbolName(70, true), "cargo");
  EXPECT_STREQ(ais::HullSymbolName(36, true), "sail");
  EXPECT_STREQ(ais::HullSymbolName(38, true), "sar-red");
  EXPECT_STREQ(ais::HullSymbolName(55, true), "le-red");
  // Symbollist.txt lists 70-74 and 79 only; Windows drew 75-78 with the default.
  EXPECT_STREQ(ais::HullSymbolName(75, true), "orange");
  EXPECT_STREQ(ais::HullSymbolName(7, true), "orange");
  EXPECT_STREQ(ais::HullSymbolName(70, false), "orange");

  std::shared_ptr<PngSymbolLibrary> sheet = ais::AisSymbolSheet();
  EXPECT_EQ(sheet->size(), 34u);
  for (const char* id : {"ais-orange", "ais-orange-stale", "ais-cargo", "ais-air", "ais-sail-stale"}) {
    const SymbolPixmap* pix = sheet->Pixmap(id);
    ASSERT_NE(pix, nullptr) << id;
    EXPECT_EQ(pix->tile.Height(), 64) << id;
    EXPECT_DOUBLE_EQ(pix->pixel_ratio, 2.0);
  }
  EXPECT_DOUBLE_EQ(ais::HullNominalLengthPx(), 32.0);
}

// A trail takes the colour of its hull's fill, read from the sprite.
TEST(AisPresentation, HullColorIsTheSpriteFill) {
  ais::Vessel sail;
  sail.has_ship_type = true;
  sail.ship_type = 36;
  const FvColor purple = ais::HullColor(sail, false);
  EXPECT_EQ(purple.r, 101);
  EXPECT_EQ(purple.g, 45);
  EXPECT_EQ(purple.b, 144);
  const FvColor faded = ais::HullColor(sail, true);
  EXPECT_FALSE(faded.r == purple.r && faded.g == purple.g && faded.b == purple.b);

  ais::Vessel cargo;
  cargo.has_ship_type = true;
  cargo.ship_type = 70;
  const FvColor yellow = ais::HullColor(cargo, false);
  EXPECT_EQ(yellow.r, 255);
  EXPECT_GT(yellow.g, 230);
  EXPECT_LT(yellow.b, 40);
}

TEST(AisOverlay, ReplaysRecordingIntoTable) {
  AisOverlay overlay;
  Replay(&overlay);
  EXPECT_EQ(overlay.decoder_stats().errors, 0u);
  EXPECT_EQ(overlay.decoder_stats().fragments_dropped, 0u);
  EXPECT_EQ(overlay.table().size(), 10u);

  const ais::Vessel* cargo = overlay.table().Find(366000101u);
  ASSERT_NE(cargo, nullptr);
  EXPECT_EQ(cargo->name, "ATLANTIC STAR");
  EXPECT_EQ(cargo->ship_type, 70);
  EXPECT_EQ(cargo->destination, "CHARLESTON");
  EXPECT_EQ(cargo->vessel_class, ais::VesselClass::kA);
  EXPECT_NEAR(cargo->sog_mps, 10.0 * kKnot, 0.06);
  EXPECT_EQ(cargo->station, "SYNTH01");
  EXPECT_FALSE(cargo->trail.empty());

  const ais::Vessel* sail = overlay.table().Find(338000103u);
  ASSERT_NE(sail, nullptr);
  EXPECT_EQ(sail->name, "SEA WIND");
  EXPECT_EQ(sail->ship_type, 36);
  EXPECT_EQ(sail->vessel_class, ais::VesselClass::kB);

  EXPECT_EQ(overlay.table().Find(970000001u)->kind, ais::TargetKind::kSart);
  EXPECT_EQ(overlay.table().Find(3669999u)->kind, ais::TargetKind::kBaseStation);
  EXPECT_TRUE(overlay.table().Find(993660002u)->virtual_aid);

  const ais::Vessel* own = overlay.table().Find(366000999u);
  ASSERT_NE(own, nullptr);
  EXPECT_TRUE(own->own_ship);
  EXPECT_FALSE(overlay.Shows(*own));
  EXPECT_FALSE(overlay.Shows(*overlay.table().Find(3669999u)));  // base stations off by default
}

TEST(AisOverlay, GoldenRender) {
  AisOverlay overlay;
  Replay(&overlay);
  ASSERT_TRUE(overlay.SetProperty("show_labels", app::PropertyValue::Bool(false)).ok());
  const MapProjection proj = Harbour();
  CpuCanvas canvas(800, 600);
  canvas.Clear(FvColor{255, 255, 255, 255});
  ASSERT_TRUE(overlay.OnDraw(proj, canvas).ok());
  // FV_AIS_RENDER_DIR=dir writes the render for a look.
  const char* dir = std::getenv("FV_AIS_RENDER_DIR");
  if (dir != nullptr) WritePng(canvas.Buffer(), std::string(dir) + "/ais_golden.png");
  EXPECT_EQ(Fnv1a(canvas.Buffer()), 9152095710700558098ull);

  if (dir != nullptr) {
    ASSERT_TRUE(overlay.SetProperty("show_labels", app::PropertyValue::Bool(true)).ok());
    if (!SystemFont().empty()) canvas.SetDefaultFont(SystemFont());
    canvas.Clear(FvColor{255, 255, 255, 255});
    ASSERT_TRUE(overlay.OnDraw(proj, canvas).ok());
    WritePng(canvas.Buffer(), std::string(dir) + "/ais_labels.png");
  }
}

TEST(AisOverlay, NamesShownOnlyAtLargeScales) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";
  AisOverlay overlay;
  Replay(&overlay);
  auto render = [&](bool labels, double scale) {
    EXPECT_TRUE(overlay.SetProperty("show_labels", app::PropertyValue::Bool(labels)).ok());
    MapProjection proj = Harbour();
    proj.SetScale(scale);
    CpuCanvas canvas(800, 600);
    EXPECT_TRUE(canvas.SetDefaultFont(font).ok());
    canvas.Clear(FvColor{255, 255, 255, 255});
    EXPECT_TRUE(overlay.OnDraw(proj, canvas).ok());
    return Fnv1a(canvas.Buffer());
  };
  EXPECT_NE(render(true, 50000.0), render(false, 50000.0));
  EXPECT_EQ(render(true, 250000.0), render(false, 250000.0));  // past label_max_scale
}

TEST(AisOverlay, HullTurnsClockwiseWithHeading) {
  AisOverlay overlay;
  ASSERT_TRUE(overlay.SetProperty("show_labels", app::PropertyValue::Bool(false)).ok());
  ASSERT_TRUE(overlay.SetProperty("vector_minutes", app::PropertyValue::Double(0.0)).ok());
  ASSERT_TRUE(overlay.SetProperty("symbol_size_pt", app::PropertyValue::Double(64.0)).ok());
  ais::AisMessage m;
  m.type = 1;
  m.mmsi = 366000001u;
  m.has_position = true;
  m.lat_deg = 32.74;
  m.lon_deg = -79.88;
  m.has_heading = true;
  m.heading_deg = 45;
  overlay.table().Update(m, kT0);
  overlay.Tick(kT0);

  const MapProjection proj = Harbour();
  CpuCanvas canvas(800, 600);
  canvas.Clear(FvColor{255, 255, 255, 255});
  ASSERT_TRUE(overlay.OnDraw(proj, canvas).ok());
  const PixelPoint c = PixelOf(proj, *overlay.table().Find(366000001u));
  auto inked = [&](int dx, int dy) {
    const unsigned char* px = canvas.Buffer().Row(c.y + dy) + 4 * (c.x + dx);
    return px[0] < 200 || px[1] < 200 || px[2] < 200;
  };
  // The hull lies along the NE-SW diagonal, not NW-SE.
  EXPECT_TRUE(inked(20, -20));
  EXPECT_TRUE(inked(-20, 20));
  EXPECT_FALSE(inked(20, 20));
  EXPECT_FALSE(inked(-20, -20));
}

TEST(AisOverlay, PickDetailsAndCpa) {
  AisOverlay overlay;
  Replay(&overlay);
  const MapProjection proj = Harbour();
  const ais::Vessel* cargo = overlay.table().Find(366000101u);
  ASSERT_NE(cargo, nullptr);

  std::vector<app::HitItem> hits;
  overlay.HitTestPoint(proj, PixelOf(proj, *cargo), 4.0, hits);
  ASSERT_FALSE(hits.empty());
  EXPECT_TRUE(std::any_of(hits.begin(), hits.end(),
                          [](const app::HitItem& h) { return h.feature == 366000101u; }));

  PositionFix own;
  own.SetPosition(32.74, -79.88);
  own.has_speed = true;
  own.speed_mps = 0.0;
  overlay.SetOwnship(own);
  std::optional<app::ItemInfoSheet> sheet = overlay.InfoAt(proj, PixelOf(proj, *cargo), 4.0);
  ASSERT_TRUE(sheet.has_value());
  EXPECT_EQ(sheet->title, "ATLANTIC STAR");
  ASSERT_NE(FindRow(*sheet, "Type"), nullptr);
  EXPECT_EQ(FindRow(*sheet, "Type")->value, "Cargo");
  EXPECT_EQ(FindRow(*sheet, "Destination")->value, "CHARLESTON");
  EXPECT_EQ(FindRow(*sheet, "Size")->value, "190 m x 32 m");
  EXPECT_EQ(FindRow(*sheet, "Speed")->value, "10.0 kn");
  EXPECT_EQ(FindRow(*sheet, "Status")->value, "Under way using engine");
  EXPECT_NE(FindRow(*sheet, "Range"), nullptr);
  EXPECT_EQ(FindRow(*sheet, "Station")->value, "SYNTH01");

  ASSERT_TRUE(overlay.SetProperty("speed_unit", app::PropertyValue::Choice(1)).ok());
  EXPECT_EQ(FindRow(*overlay.Details(366000101u), "Speed")->value, "18.5 km/h");

  // Own transponder: never picked.
  hits.clear();
  overlay.HitTestPoint(proj, PixelOf(proj, *overlay.table().Find(366000999u)), 1.0, hits);
  EXPECT_TRUE(std::none_of(hits.begin(), hits.end(),
                           [](const app::HitItem& h) { return h.feature == 366000999u; }));
}

TEST(AisOverlay, SearchByNameMmsiAndCallSign) {
  AisOverlay overlay;
  Replay(&overlay);
  std::atomic<bool> cancel{false};
  auto find = [&](const char* text) {
    app::SearchQuery q;
    q.text = text;
    std::vector<app::SearchResult> out;
    overlay.Search(q, cancel, out);
    return out;
  };
  std::vector<app::SearchResult> r = find("atlantic");
  ASSERT_EQ(r.size(), 1u);
  EXPECT_EQ(r[0].feature, 366000101u);
  EXPECT_EQ(r[0].title, "ATLANTIC STAR");
  ASSERT_EQ(find("338000103").size(), 1u);
  EXPECT_EQ(find("338000103")[0].title, "SEA WIND");
  ASSERT_EQ(find("WTUG2").size(), 1u);
  EXPECT_TRUE(find("OWN SHIP").empty());
}

TEST(AisOverlay, PropertiesFilterAndAge) {
  AisOverlay overlay;
  Replay(&overlay);
  const ais::Vessel* aton = overlay.table().Find(993660001u);
  ASSERT_NE(aton, nullptr);
  EXPECT_TRUE(overlay.Shows(*aton));
  ASSERT_TRUE(overlay.SetProperty("show_aton", app::PropertyValue::Bool(false)).ok());
  EXPECT_FALSE(overlay.Shows(*aton));

  EXPECT_FALSE(overlay.SetProperty("symbol_size_pt", app::PropertyValue::Double(2.0)).ok());
  EXPECT_FALSE(overlay.SetProperty("show_aton", app::PropertyValue::Double(1.0)).ok());
  EXPECT_FALSE(overlay.SetProperty("no_such_key", app::PropertyValue::Bool(true)).ok());

  ASSERT_TRUE(overlay.SetProperty("stale_minutes", app::PropertyValue::Double(2.0)).ok());
  EXPECT_DOUBLE_EQ(overlay.table().options().stale_after_s, 120.0);
  // The fishing boat falls silent at 296 s; at 600 s it is stale.
  EXPECT_TRUE(overlay.table().IsStale(*overlay.table().Find(366000105u), kT0 + 600));
  EXPECT_FALSE(overlay.table().IsStale(*overlay.table().Find(366000101u), kT0 + 600));
}

TEST(AisOverlay, TickDrainsPushedLinesAndReportsChanges) {
  AisOverlay overlay;
  std::thread feeder([&] { overlay.PushLine("!AIVDM,1,1,,A,13HOI:0P0000VOHLCnHQKwvL05Ip,0*23"); });
  feeder.join();
  EXPECT_TRUE(overlay.Tick(100.0));
  EXPECT_NE(overlay.table().Find(227006760u), nullptr);
  EXPECT_FALSE(overlay.Tick(101.0));
  EXPECT_TRUE(overlay.Tick(100.0 + 360.0));  // the record fades
  EXPECT_TRUE(overlay.Tick(100.0 + 1800.0));  // and is dropped
  EXPECT_EQ(overlay.table().size(), 0u);
}

TEST(AisOverlay, TickReadsTransport) {
  AisOverlay overlay;
  overlay.SetTransport(std::make_shared<FileLineTransport>(FV_AIS_SAMPLE));
  // Not opened: nothing read.
  EXPECT_FALSE(overlay.Tick(kT0));
  auto file = std::make_shared<FileLineTransport>(FV_AIS_SAMPLE);
  ASSERT_TRUE(file->Open().ok());
  overlay.SetTransport(file);
  EXPECT_TRUE(overlay.Tick(kT0 + 600));
  EXPECT_EQ(overlay.table().size(), 10u);
}

}  // namespace
}  // namespace fv
