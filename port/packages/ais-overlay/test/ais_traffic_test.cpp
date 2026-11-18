// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The peregrine.ais package: it loads from the system packages directory with
// its options, raises a collision alarm through the host once per encounter,
// reads a local receiver, and splits the Open Waters event stream. No test
// reaches the internet: Open Waters is turned off before any overlay gets a host.

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmath>
#include <string>
#include <vector>

#include "ais_traffic.h"
#include "fv_desk_fake.h"
#include "fv_desk_packages.h"
#include "fvkit/app/manager_host.h"
#include "fvkit/overlay/manager.h"
#include "scratch.h"
#include "sse_stream.h"

namespace {

using fv::app::PropertyValue;
using peregrine_ais::TrafficOverlay;

constexpr char kPackage[] = "peregrine.ais";
constexpr char kType[] = "peregrine.ais.traffic";
constexpr double kT0 = 1.8e9;
constexpr double kMetresPerDegLat = 111195.0;

/// A class A position report `north_m` north of (32.75, -79.9), steaming at `cog`.
fv::AisOverlay::Input Report(std::uint32_t mmsi, double north_m, double sog_mps, double cog,
                             double heard_s) {
  fv::AisOverlay::Input in;
  in.has_message = true;
  in.heard_s = heard_s;
  fv::ais::AisMessage& m = in.message;
  m.type = 1;
  m.mmsi = mmsi;
  m.has_position = true;
  m.lat_deg = 32.75 + north_m / kMetresPerDegLat;
  m.lon_deg = -79.9;
  m.has_sog = true;
  m.sog_mps = sog_mps;
  m.has_cog = true;
  m.cog_deg = cog;
  m.has_heading = true;
  m.heading_deg = static_cast<int>(cog);
  return in;
}

fv::PositionFix Ownship() {
  fv::PositionFix f;
  f.has_position = true;
  f.lat = 32.75;
  f.lon = -79.9;
  f.has_speed = true;
  f.speed_mps = 0.0;
  return f;
}

/// An overlay stepped by hand, Open Waters off, bound to a ManagerHost.
class TrafficTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(overlay_.SetProperty("open_waters", PropertyValue::Bool(false)).ok());
    ASSERT_TRUE(host_.Bind(overlay_));
  }
  void TearDown() override { host_.Unbind(overlay_); }

  fv::OverlayManager manager_;
  fv::app::ManagerHost host_{manager_};
  TrafficOverlay overlay_{"test", /*run_worker=*/false};
};

TEST_F(TrafficTest, AlarmsOncePerEncounterAndRearmsWhenTheTargetClears) {
  host_.SetOwnship(Ownship());
  // 2 km north, heading south at 5 m/s: closest approach 0 m in 400 s.
  overlay_.Push(Report(366000001, 2000, 5.0, 180, kT0));
  EXPECT_TRUE(overlay_.Step(kT0));
  std::vector<std::string> alarms = host_.TakeAlarms();
  ASSERT_EQ(alarms.size(), 1u);
  EXPECT_NE(alarms[0].find("366000001"), std::string::npos) << alarms[0];
  EXPECT_EQ(overlay_.Alarmed().count(366000001u), 1u);

  // Still closing: no second alarm.
  overlay_.Push(Report(366000001, 1500, 5.0, 180, kT0 + 100));
  overlay_.Step(kT0 + 100);
  EXPECT_TRUE(host_.TakeAlarms().empty());

  // Turned away: it leaves the alarm, and a new approach alarms again.
  overlay_.Push(Report(366000001, 1400, 5.0, 0, kT0 + 120));
  overlay_.Step(kT0 + 120);
  EXPECT_TRUE(overlay_.Alarmed().empty());
  overlay_.Push(Report(366000001, 1600, 5.0, 180, kT0 + 160));
  overlay_.Step(kT0 + 160);
  EXPECT_EQ(host_.TakeAlarms().size(), 1u);
}

TEST_F(TrafficTest, NoAlarmBeyondTheThresholdsOrWithoutAnOwnship) {
  // Closing, but the ownship is unknown.
  overlay_.Push(Report(366000002, 2000, 5.0, 180, kT0));
  overlay_.Step(kT0);
  EXPECT_TRUE(host_.TakeAlarms().empty());

  host_.SetOwnship(Ownship());
  // 20 km away at 5 m/s: 4000 s out, past the 10 minute default.
  overlay_.Push(Report(366000002, 20000, 5.0, 180, kT0 + 1));
  overlay_.Step(kT0 + 1);
  EXPECT_TRUE(host_.TakeAlarms().empty());

  // Inside both thresholds with the alarm turned off.
  ASSERT_TRUE(overlay_.SetProperty("cpa_alarm", PropertyValue::Bool(false)).ok());
  overlay_.Push(Report(366000002, 2000, 5.0, 180, kT0 + 2));
  overlay_.Step(kT0 + 2);
  EXPECT_TRUE(host_.TakeAlarms().empty());

  // Back on with a 1 minute window: 400 s out still does not alarm.
  ASSERT_TRUE(overlay_.SetProperty("cpa_alarm", PropertyValue::Bool(true)).ok());
  ASSERT_TRUE(overlay_.SetProperty("tcpa_minutes", PropertyValue::Double(1.0)).ok());
  overlay_.Step(kT0 + 3);
  EXPECT_TRUE(host_.TakeAlarms().empty());
}

TEST_F(TrafficTest, CreditsEachSourceOnceAndCountsTargets) {
  fv::ais::OpenWatersFrame f;
  f.kind = fv::ais::OpenWatersFrameKind::kEvent;
  f.time_s = kT0;
  f.attribution = "Source: Kystverket (NLOD 2.0)";
  f.has_fallback = true;
  f.fallback = Report(257000001, 0, 3, 90, kT0).message;
  overlay_.PushFrame(f);
  f.fallback = Report(257000002, 500, 3, 90, kT0).message;
  overlay_.PushFrame(f);
  f.attribution = "Data from AISHub";
  f.fallback = Report(257000003, 900, 3, 90, kT0).message;
  overlay_.PushFrame(f);
  overlay_.Step(kT0);
  const std::vector<std::string> credits = overlay_.Credits();
  ASSERT_EQ(credits.size(), 2u);
  EXPECT_EQ(credits[0], "Source: Kystverket (NLOD 2.0)");
  EXPECT_EQ(credits[1], "Data from AISHub");
  EXPECT_NE(overlay_.StatusLine().find("3 targets"), std::string::npos) << overlay_.StatusLine();
}

TEST_F(TrafficTest, PropertiesRoundTripAndRefuseBadValues) {
  ASSERT_TRUE(overlay_.SetProperty("receiver_port", PropertyValue::Int(2000)).ok());
  ASSERT_TRUE(overlay_.SetProperty("receiver_protocol", PropertyValue::Choice(1)).ok());
  ASSERT_TRUE(overlay_.SetProperty("cpa_nm", PropertyValue::Double(1.0)).ok());
  EXPECT_FALSE(overlay_.SetProperty("receiver_port", PropertyValue::Int(70000)).ok());
  EXPECT_FALSE(overlay_.SetProperty("cpa_nm", PropertyValue::Bool(true)).ok());
  PropertyValue v;
  ASSERT_TRUE(overlay_.GetProperty("receiver_port", &v).ok());
  EXPECT_EQ(v.i, 2000);
  ASSERT_TRUE(overlay_.GetProperty("cpa_nm", &v).ok());
  EXPECT_NEAR(v.d, 1.0, 1e-9);
  EXPECT_EQ(overlay_.settings().receiver_config.protocol, fv::ReceiverProtocol::kUdp);
  // The base overlay's options are still there.
  ASSERT_TRUE(overlay_.GetProperty("show_trails", &v).ok());
  EXPECT_TRUE(v.b);
}

TEST_F(TrafficTest, ReadsAUdpReceiver) {
  const int port = 39000 + static_cast<int>(getpid() % 1000);
  ASSERT_TRUE(overlay_.SetProperty("receiver_protocol", PropertyValue::Choice(1)).ok());
  ASSERT_TRUE(overlay_.SetProperty("receiver_port", PropertyValue::Int(port)).ok());
  ASSERT_TRUE(overlay_.SetProperty("receiver", PropertyValue::Bool(true)).ok());
  overlay_.Step(kT0);  // opens the listener

  const int s = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(s, 0);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(static_cast<uint16_t>(port));
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  // A type 1 report from the decoder's tests, MMSI 227006760.
  const std::string line = "!AIVDM,1,1,,A,13HOI:0P0000VOHLCnHQKwvL05Ip,0*23\r\n";
  ASSERT_EQ(sendto(s, line.data(), line.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof to),
            static_cast<ssize_t>(line.size()));
  close(s);

  for (int i = 1; i <= 20 && overlay_.table().Find(227006760u) == nullptr; ++i) {
    usleep(10000);
    overlay_.Step(kT0 + i * 0.25);
  }
  EXPECT_NE(overlay_.table().Find(227006760u), nullptr);
  EXPECT_NE(overlay_.StatusLine().find("receiving"), std::string::npos) << overlay_.StatusLine();
}

TEST(AisSseSplitter, HandsBackDataLinesSplitAcrossChunks) {
  peregrine_ais::SseSplitter splitter;
  std::vector<std::string> got;
  const auto take = [&](std::string_view p) { got.emplace_back(p); };
  splitter.Feed("data: {\"type\":\"wel", take);
  splitter.Feed("come\"}\r\n\r\n: keep-alive\n", take);
  splitter.Feed("event: x\ndata: {\"a\":1}\n", take);
  ASSERT_EQ(got.size(), 2u);
  EXPECT_EQ(got[0], "{\"type\":\"welcome\"}");
  EXPECT_EQ(got[1], "{\"a\":1}");
}

/// Through the Desk: the package loads, and an alarm reaches the shell as a notice.
class AisPackageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake_.desk().LoadPackages(fv::desk::PackageDirs{FV_PACKAGES_DIR, scratch_.Path("user")});
  }
  fv::desk::test::Scratch scratch_;
  fv::desk::FakeDesk fake_{400, 300};
};

TEST_F(AisPackageTest, LoadsWithItsOptionsAndRaisesAlarmsAsNotices) {
  fv::desk::Desk& desk = fake_.desk();
  const fv::desk::PackageInfo* p = desk.packages()->Find(kPackage);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->state, fv::desk::PackageState::kLoaded) << p->reason;
  const fv::app::OverlayTypeDesc* type = desk.types().Find(kType);
  ASSERT_NE(type, nullptr);
  std::shared_ptr<fv::Overlay> o = type->factory();
  ASSERT_NE(o->AsProperties(), nullptr);
  ASSERT_TRUE(o->AsProperties()->SetProperty("open_waters", PropertyValue::Bool(false)).ok());
  ASSERT_TRUE(desk.overlays().Add(o).ok());
  ASSERT_NE(o->AsHostAware(), nullptr);
  fv::app::OverlayHost* host = o->AsHostAware()->host();
  ASSERT_NE(host, nullptr);

  fv::PositionFix none;
  EXPECT_FALSE(host->OwnshipFix(&none));
  host->RaiseAlarm("AIS collision warning: test");
  desk.FlushOverlayRedraw();
  ASSERT_EQ(fake_.shell().notices.size(), 1u);
  EXPECT_EQ(fake_.shell().notices[0], "AIS collision warning: test");
}

}  // namespace
