// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/app/manager_host.h"

#include <gtest/gtest.h>

#include <memory>

#include "fvkit/overlay/manager.h"
#include "fvkit/overlay/point_overlay.h"

namespace {

using fv::app::ManagerHost;

TEST(ManagerHost, CapturesTheMouseForABoundOverlay) {
  fv::OverlayManager manager;
  auto points = std::make_shared<fv::PointOverlay>("Points");
  ASSERT_TRUE(manager.Add(points).ok());
  ManagerHost host(manager);
  ASSERT_TRUE(host.Bind(*points));
  EXPECT_EQ(points->host(), &host);

  ASSERT_TRUE(points->host()->CaptureMouse(*points).ok());
  EXPECT_EQ(manager.mouse_capture(), points.get());
  host.ReleaseMouse(*points);
  EXPECT_EQ(manager.mouse_capture(), nullptr);
}

TEST(ManagerHost, RefusesAnOverlayWithoutHostServices) {
  fv::OverlayManager manager;
  fv::Overlay plain("Plain");
  ManagerHost host(manager);
  EXPECT_FALSE(host.Bind(plain));
}

TEST(ManagerHost, DestroyingTheHostUnbindsItsOverlays) {
  fv::OverlayManager manager;
  auto points = std::make_shared<fv::PointOverlay>("Points");
  ASSERT_TRUE(manager.Add(points).ok());
  {
    ManagerHost host(manager);
    host.Bind(*points);
    ASSERT_NE(points->host(), nullptr);
  }
  EXPECT_EQ(points->host(), nullptr);
}

TEST(ManagerHost, RedrawRequestsCoalesce) {
  fv::OverlayManager manager;
  ManagerHost host(manager);
  EXPECT_FALSE(host.TakeRedraw());
  host.RequestRedraw();
  host.RequestRedraw();
  EXPECT_TRUE(host.TakeRedraw());
  EXPECT_FALSE(host.TakeRedraw());
}

}  // namespace
