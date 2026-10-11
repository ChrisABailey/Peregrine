// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// feature_windows_test.cpp — the windows the core opens for feature layers
/// (layers, form, table, style, fields, new layer), the options and profile
/// windows, and the window title, driven through a real DeskHost and a shown
/// map window on the Charleston rivers sample. Needs a display (run under
/// xvfb-run).

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "map_window.h"
#include "test/test_support.h"

namespace fs = std::filesystem;

namespace peregrine {
namespace {

using test::Drain;
using test::Scratch;
using test::SpinUntil;

/// A shown map window with a copy of the rivers sample open and current.
class FeatureWindowsTest : public test::DisplayTest {
 protected:
  void SetUp() override {
    test::DisplayTest::SetUp();
    if (IsSkipped()) return;
    window_ = std::make_unique<MapWindow>();
    window_->present();
    rivers_ = scratch_.Path("rivers.geojson");
    fs::copy_file(FV_FEATURE_RIVERS, rivers_);
    ASSERT_EQ(host().OpenOverlayFile(rivers_), "");
  }

  void TearDown() override {
    if (!window_) return;
    host().WaitForRender();
    window_.reset();
    Drain();
  }

  fv::desk::DeskHost& host() { return window_->host(); }
  MapWindow& window() { return *window_; }

  Scratch scratch_;
  std::string rivers_;
  std::unique_ptr<MapWindow> window_;
};

TEST_F(FeatureWindowsTest, TitleNamesTheOpenFile) {
  EXPECT_TRUE(SpinUntil([&] { return window().get_title() == "rivers.geojson"; }))
      << window().get_title();
}

TEST_F(FeatureWindowsTest, LayersPanelListsTheStackAndHidesALayer) {
  window().Execute("overlay.layers");
  ASSERT_TRUE(SpinUntil([&] { return window().layers_window() != nullptr; }));
  LayersWindow& layers = *window().layers_window();
  EXPECT_TRUE(layers.get_visible());
  const int count = host().LayerCount();
  ASSERT_GE(count, 1);
  EXPECT_NE(layers.list().get_row_at_index(count - 1), nullptr);
  EXPECT_EQ(layers.list().get_row_at_index(count), nullptr);

  int row = -1;
  for (int i = 0; i < count; ++i)
    if (host().LayerName(i) == "Charleston rivers") row = i;
  ASSERT_GE(row, 0);
  auto* box = dynamic_cast<Gtk::Box*>(layers.list().get_row_at_index(row)->get_child());
  ASSERT_NE(box, nullptr);
  auto* visible = dynamic_cast<Gtk::CheckButton*>(box->get_first_child());
  ASSERT_NE(visible, nullptr);
  EXPECT_TRUE(visible->get_active());
  visible->set_active(false);
  EXPECT_FALSE(host().LayerVisible(row));

  layers.close();
  EXPECT_FALSE(layers.get_visible());
  window().Execute("overlay.layers");
  ASSERT_TRUE(SpinUntil([&] { return layers.get_visible(); }));
}

TEST_F(FeatureWindowsTest, TableFiltersSelectsAndOpensTheForm) {
  window().Execute("overlay.attribute_table");
  ASSERT_TRUE(SpinUntil([&] { return window().attribute_table_window() != nullptr; }));
  AttributeTableWindow& table = *window().attribute_table_window();
  EXPECT_EQ(table.get_title(), "Attributes — Charleston rivers");
  EXPECT_EQ(table.row_count(), 3);
  EXPECT_EQ(table.count_text(), "3 features");
  EXPECT_EQ(table.view().get_columns()->get_n_items(), 3u);

  table.SetFilter("Cooper");
  EXPECT_EQ(table.row_count(), 1);
  table.SelectRow(0);
  EXPECT_EQ(host().SelectedTableRow(), 0);
  EXPECT_EQ(host().TableCell(0, 0), "Cooper River");

  table.EditRow(0);
  ASSERT_TRUE(SpinUntil([&] { return window().feature_form_window() != nullptr; }));
  FeatureFormWindow& form = *window().feature_form_window();
  EXPECT_EQ(form.get_title(), "Edit Feature — Cooper River");
  auto* depth = dynamic_cast<Gtk::Entry*>(form.editor(1));
  ASSERT_NE(depth, nullptr);
  EXPECT_EQ(depth->get_text(), "12");

  depth->set_text("-1");
  EXPECT_FALSE(form.Apply());
  EXPECT_NE(form.message(), "");
  depth = dynamic_cast<Gtk::Entry*>(form.editor(1));
  ASSERT_NE(depth, nullptr);
  depth->set_text("13.5");
  form.Ok();
  EXPECT_FALSE(form.get_visible());
  EXPECT_FALSE(host().FeatureFormOpen());
  ASSERT_TRUE(SpinUntil([&] { return host().TableCell(0, 1) == "13.5"; }));

  table.close();
  EXPECT_FALSE(host().AttributeTableOpen());
}

TEST_F(FeatureWindowsTest, StyleDialogClassifiesGraduatedAndApplies) {
  window().Execute("overlay.style");
  ASSERT_TRUE(SpinUntil([&] { return window().layer_style_window() != nullptr; }));
  LayerStyleWindow& style = *window().layer_style_window();
  EXPECT_EQ(style.get_title(), "Layer Style — Charleston rivers");
  EXPECT_FALSE(style.field().get_visible());
  ASSERT_NE(style.symbol_grid(), nullptr);

  style.renderer().set_selected(2);
  ASSERT_TRUE(SpinUntil([&] { return host().StyleRenderer() == 2 && style.field().get_visible(); }));
  style.field().set_selected(1);
  ASSERT_TRUE(SpinUntil([&] { return host().StyleField() == "flow"; }));
  style.classify_button().activate();
  Drain();
  // A class per river (three features), then "Other", one legend row each.
  const int entries = host().StyleEntryCount();
  EXPECT_EQ(entries, 4);
  EXPECT_TRUE(host().StyleEntryIsOther(entries - 1));
  EXPECT_NE(style.legend().get_row_at_index(entries - 1), nullptr);
  EXPECT_EQ(style.legend().get_row_at_index(entries), nullptr);
  style.SelectEntry(1);
  EXPECT_NE(style.symbol_grid()->get_child_at(1, 1), nullptr);

  EXPECT_TRUE(host().LayerStyleDirty());
  style.Ok();
  EXPECT_FALSE(style.get_visible());
  EXPECT_FALSE(host().LayerStyleOpen());
}

TEST_F(FeatureWindowsTest, FieldsDialogAddsAndDropsFieldsAskingFirst) {
  window().Execute("overlay.fields");
  ASSERT_TRUE(SpinUntil([&] { return window().layer_fields_window() != nullptr; }));
  LayerFieldsWindow& fields = *window().layer_fields_window();
  EXPECT_EQ(fields.get_title(), "Layer Fields — Charleston rivers");
  EXPECT_EQ(fields.selected(), 0);
  EXPECT_EQ(fields.name_entry().get_text().raw(), host().LayerFieldName(0));

  fields.AddField();
  EXPECT_EQ(fields.selected(), 3);
  fields.name_entry().set_text("kind");
  fields.CommitEditor();
  Drain();
  EXPECT_EQ(host().LayerFieldName(3), "kind");

  // Dropping "flow" loses its values, so applying asks first.
  fields.SelectField(2);
  Drain();
  EXPECT_EQ(fields.selected(), 2);
  EXPECT_EQ(host().RemoveLayerField(2), "");
  fields.Reload();
  std::string asked;
  fields.set_confirm([&asked](const std::string& loss) {
    asked = loss;
    return false;
  });
  EXPECT_FALSE(fields.Apply());
  EXPECT_NE(asked, "");
  EXPECT_TRUE(host().LayerFieldsDirty());
  fields.set_confirm([](const std::string&) { return true; });
  EXPECT_TRUE(fields.Apply());
  Drain();
  EXPECT_EQ(fields.message(), "");
  fields.close();
  EXPECT_FALSE(host().LayerFieldsOpen());
}

TEST_F(FeatureWindowsTest, NewLayerCreatesAndOpensAFile) {
  window().Execute("overlay.new_layer");
  ASSERT_TRUE(SpinUntil([&] { return window().new_layer_window() != nullptr; }));
  NewLayerWindow& dialog = *window().new_layer_window();
  EXPECT_FALSE(dialog.Create());  // no file chosen
  EXPECT_NE(dialog.message(), "");

  dialog.template_drop_down().set_selected(3);  // Rivers
  EXPECT_EQ(host().NewLayerTemplate(), 3);
  EXPECT_NE(dialog.fields_text(), "No fields");
  const std::string path = scratch_.Path("streams.gpkg");
  host().SetNewLayerPath(path);
  dialog.name_entry().set_text("Streams");
  const int before = host().LayerCount();
  EXPECT_TRUE(dialog.Create()) << dialog.message();
  EXPECT_FALSE(dialog.get_visible());
  EXPECT_TRUE(fs::exists(path));
  ASSERT_TRUE(SpinUntil([&] { return host().LayerCount() == before + 1; }));
}

TEST_F(FeatureWindowsTest, OptionsWindowEditsAPageAndDiscardsOnClose) {
  window().Execute("map.options");
  ASSERT_TRUE(SpinUntil([&] { return window().options_window(fv::desk::kOptionsMap) != nullptr; }));
  OptionsWindow& options = *window().options_window(fv::desk::kOptionsMap);
  EXPECT_EQ(options.get_title(), "Map Options");
  ASSERT_GT(host().OptionsPageCount(fv::desk::kOptionsMap), 0);
  EXPECT_FALSE(options.apply_button().get_sensitive());

  // The first check box on any page: toggling it makes the model dirty.
  std::string key;
  for (int p = 0; key.empty() && p < host().OptionsPageCount(fv::desk::kOptionsMap); ++p) {
    for (int i = 0; i < host().OptionsFieldCount(fv::desk::kOptionsMap, p); ++i) {
      const fv::desk::HostOptionsField f = host().OptionsFieldAt(fv::desk::kOptionsMap, p, i);
      if (f.type != fv::desk::kFieldBool) continue;
      options.SelectPage(p);
      key = f.key;
      break;
    }
  }
  ASSERT_FALSE(key.empty()) << "no check box on the map options";
  auto* check = dynamic_cast<Gtk::CheckButton*>(options.control(key));
  ASSERT_NE(check, nullptr);
  check->set_active(!check->get_active());
  EXPECT_TRUE(host().OptionsDirty(fv::desk::kOptionsMap));
  EXPECT_TRUE(options.apply_button().get_sensitive());

  options.close();
  EXPECT_FALSE(host().OptionsOpen(fv::desk::kOptionsMap));
}

TEST_F(FeatureWindowsTest, ProfileWindowWithoutARouteShowsItsMessage) {
  ProfileWindow profile(host(), window());
  profile.present();
  Drain();
  EXPECT_EQ(profile.get_title(), "Terrain Profile");
  profile.SetCursor(10.0);
  EXPECT_EQ(profile.readout(), "");
  profile.close();
}

TEST_F(FeatureWindowsTest, EveryToolbarIconHasArt) {
  auto theme = Gtk::IconTheme::get_for_display(window().get_display());
  if (!theme->has_icon("zoom-in-symbolic")) GTEST_SKIP() << "no symbolic icon theme installed";
  for (const auto& [name, icon] : IconNames()) EXPECT_TRUE(theme->has_icon(icon)) << name << " -> " << icon;
}

}  // namespace
}  // namespace peregrine
