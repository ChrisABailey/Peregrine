// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include "fv_desk_fake.h"
#include "fv_desk_packages.h"
#include "fv_desk_tools.h"
#include "fv_desk_user_settings.h"
#include "range_rings.h"
#include "scratch.h"
#include "zip_test_writer.h"

namespace {

namespace fs = std::filesystem;
using fv::desk::InstallResult;
using fv::desk::PackageDirs;
using fv::desk::PackageInfo;
using fv::desk::PackageLocation;
using fv::desk::PackageManager;
using fv::desk::PackageManifest;
using fv::desk::PackageState;
using fv::desk::test::RangeRings;
using fv::desk::test::Scratch;

constexpr char kId[] = "com.example.rings";

/// A package with one file overlay (`<id>.rings`, extension `ext`) and one
/// tool (`<id>.hello`), both builtin unless `overlay_impl` says otherwise.
std::string Manifest(const std::string& id = kId, const std::string& version = "1.0",
                     const std::string& overlay_impl = R"({"builtin": "test.pkg.rings"})",
                     const std::string& ext = "rng", const std::string& extra = "") {
  return R"({
  "format": 1,
  "id": ")" + id + R"(",
  "name": "Range Rings",
  "version": ")" + version + R"(",
  "publisher": "Example Corp",
  )" + extra + R"(
  "overlays": [
    { "id": ")" + id + R"(.rings", "display_name": "Range Rings",
      "icon": "resources/rings.svg", "display_order": 40,
      "file": { "extension": ")" + ext + R"(" },
      "implementation": )" + overlay_impl + R"( }
  ],
  "tools": [
    { "id": ")" + id + R"(.hello", "title": "Say Hello", "submenu": "Rings",
      "icon": "resources/hello.svg",
      "implementation": { "builtin": "test.pkg.hello" } }
  ]
})";
}

void WriteFile(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

std::string ReadFile(const fs::path& path) {
  std::ostringstream ss;
  ss << std::ifstream(path, std::ios::binary).rdbuf();
  return ss.str();
}

/// Writes a package directory `parent/<dir_name>`; returns its path.
fs::path WritePackage(const fs::path& parent, const std::string& dir_name,
                      const std::string& manifest) {
  const fs::path dir = parent / dir_name;
  WriteFile(dir / fv::desk::kPackageManifestName, manifest);
  WriteFile(dir / "resources" / "rings.svg", "<svg/>");
  return dir;
}

/// Zips every file under `dir`, each name prefixed with `prefix`.
void ZipDir(const fs::path& dir, const fs::path& zip, const std::string& prefix,
            fv_test::ZipTestWriter w = fv_test::ZipTestWriter()) {
  for (const auto& e : fs::recursive_directory_iterator(dir))
    if (e.is_regular_file())
      w.Add(prefix + fs::relative(e.path(), dir).generic_string(), ReadFile(e.path()), true);
  ASSERT_TRUE(w.Write(zip.string()));
}

fv::Status Parse(const std::string& text, PackageManifest* m, bool allow_reserved = false) {
  return fv::desk::ParsePackageManifest(text, "test.json", "/pkg", allow_reserved, m);
}

class PackagesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fv::desk::BuiltinOverlay b;
    b.factory = [] { return std::make_shared<RangeRings>(); };
    ASSERT_TRUE(fv::desk::RegisterBuiltinOverlay("test.pkg.rings", std::move(b)).ok());
    ASSERT_TRUE(fv::desk::RegisterBuiltinTool("test.pkg.hello", [this](fv::desk::ToolContext&) {
                  ++hello_runs_;
                  return fv::Status::Ok();
                }).ok());
    fs::create_directories(System());
  }
  void TearDown() override {
    fv::desk::UnregisterBuiltinOverlay("test.pkg.rings");
    fv::desk::UnregisterBuiltinTool("test.pkg.hello");
  }

  fs::path Root() const { return scratch_.Path(""); }
  fs::path User() const { return Root() / "user"; }
  fs::path System() const { return Root() / "system"; }
  fs::path Src() const { return Root() / "src"; }
  PackageDirs Dirs() const { return PackageDirs{System().string(), User().string()}; }

  /// A .pgpkg of `manifest` with the usual one-directory layout.
  fs::path MakePgpkg(const std::string& name, const std::string& manifest,
                     const std::string& dir_name = kId) {
    const fs::path src = WritePackage(Src() / name, dir_name, manifest);
    const fs::path zip = Root() / (name + ".pgpkg");
    ZipDir(src, zip, dir_name + "/");
    return zip;
  }

  Scratch scratch_;
  int hello_runs_ = 0;
};

/// A manager over fresh registries, scanned and loaded, as at a launch.
struct Launch {
  explicit Launch(const PackageDirs& dirs, const std::string& host_version = std::string())
      : manager(dirs, types, tools, host_version) {
    manager.Scan(&warnings);
    manager.LoadAll();
  }
  fv::app::OverlayTypeRegistry types;
  fv::desk::ToolRegistry tools;
  PackageManager manager;
  std::vector<std::string> warnings;
};

std::vector<std::string> ItemIds(const std::vector<fv::desk::MenuItem>& items) {
  std::vector<std::string> ids;
  for (const fv::desk::MenuItem& i : items) {
    if (!i.command_id.empty()) ids.push_back(i.command_id);
    for (const std::string& c : ItemIds(i.children)) ids.push_back(c);
  }
  return ids;
}

bool Contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

// MARK: Manifest

TEST_F(PackagesTest, ParsesAFullManifest) {
  PackageManifest m;
  const auto s = Parse(Manifest(kId, "1.2.0", R"({"library": "rings"})", "rng",
                                R"("license": "MIT", "description": "Rings.",
  "requires": { "peregrine": "1.1", "plugin_api": 1 },
  "libraries": { "macos": "lib/macos/librings.dylib", "linux-x86_64": "lib/linux-x86_64/librings.so" },)"),
                       &m);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(m.id, kId);
  EXPECT_EQ(m.version, "1.2.0");
  EXPECT_EQ(m.publisher, "Example Corp");
  EXPECT_EQ(m.requires_peregrine, "1.1");
  EXPECT_EQ(m.requires_plugin_api, 1);
  EXPECT_EQ(m.libraries.at("macos"), "lib/macos/librings.dylib");
  ASSERT_EQ(m.overlays.size(), 1u);
  EXPECT_EQ(m.overlays[0].implementation_kind, "library");
  EXPECT_EQ(m.overlays[0].desc.icon, "/pkg/resources/rings.svg");
  ASSERT_EQ(m.tools.size(), 1u);
  EXPECT_EQ(m.tools[0].desc.id, std::string(kId) + ".hello");
  EXPECT_EQ(m.tools[0].desc.submenu, "Rings");
  EXPECT_EQ(m.tools[0].desc.icon, "/pkg/resources/hello.svg");
  EXPECT_EQ(m.tools[0].desc.package_id, kId);
  EXPECT_EQ(m.CodeKinds(), std::vector<std::string>{"C library"});
}

TEST_F(PackagesTest, RefusesBadManifests) {
  struct Case {
    const char* what;
    std::string text;
  };
  const std::string builtin = R"({"builtin": "test.pkg.rings"})";
  const Case cases[] = {
      {"not reverse-DNS", Manifest("rings")},
      {"reserved fv.", Manifest("fv.rings")},
      {"reserved peregrine.", Manifest("peregrine.rings")},
      {"bad version", Manifest(kId, "1.x")},
      {"overlay id outside the package",
       R"({"format":1,"id":"com.a.b","name":"n","version":"1",
           "overlays":[{"id":"com.other.x","implementation":{"builtin":"x"}}]})"},
      {"tool id outside the package",
       R"({"format":1,"id":"com.a.b","name":"n","version":"1",
           "tools":[{"id":"com.a.bx","title":"t","implementation":{"builtin":"x"}}]})"},
      {"icon escapes",
       R"({"format":1,"id":"com.a.b","name":"n","version":"1",
           "overlays":[{"id":"com.a.b.x","icon":"../../etc/x.svg","implementation":{"builtin":"x"}}]})"},
      {"absolute icon",
       R"({"format":1,"id":"com.a.b","name":"n","version":"1",
           "tools":[{"id":"com.a.b.x","title":"t","icon":"/tmp/x.png","implementation":{"builtin":"x"}}]})"},
      {"library escapes",
       Manifest(kId, "1", R"({"library":"r"})", "rng",
                R"("requires":{"plugin_api":1}, "libraries":{"macos":"../lib.dylib"},)")},
      {"unknown platform",
       Manifest(kId, "1", R"({"library":"r"})", "rng",
                R"("requires":{"plugin_api":1}, "libraries":{"amiga":"lib/r.so"},)")},
      {"library without plugin_api",
       Manifest(kId, "1", R"({"library":"r"})", "rng", R"("libraries":{"macos":"lib/r.dylib"},)")},
      {"compiled without libraries", Manifest(kId, "1", R"({"native":"r"})")},
      {"python without python_path", Manifest(kId, "1", R"({"python":"rings:Rings"})")},
      {"C and native together",
       R"({"format":1,"id":"com.a.b","name":"n","version":"1","requires":{"plugin_api":1},
           "libraries":{"macos":"lib/x.dylib"},
           "overlays":[{"id":"com.a.b.x","implementation":{"library":"x"}}],
           "tools":[{"id":"com.a.b.t","title":"t","implementation":{"native":"t"}}]})"},
      {"no contributions", R"({"format":1,"id":"com.a.b","name":"n","version":"1"})"},
      {"no format", R"({"id":"com.a.b","name":"n","version":"1"})"},
      {"tool without title",
       R"({"format":1,"id":"com.a.b","name":"n","version":"1",
           "tools":[{"id":"com.a.b.t","implementation":{"builtin":"x"}}]})"},
      {"duplicate tool",
       R"({"format":1,"id":"com.a.b","name":"n","version":"1",
           "tools":[{"id":"com.a.b.t","title":"t","implementation":{"builtin":"x"}},
                    {"id":"com.a.b.t","title":"t","implementation":{"builtin":"x"}}]})"},
      {"unknown kind", Manifest(kId, "1", R"({"java":"Rings"})")},
      {"overlay entry error", Manifest(kId, "1", builtin, "")},
  };
  for (const Case& c : cases) {
    PackageManifest m;
    m.id = "unchanged";
    const auto s = Parse(c.text, &m);
    EXPECT_FALSE(s.ok()) << c.what;
    EXPECT_EQ(s.code, fv::kInvalidArg) << c.what << ": " << s.message;
    EXPECT_EQ(m.id, "unchanged") << c.what;
  }
}

TEST_F(PackagesTest, ReservedIdsAreForPackagesShippedWithTheApp) {
  PackageManifest m;
  EXPECT_TRUE(Parse(Manifest("fv.extras"), &m, /*allow_reserved=*/true).ok());
}

TEST_F(PackagesTest, ANewerFormatNeedsANewerPeregrine) {
  PackageManifest m;
  std::string text = Manifest();
  text.replace(text.find("\"format\": 1"), 11, "\"format\": 2");
  const auto s = Parse(text, &m);
  EXPECT_EQ(s.code, fv::kUnsupported);
  EXPECT_NE(s.message.find("newer Peregrine"), std::string::npos) << s.message;
}

TEST(PackageVersions, CompareNumerically) {
  EXPECT_LT(fv::desk::CompareVersions("1.9", "1.10"), 0);
  EXPECT_EQ(fv::desk::CompareVersions("1.2", "1.2.0"), 0);
  EXPECT_GT(fv::desk::CompareVersions("2", "1.99.99"), 0);
}

// MARK: Install

TEST_F(PackagesTest, ZipInstallsLiveIntoTheToolsMenu) {
  const fs::path pgpkg = MakePgpkg("rings", Manifest());
  fv::desk::FakeDesk fake;
  fv::desk::Desk& desk = fake.desk();
  desk.LoadPackages(Dirs());
  ASSERT_NE(desk.packages(), nullptr);
  EXPECT_TRUE(desk.packages()->packages().empty());

  InstallResult r;
  const auto s = desk.InstallPackage(pgpkg.string(), &r);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(r.id, kId);
  EXPECT_TRUE(r.live);
  EXPECT_TRUE(r.replaced_version.empty());
  EXPECT_TRUE(fs::is_regular_file(User() / kId / "resources" / "rings.svg"));
  EXPECT_FALSE(fs::exists(User() / ".staging" / kId));

  const PackageInfo* p = desk.packages()->Find(kId);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->state, PackageState::kLoaded);
  EXPECT_EQ(p->location, PackageLocation::kUser);
  ASSERT_NE(desk.types().Find(std::string(kId) + ".rings"), nullptr);
  EXPECT_EQ(desk.types().Find(std::string(kId) + ".rings")->icon,
            (User() / kId / "resources" / "rings.svg").string());

  const fv::desk::Menu* tools = desk.menus().Find("tools");
  ASSERT_NE(tools, nullptr);
  const std::string run_id = std::string("tools.run.") + kId + ".hello";
  EXPECT_TRUE(Contains(ItemIds(tools->items), run_id));
  EXPECT_TRUE(Contains(ItemIds(desk.menus().Find("file")->items),
                       std::string("overlay.new.") + kId + ".rings"));
  ASSERT_TRUE(desk.Execute(run_id).ok());
  EXPECT_EQ(hello_runs_, 1);
  EXPECT_TRUE(fake.shell().errors.empty());
}

TEST_F(PackagesTest, AZipOfThePackageContentsInstallsToo) {
  const fs::path src = WritePackage(Src(), kId, Manifest());
  fv_test::ZipTestWriter w;
  w.Add("__MACOSX/._peregrine-package.json", "finder metadata", false);
  const fs::path zip = Root() / "flat.pgpkg";
  ZipDir(src, zip, "", w);
  Launch launch(Dirs());
  InstallResult r;
  const auto s = launch.manager.Install(zip.string(), &r);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_TRUE(r.live);
  EXPECT_TRUE(fs::is_regular_file(User() / kId / fv::desk::kPackageManifestName));
  EXPECT_FALSE(fs::exists(User() / kId / "__MACOSX"));
}

TEST_F(PackagesTest, BadZipsInstallNothing) {
  // The directory is not named after the id.
  const fs::path misnamed = MakePgpkg("misnamed", Manifest(), "rings");
  // No manifest at all.
  const fs::path empty_src = Src() / "nothing" / kId;
  WriteFile(empty_src / "readme.txt", "hi");
  const fs::path no_manifest = Root() / "nothing.pgpkg";
  ZipDir(empty_src, no_manifest, std::string(kId) + "/");
  // An entry that escapes the destination.
  fv_test::ZipTestWriter evil;
  evil.Add(std::string(kId) + "/" + fv::desk::kPackageManifestName, Manifest(), true);
  evil.Add(std::string(kId) + "/../../escaped.txt", "x", false);
  const fs::path escape = Root() / "escape.pgpkg";
  ASSERT_TRUE(evil.Write(escape.string()));

  Launch launch(Dirs());
  for (const fs::path& zip : {misnamed, no_manifest, escape}) {
    InstallResult r;
    const auto s = launch.manager.Install(zip.string(), &r);
    EXPECT_FALSE(s.ok()) << zip;
    EXPECT_FALSE(fs::exists(User() / kId)) << zip;
  }
  EXPECT_FALSE(fs::exists(Root() / "escaped.txt"));
  EXPECT_FALSE(fs::exists(User() / "escaped.txt"));
  EXPECT_TRUE(fs::is_empty(User() / ".staging"));
  EXPECT_TRUE(launch.manager.packages().empty());
}

TEST_F(PackagesTest, InspectReadsTheManifestWithoutInstalling) {
  const fs::path pgpkg =
      MakePgpkg("py", Manifest(kId, "3.1", R"({"python": "rings:Rings"})", "rng",
                               R"("python_path": "python", "requires": {"python": "3.12"},)"));
  Launch launch(Dirs());
  PackageManifest m;
  const auto s = launch.manager.Inspect(pgpkg.string(), &m);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(m.version, "3.1");
  EXPECT_EQ(m.publisher, "Example Corp");
  EXPECT_EQ(m.CodeKinds(), std::vector<std::string>{"Python"});
  EXPECT_FALSE(fs::exists(User() / kId));
}

TEST_F(PackagesTest, UpgradingALoadedPackageWaitsForTheNextLaunch) {
  {
    Launch first(Dirs());
    InstallResult r;
    ASSERT_TRUE(first.manager.Install(MakePgpkg("v1", Manifest(kId, "1.0")).string(), &r).ok());
    ASSERT_TRUE(r.live);
    const auto s = first.manager.Install(MakePgpkg("v2", Manifest(kId, "2.0")).string(), &r);
    ASSERT_TRUE(s.ok()) << s.message;
    EXPECT_FALSE(r.live);
    EXPECT_EQ(r.replaced_version, "1.0");
    const PackageInfo* p = first.manager.Find(kId);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->manifest.version, "1.0");
    EXPECT_EQ(p->pending_version, "2.0");
    EXPECT_TRUE(first.manager.RestartPending());
  }
  Launch second(Dirs());
  const PackageInfo* p = second.manager.Find(kId);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->manifest.version, "2.0");
  EXPECT_EQ(p->state, PackageState::kLoaded);
  EXPECT_TRUE(p->pending_version.empty());
  EXPECT_FALSE(fs::exists(User() / ".pending" / kId));
  EXPECT_FALSE(second.manager.RestartPending());
}

// MARK: Enable, disable, remove

TEST_F(PackagesTest, EnablingLoadsAtOnceAndDisablingWaitsForTheNextLaunch) {
  WritePackage(User(), kId, Manifest());
  const std::string type = std::string(kId) + ".rings";
  {
    Launch first(Dirs());
    ASSERT_NE(first.types.Find(type), nullptr);
    ASSERT_TRUE(first.manager.SetEnabled(kId, false).ok());
    EXPECT_EQ(first.manager.Find(kId)->state, PackageState::kLoaded);
    EXPECT_TRUE(first.manager.RestartPending());
  }
  Launch second(Dirs());
  EXPECT_EQ(second.manager.Find(kId)->state, PackageState::kDisabled);
  EXPECT_EQ(second.types.Find(type), nullptr);
  EXPECT_EQ(second.tools.Find(std::string(kId) + ".hello"), nullptr);
  ASSERT_TRUE(second.manager.SetEnabled(kId, true).ok());
  EXPECT_EQ(second.manager.Find(kId)->state, PackageState::kLoaded);
  EXPECT_NE(second.types.Find(type), nullptr);
  EXPECT_NE(second.tools.Find(std::string(kId) + ".hello"), nullptr);
}

TEST_F(PackagesTest, RemovingALoadedPackageWaitsForTheNextLaunch) {
  WritePackage(User(), kId, Manifest());
  {
    Launch first(Dirs());
    ASSERT_TRUE(first.manager.Remove(kId).ok());
    EXPECT_TRUE(first.manager.Find(kId)->pending_removal);
    EXPECT_TRUE(fs::exists(User() / kId));
    EXPECT_TRUE(first.manager.RestartPending());
  }
  Launch second(Dirs());
  EXPECT_FALSE(fs::exists(User() / kId));
  EXPECT_TRUE(second.manager.packages().empty());
  EXPECT_TRUE(second.warnings.empty());
}

TEST_F(PackagesTest, RemovingAPackageThatIsNotLoadedIsImmediate) {
  WritePackage(User(), kId, Manifest());
  {
    Launch first(Dirs());
    ASSERT_TRUE(first.manager.SetEnabled(kId, false).ok());
  }
  Launch second(Dirs());
  ASSERT_EQ(second.manager.Find(kId)->state, PackageState::kDisabled);
  ASSERT_TRUE(second.manager.Remove(kId).ok());
  EXPECT_FALSE(fs::exists(User() / kId));
  EXPECT_EQ(second.manager.Find(kId), nullptr);
}

TEST_F(PackagesTest, RemovalNeverDeletesOutsideTheUserDirectory) {
  // A system package is refused.
  WritePackage(System(), "com.example.shipped", Manifest("com.example.shipped"));
  // A pending removal naming a directory elsewhere (a hand-edited state file)
  // is skipped, even though that directory holds a manifest.
  const fs::path precious = WritePackage(Root() / "elsewhere", kId, Manifest());
  WriteFile(User() / ".state.json",
            R"({"pending_removal": [")" + precious.generic_string() + R"("]})");

  Launch launch(Dirs());
  EXPECT_TRUE(fs::exists(precious / fv::desk::kPackageManifestName));
  EXPECT_FALSE(launch.warnings.empty());
  const auto s = launch.manager.Remove("com.example.shipped");
  EXPECT_EQ(s.code, fv::kInvalidArg);
  EXPECT_NE(s.message.find("disable it instead"), std::string::npos) << s.message;
  EXPECT_TRUE(fs::exists(System() / "com.example.shipped"));
}

// MARK: Locations

TEST_F(PackagesTest, LinkedBeatsUserBeatsSystem) {
  WritePackage(System(), kId, Manifest(kId, "1.0"));
  WritePackage(User(), kId, Manifest(kId, "2.0"));
  const fs::path dev = WritePackage(Root() / "dev", "rings-checkout", Manifest(kId, "3.0"));
  {
    Launch first(Dirs());
    EXPECT_EQ(first.manager.Find(kId)->manifest.version, "2.0");
    ASSERT_EQ(first.manager.packages().size(), 2u);
    EXPECT_EQ(first.manager.packages()[1].state, PackageState::kShadowed);
    // A link of an installed id is refused.
    InstallResult r;
    EXPECT_EQ(first.manager.Install(dev.string(), &r).code, fv::kInvalidArg);
  }
  WriteFile(User() / ".state.json", R"({"linked": [")" + dev.generic_string() + R"("]})");
  Launch second(Dirs());
  const PackageInfo* p = second.manager.Find(kId);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->location, PackageLocation::kLinked);
  EXPECT_EQ(p->manifest.version, "3.0");
  EXPECT_EQ(p->state, PackageState::kLoaded);
}

TEST_F(PackagesTest, LinkingADirectoryUsesItInPlace) {
  const fs::path dev = WritePackage(Root() / "dev", "anything", Manifest());
  {
    Launch first(Dirs());
    InstallResult r;
    const auto s = first.manager.Install(dev.string(), &r);
    ASSERT_TRUE(s.ok()) << s.message;
    EXPECT_TRUE(r.live);
    EXPECT_FALSE(fs::exists(User() / kId));
    ASSERT_TRUE(first.manager.SetEnabled(kId, false).ok());
  }
  Launch second(Dirs());
  ASSERT_TRUE(second.manager.Remove(kId).ok());  // not loaded: unlinked at once
  EXPECT_EQ(second.manager.Find(kId), nullptr);
  EXPECT_TRUE(fs::exists(dev / fv::desk::kPackageManifestName));
}

TEST_F(PackagesTest, ReservedIdsLoadOnlyFromTheSystemDirectory) {
  WritePackage(User(), "fv.extras", Manifest("fv.extras"));
  WritePackage(System(), "peregrine.extras", Manifest("peregrine.extras", "1", R"({"builtin": "test.pkg.rings"})", "rg2"));
  Launch launch(Dirs());
  EXPECT_EQ(launch.manager.Find("fv.extras")->state, PackageState::kFailed);
  EXPECT_EQ(launch.manager.Find("peregrine.extras")->state, PackageState::kLoaded);
}

TEST_F(PackagesTest, ADirectoryNotNamedAfterItsIdFails) {
  WritePackage(User(), "rings", Manifest());
  Launch launch(Dirs());
  const PackageInfo* p = launch.manager.Find(kId);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->state, PackageState::kFailed);
  EXPECT_NE(p->reason.find("named after the package id"), std::string::npos) << p->reason;
}

// MARK: Loading

TEST_F(PackagesTest, KindsAndVersionsThisBuildLacksAreUnavailable) {
  WritePackage(User(), "com.example.clib",
               Manifest("com.example.clib", "1", R"({"library": "rings"})", "r1",
                        R"("requires": {"plugin_api": 1}, "libraries": {"macos": "lib/r.dylib",
                           "linux-x86_64": "lib/r.so", "linux-aarch64": "lib/r.so",
                           "windows-x86_64": "lib/r.dll"},)"));
  WritePackage(User(), "com.example.api2",
               Manifest("com.example.api2", "1", R"({"library": "rings"})", "r2",
                        R"("requires": {"plugin_api": 2}, "libraries": {"macos": "lib/r.dylib"},)"));
  WritePackage(User(), "com.example.future",
               Manifest("com.example.future", "1", R"({"builtin": "test.pkg.rings"})", "r3",
                        R"("requires": {"peregrine": "9.0"},)"));
  Launch launch(Dirs(), "1.2.0");
  auto expect = [&](const char* id, const char* phrase) {
    const PackageInfo* p = launch.manager.Find(id);
    ASSERT_NE(p, nullptr) << id;
    EXPECT_EQ(p->state, PackageState::kUnavailable) << id;
    EXPECT_NE(p->reason.find(phrase), std::string::npos) << id << ": " << p->reason;
  };
  expect("com.example.api2", "plugin API 2");
  expect("com.example.future", "needs Peregrine 9.0");
  // C libraries are supported; one whose file is missing fails to load.
  const PackageInfo* clib = launch.manager.Find("com.example.clib");
  ASSERT_NE(clib, nullptr);
  EXPECT_EQ(clib->state, PackageState::kFailed);
  EXPECT_EQ(launch.types.size(), 0u);
  EXPECT_EQ(launch.tools.size(), 0u);
}

TEST_F(PackagesTest, APackageLoadsWholeOrNotAtAll) {
  const std::string text = R"({"format":1,"id":"com.example.half","name":"Half","version":"1",
    "overlays":[{"id":"com.example.half.a","display_name":"A","implementation":{"builtin":"test.pkg.rings"}},
                {"id":"com.example.half.b","display_name":"B","implementation":{"builtin":"test.pkg.missing"}}]})";
  WritePackage(User(), "com.example.half", text);
  Launch launch(Dirs());
  const PackageInfo* p = launch.manager.Find("com.example.half");
  EXPECT_EQ(p->state, PackageState::kFailed);
  EXPECT_NE(p->reason.find("test.pkg.missing"), std::string::npos) << p->reason;
  EXPECT_EQ(launch.types.Find("com.example.half.a"), nullptr);
}

TEST_F(PackagesTest, AnExtensionAlreadyClaimedFailsTheLaterPackage) {
  WritePackage(User(), "com.example.a", Manifest("com.example.a"));
  WritePackage(User(), "com.example.b", Manifest("com.example.b"));
  Launch launch(Dirs());
  EXPECT_EQ(launch.manager.Find("com.example.a")->state, PackageState::kLoaded);
  const PackageInfo* b = launch.manager.Find("com.example.b");
  EXPECT_EQ(b->state, PackageState::kFailed);
  EXPECT_NE(b->reason.find(".rng files already open as Range Rings"), std::string::npos) << b->reason;
}

// MARK: Desk

TEST_F(PackagesTest, OverlayManagerIsEnabledOnlyWhenTheShellHasOne) {
  fv::desk::FakeDesk fake;
  fv::desk::Desk& desk = fake.desk();
  const fv::desk::Command* c = desk.commands().Find("tools.overlay_manager");
  ASSERT_NE(c, nullptr);
  EXPECT_FALSE(c->enabled());
  fake.shell().provides_overlay_manager = true;
  EXPECT_TRUE(c->enabled());
  ASSERT_TRUE(desk.Execute("tools.overlay_manager").ok());
  EXPECT_EQ(fake.shell().overlay_manager_shown, 1);
  EXPECT_TRUE(Contains(ItemIds(desk.menus().Find("tools")->items), "tools.overlay_manager"));
}

TEST_F(PackagesTest, AFailingToolIsReportedAndACancelIsNot) {
  fv::desk::FakeDesk fake;
  fv::desk::Desk& desk = fake.desk();
  fv::desk::ToolDesc fails{"test.fails", "Fails", "", "", "", "", [](fv::desk::ToolContext&) {
                             return fv::Status::Error(fv::kIoError, "disk full");
                           }};
  fv::desk::ToolDesc cancels{"test.cancels", "Cancels", "", "", "", "", [](fv::desk::ToolContext&) {
                               return fv::Status::Error(fv::kInterrupted, "cancelled");
                             }};
  ASSERT_TRUE(desk.tools().Register(fails).ok());
  ASSERT_TRUE(desk.tools().Register(cancels).ok());
  desk.Refresh();
  ASSERT_TRUE(desk.Execute("tools.run.test.cancels").ok());
  EXPECT_TRUE(fake.shell().errors.empty());
  ASSERT_TRUE(desk.Execute("tools.run.test.fails").ok());
  ASSERT_EQ(fake.shell().errors.size(), 1u);
}

TEST_F(PackagesTest, ToolConfigExcludesAndAddsTools) {
  const fs::path pgpkg = MakePgpkg("rings", Manifest());
  const fs::path config = Root() / "tools.json";
  std::ofstream(config) << R"({"format": 1,
    "exclude": [")" << kId << R"(.*"],
    "tools": [{"id": "user.hello", "title": "Hello", "submenu": "Mine",
               "implementation": {"builtin": "test.pkg.hello"}}]})";
  fv::desk::FakeDesk fake;
  fv::desk::Desk& desk = fake.desk();
  desk.LoadPackages(Dirs());
  InstallResult r;
  ASSERT_TRUE(desk.InstallPackage(pgpkg.string(), &r).ok());
  ASSERT_TRUE(desk.LoadToolConfig(config.string()).ok());

  const std::vector<std::string> ids = ItemIds(desk.menus().Find("tools")->items);
  EXPECT_FALSE(Contains(ids, std::string("tools.run.") + kId + ".hello"));
  EXPECT_EQ(desk.commands().Find(std::string("tools.run.") + kId + ".hello"), nullptr);
  // The package still loads; only its menu entry is hidden.
  EXPECT_EQ(desk.packages()->Find(kId)->state, PackageState::kLoaded);
  ASSERT_TRUE(desk.Execute("tools.run.user.hello").ok());
  EXPECT_EQ(hello_runs_, 1);

  // A package installed later is hidden too.
  EXPECT_TRUE(desk.tool_config().Excludes(std::string(kId) + ".other"));
  EXPECT_FALSE(desk.tool_config().Excludes(std::string(kId) + "x.hello"));
}

TEST_F(PackagesTest, ToolConfigProblems) {
  fv::desk::ToolConfig c;
  EXPECT_TRUE(fv::desk::ReadToolConfig((Root() / "absent.json").string(), &c).ok());
  EXPECT_TRUE(c.exclude.empty());
  auto parse = [](const std::string& text) {
    fv::desk::ToolConfig out;
    return fv::desk::ParseToolConfig(text, "tools.json", &out);
  };
  EXPECT_TRUE(parse(R"({"exclude": ["a.b", "c.*"]})").ok());
  EXPECT_FALSE(parse("[1]").ok());
  EXPECT_FALSE(parse(R"({"format": 2})").ok());
  EXPECT_FALSE(parse(R"({"exclude": "a.b"})").ok());
  EXPECT_FALSE(parse(R"({"tools": [{"id": "mine.x", "title": "X",
                                    "implementation": {"builtin": "test.pkg.hello"}}]})").ok());
  EXPECT_FALSE(parse(R"({"tools": [{"id": "user.x", "title": "X",
                                    "implementation": {"library": "x"}}]})").ok());

  // An unknown builtin is reported and the rest of the file still applies.
  const fs::path config = Root() / "tools.json";
  std::ofstream(config) << R"({"exclude": ["user.gone"], "tools": [
    {"id": "user.gone", "title": "Gone", "implementation": {"builtin": "no.such.tool"}}]})";
  fv::desk::FakeDesk fake;
  EXPECT_EQ(fake.desk().LoadToolConfig(config.string()).code, fv::kNotFound);
  EXPECT_EQ(fake.desk().tool_config().exclude.size(), 1u);
  EXPECT_EQ(fv::desk::DefaultToolConfigPath(),
            (fs::path(fv::desk::DefaultUserSettingsPath()).parent_path() / "tools.json").string());
}

TEST(ToolRegistry, RefusesIncompleteAndDuplicateTools) {
  fv::desk::ToolRegistry r;
  auto ok = [](fv::desk::ToolContext&) { return fv::Status::Ok(); };
  EXPECT_FALSE(r.Register({"", "T", "", "", "", "", ok}).ok());
  EXPECT_FALSE(r.Register({"a.b", "", "", "", "", "", ok}).ok());
  EXPECT_FALSE(r.Register({"a.b", "T", "", "", "", "", nullptr}).ok());
  EXPECT_TRUE(r.Register({"a.b", "T", "", "", "", "", ok}).ok());
  EXPECT_FALSE(r.Register({"a.b", "T2", "", "", "", "", ok}).ok());
  EXPECT_EQ(r.size(), 1u);
}

TEST(PackageDirs, UserDirectoryIsPerPlatform) {
  const std::string dir = fv::desk::DefaultUserPackageDir();
  ASSERT_FALSE(dir.empty());
  EXPECT_EQ(fs::path(dir).filename(), "packages");
#if defined(__APPLE__) || defined(_WIN32)
  EXPECT_EQ(fs::path(dir).parent_path(), fs::path(fv::desk::DefaultUserSettingsPath()).parent_path());
#endif
}

}  // namespace
