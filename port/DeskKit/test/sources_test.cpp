// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "fv_desk_fake.h"
#include "fv_desk_sources.h"
#include "fvkit/app/source_spec.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/feature/geopackage.h"
#include "fvkit/overlay/feature_overlay.h"
#include "scratch.h"

namespace {

using fv::FeatureOverlay;
using fv::GeoPoint;
using fv::desk::FakeDesk;
using fv::desk::LayerRow;
using fv::desk::PropertyRow;
using fv::desk::SourceRow;
using fv::desk::SourceShown;

const std::string kRivers = FV_FEATURE_RIVERS;
const std::string kGrid = std::string("overlay.toggle.") + fv::app::kGridTypeId;

/// Writes the rivers sample to `path` twice, as tables "rivers" and "creeks".
void WriteTwoTables(const std::string& path) {
  FeatureOverlay sample;
  ASSERT_TRUE(sample.FileOpen(kRivers).ok());
  std::shared_ptr<fv::GeoPackage> gp;
  ASSERT_TRUE(fv::GeoPackage::Create(path, &gp).ok());
  ASSERT_TRUE(gp->WriteLayer(*sample.layer(), "rivers", nullptr).ok());
  // gpkg_contents identifiers are unique, so the second copy is renamed.
  fv::MemoryFeatureLayer creeks("Creeks", sample.layer()->Schema());
  std::vector<fv::Feature> all;
  ASSERT_TRUE(sample.layer()->All(&all).ok());
  for (const fv::Feature& f : all) ASSERT_TRUE(creeks.InsertWithId(f).ok());
  ASSERT_TRUE(gp->WriteLayer(creeks, "creeks", nullptr).ok());
  ASSERT_TRUE(gp->Save().ok());
}

/// The value under `section`/`key`; "-" when absent.
std::string Value(const std::vector<PropertyRow>& rows, const std::string& section, const std::string& key) {
  for (const PropertyRow& r : rows)
    if (r.section == section && r.key == key) return r.value;
  return "-";
}

/// A FakeDesk on Charleston harbour with the grid and a two-table GeoPackage open.
class SourcesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto& mv = fd_.desk().map_view();
    mv.SetViewport(mv.View().WithCamera(GeoPoint{32.86, -79.95}, 250000, 0));
    gpkg_ = dir_.Path("two.gpkg");
    WriteTwoTables(gpkg_);
    ASSERT_TRUE(fd_.Run({kGrid}).ok());
    Open(gpkg_);
  }
  void Open(const std::string& path) {
    fd_.shell().files_to_open = {path};
    ASSERT_TRUE(fd_.Run({"overlay.open"}).ok());
    ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  }
  std::vector<SourceRow> Sources() { return fd_.desk().sources().Rows(); }
  std::vector<LayerRow> Layers() { return fd_.desk().layers().Rows(); }
  /// The layer names of source `s`, top first.
  std::vector<std::string> NamesOf(const SourceRow& s) {
    const auto layers = Layers();
    std::vector<std::string> out;
    for (size_t i : s.layers) out.push_back(layers[i].name);
    return out;
  }
  std::vector<std::string> Specs() {
    std::vector<std::string> out;
    for (const auto& o : fd_.desk().overlays().OfType(FeatureOverlay::kTypeId))
      out.push_back(o->AsPersistence()->file_spec());
    std::sort(out.begin(), out.end());
    return out;
  }

  fv::desk::test::Scratch dir_;
  std::string gpkg_;
  FakeDesk fd_{400, 400};
};

TEST_F(SourcesTest, AFileOfTwoTablesIsOneSourceOfTwoLayers) {
  EXPECT_EQ(Specs(), (std::vector<std::string>{fv::app::JoinSourceSpec(gpkg_, "creeks"),
                                               fv::app::JoinSourceSpec(gpkg_, "rivers")}));
  const auto sources = Sources();
  ASSERT_EQ(sources.size(), 2u);
  EXPECT_EQ(sources[0].name, "two.gpkg");
  EXPECT_EQ(sources[0].path, gpkg_);
  EXPECT_EQ(sources[0].type_name, "Feature Layer");
  // The file's first table is on top and current.
  EXPECT_EQ(NamesOf(sources[0]), (std::vector<std::string>{"Charleston rivers", "Creeks"}));
  EXPECT_TRUE(Layers()[sources[0].layers[0]].current);
  EXPECT_TRUE(sources[0].current);
  // An overlay without a file is a source of its own.
  EXPECT_EQ(sources[1].name, "Lat/Lon Grid");
  EXPECT_TRUE(sources[1].path.empty());
  EXPECT_EQ(sources[1].layers.size(), 1u);

  Open(gpkg_);  // already open: nothing new
  EXPECT_EQ(Specs().size(), 2u);
}

TEST_F(SourcesTest, TheOverlayMenuListsAFilesLayersUnderIt) {
  const fv::desk::Menu* menu = fd_.desk().menus().Find("overlay");
  ASSERT_NE(menu, nullptr);
  const fv::desk::MenuItem* file = nullptr;
  for (const fv::desk::MenuItem& m : menu->items)
    if (m.label == "two.gpkg") file = &m;
  ASSERT_NE(file, nullptr);
  ASSERT_EQ(file->children.size(), 2u);
  EXPECT_EQ(file->children[0].label, "Charleston rivers");
  EXPECT_EQ(file->children[1].label, "Creeks");
  // The first table is current, and choosing the other makes it current.
  EXPECT_TRUE(fd_.desk().commands().IsChecked(file->children[0].command_id));
  ASSERT_TRUE(fd_.Run({file->children[1].command_id}).ok());
  EXPECT_TRUE(fd_.desk().commands().IsChecked(file->children[1].command_id));
}

TEST_F(SourcesTest, TheSourceCheckBoxShowsAndHidesEveryLayer) {
  auto& desk = fd_.desk();
  EXPECT_EQ(Sources()[0].shown, SourceShown::kAll);
  ASSERT_TRUE(desk.SetLayerVisible(Sources()[0].layers[1], false).ok());
  EXPECT_EQ(Sources()[0].shown, SourceShown::kSome);
  const uint64_t gen = desk.LayersGeneration();
  ASSERT_TRUE(desk.SetSourceVisible(0, false).ok());
  EXPECT_GT(desk.LayersGeneration(), gen);
  EXPECT_EQ(Sources()[0].shown, SourceShown::kNone);
  const auto hidden = Sources();
  for (size_t i : hidden[0].layers) EXPECT_FALSE(Layers()[i].visible);
  EXPECT_TRUE(Layers()[Sources()[1].layers[0]].visible);  // the grid is untouched

  // The menu's items act on the current layer's source.
  EXPECT_TRUE(desk.commands().IsEnabled("layers.source.show_all"));
  EXPECT_FALSE(desk.commands().IsEnabled("layers.source.hide_all"));
  ASSERT_TRUE(fd_.Run({"layers.source.show_all"}).ok());
  EXPECT_EQ(Sources()[0].shown, SourceShown::kAll);
  EXPECT_EQ(desk.SetSourceVisible(5, true).code, fv::kInvalidArg);
}

TEST_F(SourcesTest, MovingASourceGathersItsLayers) {
  auto& desk = fd_.desk();
  Open(kRivers);  // a third source, newest on top
  auto sources = Sources();
  ASSERT_EQ(sources.size(), 3u);
  EXPECT_EQ(sources[0].name, "charleston-rivers.geojson");
  EXPECT_EQ(sources[1].name, "two.gpkg");

  ASSERT_TRUE(desk.MoveSource(0, 1).ok());
  sources = Sources();
  EXPECT_EQ(sources[0].name, "two.gpkg");
  EXPECT_EQ(sources[1].name, "charleston-rivers.geojson");
  EXPECT_EQ(sources[0].layers, (std::vector<size_t>{0, 1}));

  // Interleaved by the Layers view, the source still lists both its layers...
  ASSERT_TRUE(desk.MoveLayer(2, 1).ok());
  sources = Sources();
  EXPECT_EQ(sources[0].name, "two.gpkg");
  EXPECT_EQ(sources[0].layers, (std::vector<size_t>{0, 2}));
  EXPECT_EQ(sources[1].layers, (std::vector<size_t>{1}));
  // ...and moving it below its neighbour gathers them there, in order.
  ASSERT_TRUE(desk.MoveSource(0, 1).ok());
  sources = Sources();
  EXPECT_EQ(sources[0].name, "charleston-rivers.geojson");
  EXPECT_EQ(sources[1].name, "two.gpkg");
  EXPECT_EQ(NamesOf(sources[1]), (std::vector<std::string>{"Charleston rivers", "Creeks"}));
  EXPECT_EQ(sources[1].layers, (std::vector<size_t>{1, 2}));
  EXPECT_EQ(sources[2].name, "Lat/Lon Grid");

  // Up again, above the top source.
  ASSERT_TRUE(desk.MoveSource(2, 0).ok());
  EXPECT_EQ(Sources()[0].name, "Lat/Lon Grid");
  EXPECT_EQ(desk.MoveSource(0, 7).code, fv::kInvalidArg);
}

TEST_F(SourcesTest, PropertiesDescribeTheLayerAndTheSource) {
  auto& desk = fd_.desk();
  ASSERT_TRUE(desk.SelectLayer(Sources()[0].layers[1]).ok());
  ASSERT_TRUE(fd_.Run({"layers.properties"}).ok());
  EXPECT_EQ(fd_.shell().layer_properties_shown, 1);
  EXPECT_EQ(desk.properties_title(), "Layer Properties — Creeks");
  const auto& layer = desk.properties();
  EXPECT_EQ(Value(layer, "Layer", "Type"), "Feature Layer");
  EXPECT_EQ(Value(layer, "Layer", "Geometry"), "Line");
  EXPECT_EQ(Value(layer, "Layer", "Features"), "3");
  EXPECT_NE(Value(layer, "Layer", "Fields").find("name"), std::string::npos);
  EXPECT_NE(Value(layer, "Layer", "Extent"), "-");
  EXPECT_EQ(Value(layer, "Source", "Path"), gpkg_);
  EXPECT_EQ(Value(layer, "Source", "Format"), "GeoPackage");
  EXPECT_EQ(Value(layer, "Source", "Table"), "creeks");
  EXPECT_NE(Value(layer, "Source", "Size"), "-");

  ASSERT_TRUE(desk.SelectSource(0).ok());
  ASSERT_TRUE(fd_.Run({"layers.source.properties"}).ok());
  EXPECT_EQ(desk.properties_title(), "Source Properties — two.gpkg");
  EXPECT_EQ(Value(desk.properties(), "Source", "Layers"), "2");
  EXPECT_EQ(Value(desk.properties(), "Layers", "Creeks"), "Line, 3 features");

  // The built-in grid's Properties is its Overlay Options page.
  ASSERT_TRUE(desk.SelectSource(1).ok());
  ASSERT_TRUE(fd_.Run({"layers.properties"}).ok());
  EXPECT_EQ(fd_.shell().layer_properties_shown, 2);  // the layer's and the source's, not the grid's
  ASSERT_TRUE(fd_.shell().options_shown);
  EXPECT_GE(fd_.shell().options_shown->initial_page(), 0);
}

TEST_F(SourcesTest, MenusListTheirCommandsAndFollowTheRow) {
  auto& desk = fd_.desk();
  auto ids = [](const std::vector<fv::desk::MenuItem>& items) {
    std::vector<std::string> out;
    for (const auto& m : items)
      if (!m.is_separator()) out.push_back(m.command_id);
    return out;
  };
  EXPECT_EQ(ids(desk.LayerMenu()),
            (std::vector<std::string>{"layers.properties", "overlay.style", "overlay.fields",
                                      "overlay.attribute_table", "layers.select_by_attributes",
                                      "layers.clear_selection", "overlay.export_selection", "layers.rule.add",
                                      "layers.import_style",
                                      "layers.export_style", "layers.zoom_to", "layers.close"}));
  EXPECT_EQ(ids(desk.SourceMenu()),
            (std::vector<std::string>{"layers.source.properties", "layers.source.show_all", "layers.source.hide_all",
                                      "layers.source.zoom_to", "layers.source.new_layer", "file.save",
                                      "layers.source.close"}));
  EXPECT_TRUE(desk.commands().IsEnabled("overlay.style"));
  EXPECT_TRUE(desk.commands().IsEnabled("layers.zoom_to"));
  ASSERT_TRUE(desk.SelectSource(1).ok());  // the grid: no features to style or zoom to
  EXPECT_FALSE(desk.commands().IsEnabled("overlay.style"));
  EXPECT_FALSE(desk.commands().IsEnabled("layers.zoom_to"));
  EXPECT_FALSE(desk.commands().IsEnabled("layers.source.zoom_to"));
  EXPECT_FALSE(desk.commands().IsEnabled("file.save"));
}

TEST_F(SourcesTest, ZoomToSourceFramesItsLayers) {
  auto& mv = fd_.desk().map_view();
  mv.SetViewport(mv.View().WithCamera(GeoPoint{10, 10}, 5e7, 0));
  ASSERT_TRUE(fd_.desk().SelectSource(0).ok());
  ASSERT_TRUE(fd_.Run({"layers.source.zoom_to"}).ok());
  fv::GeoRect box;
  ASSERT_TRUE(fv::desk::LayerExtent(fd_.desk().overlays().FindShared(fd_.desk().overlays().current()), &box));
  EXPECT_NEAR(mv.View().Center().lat, (box.ll.lat + box.ur.lat) / 2, 1e-6);
  EXPECT_NEAR(mv.View().Center().lon, (box.ll.lon + box.ur.lon) / 2, 1e-6);
  EXPECT_LT(mv.View().ScaleDenom(), 1e6);
  EXPECT_GT(mv.View().ScaleDenom(), 1e4);
}

TEST_F(SourcesTest, ClosingASourceClosesEveryLayer) {
  ASSERT_TRUE(fd_.desk().SelectSource(0).ok());
  ASSERT_TRUE(fd_.Run({"layers.source.close"}).ok());
  EXPECT_TRUE(fd_.desk().overlays().OfType(FeatureOverlay::kTypeId).empty());
  ASSERT_EQ(Sources().size(), 1u);
  ASSERT_TRUE(fd_.Run({"layers.close"}).ok());  // the grid, toggled off
  EXPECT_TRUE(Sources().empty());
}

TEST_F(SourcesTest, AWorkspaceReopensEachTable) {
  ASSERT_TRUE(fd_.desk().SetLayerVisible(Sources()[0].layers[1], false).ok());
  const fv::desk::Workspace saved = fd_.desk().CaptureWorkspace();
  FakeDesk other(400, 400);
  EXPECT_EQ(other.desk().ApplyWorkspace(saved), fv::app::FlowResult::kDone);
  EXPECT_TRUE(other.desk().warnings().empty()) << other.desk().warnings()[0];
  const auto sources = other.desk().sources().Rows();
  ASSERT_EQ(sources.size(), 2u);
  EXPECT_EQ(sources[0].layers.size(), 2u);
  EXPECT_EQ(sources[0].shown, SourceShown::kSome);
}

/// The overlay of the layer named `name`.
FeatureOverlay* LayerNamed(FakeDesk& fd, const std::string& name) {
  for (const auto& o : fd.desk().overlays().OfType(FeatureOverlay::kTypeId))
    if (o->Name() == name) return dynamic_cast<FeatureOverlay*>(o.get());
  return nullptr;
}

TEST_F(SourcesTest, StylingOneTableLeavesTheOtherAlone) {
  fd_.shell().properties_tabs = true;
  FeatureOverlay* rivers = LayerNamed(fd_, "Charleston rivers");
  FeatureOverlay* creeks = LayerNamed(fd_, "Creeks");
  ASSERT_TRUE(rivers && creeks);
  const double before = rivers->style().symbol.line_width_px;
  ASSERT_TRUE(fd_.desk().SelectLayer(Sources()[0].layers[1]).ok());
  ASSERT_EQ(fd_.desk().overlays().current(), creeks);
  ASSERT_TRUE(fd_.Run({"layers.properties"}).ok());
  auto style = fd_.desk().layer_style();
  ASSERT_TRUE(style);
  fv::FeatureSymbol symbol = style->EntrySymbol(0);
  symbol.line_width_px = before + 5;
  ASSERT_TRUE(style->SetEntrySymbol(0, symbol).ok());
  ASSERT_TRUE(fd_.desk().ApplyLayerProperties().ok());
  EXPECT_EQ(creeks->style().symbol.line_width_px, before + 5);
  EXPECT_EQ(rivers->style().symbol.line_width_px, before);

  // Saved and reopened, each table keeps its own style.
  ASSERT_TRUE(fd_.Run({"file.save"}).ok()) << (fd_.shell().errors.empty() ? "" : fd_.shell().errors.back().message);
  FeatureOverlay a, b;
  ASSERT_TRUE(a.FileOpen(fv::app::JoinSourceSpec(gpkg_, "rivers")).ok());
  ASSERT_TRUE(b.FileOpen(fv::app::JoinSourceSpec(gpkg_, "creeks")).ok());
  EXPECT_EQ(a.style().symbol.line_width_px, before);
  EXPECT_EQ(b.style().symbol.line_width_px, before + 5);
}

TEST_F(SourcesTest, TheEditorEditsTheLayerChosenOfSeveralOfItsType) {
  auto& desk = fd_.desk();
  FeatureOverlay* creeks = LayerNamed(fd_, "Creeks");
  FeatureOverlay* rivers = LayerNamed(fd_, "Charleston rivers");
  ASSERT_TRUE(creeks && rivers);
  // Creeks is the lower of the two; the editor takes it because it is current.
  ASSERT_TRUE(desk.SelectLayer(Sources()[0].layers[1]).ok());
  ASSERT_TRUE(fd_.Run({std::string("editor.mode.") + FeatureOverlay::kTypeId}).ok());
  EXPECT_EQ(desk.overlays().current(), creeks);
  EXPECT_TRUE(creeks->edit().has_edit_focus());
  EXPECT_FALSE(rivers->edit().has_edit_focus());
  EXPECT_EQ(Layers()[0].name, "Creeks");  // the edited layer draws on top

  fv::PixelBuffer out;
  ASSERT_TRUE(fd_.Render(&out).ok());
  for (const std::string& id : desk.commands().IdsWithPrefix("editor.tool."))
    if (desk.commands().Find(id)->label == "Draw Line") ASSERT_TRUE(fd_.Run({id}).ok());
  desk.OverlayPointerDown(100, 100);
  desk.OverlayPointerUp(100, 100);
  ASSERT_TRUE(fd_.Render(&out).ok());
  EXPECT_EQ(desk.overlays().current(), creeks);
  EXPECT_TRUE(creeks->edit().sketching());
  EXPECT_FALSE(rivers->edit().sketching());
}

}  // namespace
