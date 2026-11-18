// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// The Add Web Layer dialog's model, the web layer commands and the status line.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "fv_desk_fake.h"
#include "fv_desk_user_settings.h"
#include "fv_desk_web_layers.h"
#include "fvkit/feature/feature_source.h"
#include "fvkit/feature/web/remote_layer.h"
#include "fvkit/feature/web/service.h"
#include "fvkit/overlay/feature_overlay.h"
#include "scratch.h"

namespace fv {
namespace desk {
namespace {

using app::FlowResult;

/// Polls `c` until a listing finishes; false after two seconds.
bool WaitFor(WebLayerConnection& c) {
  for (int i = 0; i < 2000; ++i) {
    if (c.Poll()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

/// A one-layer service whose fetches wait while `block` is set.
class GaugeService : public web::IFeatureService {
 public:
  Status ListLayers(std::vector<web::RemoteLayerInfo>* out, const std::atomic<bool>*) override {
    out->push_back(Info());
    return Status::Ok();
  }
  Status Describe(const std::string&, web::RemoteLayerInfo* out, const std::atomic<bool>*) override {
    *out = Info();
    return Status::Ok();
  }
  Status FetchPage(const std::string&, const GeoRect&, const std::string&, std::vector<Feature>* out,
                   std::string* next, const std::atomic<bool>* cancel) override {
    while (block.load() && !cancel->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (cancel->load()) return Status::Error(kInterrupted, "cancelled");
    ++fetches;
    Feature f;
    f.id = 1;
    f.geometry = Geometry::Point(GeoPoint{32.78, -79.93});
    out->push_back(f);
    next->clear();
    return Status::Ok();
  }
  static web::RemoteLayerInfo Info() {
    web::RemoteLayerInfo info;
    info.id = "gauges";
    info.title = "Gauges";
    info.schema.geometry_type = GeometryType::kPoint;
    return info;
  }
  std::atomic<bool> block{false};
  std::atomic<int> fetches{0};
};

/// Registers "wf6-test" for https://wf6.test/ URLs: two layers, a service
/// style with 14 px markers, and the "gauges" layer cached through `RemoteFeatureLayer`.
class WebLayersTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FeatureSourceDesc d;
    d.name = "wf6-test";
    d.matches = [](const std::string& spec) { return spec.rfind("https://wf6.test/", 0) == 0; };
    d.list = [this](const std::string& spec, std::vector<FeatureSourceLayer>* out,
                    const std::atomic<bool>* cancel) {
      while (hold_list.load() && !cancel->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if (cancel->load()) return Status::Error(kInterrupted, "cancelled");
      if (spec.find("/broken") != std::string::npos) return Status::Error(kIoError, "HTTP 503 from wf6.test");
      FeatureSourceLayer a;
      a.spec = "https://wf6.test/api|layername=gauges";
      a.id = "gauges";
      a.title = "Gauges";
      a.geometry_type = GeometryType::kPoint;
      FeatureSourceLayer b;
      b.spec = "https://wf6.test/api|layername=basins";
      b.id = "basins";
      out->push_back(a);
      out->push_back(b);
      return Status::Ok();
    };
    d.open = [this](const std::string& spec, FeatureSourceResult* out) {
      std::shared_ptr<web::RemoteFeatureLayer> layer;
      const Status s = web::RemoteFeatureLayer::Open(service, GaugeService::Info(),
                                                     web::WebCachePath(scratch.Path("cache"), spec),
                                                     web::RemoteLayerOptions(), &layer);
      if (!s.ok()) return s;
      out->layer = layer;
      FeatureStyle style;
      style.symbol.marker_size_px = 14;
      return WriteFeatureStyleJson(style, &out->style_json);
    };
    ASSERT_TRUE(RegisterFeatureSource(d).ok());
  }
  void TearDown() override { UnregisterFeatureSource("wf6-test"); }

  test::Scratch scratch;
  std::shared_ptr<GaugeService> service = std::make_shared<GaugeService>();
  std::atomic<bool> hold_list{false};
};

TEST(WebLayerUrl, BlanksAreTrimmedAndHttpsIsAssumed) {
  EXPECT_EQ(NormalizeWebUrl("  api.weather.gc.ca \n"), "https://api.weather.gc.ca");
  EXPECT_EQ(NormalizeWebUrl("http://host/ogc"), "http://host/ogc");
  EXPECT_EQ(NormalizeWebUrl("   "), "");
}

TEST_F(WebLayersTest, ConnectListsTheLayersOffTheCallingThread) {
  hold_list = true;
  WebLayerConnection c;
  ASSERT_TRUE(c.Connect("wf6.test/api").ok());
  EXPECT_EQ(c.state(), WebConnectState::kConnecting);
  EXPECT_EQ(c.StatusLine(), "Connecting to https://wf6.test/api\xE2\x80\xA6");
  EXPECT_FALSE(c.Poll());
  hold_list = false;
  ASSERT_TRUE(WaitFor(c));
  EXPECT_EQ(c.state(), WebConnectState::kConnected);
  ASSERT_EQ(c.rows().size(), 2u);
  EXPECT_EQ(c.rows()[0].title, "Gauges");
  EXPECT_EQ(c.rows()[0].geometry, "Point");
  EXPECT_EQ(c.rows()[1].title, "basins");  // no title: the id
  EXPECT_EQ(c.rows()[1].geometry, "");
  EXPECT_FALSE(c.rows()[0].chosen);
  EXPECT_EQ(c.StatusLine(), "2 layers");
  EXPECT_FALSE(c.Poll());
}

TEST_F(WebLayersTest, AFailureOrAnUnclaimedUrlIsReported) {
  WebLayerConnection c;
  EXPECT_EQ(c.Connect("").code, kInvalidArg);
  EXPECT_EQ(c.Connect("ftp://wf6.test/api").code, kInvalidArg);
  EXPECT_EQ(c.state(), WebConnectState::kIdle);
  ASSERT_TRUE(c.Connect("https://wf6.test/broken").ok());
  ASSERT_TRUE(WaitFor(c));
  EXPECT_EQ(c.state(), WebConnectState::kFailed);
  EXPECT_EQ(c.StatusLine(), "HTTP 503 from wf6.test");
}

TEST_F(WebLayersTest, ReconnectingAbandonsTheListingInProgress) {
  hold_list = true;
  WebLayerConnection c;
  ASSERT_TRUE(c.Connect("https://wf6.test/broken").ok());
  ASSERT_TRUE(c.Connect("https://wf6.test/api").ok());
  hold_list = false;
  ASSERT_TRUE(WaitFor(c));
  EXPECT_EQ(c.state(), WebConnectState::kConnected);
  EXPECT_EQ(c.url(), "https://wf6.test/api");
  hold_list = true;
  ASSERT_TRUE(c.Connect("https://wf6.test/api").ok());
  c.Cancel();
  EXPECT_EQ(c.state(), WebConnectState::kIdle);
  EXPECT_FALSE(c.Poll());
}  // destruction joins the cancelled workers

TEST(WebLayerRecent, MostRecentFirstWithoutRepeatsUpToTheLimit) {
  UserSettings user;
  for (int i = 0; i < 10; ++i) NoteRecentWebUrl(&user, "https://host/" + std::to_string(i));
  NoteRecentWebUrl(&user, "https://host/5");
  const std::vector<std::string> urls = RecentWebUrls(user);
  ASSERT_EQ(urls.size(), kMaxRecentWebUrls);
  EXPECT_EQ(urls[0], "https://host/5");
  EXPECT_EQ(urls[1], "https://host/9");
  EXPECT_EQ(urls.back(), "https://host/2");
}

TEST_F(WebLayersTest, AddOpensTheTickedLayersAndRemembersTheService) {
  FakeDesk d;
  UserSettings user;
  d.desk().SetUserSettings(&user);
  ASSERT_TRUE(d.desk().commands().IsEnabled("overlay.add_web_layer"));
  ASSERT_TRUE(d.desk().commands().Execute("overlay.add_web_layer").ok());
  EXPECT_EQ(d.shell().add_web_layer_shown, 1);

  WebLayerConnection& c = d.desk().web_connection();
  ASSERT_TRUE(c.Connect("wf6.test/api").ok());
  ASSERT_TRUE(WaitFor(c));
  EXPECT_EQ(d.desk().AddWebLayers().code, kInvalidArg);  // nothing ticked
  c.SetChosen(0, true);
  ASSERT_TRUE(d.desk().AddWebLayers().ok());
  auto opened = d.desk().overlays().OfType(FeatureOverlay::kTypeId);
  ASSERT_EQ(opened.size(), 1u);
  EXPECT_EQ(opened[0]->AsPersistence()->file_spec(), "https://wf6.test/api|layername=gauges");
  ASSERT_EQ(d.desk().RecentWebUrls().size(), 1u);
  EXPECT_EQ(d.desk().RecentWebUrls()[0], "https://wf6.test/api");
}

TEST_F(WebLayersTest, ResetToServiceStyleBringsBackTheSourceStyleAsOneUndo) {
  FakeDesk d;
  ASSERT_EQ(d.desk().session().OpenFile(app::TypeId(), "https://wf6.test/api|layername=gauges"),
            FlowResult::kDone);
  auto* f = static_cast<FeatureOverlay*>(d.desk().overlays().current());
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->style().symbol.marker_size_px, 14);
  FeatureStyle mine = f->style();
  mine.symbol.marker_size_px = 30;
  f->SetStyle(mine);

  ASSERT_TRUE(d.desk().commands().IsEnabled("layers.reset_service_style"));
  ASSERT_TRUE(d.desk().commands().Execute("layers.reset_service_style").ok());
  EXPECT_EQ(f->style().symbol.marker_size_px, 14);
  ASSERT_TRUE(d.desk().undo().Undo().ok());
  EXPECT_EQ(f->style().symbol.marker_size_px, 30);
}

TEST_F(WebLayersTest, TheStatusLineCountsTilesLoadingAndRefreshFetchesAgain) {
  FakeDesk d;
  ASSERT_EQ(d.desk().session().OpenFile(app::TypeId(), "https://wf6.test/api|layername=gauges"),
            FlowResult::kDone);
  auto* f = static_cast<FeatureOverlay*>(d.desk().overlays().current());
  web::RemoteFeatureLayer* remote = f->remote_layer();
  ASSERT_NE(remote, nullptr);
  EXPECT_TRUE(d.desk().commands().IsEnabled("layers.refresh"));
  EXPECT_EQ(WebLayersStatus(d.desk().overlays()), "");

  service->block = true;
  std::vector<Feature> got;
  ASSERT_TRUE(f->layer()->Query(GeoRect{{32.7, -80.0}, {32.9, -79.8}}, &got).ok());
  const std::string loading = d.desk().CurrentStatus().message;
  EXPECT_NE(loading.find("Loading "), std::string::npos) << loading;
  EXPECT_NE(loading.find(" web tile"), std::string::npos) << loading;
  service->block = false;
  ASSERT_TRUE(remote->WaitIdle(5.0));
  EXPECT_EQ(WebLayersStatus(d.desk().overlays()), "");
  const int fetched = service->fetches.load();
  EXPECT_GT(fetched, 0);

  ASSERT_TRUE(d.desk().commands().Execute("layers.refresh").ok());
  ASSERT_TRUE(f->layer()->Query(GeoRect{{32.7, -80.0}, {32.9, -79.8}}, &got).ok());
  ASSERT_TRUE(remote->WaitIdle(5.0));
  EXPECT_GT(service->fetches.load(), fetched);
}

TEST(WebLayerCommands, WithoutAListingSourceOrAWebLayerTheyAreDisabled) {
  FakeDesk d;
  EXPECT_FALSE(d.desk().commands().IsEnabled("overlay.add_web_layer"));
  EXPECT_FALSE(d.desk().commands().IsEnabled("layers.refresh"));
  EXPECT_FALSE(d.desk().commands().IsEnabled("layers.reset_service_style"));
}

}  // namespace
}  // namespace desk
}  // namespace fv
