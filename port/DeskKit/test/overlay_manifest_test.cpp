// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <memory>

#include "fv_desk_fake.h"
#include "fv_desk_overlay_manifest.h"
#include "fv_desk_user_settings.h"
#include "range_rings.h"
#include "scratch.h"

using fv::desk::BuiltinOverlay;
using fv::desk::OverlayManifestEntry;
using fv::desk::ParseOverlayManifest;
using fv::desk::test::RangeRings;
using fv::desk::test::Scratch;

namespace {

constexpr char kRingsManifest[] = R"json({
  "id": "test.rings",
  "display_name": "Range Rings",
  "icon": "rangerings.svg",
  "kind": "file",
  "display_order": 40,
  "file": { "extension": "rng", "filters": [["Range Rings (*.rng)", "*.rng"]] },
  "implementation": { "builtin": "test.rings" }
})json";

/// Registers the RangeRings builtin for one test and removes it after.
class BuiltinRings {
 public:
  explicit BuiltinRings(const std::string& name = "test.rings") : name_(name) {
    BuiltinOverlay b;
    b.factory = [] { return std::make_shared<RangeRings>(); };
    EXPECT_TRUE(fv::desk::RegisterBuiltinOverlay(name_, std::move(b)).ok());
  }
  ~BuiltinRings() { fv::desk::UnregisterBuiltinOverlay(name_); }

 private:
  std::string name_;
};

void WriteFile(const std::string& path, const std::string& text) {
  std::ofstream(path, std::ios::binary) << text;
}

std::vector<OverlayManifestEntry> Parse(const std::string& text, fv::Status* s,
                                        const std::string& base = "/bundle/overlays") {
  std::vector<OverlayManifestEntry> out;
  *s = ParseOverlayManifest(text, "test.json", base, &out);
  return out;
}

/// The command ids an Overlay-menu submenu titled `title` lists.
std::vector<std::string> SubmenuIds(const fv::desk::Desk& desk, const std::string& title) {
  std::vector<std::string> ids;
  const fv::desk::Menu* m = desk.menus().Find("overlay");
  if (m == nullptr) return ids;
  for (const fv::desk::MenuItem& item : m->items)
    if (item.label == title)
      for (const fv::desk::MenuItem& c : item.children) ids.push_back(c.command_id);
  return ids;
}

// MARK: Parsing

TEST(OverlayManifest, ParsesAFileEntry) {
  fv::Status s;
  const auto entries = Parse(kRingsManifest, &s);
  ASSERT_TRUE(s.ok()) << s.message;
  ASSERT_EQ(entries.size(), 1u);
  const fv::app::OverlayTypeDesc& d = entries[0].desc;
  EXPECT_EQ(d.id, "test.rings");
  EXPECT_EQ(d.display_name, "Range Rings");
  EXPECT_EQ(d.default_display_order, 40);
  ASSERT_TRUE(d.file.has_value());
  EXPECT_EQ(d.file->default_extension, "rng");
  ASSERT_EQ(d.file->open_filters.size(), 1u);
  EXPECT_EQ(d.file->save_filters, d.file->open_filters);
  EXPECT_FALSE(d.factory);
  EXPECT_EQ(entries[0].implementation_kind, "builtin");
  EXPECT_EQ(entries[0].implementation_name, "test.rings");
  EXPECT_EQ(entries[0].source, "test.json");
}

TEST(OverlayManifest, IconFileResolvesBesideTheManifestAndANameStaysSymbolic) {
  fv::Status s;
  auto e = Parse(kRingsManifest, &s);
  ASSERT_TRUE(s.ok());
  EXPECT_EQ(e[0].desc.icon, "/bundle/overlays/rangerings.svg");
  e = Parse(R"({"id":"a.b","icon":"grid","implementation":{"builtin":"x"}})", &s);
  ASSERT_TRUE(s.ok());
  EXPECT_EQ(e[0].desc.icon, "grid");
}

TEST(OverlayManifest, KindFollowsFromFileAndDefaultsAreTheDescriptors) {
  fv::Status s;
  auto e = Parse(R"({"overlays":[
      {"id":"a.static","implementation":{"builtin":"x"}},
      {"id":"a.file","display_name":"Tracks","file":{"extension":".trk"},
       "implementation":{"builtin":"y"}}]})", &s);
  ASSERT_TRUE(s.ok()) << s.message;
  ASSERT_EQ(e.size(), 2u);
  EXPECT_FALSE(e[0].desc.file.has_value());
  EXPECT_TRUE(e[0].desc.user_controllable);
  EXPECT_EQ(e[0].desc.default_opacity, 100);
  ASSERT_TRUE(e[1].desc.file.has_value());
  EXPECT_EQ(e[1].desc.file->default_extension, "trk");
  // No filters given: one is made from the display name and extension.
  ASSERT_EQ(e[1].desc.file->open_filters.size(), 1u);
  EXPECT_EQ(e[1].desc.file->open_filters[0].first, "Tracks (*.trk)");
  EXPECT_EQ(e[1].desc.file->open_filters[0].second, "*.trk");
}

TEST(OverlayManifest, BadDocumentsAreRefusedWhole) {
  const char* bad[] = {
      "not json",
      "[]",
      R"({"display_name":"no id","implementation":{"builtin":"x"}})",
      R"({"id":"a.b"})",
      R"({"id":"a.b","implementation":{"builtin":""}})",
      R"({"id":"a.b","implementation":{"dll":"x"}})",
      R"({"id":"a.b","implementation":{"builtin":"x","python":"m:C"}})",
      R"({"id":"a.b","kind":"static","file":{"extension":"x"},"implementation":{"builtin":"x"}})",
      R"({"id":"a.b","kind":"file","implementation":{"builtin":"x"}})",
      R"({"id":"a.b","kind":"layer","implementation":{"builtin":"x"}})",
      R"({"id":"a.b","opacity":150,"implementation":{"builtin":"x"}})",
      R"({"id":"a.b","display_order":"top","implementation":{"builtin":"x"}})",
      R"({"id":"a.b","file":{"extension":"x","filters":["*.x"]},"implementation":{"builtin":"x"}})",
      R"({"overlays":[{"id":"a.b","implementation":{"builtin":"x"}},
                      {"id":"a.b","implementation":{"builtin":"y"}}]})",
      R"({"overlays":[{"id":"a.ok","implementation":{"builtin":"x"}}, 7]})",
  };
  for (const char* text : bad) {
    std::vector<OverlayManifestEntry> out(1);
    const fv::Status s = ParseOverlayManifest(text, "bad.json", "", &out);
    EXPECT_EQ(s.code, fv::kInvalidArg) << text;
    EXPECT_NE(s.message.find("bad.json"), std::string::npos) << s.message;
    EXPECT_EQ(out.size(), 1u) << "a refused document adds nothing: " << text;
  }
}

// MARK: Builtins and registration

TEST(OverlayManifest, BuiltinTableRefusesDuplicatesAndNullFactories) {
  BuiltinRings rings("test.dup");
  BuiltinOverlay again;
  again.factory = [] { return std::make_shared<RangeRings>(); };
  EXPECT_EQ(fv::desk::RegisterBuiltinOverlay("test.dup", again).code, fv::kInvalidArg);
  EXPECT_EQ(fv::desk::RegisterBuiltinOverlay("test.null", BuiltinOverlay{}).code, fv::kInvalidArg);
  EXPECT_EQ(fv::desk::RegisterBuiltinOverlay("", again).code, fv::kInvalidArg);
  EXPECT_NE(fv::desk::FindBuiltinOverlay("test.dup"), nullptr);
  EXPECT_EQ(fv::desk::FindBuiltinOverlay("test.null"), nullptr);
}

TEST(OverlayManifest, RegistrationResolvesTheBuiltin) {
  BuiltinRings rings;
  fv::Status s;
  const auto e = Parse(kRingsManifest, &s);
  ASSERT_TRUE(s.ok());
  fv::app::OverlayTypeRegistry reg;
  ASSERT_TRUE(fv::desk::RegisterManifestEntry(reg, e[0]).ok());
  const fv::app::OverlayTypeDesc* d = reg.Find("test.rings");
  ASSERT_NE(d, nullptr);
  ASSERT_TRUE(d->factory);
  EXPECT_NE(d->factory()->AsProperties(), nullptr);
  EXPECT_EQ(reg.FindByExtension(".RNG"), d);
  // The same id again is the registry's duplicate refusal.
  EXPECT_FALSE(fv::desk::RegisterManifestEntry(reg, e[0]).ok());
}

TEST(OverlayManifest, UnknownBuiltinAndLaterKindsAreNotRegistered) {
  fv::Status s;
  auto e = Parse(R"({"id":"a.b","implementation":{"builtin":"nobody"}})", &s);
  ASSERT_TRUE(s.ok());
  fv::app::OverlayTypeRegistry reg;
  EXPECT_EQ(fv::desk::RegisterManifestEntry(reg, e[0]).code, fv::kNotFound);
  e = Parse(R"({"id":"a.c","implementation":{"library":"librings.dylib"}})", &s);
  ASSERT_TRUE(s.ok());
  EXPECT_EQ(fv::desk::RegisterManifestEntry(reg, e[0]).code, fv::kUnsupported);
  e = Parse(R"({"id":"a.d","implementation":{"python":"rings:Rings"}})", &s);
  ASSERT_TRUE(s.ok());
  EXPECT_EQ(fv::desk::RegisterManifestEntry(reg, e[0]).code, fv::kUnsupported);
  EXPECT_EQ(reg.size(), 0u);
}

TEST(OverlayManifest, DirectoryLoadKeepsGoingPastABadFile) {
  BuiltinRings rings;
  Scratch dir;
  WriteFile(dir.Path("a-broken.json"), "{ nope");
  WriteFile(dir.Path("b-rings.json"), kRingsManifest);
  WriteFile(dir.Path("c-ghost.json"), R"({"id":"t.ghost","implementation":{"builtin":"ghost"}})");
  WriteFile(dir.Path("notes.txt"), "not a manifest");

  fv::app::OverlayTypeRegistry reg;
  std::vector<std::string> warnings;
  const auto ids = fv::desk::RegisterOverlayManifests(
      reg, {dir.Path("missing-subdir"), dir.Path("")}, &warnings);
  ASSERT_EQ(ids, std::vector<std::string>{"test.rings"});
  ASSERT_EQ(warnings.size(), 2u);
  EXPECT_NE(warnings[0].find("a-broken.json"), std::string::npos) << warnings[0];
  EXPECT_NE(warnings[1].find("ghost"), std::string::npos) << warnings[1];
  // A file icon is resolved beside the manifest file.
  EXPECT_EQ(reg.Find("test.rings")->icon,
            (std::filesystem::path(dir.Path("")) / "rangerings.svg").lexically_normal().string());
}

TEST(OverlayManifest, UserDirectorySitsBesideTheUserSettings) {
  const std::string dir = fv::desk::DefaultUserOverlayManifestDir();
  if (dir.empty()) GTEST_SKIP() << "no home directory";
  EXPECT_EQ(std::filesystem::path(dir).filename(), "overlays");
  EXPECT_EQ(std::filesystem::path(dir).parent_path(),
            std::filesystem::path(fv::desk::DefaultUserSettingsPath()).parent_path());
}

// MARK: The plan's proof

// A test-only overlay registered by JSON alone shows up in menus, options and
// file-open dispatch (port/desktop-plan.md §5, DK3).
TEST(OverlayManifest, JsonAloneReachesMenusOptionsAndFileOpen) {
  BuiltinRings rings;
  Scratch dir;
  WriteFile(dir.Path("rings.json"), kRingsManifest);
  fv::desk::FakeDesk fake;
  fv::desk::Desk& desk = fake.desk();
  ASSERT_EQ(desk.types().Find("test.rings"), nullptr);
  const int menus_before = fake.shell().menus_changed;

  const auto ids = desk.LoadOverlayManifests({dir.Path("")});
  ASSERT_EQ(ids, std::vector<std::string>{"test.rings"});
  EXPECT_GT(fake.shell().menus_changed, menus_before);

  // Menus: Overlay ▸ New lists it.
  const auto news = SubmenuIds(desk, "New");
  EXPECT_NE(std::find(news.begin(), news.end(), "overlay.new.test.rings"), news.end());

  // Options: Overlay ▸ Options… hands the shell a model with its page.
  ASSERT_TRUE(desk.Execute("overlay.options").ok());
  ASSERT_NE(fake.shell().options_shown, nullptr);
  const fv::desk::OptionsPage* page = fake.shell().options_shown->Page("test.rings");
  ASSERT_NE(page, nullptr);
  EXPECT_EQ(page->title(), "Range Rings");
  ASSERT_NE(page->Field("export_dir"), nullptr);
  EXPECT_EQ(page->Field("export_dir")->spec.path_kind, fv::app::PathKind::kDirectory);

  // File-open dispatch: the chooser offers *.rng and the file opens as rings.
  fake.shell().files_to_open = {dir.Path("north.rng")};
  ASSERT_TRUE(desk.Execute("overlay.open").ok());
  const auto& filters = fake.shell().last_open_chooser.open_filters;
  EXPECT_TRUE(std::any_of(filters.begin(), filters.end(),
                          [](const auto& f) { return f.second == "*.rng"; }));
  const auto opened = desk.overlays().FirstOfType("test.rings");
  ASSERT_NE(opened, nullptr);
  EXPECT_EQ(static_cast<RangeRings&>(*opened).opened,
            std::vector<std::string>{dir.Path("north.rng")});
  EXPECT_TRUE(fake.shell().errors.empty());
}

}  // namespace
