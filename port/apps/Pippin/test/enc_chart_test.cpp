// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "PPEncChart.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "fvkit/app/search.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/overlay/vector_map_overlay.h"
#include "fvkit/vector/renderer.h"

namespace fs = std::filesystem;

namespace {

/** The Charleston fixture, which carries the S-52 library beside its cells. */
std::string EncRoot() {
  const char* d = std::getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/enc";
  return fs::is_directory(p) ? p : std::string();
}

/** A TrueType font on this host for label tests; CpuCanvas has none built in. */
std::string HostFont() {
  for (const char* f : {"/System/Library/Fonts/Supplemental/Arial.ttf",
                        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"})
    if (fs::exists(f)) return f;
  return {};
}

fv::MapProjection ProjectionAt(double lat, double lon, double scale) {
  fv::MapProjection proj;
  proj.SetSurfaceSize(400, 400);
  proj.SetCenter({lat, lon});
  proj.SetPhysicalScale(scale, 0.1);
  return proj;
}

/** Pixels that are not the white chart ground. */
size_t InkedPixels(const fv::CpuCanvas& canvas) {
  const fv::PixelBuffer& b = canvas.Buffer();
  size_t n = 0;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int x = 0; x < b.Width(); ++x) {
      const unsigned char* p = row + 4 * x;
      if (p[0] != 255 || p[1] != 255 || p[2] != 255) ++n;
    }
  }
  return n;
}

}  // namespace

using pippin::EncBandForScale;

TEST(EncBandForScale, PicksTheNearestBandInLogScale) {
  const std::vector<int> all = {1, 2, 3, 4, 5};
  EXPECT_EQ(5, EncBandForScale(all, 10000));
  EXPECT_EQ(4, EncBandForScale(all, 50000));
  EXPECT_EQ(4, EncBandForScale(all, 100000));
  EXPECT_EQ(3, EncBandForScale(all, 250000));
  EXPECT_EQ(1, EncBandForScale(all, 10000000));
}

TEST(EncBandForScale, FallsBackToTheBandsPresent) {
  EXPECT_EQ(3, EncBandForScale({2, 3}, 12000)) << "no harbour cells: coastal";
  EXPECT_EQ(5, EncBandForScale({5}, 3000000));
  EXPECT_EQ(0, EncBandForScale({}, 50000));
}

TEST(EncChart, ACharlestonHarbourDrawDrawsTheHarbourBand) {
  const std::string root = EncRoot();
  if (root.empty()) GTEST_SKIP() << "no ENC test data";
  pippin::EncChart chart;
  const fv::Status s = chart.Open(root, root);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(8u, chart.cell_count());
  EXPECT_EQ((std::vector<int>{2, 3, 4, 5}), chart.bands());

  fv::CpuCanvas canvas(400, 400);
  const fv::Status r =
      chart.Render(ProjectionAt(32.78, -79.93, 12000), 254.0, 1.0,
                   pippin::EncChartPass::kBase, &canvas);
  ASSERT_TRUE(r.ok()) << r.message;
  EXPECT_EQ(5, chart.last_band());
  ASSERT_NE(nullptr, chart.last_renderer());
  EXPECT_GT(chart.last_renderer()->draws_emitted(), 0u);
  EXPECT_GT(InkedPixels(canvas), 400u * 400u / 4) << "the harbour is charted";
}

TEST(EncChart, TheUnderlayPassLeavesTheBaseRendererAlone) {
  const std::string root = EncRoot();
  if (root.empty()) GTEST_SKIP() << "no ENC test data";
  pippin::EncChart chart;
  ASSERT_TRUE(chart.Open(root, root).ok());
  fv::CpuCanvas canvas(400, 400);
  ASSERT_TRUE(chart.Render(ProjectionAt(32.7, -79.9, 300000), 254.0, 1.0,
                           pippin::EncChartPass::kUnderlay, &canvas)
                  .ok());
  EXPECT_EQ(0, chart.last_band());
  EXPECT_EQ(nullptr, chart.last_renderer());
}

/** Pixels that differ between two same-sized canvases. */
size_t DifferingPixels(const fv::CpuCanvas& a, const fv::CpuCanvas& b) {
  const fv::PixelBuffer& pa = a.Buffer();
  const fv::PixelBuffer& pb = b.Buffer();
  size_t n = 0;
  for (int y = 0; y < pa.Height(); ++y) {
    const unsigned char* ra = pa.Row(y);
    const unsigned char* rb = pb.Row(y);
    for (int x = 0; x < pa.Width(); ++x)
      if (std::memcmp(ra + 4 * x, rb + 4 * x, 3) != 0) ++n;
  }
  return n;
}

fv::FeatureDescription Desc(const char* cls,
                            std::vector<std::pair<const char*, const char*>> raw) {
  fv::FeatureDescription d;
  d.class_name = cls;
  for (const auto& kv : raw) {
    fv::FeatureAttribute a;
    a.code = kv.first;
    a.raw = kv.second;
    a.display = kv.second;
    d.attributes.push_back(a);
  }
  return d;
}

TEST(EncFeatureSummary, WritesALightInChartNotation) {
  using pippin::EncDepthUnits;
  EXPECT_EQ("Fl(2) G 6s 12m 5M",
            pippin::EncFeatureSummary(
                Desc("LIGHTS", {{"LITCHR", "2"}, {"SIGGRP", "(2)"}, {"COLOUR", "4"},
                                {"SIGPER", "6"}, {"HEIGHT", "12"}, {"VALNMR", "5"}}),
                EncDepthUnits::kMetres));
  EXPECT_EQ("Q R 2.5s", pippin::EncFeatureSummary(
                            Desc("LIGHTS", {{"LITCHR", "4"}, {"SIGGRP", "(1)"},
                                            {"COLOUR", "3"}, {"SIGPER", "2.5"}}),
                            EncDepthUnits::kFeet))
      << "a single flash's group is left off; light heights are not depths";
  EXPECT_EQ("Al.Fl WR", pippin::EncFeatureSummary(
                            Desc("LIGHTS", {{"LITCHR", "19"}, {"COLOUR", "1,3"}}),
                            EncDepthUnits::kMetres));
}

TEST(EncFeatureSummary, WritesDepthsInTheChosenUnits) {
  using pippin::EncDepthUnits;
  const fv::FeatureDescription area =
      Desc("DEPARE", {{"DRVAL1", "1.8"}, {"DRVAL2", "3.6"}});
  EXPECT_EQ("Depth 1.8\u2013" "3.6 m", pippin::EncFeatureSummary(area, EncDepthUnits::kMetres));
  EXPECT_EQ("Depth 5.9\u2013" "11.8 ft", pippin::EncFeatureSummary(area, EncDepthUnits::kFeet));
  EXPECT_EQ("Contour 10 m", pippin::EncFeatureSummary(
                                Desc("DEPCNT", {{"VALDCO", "10.0"}}), EncDepthUnits::kMetres));
  EXPECT_EQ("Depth 3.1 m", pippin::EncFeatureSummary(
                               Desc("WRECKS", {{"VALSOU", "3.1"}}), EncDepthUnits::kMetres));
  EXPECT_EQ("", pippin::EncFeatureSummary(Desc("LNDARE", {}), EncDepthUnits::kMetres));
}

TEST(EncChartSettings, ContoursOutOfOrderAreClamped) {
  pippin::EncChart chart;
  pippin::EncChartSettings s;
  s.safety_contour = 5.0;
  s.shallow_contour = 10.0;
  s.deep_contour = 2.0;
  chart.SetSettings(s);
  EXPECT_EQ(5.0, chart.settings().shallow_contour);
  EXPECT_EQ(5.0, chart.settings().deep_contour);
}

TEST(EncChart, TheSettingsChangeWhatIsDrawn) {
  const std::string root = EncRoot();
  if (root.empty()) GTEST_SKIP() << "no ENC test data";
  pippin::EncChart chart;
  ASSERT_TRUE(chart.Open(root, root).ok());
  const fv::MapProjection proj = ProjectionAt(32.76, -79.90, 20000);
  auto draw = [&](const pippin::EncChartSettings& s, fv::CpuCanvas* c) {
    chart.SetSettings(s);
    ASSERT_TRUE(chart.Render(proj, 254.0, 1.0, pippin::EncChartPass::kBase, c).ok());
  };
  const pippin::EncChartSettings defaults;
  fv::CpuCanvas base(400, 400);
  draw(defaults, &base);

  pippin::EncChartSettings shallow = defaults;
  shallow.safety_contour = 5.0;
  shallow.safety_depth = 5.0;
  fv::CpuCanvas a(400, 400);
  draw(shallow, &a);
  EXPECT_GT(DifferingPixels(base, a), 400u * 4) << "the depth shading moves";

  pippin::EncChartSettings sparse = defaults;
  sparse.category = pippin::EncDisplayCategory::kBase;
  fv::CpuCanvas b(400, 400);
  draw(sparse, &b);
  EXPECT_GT(DifferingPixels(base, b), 400u * 4) << "Base drops the standard features";

  pippin::EncChartSettings night = defaults;
  night.palette = pippin::EncPalette::kNight;
  fv::CpuCanvas c(400, 400);
  draw(night, &c);
  EXPECT_GT(DifferingPixels(base, c), 400u * 400u / 2) << "the night palette";
  EXPECT_NE(255, chart.ground_color().r);

  fv::CpuCanvas again(400, 400);
  draw(defaults, &again);
  EXPECT_EQ(0u, DifferingPixels(base, again)) << "back to the defaults, same chart";
}

/** Hiding buoy and beacon names changes the labelled chart; showing them
 *  again restores it. */
TEST(EncChart, NavaidNamesCanBeHidden) {
  const std::string root = EncRoot();
  if (root.empty()) GTEST_SKIP() << "no ENC test data";
  const std::string font = HostFont();
  if (font.empty()) GTEST_SKIP() << "no host font for labels";
  pippin::EncChart chart;
  ASSERT_TRUE(chart.Open(root, root).ok());
  chart.SetDrawLabels(true);
  const fv::MapProjection proj = ProjectionAt(32.75, -79.88, 20000);
  auto draw = [&](bool names, fv::CpuCanvas* c) {
    ASSERT_TRUE(c->SetDefaultFont(font).ok());
    pippin::EncChartSettings s;
    s.navaid_names = names;
    chart.SetSettings(s);
    ASSERT_TRUE(chart.Render(proj, 254.0, 1.0, pippin::EncChartPass::kBase, c).ok());
  };
  fv::CpuCanvas shown(400, 400), hidden(400, 400), again(400, 400);
  draw(true, &shown);
  draw(false, &hidden);
  draw(true, &again);
  EXPECT_GT(DifferingPixels(shown, hidden), 200u) << "the names are drawn text";
  EXPECT_EQ(0u, DifferingPixels(shown, again));
}

TEST(EncChart, ATapInTheHarbourDescribesWhatIsThere) {
  const std::string root = EncRoot();
  if (root.empty()) GTEST_SKIP() << "no ENC test data";
  pippin::EncChart chart;
  ASSERT_TRUE(chart.Open(root, root).ok());
  fv::CpuCanvas canvas(400, 400);
  ASSERT_TRUE(chart.Render(ProjectionAt(32.76, -79.90, 20000), 254.0, 1.0,
                           pippin::EncChartPass::kBase, &canvas)
                  .ok());
  EXPECT_TRUE(chart.FeaturesAt(-50, -50, 2.0, pippin::EncDepthUnits::kMetres).empty());
  size_t described = 0, with_depth = 0;
  for (int y = 20; y < 400; y += 40) {
    for (int x = 20; x < 400; x += 40) {
      const auto hits = chart.FeaturesAt(x, y, 4.0, pippin::EncDepthUnits::kMetres, 4);
      EXPECT_LE(hits.size(), 4u);
      for (const pippin::EncFeatureInfo& h : hits) {
        EXPECT_FALSE(h.title.empty());
        EXPECT_FALSE(h.acronym.empty());
        EXPECT_FALSE(h.source.empty());
        ++described;
        if (h.acronym == "DEPARE" && h.summary.rfind("Depth ", 0) == 0) ++with_depth;
      }
    }
  }
  EXPECT_GT(described, 50u);
  EXPECT_GT(with_depth, 10u) << "open water names its depth range";
}

TEST(EncChart, SearchFindsNamedObjectsAnywhereInTheSet) {
  const std::string root = EncRoot();
  if (root.empty()) GTEST_SKIP() << "no ENC test data";
  auto chart = std::make_unique<pippin::EncChart>();
  ASSERT_TRUE(chart->Open(root, root).ok());

  auto overlay = std::make_shared<fv::VectorMapOverlay>("Chart", chart->search_source());
  overlay->SetLabelTags({"OBJNAM"});
  fv::OverlayManager overlays;
  ASSERT_TRUE(overlays.Add(overlay).ok());

  // No area and nothing drawn: the name table answers for the whole set.
  fv::app::SearchQuery q;
  q.text = "buoy 31";
  const auto buoy = fv::app::SearchSession(overlays).Search(q);
  ASSERT_FALSE(buoy.empty());
  EXPECT_EQ("Charleston Harbor North Channel Lighted Buoy 31", buoy.front().title);

  q.text = "municipal marina";
  const auto marina = fv::app::SearchSession(overlays).Search(q);
  ASSERT_EQ(1u, marina.size()) << "one marina, not one row per cell or band";

  // The view does not limit a text search.
  fv::CpuCanvas canvas(400, 400);
  ASSERT_TRUE(chart->Render(ProjectionAt(32.70, -79.75, 12000), 254.0, 1.0,
                            pippin::EncChartPass::kBase, &canvas)
                  .ok());
  q.text = "customhouse";
  const auto reach = fv::app::SearchSession(overlays).Search(q);
  ASSERT_FALSE(reach.empty());
  EXPECT_NE(std::string::npos, reach.front().title.find("Customhouse Reach"));

  chart.reset();
  EXPECT_TRUE(fv::app::SearchSession(overlays).Search(q).empty()) << "detached";
}

TEST(EncChart, ADirectoryWithNoCellsIsAnError) {
  const fs::path empty = fs::temp_directory_path() / "pippin_enc_chart_empty";
  fs::create_directories(empty);
  pippin::EncChart chart;
  EXPECT_EQ(fv::kNotFound, chart.Open(empty.string(), empty.string()).code);
  fs::remove_all(empty);
}

#if defined(__APPLE__)
#include <mach/mach.h>

#include <chrono>
#include <cstdio>

namespace {

/** The process's physical footprint in MB, the figure iOS's memory limit uses. */
double FootprintMb() {
  task_vm_info_data_t info;
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO,
                reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
    return 0.0;
  return static_cast<double>(info.phys_footprint) / (1024.0 * 1024.0);
}

/** The unpacked NOAA Florida set, when it is present. */
std::string FloridaRoot() {
  const char* d = std::getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/enc-regions/FL/ENC_ROOT";
  return fs::is_directory(p) ? p : std::string();
}

}  // namespace

/** Memory and draw time over the Florida set at a phone-sized surface: one
 *  draw per band at Miami, then a run down the Keys at approach scale.
 *  Run with --gtest_also_run_disabled_tests. */
TEST(EncChartBench, DISABLED_FloridaFootprint) {
  const std::string fl = FloridaRoot();
  const std::string s52 = EncRoot();
  if (fl.empty() || s52.empty()) GTEST_SKIP() << "no Florida ENC set";
  const double before = FootprintMb();
  pippin::EncChart chart;
  ASSERT_TRUE(chart.Open(fl, s52).ok());
  chart.SetDrawLabels(true);
  std::printf("open: %zu cells, footprint %+.0f MB\n", chart.cell_count(),
              FootprintMb() - before);
  fv::CpuCanvas canvas(1320, 2868);
  double peak = 0.0;
  auto draw = [&](double lat, double lon, double scale) {
    fv::MapProjection proj;
    proj.SetSurfaceSize(1320, 2868);
    proj.SetCenter({lat, lon});
    proj.SetPhysicalScale(scale, 0.0554);
    const auto t0 = std::chrono::steady_clock::now();
    const fv::Status s = chart.Render(proj, 288.0, 3.0,
                                      pippin::EncChartPass::kBase, &canvas);
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
    EXPECT_TRUE(s.ok()) << s.message;
    const double mb = FootprintMb() - before;
    peak = std::max(peak, mb);
    return std::make_pair(ms, mb);
  };
  for (double scale : {3000000.0, 1000000.0, 300000.0, 50000.0, 12000.0}) {
    const auto r = draw(25.77, -80.17, scale);
    std::printf("Miami 1:%.0f band %d: %.0f ms, %zu draws, %zu cells held, "
                "footprint %+.0f MB\n",
                scale, chart.last_band(), r.first,
                chart.last_renderer()->draws_emitted(),
                chart.resident_cell_count(), r.second);
  }
  double worst_ms = 0.0;
  for (int i = 0; i <= 40; ++i) {
    const double f = i / 40.0;
    const auto r = draw(25.77 + f * (24.55 - 25.77), -80.17 + f * (-81.80 + 80.17),
                        50000.0);
    worst_ms = std::max(worst_ms, r.first);
  }
  std::printf("Keys run at 1:50000: worst draw %.0f ms, footprint %+.0f MB\n",
              worst_ms, FootprintMb() - before);
  std::printf("peak footprint %+.0f MB\n", peak);
}
#endif  // __APPLE__

TEST(EncChart, AHarbourScaleDrawOutsideTheHarbourCellsFallsBackToCoarserBands) {
  const std::string root = EncRoot();
  if (root.empty()) GTEST_SKIP() << "no ENC test data";
  pippin::EncChart chart;
  ASSERT_TRUE(chart.Open(root, root).ok());
  fv::CpuCanvas canvas(400, 400);
  // Offshore of Charleston: approach cells only, no harbour cell.
  ASSERT_TRUE(chart.Render(ProjectionAt(32.70, -79.85, 12000), 254.0, 1.0,
                           pippin::EncChartPass::kBase, &canvas)
                  .ok());
  EXPECT_EQ(4, chart.last_band());
  EXPECT_GT(InkedPixels(canvas), 400u * 400u / 4) << "the approach chart shows";
  EXPECT_FALSE(chart.FeaturesAt(200, 200, 4, pippin::EncDepthUnits::kMetres).empty())
      << "a tap answers from the band drawn there";
}
