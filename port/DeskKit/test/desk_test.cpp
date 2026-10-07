// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <map>

#include "fv_desk_fake.h"
#include "fv_map_enums.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/formats/registry.h"
#include "fvkit/overlay/point_overlay.h"
#include "fvkit/raster.h"
#include "scratch.h"
#include "stub_formats.h"

using fv::GeoPoint;
using fv::GeoRect;
using fv::app::FlowResult;
using fv::desk::FakeDesk;
using fv::desk::Menu;
using fv::desk::MenuItem;
using fv::desk::Workspace;
using fv::desk::test::Frame;
using fv::desk::test::Scratch;
using fv::desk::test::StubEnumerator;
using fv::desk::test::StubFrames;

namespace {

/// Catalogs whose frames come from stub enumerators named after real formats.
/// No file is opened by a scan, and nothing here can draw.
class DeskTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fv::ClearFormatRegistryForTest();
    const GeoRect wide{{30, -82}, {34, -78}};
    const GeoRect harbour{{32.7, -80.0}, {32.9, -79.8}};
    StubFrames()["cadrg"] = {Frame("gnc", wide, "GNC", 5e6), Frame("onc", wide, "ONC", 1e6)};
    StubFrames()["enc"] = {Frame("c", wide, "Coastal", 80000), Frame("h", harbour, "Harbour", 20000)};
    for (const char* key : {"cadrg", "enc"}) {
      fv::FormatFactories f;
      f.format_key = key;
      const std::string k = key;
      f.make_enumerator = [k] { return std::make_shared<StubEnumerator>(k); };
      ASSERT_TRUE(fv::RegisterFormat(f).ok());
    }
  }
  void TearDown() override { fv::ClearFormatRegistryForTest(); }

  /// Adds and scans one stub source; returns its id.
  int64_t AddSource(fv::Catalog& cat, const char* format) {
    int64_t src = 0;
    int added = 0;
    EXPECT_TRUE(cat.AddDataSource(std::string("/stub/") + format, format, 0, &src).ok());
    EXPECT_TRUE(cat.Scan(src, &added).ok());
    return src;
  }
};

std::vector<std::string> Ids(const std::vector<MenuItem>& items) {
  std::vector<std::string> out;
  for (const MenuItem& m : items)
    if (!m.command_id.empty()) out.push_back(m.command_id);
  return out;
}

std::vector<std::string> GroupItems(const fv::desk::Desk& desk) {
  std::vector<std::string> out;
  for (const std::string& id : Ids(desk.menus().Find("map")->items))
    if (id.compare(0, 10, "map.group.") == 0) out.push_back(id);
  return out;
}

// MARK: Map groups follow the data

TEST_F(DeskTest, FamiliesAppearOnlyWithData) {
  FakeDesk fd;
  fv::desk::Desk& desk = fd.desk();
  EXPECT_TRUE(GroupItems(desk).empty());
  EXPECT_EQ(desk.CurrentGroup(), nullptr);
  EXPECT_EQ(desk.CurrentStatus().message, "No map data in the catalog");
  EXPECT_FALSE(desk.commands().IsEnabled("map.recenter"));

  auto cat = std::make_shared<fv::Catalog>();
  ASSERT_TRUE(cat->Open(":memory:").ok());
  AddSource(*cat, "enc");
  AddSource(*cat, "cadrg");
  desk.SetCatalog(cat);
  // Table order, not scan order; nothing for elevation, OSM or DNC.
  EXPECT_EQ(GroupItems(desk), (std::vector<std::string>{"map.group.raster", "map.group.enc"}));
  ASSERT_NE(desk.CurrentGroup(), nullptr);
  EXPECT_EQ(desk.CurrentGroup()->id, "raster");
  EXPECT_TRUE(desk.commands().IsChecked("map.group.raster"));
  EXPECT_FALSE(desk.commands().IsChecked("map.group.enc"));
  EXPECT_EQ(desk.SelectGroup("dnc").code, fv::kUnsupported);
  EXPECT_EQ(desk.SelectGroup("nitf").code, fv::kNotFound);
}

TEST_F(DeskTest, MenuModelRebuildsOnACatalogChange) {
  FakeDesk fd;
  fv::desk::Desk& desk = fd.desk();
  auto cat = std::make_shared<fv::Catalog>();
  ASSERT_TRUE(cat->Open(":memory:").ok());
  AddSource(*cat, "cadrg");
  desk.SetCatalog(cat);
  EXPECT_EQ(GroupItems(desk), (std::vector<std::string>{"map.group.raster"}));

  const int before = fd.shell().menus_changed;
  const uint64_t gen = desk.menus().Generation();
  const int64_t enc = AddSource(*cat, "enc");
  desk.CatalogChanged();
  EXPECT_GT(fd.shell().menus_changed, before);
  EXPECT_GT(desk.menus().Generation(), gen);
  EXPECT_EQ(GroupItems(desk), (std::vector<std::string>{"map.group.raster", "map.group.enc"}));

  ASSERT_TRUE(desk.Execute("map.group.enc").ok());
  EXPECT_EQ(desk.CurrentGroup()->id, "enc");
  // The ENC series survive their source's removal; the group must not.
  ASSERT_TRUE(cat->RemoveDataSource(enc).ok());
  desk.CatalogChanged();
  EXPECT_EQ(GroupItems(desk), (std::vector<std::string>{"map.group.raster"}));
  EXPECT_EQ(desk.CurrentGroup()->id, "raster");

  // A change that alters no menu is not signalled.
  const int settled = fd.shell().menus_changed;
  desk.CatalogChanged();
  EXPECT_EQ(fd.shell().menus_changed, settled);
}

TEST_F(DeskTest, GroupChoosesTheProductUnderTheView) {
  FakeDesk fd;
  fv::desk::Desk& desk = fd.desk();
  auto cat = std::make_shared<fv::Catalog>();
  ASSERT_TRUE(cat->Open(":memory:").ok());
  AddSource(*cat, "enc");
  auto& mv = desk.map_view();
  mv.SetViewport(mv.View().WithCamera(GeoPoint{32.8, -79.9}, 20000, 0));
  desk.SetCatalog(cat);
  ASSERT_EQ(desk.CurrentGroup()->id, "enc");
  ASSERT_TRUE(mv.HasProduct());
  EXPECT_EQ(mv.Product().series_key, "Harbour");
  EXPECT_EQ(desk.CurrentStatus().product, "enc Harbour");
  EXPECT_EQ(desk.CurrentStatus().scale, "1:20,000");

  ASSERT_TRUE(desk.Execute("map.zoom_out").ok());
  ASSERT_TRUE(desk.Execute("map.zoom_out").ok());
  EXPECT_EQ(mv.Product().series_key, "Coastal");
}

TEST(FormatScale, GroupsThousands) {
  EXPECT_EQ(fv::desk::FormatScale(50000), "1:50,000");
  EXPECT_EQ(fv::desk::FormatScale(999.6), "1:1,000");
  EXPECT_EQ(fv::desk::FormatScale(5e6), "1:5,000,000");
  EXPECT_EQ(fv::desk::FormatScale(0), "");
}

// MARK: Overlay commands

TEST_F(DeskTest, OverlayTypesBecomeCommands) {
  FakeDesk fd;
  fv::desk::Desk& desk = fd.desk();
  const std::string grid = std::string("overlay.toggle.") + fv::app::kGridTypeId;
  const std::string points = std::string("overlay.new.") + fv::PointOverlay::kTypeId;
  ASSERT_NE(desk.commands().Find(grid), nullptr);
  ASSERT_NE(desk.commands().Find(points), nullptr);
  EXPECT_FALSE(desk.commands().IsChecked(grid));
  EXPECT_FALSE(desk.commands().IsEnabled("file.save"));

  ASSERT_TRUE(fd.Run({grid}).ok());
  EXPECT_TRUE(desk.commands().IsChecked(grid));
  // A static overlay is not a document: File ▸ Save stays off.
  EXPECT_FALSE(desk.commands().IsEnabled("file.save"));

  ASSERT_TRUE(fd.Run({points}).ok());
  EXPECT_TRUE(desk.commands().IsEnabled("file.save"));
  const auto instances = desk.commands().IdsWithPrefix("overlay.instance.");
  ASSERT_EQ(instances.size(), 1u);
  EXPECT_TRUE(desk.commands().IsChecked(instances[0]));
  EXPECT_NE(Ids(desk.menus().Find("overlay")->items).end(),
            std::find(Ids(desk.menus().Find("overlay")->items).begin(),
                      Ids(desk.menus().Find("overlay")->items).end(), instances[0]));

  fd.shell().next_save_answer = fv::app::AppShell::SaveAnswer::kDiscard;
  ASSERT_TRUE(fd.Run({"file.close"}).ok());
  EXPECT_TRUE(desk.commands().IdsWithPrefix("overlay.instance.").empty());
}

TEST_F(DeskTest, QuitStopsOnACancel) {
  FakeDesk fd;
  ASSERT_TRUE(fd.Run({std::string("overlay.new.") + fv::PointOverlay::kTypeId}).ok());
  fv::Overlay* doc = fd.desk().overlays().current();
  ASSERT_NE(doc, nullptr);
  ASSERT_NE(doc->AsPersistence(), nullptr);
  doc->AsPersistence()->set_dirty(true);
  fd.shell().next_save_answer = fv::app::AppShell::SaveAnswer::kCancel;
  ASSERT_TRUE(fd.Run({"app.quit"}).ok());
  EXPECT_EQ(fd.shell().quit_calls, 0);
  fd.shell().next_save_answer = fv::app::AppShell::SaveAnswer::kDiscard;
  ASSERT_TRUE(fd.Run({"app.quit"}).ok());
  EXPECT_EQ(fd.shell().quit_calls, 1);
}

// MARK: The editor menu

class ToolEditor : public fv::app::OverlayEditor {
 public:
  explicit ToolEditor(int* hits) : hits_(hits) {}
  fv::Status Activate() override { return fv::Status::Ok(); }
  fv::Status Deactivate() override { return fv::Status::Ok(); }
  bool AutoEnterOnCreate() const override { return false; }
  std::vector<fv::app::MenuNode> Tools() const override {
    fv::app::MenuNode draw;
    draw.label = "Draw";
    draw.icon = "pencil";
    draw.action = [this] { ++*hits_; };
    fv::app::MenuNode locked;
    locked.label = "Locked";
    locked.enabled = false;
    locked.action = [this] { *hits_ += 100; };
    fv::app::MenuNode sub;
    sub.label = "More";
    fv::app::MenuNode inner;
    inner.label = "Inner";
    inner.checked = true;
    inner.action = [] {};
    sub.children = {inner};
    return {draw, fv::app::MenuNode{}, locked, sub};
  }

 private:
  int* hits_;
};

TEST_F(DeskTest, TheEditorMenuExistsOnlyWhileEditing) {
  FakeDesk fd;
  fv::desk::Desk& desk = fd.desk();
  int hits = 0;
  fv::app::OverlayTypeDesc d;
  d.id = "test.sketch";
  d.display_name = "Sketch";
  d.factory = [] { return std::make_shared<fv::Overlay>("sketch"); };
  d.editor_factory = [&hits] { return std::make_unique<ToolEditor>(&hits); };
  ASSERT_TRUE(desk.types().Register(std::move(d)).ok());
  desk.Refresh();
  EXPECT_EQ(desk.menus().Find("editor"), nullptr);

  ASSERT_TRUE(fd.Run({"editor.mode.test.sketch"}).ok());
  EXPECT_EQ(fd.shell().editor_mode, "test.sketch");
  const Menu* editor = desk.menus().Find("editor");
  ASSERT_NE(editor, nullptr);
  EXPECT_EQ(editor->title, "Sketch");
  ASSERT_EQ(editor->items.size(), 4u);
  EXPECT_EQ(editor->items[0].label, "Draw");
  EXPECT_EQ(editor->items[0].icon, "pencil");
  EXPECT_TRUE(editor->items[1].is_separator());
  EXPECT_EQ(editor->items[3].label, "More");
  ASSERT_EQ(editor->items[3].children.size(), 1u);
  EXPECT_TRUE(desk.commands().IsChecked(editor->items[3].children[0].command_id));
  EXPECT_TRUE(desk.commands().IsChecked("editor.mode.test.sketch"));

  ASSERT_TRUE(desk.Execute(editor->items[0].command_id).ok());
  EXPECT_EQ(hits, 1);
  EXPECT_EQ(desk.Execute(editor->items[2].command_id).code, fv::kUnsupported);
  EXPECT_EQ(hits, 1);

  ASSERT_TRUE(fd.Run({"editor.mode.test.sketch"}).ok());
  EXPECT_EQ(desk.menus().Find("editor"), nullptr);
  EXPECT_TRUE(desk.commands().IdsWithPrefix("editor.tool.").empty());
}

// MARK: Workspace

TEST_F(DeskTest, WorkspaceRoundTrips) {
  Scratch dir;
  const std::string catalog_path = dir.Path("catalog.sqlite");
  {
    fv::Catalog cat;
    ASSERT_TRUE(cat.Open(catalog_path).ok());
    AddSource(cat, "cadrg");
    AddSource(cat, "enc");
  }
  const std::string grid = std::string("overlay.toggle.") + fv::app::kGridTypeId;
  const std::string points_file = dir.Path("marks.fvpoints");

  FakeDesk a;
  auto& mv = a.desk().map_view();
  mv.SetViewport(mv.View().WithCamera(GeoPoint{32.8, -79.9}, 20000, 30));
  ASSERT_TRUE(a.desk().OpenCatalog(catalog_path).ok());
  ASSERT_TRUE(a.Run({"map.group.enc", "map.projection.mercator", grid,
                     std::string("overlay.new.") + fv::PointOverlay::kTypeId})
                  .ok());
  a.shell().save_spec = points_file;
  ASSERT_TRUE(a.Run({"file.save_as"}).ok());
  ASSERT_TRUE(a.shell().errors.empty()) << a.shell().errors[0].message;
  mv.SetViewport(mv.View().WithCamera(GeoPoint{32.81, -79.91}, 26000, 30));
  ASSERT_TRUE(mv.HasProduct());
  const std::string drawn = mv.Product().series_key;

  const std::string ws_path = dir.Path("charleston.fvws");
  a.shell().save_spec = ws_path;
  ASSERT_TRUE(a.Run({"file.save_workspace"}).ok());
  const Workspace saved = a.desk().CaptureWorkspace();
  EXPECT_EQ(saved.catalog_path, catalog_path);
  EXPECT_EQ(saved.map_group, "enc");
  EXPECT_EQ(saved.product_series_key, drawn);
  EXPECT_EQ(saved.projection, fv::ProjectionType::kMercator);
  EXPECT_EQ(saved.overlays.at("count"), "2");

  FakeDesk b;
  b.shell().files_to_open = {ws_path};
  ASSERT_TRUE(b.Run({"file.open_workspace"}).ok());
  EXPECT_TRUE(b.shell().errors.empty());
  EXPECT_TRUE(b.desk().warnings().empty()) << b.desk().warnings()[0];
  const Workspace restored = b.desk().CaptureWorkspace();
  EXPECT_EQ(restored, saved);
  EXPECT_EQ(b.desk().CurrentGroup()->id, "enc");
  EXPECT_TRUE(b.desk().commands().IsChecked(grid));
  EXPECT_TRUE(b.desk().commands().IsChecked("map.projection.mercator"));
  auto docs = b.desk().overlays().OfType(fv::PointOverlay::kTypeId);
  ASSERT_EQ(docs.size(), 1u);
  EXPECT_EQ(docs[0]->AsPersistence()->file_spec(), points_file);
}

TEST_F(DeskTest, WorkspaceRestoreIsBestEffort) {
  FakeDesk fd;
  Workspace w;
  w.catalog_path = "/no/such/catalog.sqlite";
  w.map_group = "dnc";
  w.overlays = {{"count", "1"}, {"0.type", "gone.plugin"}, {"0.file", ""}, {"0.visible", "true"}};
  EXPECT_EQ(fd.desk().ApplyWorkspace(w), FlowResult::kDone);
  EXPECT_GE(fd.desk().warnings().size(), 3u);
  EXPECT_TRUE(fd.desk().overlays().Overlays().empty());
}

TEST_F(DeskTest, ACancelledCloseStopsTheRestore) {
  FakeDesk fd;
  ASSERT_TRUE(fd.Run({std::string("overlay.new.") + fv::PointOverlay::kTypeId}).ok());
  fd.desk().overlays().current()->AsPersistence()->set_dirty(true);
  fd.shell().next_save_answer = fv::app::AppShell::SaveAnswer::kCancel;
  Workspace w;
  w.map_group = "raster";
  EXPECT_EQ(fd.desk().ApplyWorkspace(w), FlowResult::kCanceled);
  EXPECT_EQ(fd.desk().overlays().Overlays().size(), 1u);
}

// MARK: Rendering

TEST_F(DeskTest, RenderDrawsTheOverlayStack) {
  Scratch dir;
  FakeDesk fd(320, 240);
  auto& mv = fd.desk().map_view();
  mv.SetViewport(mv.View().WithCamera(GeoPoint{32.8, -79.9}, 2e6, 0));
  fv::PixelBuffer empty;
  ASSERT_TRUE(fd.Render(&empty).ok());
  ASSERT_TRUE(fd.Run({std::string("overlay.toggle.") + fv::app::kGridTypeId}).ok());
  fv::PixelBuffer with_grid;
  ASSERT_TRUE(fd.Render(&with_grid).ok());
  ASSERT_EQ(with_grid.Width(), 320);
  int changed = 0;
  for (int y = 0; y < with_grid.Height(); ++y)
    for (int x = 0; x < with_grid.Width() * 4; ++x)
      changed += with_grid.Row(y)[x] != empty.Row(y)[x];
  EXPECT_GT(changed, 0);
  const std::string png = dir.Path("grid.png");
  ASSERT_TRUE(fd.RenderPng(png).ok());
  EXPECT_GT(std::filesystem::file_size(png), 100u);
}

TEST(DeskData, RendersAGeoTiffBaseMap) {
  const char* root = std::getenv("FVW_TESTDATA_DIR");
  const std::string dir = root ? std::string(root) + "/geotiff" : std::string();
  if (dir.empty() || !std::filesystem::exists(dir)) GTEST_SKIP() << "no testdata/geotiff";
  fv::ClearFormatRegistryForTest();
  fv::RegisterBuiltinFormats();
  auto cat = std::make_shared<fv::Catalog>();
  ASSERT_TRUE(cat->Open(":memory:").ok());
  int64_t src = 0;
  int added = 0;
  ASSERT_TRUE(cat->AddDataSource(dir, "geotiff", 0, &src).ok());
  ASSERT_TRUE(cat->Scan(src, &added).ok());
  ASSERT_GT(added, 0);

  FakeDesk fd(256, 256);
  fd.desk().SetCatalog(cat);
  ASSERT_EQ(fd.desk().CurrentGroup()->id, "raster");
  ASSERT_TRUE(fd.Run({"map.recenter"}).ok());
  auto& mv = fd.desk().map_view();
  ASSERT_TRUE(mv.HasProduct());
  EXPECT_EQ(mv.Product().format, "geotiff");
  fv::PixelBuffer buf;
  ASSERT_TRUE(fd.Render(&buf).ok());
  int lit = 0;
  for (int y = 0; y < buf.Height(); ++y)
    for (int x = 0; x < buf.Width(); ++x) {
      const unsigned char* p = buf.Row(y) + 4 * x;
      lit += (p[0] | p[1] | p[2]) != 0;
    }
  EXPECT_GT(lit, buf.Width() * buf.Height() / 4);
  fv::ClearFormatRegistryForTest();
}

}  // namespace
