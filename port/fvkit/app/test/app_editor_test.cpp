// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// App layer A4: editors and the mode dance (fvkit-app-plan-COMPLETE.md §3d).
//
// The proof the plan asks for is "tests for invariants 3d.1-4; a trivial
// points editor test double", and that is what these are. The doubles share
// ONE event log, because three of the four invariants are statements about
// ORDER -- "ReleaseEditFocus before the switch, EnterEditFocus after" is not
// checkable with a pair of counters, and a test that counted them would have
// passed on a version that bracketed the focus backwards.

#include "fvkit/app/editor.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "fake_shell.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/session.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/settings.h"

namespace {

using fv::Status;
using fv::app::EditorManager;
using fv::app::EditorUiConstraints;
using fv::app::FakeShell;
using fv::app::FileTypeDesc;
using fv::app::FlowResult;
using fv::app::OverlayEditor;
using fv::app::OverlaySession;
using fv::app::OverlayTypeDesc;
using fv::app::OverlayTypeRegistry;
using fv::app::TypeId;

// ---------------------------------------------------------------------------
// Doubles
// ---------------------------------------------------------------------------

// One log for every double in the fixture. Entries are "verb:who", so a mode
// switch reads as a sentence and the assertion is on the whole sentence.
using EventLog = std::vector<std::string>;

// The plan's "trivial points editor": tool state is a count of the points it
// would have collected, and Activate/Deactivate are what bracket its life.
class PointsEditor : public OverlayEditor {
 public:
  PointsEditor(EventLog* log, std::string name) : log_(log), name_(std::move(name)) {}

  Status Activate() override {
    log_->push_back("activate:" + name_);
    ++activations;
    if (refuse_activate) {
      return Status::Error(fv::kInternal, name_ + " refused to activate");
    }
    ++points;  // tool state, to prove the instance is REUSED and not remade
    return Status::Ok();
  }
  Status Deactivate() override {
    log_->push_back("deactivate:" + name_);
    ++deactivations;
    return refuse_deactivate
               ? Status::Error(fv::kInternal, name_ + " refused to deactivate")
               : Status::Ok();
  }
  bool AutoEnterOnCreate() const override { return auto_enter; }
  EditorUiConstraints UiConstraints() const override { return constraints; }

  int activations = 0, deactivations = 0, points = 0;
  bool auto_enter = true;
  bool refuse_activate = false, refuse_deactivate = false;
  EditorUiConstraints constraints;

 private:
  EventLog* log_;
  std::string name_;
};

// An overlay that can be edited. Records the focus bracket into the shared log.
class EditableOverlay : public fv::Overlay, public fv::app::EditTarget {
 public:
  EditableOverlay(EventLog* log, std::string name)
      : Overlay(std::move(name)), log_(log) {}

  fv::app::EditTarget* AsEditTarget() override { return this; }
  void EnterEditFocus() override {
    log_->push_back("enter:" + Name());
    ++entered;
  }
  void ReleaseEditFocus() override {
    log_->push_back("release:" + Name());
    ++released;
  }
  int entered = 0, released = 0;

 private:
  EventLog* log_;
};

// The same, plus a document, so a file type's create-and-enter path is real.
class EditableDoc : public EditableOverlay, public fv::app::Persistence {
 public:
  EditableDoc(EventLog* log, std::string name)
      : EditableOverlay(log, std::move(name)) {}

  fv::app::Persistence* AsPersistence() override { return this; }
  Status FileNew() override {
    ++new_calls;
    return new_fails ? Status::Error(fv::kIoError, "FileNew refused")
                     : Status::Ok();
  }
  Status FileOpen(const std::string&) override { return Status::Ok(); }
  Status FileSaveAs(const std::string&, int) override { return Status::Ok(); }

  int new_calls = 0;
  bool new_fails = false;
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

constexpr const char kRouteType[] = "test.route";    // file type, has an editor
constexpr const char kShapeType[] = "test.shape";    // file type, has an editor
constexpr const char kMarkType[] = "test.mark";      // static type, has an editor
constexpr const char kPlainType[] = "test.plain";    // file type, NO editor
// The two the ORDER tests need: everything above is display order 100, which
// cannot tell "on top" from "in its band" apart.
constexpr const char kUnderType[] = "test.under";    // order 50, below the rest
constexpr const char kCrownType[] = "test.crown";    // top-most: the crosshair

class EditorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    RegisterFileType(kRouteType, "rte", &route_editor_);
    RegisterFileType(kShapeType, "shp", &shape_editor_);

    OverlayTypeDesc mark;
    mark.id = kMarkType;
    mark.display_name = "Test Mark";
    mark.default_display_order = 100;
    mark.factory = [this] {
      return std::make_shared<EditableOverlay>(&log_, "Mark");
    };
    mark.editor_factory = [this] {
      auto e = std::unique_ptr<PointsEditor>(new PointsEditor(&log_, "mark-ed"));
      mark_editor_ = e.get();
      return std::unique_ptr<OverlayEditor>(std::move(e));
    };
    ASSERT_TRUE(registry_.Register(mark).ok());

    OverlayTypeDesc plain;
    plain.id = kPlainType;
    plain.display_name = "Test Plain";
    plain.default_display_order = 100;
    FileTypeDesc plain_file;
    plain_file.default_extension = "pln";
    plain_file.save_filters = {{"Plain (*.pln)", "*.pln"}};
    plain.file = plain_file;
    plain.factory = [this] {
      return std::make_shared<EditableDoc>(&log_, "Plain");
    };
    ASSERT_TRUE(registry_.Register(plain).ok());

    OverlayTypeDesc under;
    under.id = kUnderType;
    under.display_name = "Test Under";
    under.default_display_order = 50;
    under.factory = [this] {
      return std::make_shared<EditableOverlay>(&log_, "Under");
    };
    ASSERT_TRUE(registry_.Register(under).ok());

    // No editor and top-most: the crosshair, which is drawn over everything
    // however the rest of the stack is arranged and is never edited.
    OverlayTypeDesc crown;
    crown.id = kCrownType;
    crown.display_name = "Test Crown";
    crown.default_display_order = 100;
    crown.is_top_most = true;
    crown.factory = [this] {
      return std::make_shared<EditableOverlay>(&log_, "Crown");
    };
    ASSERT_TRUE(registry_.Register(crown).ok());

    manager_.SetTypeRegistry(&registry_);
    editors_.SetSession(&session_);
    session_.SetEditorManager(&editors_);
  }

  void RegisterFileType(const char* id, const char* ext,
                        PointsEditor** editor_out) {
    OverlayTypeDesc desc;
    desc.id = id;
    desc.display_name = id;
    desc.default_display_order = 100;
    FileTypeDesc file;
    file.default_extension = ext;
    file.open_filters = {{id, std::string("*.") + ext}};
    file.save_filters = {{id, std::string("*.") + ext}};
    desc.file = file;
    const std::string name = id;
    desc.factory = [this, name] {
      return std::make_shared<EditableDoc>(&log_, name + "-doc");
    };
    desc.editor_factory = [this, name, editor_out] {
      auto e = std::unique_ptr<PointsEditor>(
          new PointsEditor(&log_, name + "-ed"));
      *editor_out = e.get();
      return std::unique_ptr<OverlayEditor>(std::move(e));
    };
    ASSERT_TRUE(registry_.Register(desc).ok());
  }

  // An overlay of `type`, added straight to the stack -- no flow, no editor
  // involvement, so a test can arrange a stack before the mode dance starts.
  std::shared_ptr<fv::Overlay> Place(const char* type, const std::string& name) {
    auto o = std::make_shared<EditableDoc>(&log_, name);
    o->set_type_id(type);
    EXPECT_TRUE(manager_.Add(o).ok());
    return o;
  }

  EditableOverlay* AsEditable(const std::shared_ptr<fv::Overlay>& o) {
    return static_cast<EditableOverlay*>(o.get());
  }

  EventLog log_;
  OverlayTypeRegistry registry_;
  fv::OverlayManager manager_;
  FakeShell shell_;
  fv::Settings settings_;
  OverlaySession session_{registry_, manager_, shell_, settings_};
  EditorManager editors_{registry_, manager_, shell_};
  PointsEditor* route_editor_ = nullptr;
  PointsEditor* shape_editor_ = nullptr;
  PointsEditor* mark_editor_ = nullptr;
};

// ---------------------------------------------------------------------------
// Invariant 3d.1 — the mode makes the current overlay match
// ---------------------------------------------------------------------------

TEST_F(EditorTest, EnteringAModeAdoptsTheTopmostOverlayOfThatType) {
  std::shared_ptr<fv::Overlay> lower = Place(kRouteType, "lower");
  std::shared_ptr<fv::Overlay> upper = Place(kRouteType, "upper");

  EXPECT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  EXPECT_EQ(editors_.CurrentMode(), kRouteType);
  EXPECT_EQ(editors_.edited(), upper.get());
  EXPECT_EQ(manager_.current(), upper.get());
  EXPECT_EQ(AsEditable(upper)->entered, 1);
  EXPECT_EQ(AsEditable(lower)->entered, 0);
  ASSERT_NE(route_editor_, nullptr);
  EXPECT_EQ(route_editor_->activations, 1);
}

// The mode has nothing to edit, so it CREATES one -- through the session's own
// FileNew flow, not by reaching past it into the stack.
TEST_F(EditorTest, EnteringAModeWithNothingOpenCreatesADocument) {
  EXPECT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  ASSERT_EQ(manager_.Overlays().size(), 1u);
  fv::Overlay* made = manager_.Overlays()[0].get();
  EXPECT_EQ(made->type_id(), kRouteType);
  EXPECT_EQ(static_cast<EditableDoc*>(made)->new_calls, 1);
  EXPECT_EQ(editors_.edited(), made);
  EXPECT_EQ(manager_.current(), made);
  // Created once, entered once: the auto-enter inside the creation flow must
  // not fire a SECOND adoption on top of the one that asked for it.
  EXPECT_EQ(route_editor_->activations, 1);
  EXPECT_EQ(static_cast<EditableOverlay*>(made)->entered, 1);
}

// A static type's creation is the toggle, not FileNew.
TEST_F(EditorTest, AStaticTypesModeTogglesItOn) {
  EXPECT_EQ(editors_.SetMode(kMarkType), FlowResult::kDone);
  ASSERT_EQ(manager_.Overlays().size(), 1u);
  EXPECT_EQ(manager_.Overlays()[0]->type_id(), kMarkType);
  EXPECT_EQ(editors_.edited(), manager_.Overlays()[0].get());
  EXPECT_EQ(manager_.current(), manager_.Overlays()[0].get());
}

// The other half of invariant 1: an editor that does not auto-enter simply
// waits. The mode is on, the palette is up, and nothing has been created.
TEST_F(EditorTest, AnEditorThatDoesNotAutoEnterWaitsWithNothingToEdit) {
  // Force the instance into existence so the flag can be set before entry.
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  ASSERT_EQ(editors_.SetMode(TypeId()), FlowResult::kDone);
  ASSERT_EQ(session_.CloseAll(), FlowResult::kDone);
  route_editor_->auto_enter = false;

  EXPECT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  EXPECT_EQ(editors_.CurrentMode(), kRouteType);
  EXPECT_EQ(editors_.edited(), nullptr);
  EXPECT_TRUE(manager_.Overlays().empty());
}

// Invariant 1's fallback. A creation that does not complete leaves the mode at
// none rather than active over nothing -- reached here through a FileNew that
// refuses, which is the same branch a user's cancel takes.
TEST_F(EditorTest, AModeWhoseDocumentCannotBeCreatedFallsBackToNone) {
  OverlayTypeDesc bad;
  bad.id = "test.bad";
  bad.display_name = "Bad";
  FileTypeDesc file;
  file.default_extension = "bad";
  bad.file = file;
  bad.factory = [this] {
    auto o = std::make_shared<EditableDoc>(&log_, "bad");
    o->new_fails = true;
    return o;
  };
  PointsEditor* bad_editor = nullptr;
  bad.editor_factory = [this, &bad_editor] {
    auto e = std::unique_ptr<PointsEditor>(new PointsEditor(&log_, "bad-ed"));
    bad_editor = e.get();
    return std::unique_ptr<OverlayEditor>(std::move(e));
  };
  ASSERT_TRUE(registry_.Register(bad).ok());

  EXPECT_EQ(editors_.SetMode("test.bad"), FlowResult::kFailed);
  EXPECT_TRUE(editors_.CurrentMode().empty());
  EXPECT_EQ(editors_.CurrentEditor(), nullptr);
  EXPECT_TRUE(manager_.Overlays().empty());
  // Activated, then deactivated again on the way back out.
  ASSERT_NE(bad_editor, nullptr);
  EXPECT_EQ(bad_editor->activations, 1);
  EXPECT_EQ(bad_editor->deactivations, 1);
  // And the shell was told the mode is off, not left showing a palette.
  EXPECT_EQ(shell_.editor_changes.back().first, TypeId());
}

TEST_F(EditorTest, ATypeWithNoEditorCannotBeEntered) {
  EXPECT_EQ(editors_.SetMode(kPlainType), FlowResult::kFailed);
  EXPECT_EQ(editors_.last_error().code, fv::kUnsupported);
  EXPECT_TRUE(editors_.CurrentMode().empty());
}

TEST_F(EditorTest, AnUnregisteredTypeCannotBeEntered) {
  EXPECT_EQ(editors_.SetMode("test.nope"), FlowResult::kFailed);
  EXPECT_EQ(editors_.last_error().code, fv::kNotFound);
}

TEST_F(EditorTest, AnEditorThatRefusesToActivateLeavesNoMode) {
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  ASSERT_EQ(editors_.SetMode(TypeId()), FlowResult::kDone);
  route_editor_->refuse_activate = true;

  const size_t changes = shell_.editor_changes.size();
  EXPECT_EQ(editors_.SetMode(kRouteType), FlowResult::kFailed);
  EXPECT_TRUE(editors_.CurrentMode().empty());
  EXPECT_EQ(editors_.CurrentEditor(), nullptr);
  // And no notification at all: there was no mode to leave, so nothing
  // transitioned. A shell told "edit mode left" here would tear down a palette
  // it never put up.
  EXPECT_EQ(shell_.editor_changes.size(), changes);
}

TEST_F(EditorTest, ReenteringTheSameModeIsANoOp) {
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  const size_t events = log_.size();
  EXPECT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  EXPECT_EQ(route_editor_->activations, 1);
  EXPECT_EQ(log_.size(), events);
}

// One editor instance per TYPE, kept across entries: tool state survives
// leaving the mode, which is why the factory is not called twice.
TEST_F(EditorTest, TheEditorInstanceIsPerTypeAndIsReused) {
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  OverlayEditor* first = editors_.CurrentEditor();
  ASSERT_EQ(editors_.SetMode(TypeId()), FlowResult::kDone);
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);

  EXPECT_EQ(editors_.CurrentEditor(), first);
  EXPECT_EQ(route_editor_->activations, 2);
  EXPECT_EQ(route_editor_->points, 2);  // the same instance kept counting
}

TEST_F(EditorTest, ToggleEditorTurnsAModeOnAndBackOff) {
  EXPECT_EQ(editors_.ToggleEditor(kRouteType), FlowResult::kDone);
  EXPECT_EQ(editors_.CurrentMode(), kRouteType);
  EXPECT_EQ(editors_.ToggleEditor(kRouteType), FlowResult::kDone);
  EXPECT_TRUE(editors_.CurrentMode().empty());
  EXPECT_EQ(route_editor_->deactivations, 1);
}

TEST_F(EditorTest, ConstraintsComeFromTheActiveEditorAndVanishWithIt) {
  EXPECT_FALSE(editors_.ActiveConstraints().requires_north_up);
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  route_editor_->constraints.requires_north_up = true;
  route_editor_->constraints.disable_overlay_reorder = true;

  EXPECT_TRUE(editors_.ActiveConstraints().requires_north_up);
  EXPECT_TRUE(editors_.ActiveConstraints().disable_overlay_reorder);
  EXPECT_FALSE(editors_.ActiveConstraints().disable_rotation);

  ASSERT_EQ(editors_.SetMode(TypeId()), FlowResult::kDone);
  EXPECT_FALSE(editors_.ActiveConstraints().requires_north_up);
}

// ---------------------------------------------------------------------------
// Invariant 3d.2 — the current overlay makes the mode match
// ---------------------------------------------------------------------------

TEST_F(EditorTest, MakingAnOverlayOfAnotherTypeCurrentSwitchesTheEditor) {
  std::shared_ptr<fv::Overlay> route = Place(kRouteType, "route");
  std::shared_ptr<fv::Overlay> shape = Place(kShapeType, "shape");
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  ASSERT_EQ(editors_.edited(), route.get());

  ASSERT_TRUE(manager_.MakeCurrent(shape).ok());
  EXPECT_EQ(editors_.CurrentMode(), kShapeType);
  EXPECT_EQ(editors_.edited(), shape.get());
  EXPECT_EQ(route_editor_->deactivations, 1);
  EXPECT_EQ(shape_editor_->activations, 1);
}

// The overlay the user named is adopted, NOT the topmost of its type: the
// click was on this row.
TEST_F(EditorTest, TheSwitchAdoptsTheOverlayThatBecameCurrentNotTheTopmost) {
  std::shared_ptr<fv::Overlay> route = Place(kRouteType, "route");
  std::shared_ptr<fv::Overlay> low = Place(kShapeType, "shape-low");
  std::shared_ptr<fv::Overlay> high = Place(kShapeType, "shape-high");
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);

  ASSERT_TRUE(manager_.MakeCurrent(low).ok());
  EXPECT_EQ(editors_.CurrentMode(), kShapeType);
  EXPECT_EQ(editors_.edited(), low.get());
  EXPECT_EQ(AsEditable(high)->entered, 0);
}

TEST_F(EditorTest, MakingAnOverlayWithNoEditorCurrentLeavesEditMode) {
  std::shared_ptr<fv::Overlay> route = Place(kRouteType, "route");
  std::shared_ptr<fv::Overlay> plain = Place(kPlainType, "plain");
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);

  ASSERT_TRUE(manager_.MakeCurrent(plain).ok());
  EXPECT_TRUE(editors_.CurrentMode().empty());
  EXPECT_EQ(editors_.CurrentEditor(), nullptr);
  EXPECT_EQ(editors_.edited(), nullptr);
  EXPECT_EQ(AsEditable(route)->released, 1);
  EXPECT_EQ(route_editor_->deactivations, 1);
}

TEST_F(EditorTest, ClearingTheCurrentOverlayLeavesEditMode) {
  Place(kRouteType, "route");
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);

  ASSERT_TRUE(manager_.MakeCurrent(nullptr).ok());
  EXPECT_TRUE(editors_.CurrentMode().empty());
  EXPECT_EQ(editors_.edited(), nullptr);
}

// Same type, different document: the focus bracket moves and the editor is
// left alone. Re-activating it here would throw away the tool state on every
// click in an overlay list.
TEST_F(EditorTest, MakingAnotherOverlayOfTheSameTypeCurrentOnlyMovesFocus) {
  std::shared_ptr<fv::Overlay> lower = Place(kRouteType, "lower");
  std::shared_ptr<fv::Overlay> upper = Place(kRouteType, "upper");
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  ASSERT_EQ(editors_.edited(), upper.get());
  log_.clear();

  ASSERT_TRUE(manager_.MakeCurrent(lower).ok());
  EXPECT_EQ(editors_.CurrentMode(), kRouteType);
  EXPECT_EQ(editors_.edited(), lower.get());
  EXPECT_EQ(route_editor_->activations, 1);
  EXPECT_EQ(route_editor_->deactivations, 0);
  EXPECT_EQ(log_, (EventLog{"release:upper", "enter:lower"}));
}

// Nothing chases anything when no editor is on -- current is free to move.
TEST_F(EditorTest, WithNoModeActiveMakingAnOverlayCurrentEntersNoEditor) {
  std::shared_ptr<fv::Overlay> route = Place(kRouteType, "route");
  ASSERT_TRUE(manager_.MakeCurrent(route).ok());
  EXPECT_TRUE(editors_.CurrentMode().empty());
  EXPECT_EQ(editors_.edited(), nullptr);
  EXPECT_EQ(AsEditable(route)->entered, 0);
}

// ---------------------------------------------------------------------------
// Invariant 3d.3 — closing the edited overlay
// ---------------------------------------------------------------------------

TEST_F(EditorTest, ClosingTheEditedOverlayFallsToTheNextOfThatType) {
  std::shared_ptr<fv::Overlay> lower = Place(kRouteType, "lower");
  // A shape sits ABOVE both routes, so A2's "topmost remaining" rule and A4's
  // "next of that type" rule give different answers -- which is the whole
  // point of the refinement.
  std::shared_ptr<fv::Overlay> upper = Place(kRouteType, "upper");
  std::shared_ptr<fv::Overlay> shape = Place(kShapeType, "shape");
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  ASSERT_EQ(editors_.edited(), upper.get());

  EXPECT_EQ(session_.Close(*upper), FlowResult::kDone);
  EXPECT_EQ(editors_.CurrentMode(), kRouteType);
  EXPECT_EQ(editors_.edited(), lower.get());
  EXPECT_EQ(manager_.current(), lower.get());
  EXPECT_EQ(AsEditable(lower)->entered, 1);
}

TEST_F(EditorTest, ClosingTheLastOverlayOfTheEditedTypeLeavesTheMode) {
  std::shared_ptr<fv::Overlay> route = Place(kRouteType, "route");
  Place(kShapeType, "shape");
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);

  EXPECT_EQ(session_.Close(*route), FlowResult::kDone);
  EXPECT_TRUE(editors_.CurrentMode().empty());
  EXPECT_EQ(editors_.CurrentEditor(), nullptr);
  EXPECT_EQ(editors_.edited(), nullptr);
  EXPECT_EQ(route_editor_->deactivations, 1);
}

// The A3 flow used to release focus itself. With an EditorManager wired it
// must not, or the overlay hears it twice.
TEST_F(EditorTest, TheEditedOverlayIsReleasedExactlyOnceOnClose) {
  std::shared_ptr<fv::Overlay> route = Place(kRouteType, "route");
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);

  EXPECT_EQ(session_.Close(*route), FlowResult::kDone);
  EXPECT_EQ(AsEditable(route)->released, 1);
}

// The removal path is the stack's, not the session's, so a raw Remove obeys
// the same invariant.
TEST_F(EditorTest, RemovingTheEditedOverlayDirectlyObeysTheSameRule) {
  std::shared_ptr<fv::Overlay> lower = Place(kRouteType, "lower");
  std::shared_ptr<fv::Overlay> upper = Place(kRouteType, "upper");
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);

  ASSERT_TRUE(manager_.Remove(upper).ok());
  EXPECT_EQ(editors_.edited(), lower.get());
  EXPECT_EQ(manager_.current(), lower.get());
}

// Closing something else must not disturb the mode or the focus.
TEST_F(EditorTest, ClosingAnUnrelatedOverlayLeavesTheEditedOneAlone) {
  std::shared_ptr<fv::Overlay> route = Place(kRouteType, "route");
  std::shared_ptr<fv::Overlay> plain = Place(kPlainType, "plain");
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  ASSERT_EQ(editors_.edited(), route.get());

  EXPECT_EQ(session_.Close(*plain), FlowResult::kDone);
  EXPECT_EQ(editors_.CurrentMode(), kRouteType);
  EXPECT_EQ(editors_.edited(), route.get());
  EXPECT_EQ(AsEditable(route)->released, 0);
}

// ---------------------------------------------------------------------------
// Invariant 3d.4 — the bracket, as an ORDER and not a pair of counters
// ---------------------------------------------------------------------------

TEST_F(EditorTest, AModeSwitchBracketsFocusAroundTheEditorSwap) {
  std::shared_ptr<fv::Overlay> route = Place(kRouteType, "route");
  std::shared_ptr<fv::Overlay> shape = Place(kShapeType, "shape");
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  log_.clear();

  ASSERT_EQ(editors_.SetMode(kShapeType), FlowResult::kDone);
  // Release BEFORE the switch, enter AFTER it -- and the old editor is torn
  // down before the new one is built, so the two are never both live.
  EXPECT_EQ(log_, (EventLog{"release:route", "deactivate:test.route-ed",
                            "activate:test.shape-ed", "enter:shape"}));
}

TEST_F(EditorTest, LeavingEditModeReleasesFocusBeforeDeactivating) {
  Place(kRouteType, "route");
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  log_.clear();

  ASSERT_EQ(editors_.SetMode(TypeId()), FlowResult::kDone);
  EXPECT_EQ(log_, (EventLog{"release:route", "deactivate:test.route-ed"}));
}

// An editor that refuses to deactivate is reported and ignored: it does not
// get to keep the user in a tool state with no way out.
TEST_F(EditorTest, AnEditorCannotRefuseToBeLeft) {
  Place(kRouteType, "route");
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  route_editor_->refuse_deactivate = true;

  EXPECT_EQ(editors_.SetMode(TypeId()), FlowResult::kDone);
  EXPECT_TRUE(editors_.CurrentMode().empty());
  EXPECT_FALSE(shell_.errors.empty());
}

// ---------------------------------------------------------------------------
// The shell seam and the session wiring
// ---------------------------------------------------------------------------

TEST_F(EditorTest, TheShellIsToldOncePerSettledMode) {
  Place(kRouteType, "route");
  Place(kShapeType, "shape");
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  ASSERT_EQ(shell_.editor_changes.size(), 1u);
  EXPECT_EQ(shell_.editor_changes[0].first, kRouteType);
  EXPECT_EQ(shell_.editor_changes[0].second, editors_.CurrentEditor());

  ASSERT_EQ(editors_.SetMode(kShapeType), FlowResult::kDone);
  ASSERT_EQ(shell_.editor_changes.size(), 2u);
  EXPECT_EQ(shell_.editor_changes[1].first, kShapeType);

  ASSERT_EQ(editors_.SetMode(TypeId()), FlowResult::kDone);
  ASSERT_EQ(shell_.editor_changes.size(), 3u);
  EXPECT_EQ(shell_.editor_changes[2].first, TypeId());
  EXPECT_EQ(shell_.editor_changes[2].second, nullptr);
}

// ~ m_bAutoEnterOverlayEditor: a NEW document of an editable type enters its
// editor, from a cold start with no mode active.
TEST_F(EditorTest, CreatingADocumentEntersItsEditor) {
  EXPECT_EQ(session_.NewFileOverlay(kRouteType), FlowResult::kDone);
  EXPECT_EQ(editors_.CurrentMode(), kRouteType);
  ASSERT_EQ(manager_.Overlays().size(), 1u);
  EXPECT_EQ(editors_.edited(), manager_.Overlays()[0].get());
  EXPECT_EQ(route_editor_->activations, 1);
}

TEST_F(EditorTest, CreatingADocumentOfATypeWithNoEditorEntersNothing) {
  EXPECT_EQ(session_.NewFileOverlay(kPlainType), FlowResult::kDone);
  EXPECT_TRUE(editors_.CurrentMode().empty());
  EXPECT_EQ(editors_.edited(), nullptr);
}

// Opening is not creating. FalconView draws that line and so does this.
TEST_F(EditorTest, OpeningADocumentDoesNotEnterAnEditor) {
  EXPECT_EQ(session_.OpenFile(kRouteType, "kiawah.rte"), FlowResult::kDone);
  EXPECT_TRUE(editors_.CurrentMode().empty());
  EXPECT_EQ(editors_.edited(), nullptr);
}

// With no session wired, entering a mode with nothing open is the waiting
// state rather than a failure -- an EditorManager without a flow layer is a
// legitimate configuration (a viewer that never creates documents).
TEST_F(EditorTest, WithNoSessionAModeWithNothingToEditSimplyWaits) {
  editors_.SetSession(nullptr);
  EXPECT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  EXPECT_EQ(editors_.CurrentMode(), kRouteType);
  EXPECT_EQ(editors_.edited(), nullptr);
  EXPECT_TRUE(manager_.Overlays().empty());
}

// ---------------------------------------------------------------------------
// Reentrancy (plan §6)
// ---------------------------------------------------------------------------

// A shell that answers OnEditorChanged by changing the mode again is the one
// reentrant path the app layer really has. It gets a loud kFailed, not a
// half-switched mode.
TEST_F(EditorTest, ASetModeFromInsideOnEditorChangedFailsLoudly) {
  class ReentrantShell : public FakeShell {
   public:
    EditorManager* editors = nullptr;
    FlowResult nested = FlowResult::kDone;
    int nested_calls = 0;
    void OnEditorChanged(const TypeId& id, OverlayEditor* e) override {
      FakeShell::OnEditorChanged(id, e);
      if (editors == nullptr || id.empty()) return;
      ++nested_calls;
      nested = editors->SetMode(TypeId());
    }
  };

  ReentrantShell shell;
  fv::OverlayManager manager;
  manager.SetTypeRegistry(&registry_);
  EditorManager editors(registry_, manager, shell);
  shell.editors = &editors;

  auto route = std::make_shared<EditableDoc>(&log_, "route");
  route->set_type_id(kRouteType);
  ASSERT_TRUE(manager.Add(route).ok());

  EXPECT_EQ(editors.SetMode(kRouteType), FlowResult::kDone);
  EXPECT_EQ(shell.nested_calls, 1);
  EXPECT_EQ(shell.nested, FlowResult::kFailed);
  EXPECT_EQ(editors.CurrentMode(), kRouteType);  // still settled, not half-left
}

// ---------------------------------------------------------------------------
// Invariant 5 — the thing being edited is on top
// ---------------------------------------------------------------------------

// Names bottom-to-top, which is the stack's own direction.
std::vector<std::string> Order(const fv::OverlayManager& m) {
  std::vector<std::string> out;
  for (const auto& o : m.Overlays()) out.push_back(o->Name());
  return out;
}

TEST_F(EditorTest, TheEditedOverlayGoesAboveEverythingButTheTopMostBand) {
  std::shared_ptr<fv::Overlay> under = Place(kUnderType, "under");
  std::shared_ptr<fv::Overlay> route = Place(kRouteType, "route");
  std::shared_ptr<fv::Overlay> shape = Place(kShapeType, "shape");
  std::shared_ptr<fv::Overlay> crown = Place(kCrownType, "crown");
  ASSERT_EQ(Order(manager_),
            (std::vector<std::string>{"under", "route", "shape", "crown"}));

  // The ROUTE is edited, so it goes over the shape it is registered below --
  // "on top" is a statement about what the user is working on, not about which
  // type outranks which.
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  EXPECT_EQ(Order(manager_),
            (std::vector<std::string>{"under", "shape", "route", "crown"}));

  // And the crosshair stays over the lot, which is what it is flagged for.
  ASSERT_EQ(editors_.SetMode(kShapeType), FlowResult::kDone);
  EXPECT_EQ(Order(manager_),
            (std::vector<std::string>{"under", "route", "shape", "crown"}));
}

TEST_F(EditorTest, LeavingTheModePutsTheStackBackInItsDefaultOrder) {
  std::shared_ptr<fv::Overlay> under = Place(kUnderType, "under");
  std::shared_ptr<fv::Overlay> route = Place(kRouteType, "route");
  std::shared_ptr<fv::Overlay> shape = Place(kShapeType, "shape");

  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  ASSERT_EQ(Order(manager_),
            (std::vector<std::string>{"under", "shape", "route"}));

  ASSERT_EQ(editors_.SetMode(TypeId()), FlowResult::kDone);
  // Back to bands. The route and the shape share a display order, so the sort
  // is stable and leaves the raise's relative order alone -- the thing most
  // recently edited stays the upper of its peers, which is the same answer
  // Add's newest-on-top rule gives.
  EXPECT_EQ(Order(manager_),
            (std::vector<std::string>{"under", "shape", "route"}));
  // The one that must move back is the one that crossed a BAND.
  EXPECT_EQ(manager_.Overlays().front()->Name(), "under");
}

TEST_F(EditorTest, ARaiseNeverLiftsAnOverlayOutOfItsPlaceBelowATopMostOne) {
  std::shared_ptr<fv::Overlay> crown = Place(kCrownType, "crown");
  std::shared_ptr<fv::Overlay> route = Place(kRouteType, "route");
  // Add already put the route UNDER the crown; editing must not change that.
  ASSERT_EQ(Order(manager_), (std::vector<std::string>{"route", "crown"}));
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  EXPECT_EQ(Order(manager_), (std::vector<std::string>{"route", "crown"}));
}

TEST_F(EditorTest, MakingAnotherOverlayCurrentRaisesThatOneInstead) {
  std::shared_ptr<fv::Overlay> lower = Place(kRouteType, "lower");
  std::shared_ptr<fv::Overlay> upper = Place(kRouteType, "upper");
  ASSERT_EQ(editors_.SetMode(kRouteType), FlowResult::kDone);
  ASSERT_EQ(editors_.edited(), upper.get());   // the topmost of its type

  // Invariant 2 moves the edit; invariant 5 moves the stack after it. This is
  // the whole of what a shell does to bring a dimmed overlay forward.
  ASSERT_TRUE(manager_.MakeCurrent(lower).ok());
  EXPECT_EQ(editors_.edited(), lower.get());
  EXPECT_EQ(Order(manager_), (std::vector<std::string>{"upper", "lower"}));
}

}  // namespace
