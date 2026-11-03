// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// DeskHost's Overlay Manager surface: the command is offered once packages
// are loaded, the C example installs from its .pgpkg and draws, and disable
// and remove wait for the next launch.

#include <gtest/gtest.h>

#include <filesystem>

#include "fv_desk_host.h"
#include "scratch.h"

namespace {

using fv::desk::DeskHost;
using fv::desk::HostPackage;
using fv::desk::test::Scratch;

constexpr char kPackage[] = "com.example.rangerings";

/// Releases the host at scope exit.
struct HostRef {
  DeskHost* host = DeskHost::Create();
  ~HostRef() { fv_desk_host_release(host); }
  DeskHost* operator->() { return host; }
};

/// The row of `id`, or -1.
int RowOf(DeskHost* h, const std::string& id) {
  for (int i = 0; i < h->PackageCount(); ++i)
    if (h->PackageAt(i).id == id) return i;
  return -1;
}

TEST(HostPackages, TheManagerIsOfferedOncePackagesLoad) {
  Scratch scratch;
  HostRef h;
  EXPECT_FALSE(h->IsEnabled("tools.overlay_manager"));
  h->LoadPackages(std::string(), scratch.Path("user"), "1.0");
  EXPECT_TRUE(h->IsEnabled("tools.overlay_manager"));
  ASSERT_EQ(h->Execute("tools.overlay_manager"), "");
  EXPECT_TRUE(h->Tick().overlay_manager_requested);
  EXPECT_TRUE(h->TakeOverlayManagerRequest());
  EXPECT_FALSE(h->TakeOverlayManagerRequest());
}

TEST(HostPackages, InspectDescribesThePackageForTheConfirmation) {
  Scratch scratch;
  HostRef h;
  h->LoadPackages(std::string(), scratch.Path("user"), "1.0");
  const HostPackage p = h->InspectPackage(FV_RANGERINGS_PGPKG);
  ASSERT_EQ(p.error, "");
  EXPECT_EQ(p.id, kPackage);
  EXPECT_EQ(p.name, "Range Rings");
  EXPECT_EQ(p.publisher, "Peregrine SDK example");
  EXPECT_EQ(p.code_kinds, "C library");
  EXPECT_EQ(p.overlays, "Range Rings");
  EXPECT_EQ(p.tools, "Open Sample Rings");
  EXPECT_EQ(h->PackageCount(), 0);  // inspecting installs nothing
  EXPECT_NE(h->InspectPackage(scratch.Path("missing.pgpkg")).error, "");
}

TEST(HostPackages, InstallDisableAndRemoveAcrossALaunch) {
  Scratch scratch;
  const std::string user = scratch.Path("user");
  {
    HostRef h;
    h->LoadPackages(std::string(), user, "1.0");
    const fv::desk::HostInstallOutcome o = h->InstallPackage(FV_RANGERINGS_PGPKG);
    ASSERT_EQ(o.error, "");
    EXPECT_EQ(o.id, kPackage);
    EXPECT_TRUE(o.live);
    EXPECT_TRUE(h->OpensAsOverlay("charleston.rng"));
    EXPECT_TRUE(h->IsEnabled("tools.run.com.example.rangerings.open_sample"));
    const int row = RowOf(h.host, kPackage);
    ASSERT_GE(row, 0);
    HostPackage p = h->PackageAt(row);
    EXPECT_EQ(p.state, "Loaded");
    EXPECT_EQ(p.location, "User");
    EXPECT_TRUE(p.removable);
    EXPECT_FALSE(h->PackagesRestartPending());

    ASSERT_EQ(h->SetPackageEnabled(kPackage, false), "");
    p = h->PackageAt(row);
    EXPECT_FALSE(p.enabled);
    EXPECT_EQ(p.pending, "Disabled at relaunch");
    EXPECT_TRUE(h->PackagesRestartPending());
  }
  {
    HostRef h;
    h->LoadPackages(std::string(), user, "1.0");
    const int row = RowOf(h.host, kPackage);
    ASSERT_GE(row, 0);
    EXPECT_EQ(h->PackageAt(row).state, "Disabled");
    EXPECT_FALSE(h->OpensAsOverlay("charleston.rng"));
    ASSERT_EQ(h->RemovePackage(kPackage), "");
  }
  {
    HostRef h;
    h->LoadPackages(std::string(), user, "1.0");
    EXPECT_EQ(RowOf(h.host, kPackage), -1);
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(user) / kPackage));
    // Removal forgets the disabled choice: a reinstall loads at once.
    const fv::desk::HostInstallOutcome o = h->InstallPackage(FV_RANGERINGS_PGPKG);
    ASSERT_EQ(o.error, "");
    EXPECT_TRUE(o.live);
    EXPECT_EQ(h->PackageAt(RowOf(h.host, kPackage)).state, "Loaded");
  }
}

}  // namespace
