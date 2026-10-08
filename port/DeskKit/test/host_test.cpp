// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// DeskHost: frames reach the shell, input reaches the view, a keyboard step
// keeps the position under the cursor, overlays draw over a cached base map
// and a newer request interrupts an older render.

#include "fv_desk_host.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <thread>
#include <vector>

#include "fv_desk_user_settings.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/formats/dted_shaded.h"
#include "fvkit/formats/registry.h"
#include "scratch.h"
#include "stub_formats.h"

using fv::GeoRect;
using fv::desk::DeskHost;
using fv::desk::FramePlacement;
using fv::desk::HostTick;
using fv::desk::test::Frame;
using fv::desk::test::Scratch;
using fv::desk::test::StubFrames;
using fv::desk::test::StubRasterReads;

namespace {

/// Releases the host at scope exit.
struct HostRef {
  DeskHost* host = DeskHost::Create();
  ~HostRef() { fv_desk_host_release(host); }
  DeskHost* operator->() { return host; }
};

/// Points peregrine.ini lookup at an empty file for the scope, so LoadSettings
/// reads nothing from the machine running the test.
struct EmptyIni {
  explicit EmptyIni(const Scratch& dir) {
    const std::string path = dir.Path("peregrine.ini");
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (f != nullptr) std::fclose(f);
    setenv("FVW_SETTINGS", path.c_str(), 1);
  }
  ~EmptyIni() { unsetenv("FVW_SETTINGS"); }
};

/// The index of the options page holding `key`, or -1.
int PageWith(DeskHost* h, int kind, const std::string& key) {
  for (int p = 0; p < h->OptionsPageCount(kind); ++p)
    for (int f = 0; f < h->OptionsFieldCount(kind, p); ++f)
      if (std::string(h->OptionsFieldAt(kind, p, f).key) == key) return p;
  return -1;
}

/// One tick, ignoring what changed.
void Tick(DeskHost* host) { (void)host->Tick(); }

/// Ticks until the worker has delivered the frame for the current view.
HostTick Settle(DeskHost* host) {
  HostTick t = host->Tick();
  host->WaitForRender();
  const HostTick u = host->Tick();
  t.new_frame |= u.new_frame;
  t.redraw |= u.redraw;
  t.status_changed |= u.status_changed;
  return t;
}

TEST(DeskHost, DeliversAFrameAtThePixelSize) {
  HostRef h;
  EXPECT_FALSE(h->HasFrame());
  EXPECT_EQ(h->FramePixels(), nullptr);
  // No surface yet: nothing is requested.
  h->Tick();
  h->WaitForRender();
  EXPECT_FALSE(h->Tick().new_frame);

  h->Resize(200, 100, 2.0, 0.25);
  const HostTick t = Settle(h.host);
  EXPECT_TRUE(t.new_frame);
  EXPECT_TRUE(t.redraw);
  ASSERT_TRUE(h->HasFrame());
  EXPECT_EQ(h->FrameWidth(), 400);
  EXPECT_EQ(h->FrameHeight(), 200);
  EXPECT_DOUBLE_EQ(h->FrameDisplayScale(), 2.0);
  ASSERT_NE(h->FramePixels(), nullptr);
  // No catalog: an opaque black ground.
  EXPECT_EQ(h->FramePixels()[0], 0);
  EXPECT_EQ(h->FramePixels()[3], 255);
  std::vector<uint8_t> copy(400 * 200 * 4, 7);
  EXPECT_FALSE(h->CopyFrame(copy.data(), copy.size() - 1));
  ASSERT_TRUE(h->CopyFrame(copy.data(), copy.size()));
  EXPECT_EQ(copy[0], 0);
  EXPECT_EQ(copy[3], 255);

  // The frame was drawn for the live view, so it sits where it is.
  FramePlacement p = h->Placement();
  ASSERT_TRUE(p.valid);
  EXPECT_NEAR(p.a, 1.0, 1e-9);
  EXPECT_NEAR(p.tx, 0.0, 1e-6);

  // Nothing changed: no new request and no new frame.
  const HostTick idle = Settle(h.host);
  EXPECT_FALSE(idle.new_frame);
  EXPECT_FALSE(idle.redraw);

  // A drag moves the placement before the next frame arrives.
  h->PointerDown(100, 50);
  h->PointerDrag(130, 40);
  EXPECT_TRUE(h->Tick().redraw);
  p = h->Placement();
  ASSERT_TRUE(p.valid);
  EXPECT_NEAR(p.tx, 30.0, 0.5);
  EXPECT_NEAR(p.ty, -10.0, 0.5);
  h->PointerUp(130, 40);
  EXPECT_TRUE(Settle(h.host).new_frame);
  EXPECT_NEAR(h->Placement().tx, 0.0, 1e-6);
}

TEST(DeskHost, StatusReportsScaleAndCursor) {
  HostRef h;
  h->Resize(200, 100, 1.0, 0.25);
  EXPECT_EQ(h->StatusMessage(), "No map data in the catalog");
  EXPECT_EQ(h->StatusScale(), "1:10,000,000");
  EXPECT_EQ(h->StatusPosition(), "");
  h->Tick();
  // The view centre is (0, 0); one point left of it is west.
  h->Hover(99.5 - 1, 49.5);
  EXPECT_TRUE(h->Tick().status_changed);
  const std::string pos = h->StatusPosition();
  EXPECT_NE(pos.find("00\xC2\xB0" "00.000' N"), std::string::npos) << pos;
  EXPECT_NE(pos.find("' W"), std::string::npos) << pos;
  h->HoverExit();
  EXPECT_EQ(h->StatusPosition(), "");
}

TEST(DeskHost, OpenCatalogReportsAMissingFile) {
  HostRef h;
  EXPECT_NE(h->OpenCatalog("/nonexistent/catalog.sqlite"), "");
  EXPECT_EQ(h->CatalogPath(), "");
  EXPECT_NE(h->Execute("no.such.command"), "");
}

class DeskHostStub : public ::testing::Test {
 protected:
  void SetUp() override {
    fv::ClearFormatRegistryForTest();
    // Three nested raster series. GNC sets the data centre, (32.5, -80); the
    // finer two reach the north-west, where the step tests put the cursor.
    StubFrames()["cadrg"] = {
        Frame("gnc", GeoRect{{28, -84}, {37, -76}}, "GNC", 5e6),
        Frame("jnc", GeoRect{{31, -83}, {35, -78}}, "JNC", 2e6),
        Frame("tpc", GeoRect{{32, -82.5}, {34, -79}}, "TPC", 5e5),
    };
    ASSERT_TRUE(fv::desk::test::RegisterStubFormat("cadrg").ok());
  }
  void TearDown() override { fv::ClearFormatRegistryForTest(); }

  std::string MakeCatalog(Scratch& dir) {
    const std::string path = dir.Path("catalog.sqlite");
    fv::Catalog cat;
    EXPECT_TRUE(cat.Open(path).ok());
    int64_t src = 0;
    int added = 0;
    EXPECT_TRUE(cat.AddDataSource("/stub/cadrg", "cadrg", 0, &src).ok());
    EXPECT_TRUE(cat.Scan(src, &added).ok());
    return path;
  }
};

TEST_F(DeskHostStub, OpeningACatalogCentresOnItsData) {
  Scratch dir;
  const std::string path = MakeCatalog(dir);
  HostRef h;
  h->Resize(400, 300, 1.0, 0.25);
  ASSERT_EQ(h->OpenCatalog(path), "");
  EXPECT_EQ(h->CatalogPath(), path);
  // Recentred on the union of the frames and settled on a series there.
  EXPECT_EQ(h->StatusProduct().rfind("cadrg ", 0), 0u) << h->StatusProduct();
  EXPECT_EQ(h->StatusMessage(), "");
  // The stub format cannot draw, but the frame still arrives.
  EXPECT_TRUE(Settle(h.host).new_frame);
}

TEST_F(DeskHostStub, GoToChoosesTheSeriesNearestTheScale) {
  Scratch dir;
  const std::string path = MakeCatalog(dir);
  HostRef h;
  h->Resize(400, 300, 1.0, 0.25);
  ASSERT_EQ(h->OpenCatalog(path), "");
  h->GoTo(33, -80, 6e5);
  EXPECT_EQ(h->StatusProduct(), "cadrg TPC");
  EXPECT_EQ(h->StatusScale(), "1:500,000");
  EXPECT_NEAR(h->CenterLat(), 33, 1e-9);
  EXPECT_NEAR(h->CenterLon(), -80, 1e-9);
  EXPECT_DOUBLE_EQ(h->ScaleDenom(), 5e5);
  // Outside TPC and JNC only GNC is left.
  h->GoTo(29, -77, 6e5);
  EXPECT_EQ(h->StatusProduct(), "cadrg GNC");
}

TEST_F(DeskHostStub, PageUpAtACornerStepsTheMapUnderTheCursor) {
  Scratch dir;
  const std::string path = MakeCatalog(dir);
  HostRef h;
  h->Resize(400, 300, 1.0, 0.25);
  ASSERT_EQ(h->OpenCatalog(path), "");
  ASSERT_EQ(h->Execute("map.zoom_out"), "");
  ASSERT_EQ(h->Execute("map.zoom_out"), "");
  ASSERT_EQ(h->StatusProduct(), "cadrg GNC");

  // Near the upper-left corner, about (33.7, -82.1): inside JNC and TPC.
  h->Hover(40, 30);
  const std::string before = h->StatusPosition();
  h->Step(+1);
  EXPECT_EQ(h->StatusProduct(), "cadrg JNC");
  EXPECT_EQ(h->StatusScale(), "1:2,000,000");
  EXPECT_EQ(h->StatusPosition(), before);
  h->Step(+1);
  EXPECT_EQ(h->StatusProduct(), "cadrg TPC");
  EXPECT_EQ(h->StatusPosition(), before);
  // Past the finest series the raster ladder stops and says so.
  h->Step(+1);
  EXPECT_EQ(h->StatusProduct(), "cadrg TPC");
  EXPECT_EQ(h->StatusMessage(), "No further map at this point");
}

// MARK: Menus, questions and jobs

using fv::desk::HostMenuEntry;
using fv::desk::HostRequest;

/// The entry for `id` in the flattened menu bar; kind is kMenuSeparator
/// when absent.
HostMenuEntry MenuEntry(DeskHost* h, const std::string& id) {
  for (int i = 0; i < h->MenuEntryCount(); ++i) {
    const HostMenuEntry e = h->MenuEntryAt(i);
    if (e.id == id) return e;
  }
  return HostMenuEntry();
}

TEST(DeskHost, MenusFlattenInPreorderWithShortcuts) {
  HostRef h;
  std::vector<std::string> tops;
  int projections = 0;
  bool in_projection = false;
  for (int i = 0; i < h->MenuEntryCount(); ++i) {
    const HostMenuEntry e = h->MenuEntryAt(i);
    if (e.kind == fv::desk::kMenuTop) {
      EXPECT_EQ(e.depth, 0);
      tops.push_back(e.label);
    }
    if (e.depth <= 1) in_projection = e.kind == fv::desk::kMenuSubmenu && e.label == "Projection";
    if (in_projection && e.kind == fv::desk::kMenuCommand) {
      EXPECT_EQ(e.depth, 2);
      ++projections;
    }
  }
  EXPECT_EQ(tops, (std::vector<std::string>{"File", "Map", "Overlay"}));
  EXPECT_EQ(projections, 5);

  const HostMenuEntry save = MenuEntry(h.host, "file.save");
  EXPECT_EQ(save.kind, fv::desk::kMenuCommand);
  EXPECT_EQ(save.depth, 1);
  EXPECT_EQ(save.key, "S");
  EXPECT_EQ(save.modifiers, unsigned{fv::desk::kHostPrimary});
  const HostMenuEntry save_as = MenuEntry(h.host, "file.save_as");
  EXPECT_EQ(save_as.modifiers, unsigned{fv::desk::kHostPrimary | fv::desk::kHostShift});
  EXPECT_EQ(MenuEntry(h.host, "map.zoom_in").key, "=");

  EXPECT_FALSE(h->IsEnabled("file.save"));  // no file overlay
  EXPECT_TRUE(h->IsEnabled("map.zoom_in"));
  int checked = 0;
  for (int i = 0; i < h->MenuEntryCount(); ++i) {
    const std::string id = h->MenuEntryAt(i).id;
    if (id.rfind("map.projection.", 0) == 0) checked += h->IsChecked(id);
  }
  EXPECT_EQ(checked, 1);

  ASSERT_GE(h->ToolbarEntryCount(), 3);
  EXPECT_EQ(h->ToolbarEntryAt(0).id, "map.zoom_in");
  EXPECT_EQ(h->ToolbarEntryAt(0).icon, "zoom_in");
  EXPECT_EQ(h->ToolbarEntryAt(0).depth, 0);
  EXPECT_FALSE(h->ToolbarEntryAt(0).checkable);
  for (int i = 0; i < h->MenuEntryCount(); ++i) {
    const HostMenuEntry e = h->MenuEntryAt(i);
    if (e.id.rfind("map.projection.", 0) == 0) EXPECT_TRUE(e.checkable) << e.id;
  }
  EXPECT_FALSE(save.checkable);
}

/// What the test's request handler saw and how it answers.
struct Answers {
  DeskHost* host = nullptr;
  std::vector<HostRequest> asked;
  std::vector<std::vector<std::string>> items;
  std::string path;  ///< answered to choosers; "" cancels
  std::string directory;
};

void Answer(void* context) {
  Answers& a = *static_cast<Answers*>(context);
  const HostRequest r = a.host->PendingRequest();
  a.asked.push_back(r);
  std::vector<std::string> labels;
  for (int i = 0; i < r.item_count; ++i)
    labels.push_back(a.host->RequestItemLabel(i) + "|" + a.host->RequestItemPattern(i));
  a.items.push_back(labels);
  if (r.kind == fv::desk::kRequestChooseDirectory) {
    if (!a.directory.empty()) a.host->AnswerPath(a.directory);
  } else if (!a.path.empty()) {
    a.host->AnswerPath(a.path);
    a.host->AnswerIndex(0);
  }
}

TEST(DeskHost, QuestionsReachTheRequestHandler) {
  Scratch dir;
  HostRef h;
  // Without a handler a question is a cancel.
  EXPECT_EQ(h->Execute("file.open_workspace"), "");
  EXPECT_EQ(h->TakeError(), "");

  Answers a;
  a.host = h.host;
  h->SetRequestHandler(&Answer, &a);
  EXPECT_EQ(h->Execute("file.open_workspace"), "");
  ASSERT_EQ(a.asked.size(), 1u);
  EXPECT_EQ(a.asked[0].kind, fv::desk::kRequestChooseOpen);
  EXPECT_EQ(a.items[0], std::vector<std::string>{"Peregrine Workspace (*.fvws)|*.fvws"});
  EXPECT_EQ(h->PendingRequest().kind, fv::desk::kRequestNone);

  // An answered path flows on: this one does not exist, so loading fails.
  a.path = dir.Path("missing.fvws");
  EXPECT_EQ(h->Execute("file.open_workspace"), "");
  EXPECT_NE(h->TakeError(), "");

  // Save Workspace asks for a save spec and writes there.
  a.path = dir.Path("saved.fvws");
  EXPECT_EQ(h->Execute("file.save_workspace"), "");
  EXPECT_EQ(a.asked.back().kind, fv::desk::kRequestChooseSave);
  EXPECT_EQ(a.asked.back().suggested_name, "Untitled.fvws");
  EXPECT_TRUE(std::filesystem::exists(a.path));
  h->SetRequestHandler(nullptr, nullptr);
}

TEST(DeskHost, QuitIsRequestedOnceEverythingIsClosed) {
  HostRef h;
  EXPECT_FALSE(h->QuitRequested());
  EXPECT_EQ(h->Execute("app.quit"), "");
  EXPECT_TRUE(h->QuitRequested());
}

TEST_F(DeskHostStub, ACatalogBuildReportsProgressThroughTick) {
  Scratch dir;
  const std::string root = dir.Path("data");
  std::filesystem::create_directories(root);
  HostRef h;
  h->Resize(400, 300, 1.0, 0.25);
  Answers a;
  a.host = h.host;
  a.directory = root;
  a.path = dir.Path("built.sqlite");
  h->SetRequestHandler(&Answer, &a);
  Tick(h.host);

  ASSERT_EQ(h->Execute("map.catalog_build"), "");
  ASSERT_EQ(a.asked.size(), 2u);
  EXPECT_EQ(a.asked[0].kind, fv::desk::kRequestChooseDirectory);
  EXPECT_EQ(a.asked[1].kind, fv::desk::kRequestChooseSave);
  EXPECT_TRUE(h->JobActive());
  EXPECT_FALSE(h->IsEnabled("map.catalog_build"));

  bool job_changed = false, catalog_changed = false, menus_changed = false;
  for (int i = 0; i < 2000 && h->JobActive(); ++i) {
    const HostTick t = h->Tick();
    job_changed |= t.job_changed;
    catalog_changed |= t.catalog_changed;
    menus_changed |= t.menus_changed;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  ASSERT_FALSE(h->JobActive());
  EXPECT_TRUE(job_changed);
  EXPECT_TRUE(catalog_changed);
  EXPECT_TRUE(menus_changed);  // the Raster group appeared
  EXPECT_EQ(h->CatalogPath(), a.path);
  EXPECT_EQ(h->TakeNotice(), "Catalogued 3 frames from 1 source.");
  EXPECT_EQ(h->TakeNotice(), "");
  EXPECT_EQ(h->StatusProduct().rfind("cadrg ", 0), 0u) << h->StatusProduct();
  h->SetRequestHandler(nullptr, nullptr);
}

// MARK: Overlay drawing

/// Four drawable series over the same degree square at 1:4M, 1:2M, 1:1M and
/// 1:500k, S0 to S3, each tiled in quarter-degree frames.
class DeskHostDrawn : public DeskHostStub {
 protected:
  void SetUp() override {
    fv::ClearFormatRegistryForTest();
    StubRasterReads().Reset();
    std::vector<fv::FrameInfo> frames;
    const double scales[] = {4e6, 2e6, 1e6, 5e5};
    for (int k = 0; k < 4; ++k) {
      const std::string key = "S" + std::to_string(k);
      for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
          const std::string name = key + "_" + std::to_string(i) + std::to_string(j);
          const GeoRect r{{32 + 0.25 * i, -81 + 0.25 * j}, {32.25 + 0.25 * i, -80.75 + 0.25 * j}};
          frames.push_back(Frame(name.c_str(), r, key.c_str(), scales[k]));
        }
    }
    StubFrames()["cadrg"] = frames;
    ASSERT_TRUE(fv::desk::test::RegisterDrawableStubFormat("cadrg").ok());
  }
  void TearDown() override {
    StubRasterReads().Release();
    DeskHostStub::TearDown();
  }

  std::vector<uint8_t> Pixels(DeskHost* h) {
    const uint8_t* px = h->FramePixels();
    return std::vector<uint8_t>(px, px + static_cast<size_t>(h->FrameWidth()) * h->FrameHeight() * 4);
  }
};

TEST_F(DeskHostDrawn, AGridToggledFromTheMenuDrawsOverTheSameBaseMap) {
  Scratch dir;
  const std::string path = MakeCatalog(dir);
  HostRef h;
  h->Resize(400, 300, 1.0, 0.25);
  ASSERT_EQ(h->OpenCatalog(path), "");
  h->GoTo(32.5, -80.5, 5e5);
  ASSERT_TRUE(Settle(h.host).new_frame);
  const int reads = StubRasterReads().Count();
  ASSERT_GT(reads, 0);
  const std::vector<uint8_t> plain = Pixels(h.host);
  // The stub colour at the centre: the base map drew.
  const size_t mid = (150u * 400u + 200u) * 4u;
  EXPECT_EQ(plain[mid + 2], 200);

  ASSERT_EQ(h->Execute("overlay.toggle.fv.grid"), "");
  EXPECT_TRUE(h->IsChecked("overlay.toggle.fv.grid"));
  EXPECT_TRUE(Settle(h.host).new_frame);
  EXPECT_NE(Pixels(h.host), plain);
  EXPECT_EQ(StubRasterReads().Count(), reads);

  ASSERT_EQ(h->Execute("overlay.toggle.fv.grid"), "");
  EXPECT_TRUE(Settle(h.host).new_frame);
  EXPECT_EQ(Pixels(h.host), plain);
  EXPECT_EQ(StubRasterReads().Count(), reads);
}

/// Strong red pixels: grid ink after its line colour is set to red.
int RedCount(const std::vector<uint8_t>& px) {
  int n = 0;
  for (size_t i = 0; i + 3 < px.size(); i += 4) n += px[i] > 200 && px[i + 1] < 60 && px[i + 2] < 60;
  return n;
}

// DK6's proof: the Grid page changes colour and ticks through the generic
// field API, with no Grid-specific code in the host or the test.
TEST_F(DeskHostDrawn, TheGridOptionsPageChangesColourAndTicks) {
  Scratch dir;
  EmptyIni ini(dir);
  const std::string path = MakeCatalog(dir);
  HostRef h;
  ASSERT_EQ(h->LoadSettings(dir.Path("user-settings.json")), "");
  h->Resize(400, 300, 1.0, 0.25);
  ASSERT_EQ(h->OpenCatalog(path), "");
  h->GoTo(32.5, -80.5, 5e5);
  ASSERT_EQ(h->Execute("overlay.toggle.fv.grid"), "");
  ASSERT_TRUE(Settle(h.host).new_frame);
  const std::vector<uint8_t> before = Pixels(h.host);
  ASSERT_EQ(RedCount(before), 0);
  const int reads = StubRasterReads().Count();

  ASSERT_EQ(h->Execute("overlay.options"), "");
  EXPECT_TRUE(h->Tick().options_requested);
  ASSERT_EQ(h->TakeOptionsRequest(), fv::desk::kOptionsOverlay);
  EXPECT_EQ(h->TakeOptionsRequest(), -1);
  const int kind = fv::desk::kOptionsOverlay;
  const int page = PageWith(h.host, kind, "line_color");
  ASSERT_GE(page, 0);

  ASSERT_EQ(h->SetOptionValue(kind, page, "line_color", "#FF0000FF"), "");
  ASSERT_EQ(h->SetOptionValue(kind, page, "show_casing", "false"), "");
  EXPECT_TRUE(h->OptionsDirty(kind));
  ASSERT_EQ(h->ApplyOptions(kind), "");
  EXPECT_FALSE(h->OptionsDirty(kind));
  ASSERT_TRUE(Settle(h.host).new_frame);
  const std::vector<uint8_t> red = Pixels(h.host);
  EXPECT_GT(RedCount(red), 0);

  ASSERT_EQ(h->SetOptionValue(kind, page, "show_ticks", "false"), "");
  ASSERT_EQ(h->ApplyOptions(kind), "");
  ASSERT_TRUE(Settle(h.host).new_frame);
  const std::vector<uint8_t> no_ticks = Pixels(h.host);
  EXPECT_NE(no_ticks, red);
  EXPECT_LT(RedCount(no_ticks), RedCount(red));
  // Overlay options never redraw the base map.
  EXPECT_EQ(StubRasterReads().Count(), reads);

  fv::desk::UserSettings saved;
  ASSERT_TRUE(saved.Load(dir.Path("user-settings.json")).ok());
  EXPECT_EQ(saved.Get("grid.line_color"), "#FF0000FF");
  EXPECT_EQ(saved.Get("grid.show_ticks"), "false");
  h->CloseOptions(kind);
  EXPECT_FALSE(h->OptionsOpen(kind));
}

TEST_F(DeskHostDrawn, AMapOptionChangeRedrawsTheBaseMap) {
  Scratch dir;
  EmptyIni ini(dir);
  const std::string path = MakeCatalog(dir);
  HostRef h;
  ASSERT_EQ(h->LoadSettings(dir.Path("user-settings.json")), "");
  h->Resize(400, 300, 1.0, 0.25);
  ASSERT_EQ(h->OpenCatalog(path), "");
  h->GoTo(32.5, -80.5, 5e5);
  ASSERT_TRUE(Settle(h.host).new_frame);
  const int reads = StubRasterReads().Count();

  ASSERT_EQ(h->Execute("map.options"), "");
  ASSERT_EQ(h->TakeOptionsRequest(), fv::desk::kOptionsMap);
  const int kind = fv::desk::kOptionsMap;
  ASSERT_EQ(h->OptionsPageCount(kind), 1);
  EXPECT_EQ(std::string(h->OptionsPageTitle(kind, 0)), "Elevation");
  ASSERT_EQ(h->SetOptionValue(kind, 0, "elevation_bands_ft", "5000, 2500"), "");
  EXPECT_EQ(std::string(h->OptionsFieldAt(kind, 0, 0).value), "2500,5000");
  ASSERT_EQ(h->ApplyOptions(kind), "");
  EXPECT_EQ(fv::DtedShadedElevationBands(), (std::vector<int>{2500, 5000}));
  ASSERT_TRUE(Settle(h.host).new_frame);
  EXPECT_GT(StubRasterReads().Count(), reads);

  // A new host reads the saved breaks back.
  fv::SetDtedShadedElevationBands({});
  HostRef next;
  ASSERT_EQ(next->LoadSettings(dir.Path("user-settings.json")), "");
  EXPECT_EQ(fv::DtedShadedElevationBands(), (std::vector<int>{2500, 5000}));
  fv::SetDtedShadedElevationBands({});
}

TEST(DeskHost, AnUnreadableUserSettingsFileIsNeverOverwritten) {
  Scratch dir;
  EmptyIni ini(dir);
  const std::string path = dir.Path("user-settings.json");
  {
    std::FILE* f = std::fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fputs("{ \"values\": { \"grid.line_width\": \"2\", } ", f);
    std::fclose(f);
  }
  HostRef h;
  EXPECT_NE(h->LoadSettings(path), "");
  ASSERT_EQ(h->Execute("overlay.options"), "");
  const int kind = fv::desk::kOptionsOverlay;
  const int page = PageWith(h.host, kind, "line_width");
  ASSERT_GE(page, 0);
  ASSERT_EQ(h->SetOptionValue(kind, page, "line_width", "4"), "");
  ASSERT_EQ(h->ApplyOptions(kind), "");
  std::FILE* f = std::fopen(path.c_str(), "r");
  ASSERT_NE(f, nullptr);
  char buf[128] = {};
  std::fread(buf, 1, sizeof buf - 1, f);
  std::fclose(f);
  EXPECT_NE(std::string(buf).find("\"2\", }"), std::string::npos);
}

TEST(DeskHost, OptionsFieldsDescribeTheirControls) {
  HostRef h;
  ASSERT_EQ(h->Execute("overlay.options"), "");
  const int kind = fv::desk::kOptionsOverlay;
  const int page = PageWith(h.host, kind, "line_width");
  ASSERT_GE(page, 0);
  bool saw_width = false;
  std::string previous_section;
  for (int f = 0; f < h->OptionsFieldCount(kind, page); ++f) {
    const fv::desk::HostOptionsField field = h->OptionsFieldAt(kind, page, f);
    if (std::string(field.key) == "line_width") {
      saw_width = true;
      EXPECT_EQ(field.type, fv::desk::kFieldInt);
      EXPECT_EQ(std::string(field.section), "Lines");
      EXPECT_EQ(field.min, 1);
      EXPECT_EQ(field.max, 8);
    }
    if (std::string(field.key) == "line_color") EXPECT_EQ(field.type, fv::desk::kFieldColor);
  }
  EXPECT_TRUE(saw_width);
  EXPECT_NE(h->SetOptionValue(kind, page, "line_width", "wide"), "");
  EXPECT_NE(h->SetOptionValue(kind, page, "no_such_key", "1"), "");
  EXPECT_NE(h->SetOptionValue(kind, 99, "line_width", "2"), "");
  ASSERT_EQ(h->SetOptionValue(kind, page, "line_width", "3"), "");
  EXPECT_TRUE(h->OptionsDirty(kind));
  h->RevertOptions(kind);
  EXPECT_FALSE(h->OptionsDirty(kind));
  ASSERT_EQ(h->SetOptionValue(kind, page, "line_width", "3"), "");
  ASSERT_EQ(h->ResetOptionsPage(kind, page), "");
  EXPECT_FALSE(h->OptionsDirty(kind));
  EXPECT_FALSE(h->OptionsOpen(fv::desk::kOptionsMap));
  EXPECT_EQ(h->OptionsPageCount(fv::desk::kOptionsMap), 0);
}

TEST_F(DeskHostDrawn, AHeldPageUpRendersOnlyTheLastStep) {
  Scratch dir;
  const std::string path = MakeCatalog(dir);
  HostRef h;
  h->Resize(400, 300, 1.0, 0.25);
  ASSERT_EQ(h->OpenCatalog(path), "");
  h->GoTo(32.5, -80.5, 4e6);
  ASSERT_EQ(h->StatusProduct(), "cadrg S0");
  ASSERT_TRUE(Settle(h.host).new_frame);
  const uint64_t frames = h->FramesRendered();
  // S0 and S1 both show all sixteen of their frames; S3 shows fewer.
  const int whole_series = StubRasterReads().Count();

  // The first step's render is held inside its first map frame while the
  // key repeats twice more.
  const int before = StubRasterReads().Count();
  StubRasterReads().Hold();
  h->Step(+1);
  Tick(h.host);
  StubRasterReads().WaitUntilHeld();
  h->Step(+1);
  Tick(h.host);
  h->Step(+1);
  Tick(h.host);
  ASSERT_EQ(h->StatusProduct(), "cadrg S3");
  StubRasterReads().Release();

  EXPECT_TRUE(Settle(h.host).new_frame);
  // The held render was abandoned after its first map frame and the middle
  // step never started.
  EXPECT_EQ(h->FramesRendered(), frames + 1);
  EXPECT_LT(StubRasterReads().Count() - before, whole_series);
  const FramePlacement p = h->Placement();
  ASSERT_TRUE(p.valid);
  EXPECT_NEAR(p.a, 1.0, 1e-9);
  EXPECT_NEAR(p.tx, 0.0, 1e-6);
}

TEST_F(DeskHostDrawn, AnOverlayChangeNeitherWaitsForNorRedrawsTheBaseMap) {
  Scratch dir;
  const std::string path = MakeCatalog(dir);
  HostRef h;
  h->Resize(400, 300, 1.0, 0.25);
  ASSERT_EQ(h->OpenCatalog(path), "");
  h->GoTo(32.5, -80.5, 5e5);
  ASSERT_TRUE(Settle(h.host).new_frame);
  const int one_base = StubRasterReads().Count();
  const std::vector<uint8_t> plain = Pixels(h.host);
  h->GoTo(32.6, -80.6, 5e5);
  ASSERT_TRUE(Settle(h.host).new_frame);
  const uint64_t frames = h->FramesRendered();

  // Back to the first view, its base map held mid-render.
  const int before = StubRasterReads().Count();
  StubRasterReads().Hold();
  h->GoTo(32.5, -80.5, 5e5);
  Tick(h.host);
  StubRasterReads().WaitUntilHeld();
  // The command takes the stack lock, which the base map pass does not hold.
  auto toggled = std::async(std::launch::async,
                            [&h] { return h->Execute("overlay.toggle.fv.grid"); });
  const bool returned =
      toggled.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  StubRasterReads().Release();
  EXPECT_TRUE(returned);
  EXPECT_EQ(toggled.get(), "");

  EXPECT_TRUE(Settle(h.host).new_frame);
  // One frame, with the grid, over one rendering of the base map.
  EXPECT_EQ(h->FramesRendered(), frames + 1);
  EXPECT_EQ(StubRasterReads().Count() - before, one_base);
  EXPECT_NE(Pixels(h.host), plain);
}

TEST(DeskHostData, DrawsAGeoTiffBaseMap) {
  const char* root = std::getenv("FVW_TESTDATA_DIR");
  const std::string dir = root ? std::string(root) + "/geotiff" : std::string();
  if (dir.empty() || !std::filesystem::exists(dir)) GTEST_SKIP() << "no testdata/geotiff";
  fv::ClearFormatRegistryForTest();
  fv::RegisterBuiltinFormats();
  Scratch scratch;
  const std::string path = scratch.Path("catalog.sqlite");
  {
    fv::Catalog cat;
    ASSERT_TRUE(cat.Open(path).ok());
    int64_t src = 0;
    int added = 0;
    ASSERT_TRUE(cat.AddDataSource(dir, "geotiff", 0, &src).ok());
    ASSERT_TRUE(cat.Scan(src, &added).ok());
    ASSERT_GT(added, 0);
  }
  HostRef h;
  h->Resize(128, 128, 2.0, 0.25);
  ASSERT_EQ(h->OpenCatalog(path), "");
  EXPECT_EQ(h->StatusProduct().rfind("geotiff ", 0), 0u) << h->StatusProduct();
  ASSERT_TRUE(Settle(h.host).new_frame);
  ASSERT_EQ(h->FrameWidth(), 256);
  const uint8_t* px = h->FramePixels();
  int lit = 0;
  for (int i = 0; i < 256 * 256; ++i) lit += (px[4 * i] | px[4 * i + 1] | px[4 * i + 2]) != 0;
  EXPECT_GT(lit, 256 * 256 / 4);
  fv::ClearFormatRegistryForTest();
}

}  // namespace
