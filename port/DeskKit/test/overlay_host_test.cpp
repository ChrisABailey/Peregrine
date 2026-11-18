// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The host services (app::OverlayHost) through FakeDesk, for a C++ overlay
// and for a C plugin overlay: a redraw requested from a worker thread, mouse
// capture, the dirty flag and elevation. Then the optional overlay functions
// of C API 2.0 through their app:: capabilities.

#include <gtest/gtest.h>

#include <filesystem>
#include <thread>

#include "fv_desk_fake.h"
#include "fv_desk_library_plugin.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/overlay_host.h"
#include "fvkit/app/properties.h"
#include "fvkit/app/search.h"
#include "fvkit/overlay/manager.h"

namespace {

namespace fs = std::filesystem;

/// A C++ overlay that captures on a left press and counts drags while it holds the mouse.
class NativeOverlay : public fv::Overlay, public fv::app::HostAware {
 public:
  NativeOverlay() : Overlay("Native") {}
  fv::app::HostAware* AsHostAware() override { return this; }
  bool OnMouseDown(const fv::MouseEvent&) override { return host()->CaptureMouse(*this).ok(); }
  bool OnMouseMove(const fv::MouseEvent&) override {
    ++moves;
    return true;
  }
  bool OnMouseUp(const fv::MouseEvent&) override {
    host()->ReleaseMouse(*this);
    return true;
  }
  int moves = 0;
};

class OverlayHostTest : public ::testing::Test {
 protected:
  fv::desk::Desk& desk() { return fake_.desk(); }

  /// Requests a redraw from a worker thread through `host`.
  static void RedrawFromWorker(fv::app::OverlayHost* host) {
    std::thread worker([host] { host->RequestRedraw(); });
    worker.join();
  }

  fv::desk::FakeDesk fake_;
};

TEST_F(OverlayHostTest, NativeOverlayIsBoundWhileInTheStack) {
  auto o = std::make_shared<NativeOverlay>();
  EXPECT_EQ(o->host(), nullptr);
  ASSERT_TRUE(desk().overlays().Add(o).ok());
  ASSERT_NE(o->host(), nullptr);
  EXPECT_EQ(o->host()->DisplayScale(), 1.0);
  ASSERT_TRUE(desk().overlays().Remove(o).ok());
  EXPECT_EQ(o->host(), nullptr);
}

TEST_F(OverlayHostTest, NativeOverlayRedrawsFromAWorkerThread) {
  auto o = std::make_shared<NativeOverlay>();
  ASSERT_TRUE(desk().overlays().Add(o).ok());
  EXPECT_FALSE(desk().FlushOverlayRedraw());

  const int before = fake_.shell().invalidate_calls;
  RedrawFromWorker(o->host());
  RedrawFromWorker(o->host());
  EXPECT_TRUE(desk().FlushOverlayRedraw());
  EXPECT_EQ(fake_.shell().invalidate_calls, before + 1);  // the two requests coalesce
  EXPECT_FALSE(desk().FlushOverlayRedraw());
}

TEST_F(OverlayHostTest, NativeOverlayCapturesTheMouseWithNoEditor) {
  auto o = std::make_shared<NativeOverlay>();
  ASSERT_TRUE(desk().overlays().Add(o).ok());
  // No editor: the Desk passes no pointer events until the overlay captures.
  EXPECT_FALSE(desk().OverlayPointerDrag(10, 10));
  ASSERT_TRUE(o->OnMouseDown(fv::MouseEvent{}));
  EXPECT_EQ(desk().overlays().mouse_capture(), o.get());
  EXPECT_TRUE(desk().OverlayPointerDrag(20, 20));
  EXPECT_EQ(o->moves, 1);
  EXPECT_TRUE(desk().OverlayPointerUp(20, 20));
  EXPECT_EQ(desk().overlays().mouse_capture(), nullptr);
  EXPECT_FALSE(desk().OverlayPointerDrag(30, 30));
}

TEST_F(OverlayHostTest, ReleaseByAnotherOverlayKeepsTheCapture) {
  auto a = std::make_shared<NativeOverlay>();
  auto b = std::make_shared<NativeOverlay>();
  ASSERT_TRUE(desk().overlays().Add(a).ok());
  ASSERT_TRUE(desk().overlays().Add(b).ok());
  ASSERT_TRUE(a->host()->CaptureMouse(*a).ok());
  b->host()->ReleaseMouse(*b);
  EXPECT_EQ(desk().overlays().mouse_capture(), a.get());
}

TEST_F(OverlayHostTest, ElevationIsAbsentWithoutACatalog) {
  auto o = std::make_shared<NativeOverlay>();
  ASSERT_TRUE(desk().overlays().Add(o).ok());
  double metres = -1;
  EXPECT_FALSE(o->host()->ElevationAt(fv::GeoPoint{32.7765, -79.9311}, &metres));
}

TEST_F(OverlayHostTest, PushedUndoReachesTheDeskStack) {
  auto o = std::make_shared<NativeOverlay>();
  ASSERT_TRUE(desk().overlays().Add(o).ok());
  int undone = 0;
  o->host()->PushUndo(fv::app::UndoEntry{"Nudge", o.get(),
                                         [&undone] { ++undone; return fv::Status::Ok(); },
                                         [] { return fv::Status::Ok(); }});
  EXPECT_EQ(desk().undo().UndoLabel(), "Nudge");
  ASSERT_TRUE(desk().undo().Undo().ok());
  EXPECT_EQ(undone, 1);
}

// MARK: C plugin

class LibraryHostTest : public OverlayHostTest {
 protected:
  void SetUp() override {
    const fv::Status s = fv::desk::LoadPluginLibrary(FV_HOST_PLUGIN, fs::path(FV_HOST_PLUGIN).parent_path().string(),
                                                     "1.0", &lib_);
    ASSERT_TRUE(s.ok()) << s.message;
    const fv_overlay_class* cls = lib_->FindOverlay("host");
    ASSERT_NE(cls, nullptr);
    overlay_ = fv::desk::CreateLibraryOverlay(*lib_, *cls, "com.example.host.o", "Host");
    ASSERT_NE(overlay_, nullptr);
    ASSERT_NE(overlay_->AsHostAware(), nullptr);
    ASSERT_TRUE(desk().overlays().Add(overlay_).ok());
  }

  fv::Status SetInt(const std::string& key, long long v) {
    fv::app::PropertyValue value;
    value.type = fv::app::PropertyType::kInt;
    value.i = v;
    return overlay_->AsProperties()->SetProperty(key, value);
  }

  long long GetInt(const std::string& key) {
    fv::app::PropertyValue value;
    EXPECT_TRUE(overlay_->AsProperties()->GetProperty(key, &value).ok());
    return value.i;
  }

  static fv::MouseEvent Button(int button) {
    fv::MouseEvent e;
    e.button = button;
    return e;
  }

  const fv::desk::PluginLibrary* lib_ = nullptr;
  std::shared_ptr<fv::Overlay> overlay_;
};

TEST_F(LibraryHostTest, HostAnswersApi20) {
  EXPECT_EQ(lib_->host.api_major, 2u);
  EXPECT_EQ(lib_->host.api_minor, 0u);
  EXPECT_NE(lib_->host.edited, nullptr);
}

TEST_F(LibraryHostTest, RedrawsFromAWorkerThread) {
  const int before = fake_.shell().invalidate_calls;
  ASSERT_TRUE(SetInt("worker_redraws", 3).ok());
  EXPECT_TRUE(desk().FlushOverlayRedraw());
  EXPECT_EQ(fake_.shell().invalidate_calls, before + 1);
  EXPECT_FALSE(desk().FlushOverlayRedraw());
}

TEST_F(LibraryHostTest, ARedrawAfterLeavingTheStackIsDropped) {
  ASSERT_TRUE(desk().overlays().Remove(overlay_).ok());
  ASSERT_TRUE(SetInt("worker_redraws", 1).ok());
  EXPECT_FALSE(desk().FlushOverlayRedraw());
}

TEST_F(LibraryHostTest, CapturesAndReleasesTheMouse) {
  ASSERT_TRUE(overlay_->OnMouseDown(Button(0)));
  EXPECT_EQ(desk().overlays().mouse_capture(), overlay_.get());
  EXPECT_TRUE(desk().OverlayPointerDrag(20, 20));  // routed to the capture, no editor
  EXPECT_TRUE(desk().OverlayPointerUp(20, 20));
  EXPECT_EQ(desk().overlays().mouse_capture(), nullptr);
}

TEST_F(LibraryHostTest, SetsAndClearsTheDirtyFlag) {
  fv::app::Persistence* p = overlay_->AsPersistence();
  ASSERT_NE(p, nullptr);
  EXPECT_FALSE(p->is_dirty());
  EXPECT_FALSE(overlay_->OnMouseDown(Button(1)));  // unhandled; the plugin's set_dirty decides
  EXPECT_TRUE(p->is_dirty());
  EXPECT_FALSE(overlay_->OnMouseDown(Button(2)));
  EXPECT_FALSE(p->is_dirty());
}

TEST_F(LibraryHostTest, ElevationIsOutOfCoverageWithoutACatalog) {
  overlay_->OnMouseMove(fv::MouseEvent{});
  EXPECT_EQ(GetInt("elevation_rc"), FV_ERR_OUT_OF_COVERAGE);
}

TEST_F(LibraryHostTest, APropertyWriteLeavesTheDocumentClean) {
  ASSERT_TRUE(SetInt("worker_redraws", 0).ok());
  EXPECT_FALSE(overlay_->AsPersistence()->is_dirty());  // only set_dirty marks it
}

// MARK: C plugin capabilities (API 2.0)

class LibraryCapabilityTest : public LibraryHostTest {
 protected:
  void SetUp() override {
    LibraryHostTest::SetUp();
    if (HasFatalFailure()) return;
    ASSERT_TRUE(proj_.SetSurfaceSize(512, 512).ok());
    ASSERT_TRUE(proj_.SetCenter(fv::GeoPoint{32.7765, -79.9311}).ok());
    ASSERT_TRUE(proj_.SetScale(100000).ok());
  }

  fv::MapProjection proj_;
};

TEST_F(LibraryCapabilityTest, KeysReachTheOverlay) {
  fv::KeyEvent esc;
  esc.key = fv::Key::kEscape;
  EXPECT_TRUE(overlay_->OnKeyDown(esc));
  fv::KeyEvent a;
  a.key = 'A';
  a.text = 'a';
  EXPECT_FALSE(overlay_->OnKeyDown(a));
  EXPECT_EQ(GetInt("keys"), 1);
}

TEST_F(LibraryCapabilityTest, EditHintFollowsThePoint) {
  fv::app::EditHint* hint = overlay_->AsEditHint();
  ASSERT_NE(hint, nullptr);
  EXPECT_EQ(hint->EditHintAt(fv::PixelPoint{10, 10}), "Add mark");
  EXPECT_EQ(hint->EditHintAt(fv::PixelPoint{200, 10}), "");
}

TEST_F(LibraryCapabilityTest, ContextMenuSectionRunsItsCommand) {
  fv::app::ContextMenu* cm = overlay_->AsContextMenu();
  ASSERT_NE(cm, nullptr);
  fv::app::MenuNode section;
  cm->AppendMenuItems(proj_, fv::PixelPoint{50, 50}, section);
  ASSERT_EQ(section.children.size(), 5u);
  EXPECT_EQ(section.children[0].label, "Mark Here");
  EXPECT_EQ(section.children[4].label, "Mark Four Times");
  EXPECT_TRUE(section.children[0].enabled);
  EXPECT_TRUE(section.children[1].is_separator());
  const fv::app::MenuNode& more = section.children[2];
  EXPECT_EQ(more.label, "More");
  ASSERT_EQ(more.children.size(), 1u);
  EXPECT_EQ(more.children[0].label, "Mark Twice");
  EXPECT_EQ(more.children[0].icon, "marks");
  EXPECT_FALSE(more.children[0].enabled);

  ASSERT_TRUE(section.children[0].action);
  section.children[0].action();
  EXPECT_EQ(GetInt("edits"), 1);
  EXPECT_TRUE(overlay_->AsPersistence()->is_dirty());
}

TEST_F(LibraryCapabilityTest, AMenuChosenAfterCloseDoesNothing) {
  fv::app::MenuNode section;
  overlay_->AsContextMenu()->AppendMenuItems(proj_, fv::PixelPoint{50, 50}, section);
  ASSERT_TRUE(desk().overlays().Remove(overlay_).ok());
  overlay_.reset();  // the plugin's destroy has run
  section.children[0].action();
}

TEST_F(LibraryCapabilityTest, ItemInfoFillsTheSheet) {
  fv::app::ItemInfo* info = overlay_->AsItemInfo();
  ASSERT_NE(info, nullptr);
  const std::optional<fv::app::ItemInfoSheet> sheet = info->InfoAt(proj_, fv::PixelPoint{51, 49}, 3);
  ASSERT_TRUE(sheet.has_value());
  EXPECT_EQ(sheet->title, "Host item");
  ASSERT_EQ(sheet->rows.size(), 2u);
  EXPECT_EQ(sheet->rows[1].section, "Item");
  EXPECT_EQ(sheet->rows[1].key, "Id");
  EXPECT_EQ(sheet->rows[1].value, "7");
  EXPECT_EQ(sheet->edit_label, "Edit Host\xE2\x80\xA6");
  EXPECT_FALSE(info->InfoAt(proj_, fv::PixelPoint{300, 300}, 3).has_value());
}

TEST_F(LibraryCapabilityTest, SnapToOffersItsPoint) {
  fv::app::SnapTo* snap = overlay_->AsSnapTo();
  ASSERT_NE(snap, nullptr);
  std::vector<fv::app::SnapToItem> items;
  snap->SnapToPoint(proj_, fv::PixelPoint{256, 256}, 5, items);
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].description, "Host centre");
  EXPECT_EQ(items[0].overlay, overlay_.get());
  EXPECT_DOUBLE_EQ(items[0].distance_px, 2.0);
  EXPECT_DOUBLE_EQ(items[0].point.lat, 32.7765);
}

TEST_F(LibraryCapabilityTest, SearchMatchesTextAreaAndCap) {
  ASSERT_NE(overlay_->AsSearch(), nullptr);
  const fv::app::SearchSession session(desk().overlays());
  fv::app::SearchQuery q;
  q.text = "host";
  EXPECT_EQ(session.Search(q).size(), 3u);
  q.max_results = 2;
  EXPECT_EQ(session.Search(q).size(), 2u);

  q = fv::app::SearchQuery();
  q.text = "north";
  std::vector<fv::app::SearchResult> north = session.Search(q);
  ASSERT_EQ(north.size(), 1u);
  EXPECT_EQ(north[0].title, "Host North");
  EXPECT_EQ(north[0].detail, "host \xC2\xB7 test");
  EXPECT_EQ(north[0].feature, 2u);
  EXPECT_EQ(north[0].overlay, overlay_.get());

  q = fv::app::SearchQuery();
  q.area = fv::GeoRect{fv::GeoPoint{32, -81}, fv::GeoPoint{34, -79}};
  std::vector<fv::app::SearchResult> in_area = session.Search(q);
  ASSERT_EQ(in_area.size(), 2u);  // the centre and the north item
}

TEST_F(LibraryCapabilityTest, EditFocusAndItsUndoHistory) {
  fv::app::EditTarget* t = overlay_->AsEditTarget();
  ASSERT_NE(t, nullptr);
  t->EnterEditFocus();
  EXPECT_EQ(GetInt("focus"), 1);
  EXPECT_FALSE(t->CanUndo());
  fv::app::MenuNode section;
  overlay_->AsContextMenu()->AppendMenuItems(proj_, fv::PixelPoint{50, 50}, section);
  section.children[0].action();  // Mark Here
  EXPECT_TRUE(t->CanUndo());
  t->Undo();
  EXPECT_EQ(GetInt("edits"), 0);
  EXPECT_FALSE(t->CanUndo());
  EXPECT_TRUE(t->CanRedo());
  t->Redo();
  EXPECT_EQ(GetInt("edits"), 1);
  t->ReleaseEditFocus();
  EXPECT_EQ(GetInt("focus"), 0);
}

TEST_F(LibraryCapabilityTest, SavePassesTheFormatAndRevertClearsDirty) {
  fv::app::Persistence* p = overlay_->AsPersistence();
  ASSERT_TRUE(p->FileSaveAs("/nonexistent/host.dat", 2).ok());
  EXPECT_EQ(GetInt("last_format"), 2);
  overlay_->OnMouseDown(Button(1));
  ASSERT_TRUE(p->is_dirty());
  ASSERT_TRUE(p->SupportsRevert());
  ASSERT_TRUE(p->Revert("/nonexistent/host.dat").ok());
  EXPECT_EQ(GetInt("reverted"), 1);
  EXPECT_FALSE(p->is_dirty());
}

}  // namespace
