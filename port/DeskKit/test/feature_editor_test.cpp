// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// The feature editor through FakeDesk: draw an area with a hole, drag a
/// vertex, and undo both from Edit ▸ Undo.
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "fv_desk_fake.h"
#include "fv_desk_feature_editor.h"
#include "fvkit/raster.h"
#include "fvkit/overlay/feature_overlay.h"

namespace {

using fv::desk::FakeDesk;

class FeatureEditorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fd_.desk().GoTo(fv::GeoPoint{32.80, -79.90}, 50000.0);
    ASSERT_TRUE(fd_.Run({std::string("overlay.new.") + fv::FeatureOverlay::kTypeId}).ok());
    overlay_ = dynamic_cast<fv::FeatureOverlay*>(fd_.desk().overlays().current());
    ASSERT_NE(overlay_, nullptr);
    ASSERT_EQ(fd_.shell().editor_mode, fv::FeatureOverlay::kTypeId);
    Render();
  }

  void Render() {
    fv::PixelBuffer out;
    ASSERT_TRUE(fd_.Render(&out).ok());
  }

  /// Runs the palette tool labelled `label`.
  void Tool(const std::string& label) {
    for (const std::string& id : fd_.desk().commands().IdsWithPrefix("editor.tool.")) {
      if (fd_.desk().commands().Find(id)->label != label) continue;
      ASSERT_TRUE(fd_.desk().commands().IsEnabled(id)) << label;
      ASSERT_TRUE(fd_.Run({id}).ok());
      return;
    }
    FAIL() << "no tool " << label;
  }

  void Click(double x, double y) {
    fd_.desk().OverlayPointerDown(x, y);
    fd_.desk().OverlayPointerUp(x, y);
  }

  void Key(int key) {
    fv::KeyEvent e;
    e.key = key;
    fd_.desk().OverlayKeyDown(e);
  }

  fv::Feature Feature(int64_t id) {
    fv::Feature f;
    EXPECT_TRUE(overlay_->layer()->Get(id, &f).ok());
    return f;
  }

  FakeDesk fd_{400, 400};
  fv::FeatureOverlay* overlay_ = nullptr;
};

TEST_F(FeatureEditorTest, AreaWithHoleThenVertexDragUndoneFromTheEditMenu) {
  auto& commands = fd_.desk().commands();
  EXPECT_FALSE(commands.IsEnabled("edit.undo"));

  Tool("Draw Area");
  Click(100, 100);
  Click(300, 100);
  Click(300, 300);
  Click(100, 300);
  Key(fv::Key::kReturn);
  const int64_t id = overlay_->selected();
  ASSERT_NE(id, 0);
  const fv::Geometry area = Feature(id).geometry;
  ASSERT_EQ(area.paths.size(), 1u);

  Tool("Add Hole");
  Click(150, 150);
  Click(250, 150);
  Click(200, 250);
  Click(151, 151);  // on the first vertex: closes the hole
  const fv::Geometry holed = Feature(id).geometry;
  ASSERT_EQ(holed.paths.size(), 2u);

  Tool("Select");
  Render();
  ASSERT_TRUE(fd_.desk().OverlayPointerDown(300, 300));
  fd_.desk().OverlayPointerDrag(320, 320);
  fd_.desk().OverlayPointerDrag(340, 330);
  fd_.desk().OverlayPointerUp(340, 330);
  const fv::Geometry dragged = Feature(id).geometry;
  EXPECT_NE(dragged, holed);
  EXPECT_EQ(fd_.desk().undo().UndoLabel(), "Move Vertex");
  EXPECT_TRUE(overlay_->is_dirty());

  ASSERT_TRUE(commands.IsEnabled("edit.undo"));
  ASSERT_TRUE(fd_.Run({"edit.undo"}).ok());
  EXPECT_EQ(Feature(id).geometry, holed);
  ASSERT_TRUE(fd_.Run({"edit.undo"}).ok());
  EXPECT_EQ(Feature(id).geometry, area);
  ASSERT_TRUE(fd_.Run({"edit.redo", "edit.redo"}).ok());
  EXPECT_EQ(Feature(id).geometry, dragged);
  EXPECT_TRUE(fd_.shell().errors.empty());
}

TEST_F(FeatureEditorTest, PaletteFollowsTheSelectionAndTheLayer) {
  auto has_enabled = [this](const std::string& label) {
    for (const std::string& id : fd_.desk().commands().IdsWithPrefix("editor.tool."))
      if (fd_.desk().commands().Find(id)->label == label) return fd_.desk().commands().IsEnabled(id);
    return false;
  };
  EXPECT_TRUE(has_enabled("Draw Line"));
  EXPECT_FALSE(has_enabled("Add Hole"));
  EXPECT_FALSE(has_enabled("Delete Feature"));
  Tool("Add Point");
  Click(200, 200);
  EXPECT_TRUE(has_enabled("Delete Feature"));
  EXPECT_FALSE(has_enabled("Add Hole"));  // a point has no area to cut
  Tool("Delete Feature");
  EXPECT_EQ(overlay_->layer()->Count(), 0u);
}

TEST_F(FeatureEditorTest, ShiftDragBoxSelectsAndClosingDropsHistory) {
  Tool("Add Point");
  Click(100, 100);
  Click(150, 120);
  Tool("Select");
  overlay_->SetSelection({});
  Render();
  ASSERT_TRUE(fd_.desk().OverlayPointerDown(80, 80, true));
  fd_.desk().OverlayPointerDrag(120, 110);
  fd_.desk().OverlayPointerDrag(170, 140);
  fd_.desk().OverlayPointerUp(170, 140);
  EXPECT_EQ(overlay_->selection().size(), 2u);

  ASSERT_GT(fd_.desk().undo().undo_count(), 0u);
  fd_.shell().next_save_answer = fv::app::AppShell::SaveAnswer::kDiscard;
  ASSERT_TRUE(fd_.Run({"file.close"}).ok());
  EXPECT_EQ(fd_.desk().undo().undo_count(), 0u);
}

TEST_F(FeatureEditorTest, FormApplyIsUndoable) {
  Tool("Add Point");
  Click(200, 200);
  const int64_t id = overlay_->selected();
  // A schemaless layer has no fields to edit, but Apply still records an entry.
  ASSERT_TRUE(fd_.Run({"overlay.edit_feature"}).ok());
  ASSERT_TRUE(fd_.desk().ApplyFeatureForm().ok());
  EXPECT_EQ(fd_.desk().undo().UndoLabel(), "Edit Attributes");
  EXPECT_NE(id, 0);
}

TEST_F(FeatureEditorTest, LayerDialogsAreOnThePaletteNotTheOverlayMenu) {
  Tool("Attribute Table…");
  EXPECT_EQ(fd_.shell().attribute_table_shown, 1);
  Tool("Layer Style…");
  EXPECT_EQ(fd_.shell().layer_style_shown, 1);
  Tool("Add Point");
  Click(200, 200);
  Tool("Style This Feature…");
  EXPECT_EQ(fd_.shell().layer_style_shown, 2);
  Tool("Edit Feature…");
  EXPECT_EQ(fd_.shell().feature_form_shown, 1);

  const fv::desk::Menu* overlay = fd_.desk().menus().Find("overlay");
  ASSERT_NE(overlay, nullptr);
  for (const fv::desk::MenuItem& item : overlay->items)
    EXPECT_NE(item.command_id.rfind("overlay.attribute_table", 0), 0u) << item.command_id;
}

TEST_F(FeatureEditorTest, DoubleClickEditsAFeatureAndFinishesAShape) {
  auto double_click = [this](double x, double y) {
    Click(x, y);
    fd_.desk().OverlayPointerDown(x, y, false, 2);
    fd_.desk().OverlayPointerUp(x, y);
  };
  Tool("Draw Line");
  Click(100, 100);
  Click(200, 120);
  double_click(300, 100);
  EXPECT_FALSE(overlay_->edit().sketching());
  const int64_t line = overlay_->selected();
  ASSERT_NE(line, 0);
  EXPECT_EQ(Feature(line).geometry.paths[0].size(), 3u);
  EXPECT_EQ(fd_.shell().feature_form_shown, 0);

  Tool("Select");
  overlay_->SetSelection({});
  Render();
  double_click(200, 120);
  EXPECT_EQ(fd_.shell().feature_form_shown, 1);
  EXPECT_EQ(overlay_->selected(), line);
  EXPECT_TRUE(fd_.shell().errors.empty());
}

TEST_F(FeatureEditorTest, DocumentNameNamesTheCurrentFile) {
  EXPECT_EQ(fd_.desk().DocumentName(), std::string("unnamed.") + fv::FeatureOverlay::kExtension);
}

TEST_F(FeatureEditorTest, StatusSaysWhatAClickWouldDoAndWhatIsUnderThePointer) {
  auto& desk = fd_.desk();
  auto feedback = [&] { return desk.CurrentStatus().feedback; };
  Tool("Add Point");
  desk.OverlayHover(50, 50);
  EXPECT_EQ(feedback(), "Add point");
  Click(200, 200);
  const int64_t id = overlay_->selected();
  const std::string title = overlay_->FeatureTitle(Feature(id));
  Tool("Select");
  overlay_->SetSelection({});
  Render();
  desk.OverlayHover(200, 200);
  EXPECT_EQ(feedback(), "Select point \u201C" + title + "\u201D");
  EXPECT_EQ(desk.HoverToolTip(), title);

  // Not editing: the hover names the feature; off it, the base map speaks.
  ASSERT_TRUE(fd_.Run({std::string("editor.mode.") + fv::FeatureOverlay::kTypeId}).ok());
  ASSERT_FALSE(desk.Editing());
  desk.SetMapHint(fv::app::HintText{"Kiawah Island Parkway", "Kiawah Island Parkway (road)"});
  desk.OverlayHover(200, 200);
  EXPECT_EQ(feedback(), overlay_->Name() + ": " + title);
  EXPECT_EQ(desk.HoverToolTip(), title);
  desk.OverlayHover(350, 40);
  EXPECT_EQ(feedback(), "Kiawah Island Parkway (road)");
  EXPECT_EQ(desk.HoverToolTip(), "Kiawah Island Parkway");
  desk.OverlayHoverExit();
  desk.SetMapHint(fv::app::HintText());
  EXPECT_EQ(feedback(), "");
  EXPECT_EQ(desk.HoverToolTip(), "");
}

}  // namespace

