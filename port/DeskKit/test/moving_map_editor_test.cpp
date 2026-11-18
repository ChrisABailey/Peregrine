// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for the moving map editor, Listen, the Feed dialog and how a
/// workspace keeps a moving map.

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <string>

#include "fv_desk_fake.h"
#include "fv_desk_menu_model.h"
#include "fv_desk_moving_map.h"
#include "fv_desk_options.h"
#include "fv_desk_workspace.h"
#include "fvkit/overlay/moving_map_overlay.h"
#include "fvkit/settings.h"
#include "scratch.h"

using fv::GeoPoint;
using fv::app::PropertyValue;
using fv::desk::DeskMovingMap;
using fv::desk::FakeDesk;
using fv::desk::MovingMapFeed;
using fv::desk::test::Scratch;

namespace {

const char kNew[] = "overlay.new.fv.movingmap";

class MovingMapEditorTest : public ::testing::Test {
 protected:
  void SetUp() override { Prepare(fd_); }
  static void Prepare(FakeDesk& fd) {
    fd.settings().Set("movingmap.slew_seconds", "0");
    fd.settings().Set("movingmap.snap_to_road", "false");
    auto& mv = fd.desk().map_view();
    mv.SetViewport(mv.View().WithCamera(GeoPoint{32.8, -79.9}, 50000, 0));
  }
  DeskMovingMap* Top() {
    const fv::desk::MovingMapDriver* d = fd_.desk().ownship_map();
    return d != nullptr ? d->overlay() : nullptr;
  }
  std::shared_ptr<fv::Overlay> TopShared() {
    return fd_.desk().overlays().FirstOfType(fv::MovingMapOverlay::kTypeId);
  }
  void Ticks(int n) {
    for (int i = 0; i < n; ++i) fd_.desk().TickMovingMap(0.1);
  }
  /// Enters the moving map editor, unless New already did, and returns its
  /// tool commands by icon (by label for one with no icon).
  std::map<std::string, std::string> EnterEditor() {
    if (!fd_.desk().Editing())
      EXPECT_TRUE(fd_.Run({std::string("editor.mode.") + fv::MovingMapOverlay::kTypeId}).ok());
    std::map<std::string, std::string> by_icon;
    const fv::desk::Menu* editor = fd_.desk().menus().Find("editor");
    if (editor == nullptr) return by_icon;
    for (const fv::desk::MenuItem& item : editor->items)
      if (!item.is_separator()) by_icon[item.icon.empty() ? item.label : item.icon] = item.command_id;
    return by_icon;
  }
  FakeDesk fd_;
};

TEST_F(MovingMapEditorTest, ToolsFlipTheRightSettingOnTheRightObject) {
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  DeskMovingMap* lower = Top();
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  DeskMovingMap* upper = Top();
  ASSERT_NE(lower, upper);
  std::map<std::string, std::string> tools = EnterEditor();
  for (const char* icon : {"course_up", "continuous", "snap_to_road", "listen", "feed", "ownship"})
    ASSERT_EQ(tools.count(icon), 1u) << icon;
  EXPECT_EQ(tools.count("Auto Centre"), 0u);

  // Window settings: saved, and applied to every map. Not centring on every
  // fix, the map does not follow and course up is greyed.
  EXPECT_FALSE(upper->modes().auto_center);
  EXPECT_FALSE(fd_.desk().commands().IsEnabled(tools["course_up"]));
  ASSERT_TRUE(fd_.desk().Execute(tools["continuous"]).ok());
  EXPECT_TRUE(upper->modes().continuous);
  EXPECT_TRUE(upper->modes().auto_center);
  EXPECT_TRUE(fd_.desk().commands().IsEnabled(tools["course_up"]));
  EXPECT_FALSE(fd_.desk().commands().IsChecked(tools["course_up"]));
  ASSERT_TRUE(fd_.desk().Execute(tools["course_up"]).ok());
  EXPECT_TRUE(fd_.settings().GetBool("movingmap.auto_rotate", false));
  EXPECT_TRUE(fd_.desk().commands().IsChecked(tools["course_up"]));
  EXPECT_TRUE(upper->modes().auto_rotate);
  EXPECT_TRUE(lower->modes().auto_rotate);
  // Centring off again stops the following and the turning both.
  ASSERT_TRUE(fd_.desk().Execute(tools["continuous"]).ok());
  EXPECT_FALSE(upper->modes().auto_center);
  EXPECT_FALSE(upper->modes().auto_rotate);
  EXPECT_FALSE(fd_.desk().commands().IsEnabled(tools["course_up"]));
  EXPECT_FALSE(fd_.desk().commands().IsChecked(tools["course_up"]));

  // Map settings: the edited map alone.
  ASSERT_FALSE(upper->feed_config().snap_to_road);
  ASSERT_TRUE(fd_.desk().Execute(tools["snap_to_road"]).ok());
  EXPECT_TRUE(upper->feed_config().snap_to_road);
  EXPECT_FALSE(lower->feed_config().snap_to_road);
  EXPECT_TRUE(fd_.desk().commands().IsChecked(tools["snap_to_road"]));

  ASSERT_TRUE(fd_.desk().Execute(tools["ownship"]).ok());
  EXPECT_EQ(fd_.shell().ownship_shown, 1);
}

TEST_F(MovingMapEditorTest, ListenOffStopsFixesAndOnResumesThem) {
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  std::map<std::string, std::string> tools = EnterEditor();
  Ticks(3);
  const size_t heard = Top()->recording().size();
  ASSERT_GT(heard, 0u);
  EXPECT_TRUE(fd_.desk().commands().IsChecked(tools["listen"]));

  ASSERT_TRUE(fd_.desk().Execute(tools["listen"]).ok());
  EXPECT_FALSE(Top()->feed_config().listening);
  EXPECT_FALSE(fd_.desk().commands().IsChecked(tools["listen"]));
  EXPECT_EQ(Top()->source(), nullptr);
  Ticks(5);
  EXPECT_EQ(Top()->recording().size(), heard);
  EXPECT_TRUE(fd_.desk().ownship().has_position());  // held at the last fix

  ASSERT_TRUE(fd_.desk().Execute(tools["listen"]).ok());
  EXPECT_TRUE(Top()->feed_config().listening);
  EXPECT_NE(Top()->source(), nullptr);
  fv::PositionFix next;
  next.SetPosition(32.81, -79.89);
  Top()->PushFix(next);
  Ticks(1);
  EXPECT_EQ(Top()->recording().size(), heard + 1);  // the trail continues
}

TEST_F(MovingMapEditorTest, ListenAndFeedAreDisabledForAReplay) {
  Scratch dir;
  const std::string path = dir.Path("ride.gpx");
  std::ofstream(path) << "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" creator=\"t\"><trk><trkseg>"
                         "<trkpt lat=\"32.8\" lon=\"-79.9\"><time>2026-11-09T12:00:00Z</time></trkpt>"
                         "<trkpt lat=\"32.8\" lon=\"-79.89\"><time>2026-11-09T12:00:01Z</time></trkpt>"
                         "</trkseg></trk></gpx>\n";
  ASSERT_EQ(fd_.desk().session().OpenFile(fv::app::TypeId(), path), fv::app::FlowResult::kDone);
  std::map<std::string, std::string> tools = EnterEditor();
  EXPECT_FALSE(fd_.desk().commands().IsEnabled(tools["listen"]));
  // A replay's track is its feed; snapping stays on the toolbar.
  EXPECT_FALSE(fd_.desk().commands().IsEnabled(tools["feed"]));
  EXPECT_TRUE(fd_.desk().commands().IsEnabled(tools["snap_to_road"]));
  EXPECT_FALSE(fd_.desk().OpenMovingMapFeed());
  EXPECT_EQ(fd_.shell().feed_shown, nullptr);
}

TEST_F(MovingMapEditorTest, ApplyOnTheFeedDialogRestartsOnlyThatMap) {
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  DeskMovingMap* lower = Top();
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  Ticks(2);
  const std::shared_ptr<fv::IPositionSource> lower_source = lower->source();
  ASSERT_NE(lower_source, nullptr);

  ASSERT_TRUE(fd_.desk().OpenMovingMapFeed());
  std::shared_ptr<fv::desk::OptionsModel> model = fd_.shell().feed_shown;
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->pages().size(), 1u);
  fv::desk::OptionsPage* page = model->pages()[0].get();
  EXPECT_EQ(page->title(), "Feed for " + Top()->Name());
  // Snapping is the toolbar's Snap to Road, not the dialog's.
  EXPECT_EQ(page->Field("snap_to_road"), nullptr);
  const bool snapping = Top()->feed_config().snap_to_road;
  ASSERT_TRUE(page->Set("feed", PropertyValue::Choice(1)).ok());
  ASSERT_TRUE(page->Set("nmea_host", PropertyValue::String("127.0.0.1")).ok());
  ASSERT_TRUE(page->Set("nmea_port", PropertyValue::Int(1)).ok());
  EXPECT_EQ(Top()->feed_config().kind, MovingMapFeed::kDemo);  // nothing until Apply
  ASSERT_TRUE(fd_.desk().ApplyOverlayOptions(*model).ok());

  EXPECT_EQ(Top()->feed_config().kind, MovingMapFeed::kNmeaTcp);
  EXPECT_EQ(Top()->feed_config().host, "127.0.0.1");
  EXPECT_EQ(Top()->feed_config().snap_to_road, snapping);
  EXPECT_EQ(fd_.desk().ownship_map()->feed(), MovingMapFeed::kNmeaTcp);
  EXPECT_EQ(lower->source(), lower_source);
  EXPECT_EQ(lower->feed_config().kind, MovingMapFeed::kDemo);
  EXPECT_EQ(fd_.settings().GetString("movingmap.feed", ""), "");  // no setting written
}

TEST_F(MovingMapEditorTest, TheOptionsPageIsTheDefaultFeed) {
  std::shared_ptr<fv::desk::OptionsModel> model = fd_.desk().OverlayOptions();
  fv::desk::OptionsPage* page = model->Page(fv::MovingMapOverlay::kTypeId);
  ASSERT_NE(page, nullptr);
  bool found = false;
  for (const fv::desk::OptionsSection& s : page->sections())
    if (s.title == "Default Feed") found = true;
  EXPECT_TRUE(found);
  // The map's own feed is not on the page.
  EXPECT_EQ(page->Field("listening"), nullptr);
}

TEST_F(MovingMapEditorTest, AWorkspaceKeepsALiveMapNeverSaved) {
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  fv::desk::FeedConfig config = Top()->feed_config();
  config.kind = MovingMapFeed::kNmeaUdp;
  config.port = 10111;
  config.listening = false;
  config.snap_to_road = true;
  ASSERT_TRUE(fd_.desk().SetMovingMapFeed(*TopShared(), config));
  const std::string name = Top()->Name();
  const fv::desk::Workspace saved = fd_.desk().CaptureWorkspace();
  EXPECT_EQ(saved.overlays.at("0.file"), "");
  EXPECT_EQ(saved.overlays.at("0.new"), "true");
  EXPECT_EQ(saved.overlays.at("0.prop.feed"), "nmea_udp");

  FakeDesk b;
  Prepare(b);
  ASSERT_EQ(b.desk().ApplyWorkspace(saved), fv::app::FlowResult::kDone);
  EXPECT_TRUE(b.desk().warnings().empty()) << b.desk().warnings()[0];
  ASSERT_EQ(b.desk().moving_map_count(), 1u);
  b.desk().TickMovingMap(0.1);
  const DeskMovingMap* restored = b.desk().ownship_map()->overlay();
  EXPECT_EQ(restored->Name(), name);
  EXPECT_FALSE(restored->replay());
  EXPECT_EQ(restored->feed_config(), config);
  EXPECT_EQ(b.desk().ownship_map()->feed(), MovingMapFeed::kNmeaUdp);
  EXPECT_EQ(b.desk().feed_receivers().open_count(), 0u);  // not listening
  EXPECT_EQ(b.desk().CaptureWorkspace(), saved);
}

TEST_F(MovingMapEditorTest, AWorkspaceReopensAReplayFromItsFile) {
  Scratch dir;
  const std::string path = dir.Path("ride.gpx");
  std::ofstream(path) << "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" creator=\"t\"><trk><trkseg>"
                         "<trkpt lat=\"32.8\" lon=\"-79.9\"><time>2026-11-09T12:00:00Z</time></trkpt>"
                         "<trkpt lat=\"32.8\" lon=\"-79.89\"><time>2026-11-09T12:00:01Z</time></trkpt>"
                         "</trkseg></trk></gpx>\n";
  ASSERT_EQ(fd_.desk().session().OpenFile(fv::app::TypeId(), path), fv::app::FlowResult::kDone);
  fv::desk::FeedConfig config = Top()->feed_config();
  config.snap_to_road = true;
  ASSERT_TRUE(fd_.desk().SetMovingMapFeed(*TopShared(), config));
  const fv::desk::Workspace saved = fd_.desk().CaptureWorkspace();
  EXPECT_EQ(saved.overlays.at("0.file"), path);
  EXPECT_EQ(saved.overlays.count("0.new"), 0u);

  FakeDesk b;
  Prepare(b);
  ASSERT_EQ(b.desk().ApplyWorkspace(saved), fv::app::FlowResult::kDone);
  ASSERT_EQ(b.desk().moving_map_count(), 1u);
  const DeskMovingMap* restored = b.desk().ownship_map()->overlay();
  EXPECT_TRUE(restored->replay());
  EXPECT_TRUE(restored->feed_config().snap_to_road);
}

}  // namespace
