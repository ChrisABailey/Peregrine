// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// App layer A1: overlay types as data, and capabilities by accessor.
// Covers fvkit-app-plan-COMPLETE.md §3a/§3b -- register / duplicate-reject /
// by-extension / static-vs-file, the capability accessors' nullptr default,
// Persistence's change-only notification, and the grid toggled through its
// descriptor over the existing L4 stack.

#include "fvkit/app/capabilities.h"
#include "fvkit/app/editor.h"
#include "fvkit/app/type_registry.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "fvkit/overlay/grid.h"
#include "fvkit/overlay/manager.h"

namespace {

using fv::app::OverlayTypeDesc;
using fv::app::OverlayTypeRegistry;

// A minimal overlay for descriptors that only need to make SOMETHING.
class PlainOverlay : public fv::Overlay {
 public:
  PlainOverlay() : Overlay("plain") {}
};

OverlayTypeDesc StaticDesc(const std::string& id) {
  OverlayTypeDesc d;
  d.id = id;
  d.display_name = id;
  d.factory = [] { return std::make_shared<PlainOverlay>(); };
  return d;
}

OverlayTypeDesc FileDesc(const std::string& id, const std::string& ext) {
  OverlayTypeDesc d = StaticDesc(id);
  fv::app::FileTypeDesc f;
  f.default_extension = ext;
  f.open_filters = {{"Test Files (*." + ext + ")", "*." + ext}};
  f.save_filters = f.open_filters;
  d.file = f;
  return d;
}

class NullEditor : public fv::app::OverlayEditor {
 public:
  fv::Status Activate() override { return fv::Status::Ok(); }
  fv::Status Deactivate() override { return fv::Status::Ok(); }
};

// ---------------------------------------------------------------------------
// §3a — the registry
// ---------------------------------------------------------------------------

TEST(OverlayTypeRegistry, RegistersAndFindsByIdInRegistrationOrder) {
  OverlayTypeRegistry r;
  ASSERT_TRUE(r.Register(StaticDesc("fv.b")).ok());
  ASSERT_TRUE(r.Register(StaticDesc("fv.a")).ok());
  ASSERT_TRUE(r.Register(FileDesc("fv.c", "rte")).ok());

  EXPECT_EQ(r.size(), 3u);
  ASSERT_NE(r.Find("fv.a"), nullptr);
  EXPECT_EQ(r.Find("fv.a")->id, "fv.a");
  EXPECT_EQ(r.Find("fv.nope"), nullptr);

  // Registration order, NOT sorted -- menus and the Tools list are built in
  // the order the shell registered, which is the order it wants them shown.
  std::vector<std::string> ids;
  for (const auto* d : r.All()) ids.push_back(d->id);
  EXPECT_EQ(ids, (std::vector<std::string>{"fv.b", "fv.a", "fv.c"}));
}

TEST(OverlayTypeRegistry, DescriptorPointersSurviveLaterRegistrations) {
  // Menus, stack rows and the session file all hold a descriptor pointer, so
  // growth must not move one. (This is why order_ is unique_ptrs.)
  OverlayTypeRegistry r;
  ASSERT_TRUE(r.Register(StaticDesc("fv.first")).ok());
  const OverlayTypeDesc* first = r.Find("fv.first");
  ASSERT_NE(first, nullptr);
  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(r.Register(StaticDesc("fv.filler" + std::to_string(i))).ok());
  }
  EXPECT_EQ(r.Find("fv.first"), first);
  EXPECT_EQ(first->id, "fv.first");
}

TEST(OverlayTypeRegistry, RejectsDuplicateEmptyIdAndMissingFactory) {
  OverlayTypeRegistry r;
  ASSERT_TRUE(r.Register(StaticDesc("fv.a")).ok());

  fv::Status dup = r.Register(StaticDesc("fv.a"));
  EXPECT_FALSE(dup.ok());
  EXPECT_EQ(dup.code, fv::kInvalidArg);
  EXPECT_NE(dup.message.find("already registered"), std::string::npos);

  EXPECT_FALSE(r.Register(StaticDesc("")).ok());

  // A type with no factory would fail later, at the point where the user
  // clicked something; it fails here instead.
  OverlayTypeDesc no_factory;
  no_factory.id = "fv.broken";
  fv::Status s = r.Register(std::move(no_factory));
  EXPECT_FALSE(s.ok());
  EXPECT_NE(s.message.find("factory"), std::string::npos);

  EXPECT_EQ(r.size(), 1u);  // nothing partial got in
}

TEST(OverlayTypeRegistry, FindByExtensionIgnoresCaseAndALeadingDot) {
  OverlayTypeRegistry r;
  ASSERT_TRUE(r.Register(StaticDesc("fv.grid_like")).ok());
  ASSERT_TRUE(r.Register(FileDesc("fv.route", "rte")).ok());

  EXPECT_EQ(r.FindByExtension("rte"), r.Find("fv.route"));
  EXPECT_EQ(r.FindByExtension(".rte"), r.Find("fv.route"));
  EXPECT_EQ(r.FindByExtension("RTE"), r.Find("fv.route"));
  EXPECT_EQ(r.FindByExtension(".RtE"), r.Find("fv.route"));

  EXPECT_EQ(r.FindByExtension("shp"), nullptr);
  EXPECT_EQ(r.FindByExtension(""), nullptr);
  EXPECT_EQ(r.FindByExtension("."), nullptr);
}

TEST(OverlayTypeRegistry, FirstClaimantOfAnExtensionKeepsIt) {
  // So a plugin registered later cannot silently steal a built-in's files.
  OverlayTypeRegistry r;
  ASSERT_TRUE(r.Register(FileDesc("fv.route", "rte")).ok());
  ASSERT_TRUE(r.Register(FileDesc("plugin.route", "RTE")).ok());
  EXPECT_EQ(r.FindByExtension("rte"), r.Find("fv.route"));
}

TEST(OverlayTypeRegistry, StaticVersusFileIsTheOptionalFileDescriptor) {
  OverlayTypeRegistry r;
  ASSERT_TRUE(r.Register(StaticDesc("fv.static")).ok());
  ASSERT_TRUE(r.Register(FileDesc("fv.doc", "rte")).ok());

  EXPECT_TRUE(r.IsStatic("fv.static"));
  EXPECT_FALSE(r.IsFile("fv.static"));
  EXPECT_TRUE(r.IsFile("fv.doc"));
  EXPECT_FALSE(r.IsStatic("fv.doc"));

  // An unregistered id is neither -- both queries answer "no", so a caller
  // cannot infer a type exists from IsStatic being false.
  EXPECT_FALSE(r.IsStatic("fv.nope"));
  EXPECT_FALSE(r.IsFile("fv.nope"));
}

TEST(OverlayTypeRegistry, WithEditorsIsTheToolsMenuSource) {
  OverlayTypeRegistry r;
  ASSERT_TRUE(r.Register(StaticDesc("fv.plain")).ok());

  OverlayTypeDesc editable = FileDesc("fv.drawing", "drw");
  editable.editor_factory = [] { return std::make_unique<NullEditor>(); };
  ASSERT_TRUE(r.Register(std::move(editable)).ok());

  auto with = r.WithEditors();
  ASSERT_EQ(with.size(), 1u);
  EXPECT_EQ(with[0]->id, "fv.drawing");

  // And the factory actually makes one (A4 drives it).
  auto ed = with[0]->editor_factory();
  ASSERT_NE(ed, nullptr);
  EXPECT_TRUE(ed->Activate().ok());
  EXPECT_EQ(ed->DefaultCursor(), fv::app::CursorId::kCrosshair);
  EXPECT_TRUE(ed->AutoEnterOnCreate());
  EXPECT_FALSE(ed->UiConstraints().requires_north_up);
}

// ---------------------------------------------------------------------------
// The grid as the first built-in static type, toggled through its descriptor
// ---------------------------------------------------------------------------

TEST(BuiltinOverlayTypes, GridIsRegisteredAsAStaticTypeWithAWorkingFactory) {
  OverlayTypeRegistry r;
  ASSERT_TRUE(fv::app::RegisterBuiltinOverlayTypes(r).ok());

  const OverlayTypeDesc* grid = r.Find(fv::app::kGridTypeId);
  ASSERT_NE(grid, nullptr);
  EXPECT_TRUE(r.IsStatic(fv::app::kGridTypeId));
  EXPECT_FALSE(grid->display_name.empty());  // empty would hide it from menus
  EXPECT_FALSE(grid->file.has_value());
  EXPECT_EQ(grid->editor_factory, nullptr);  // nothing to edit on a graticule
  EXPECT_FALSE(grid->is_top_most);

  auto made = grid->factory();
  ASSERT_NE(made, nullptr);
  EXPECT_NE(dynamic_cast<fv::GridOverlay*>(made.get()), nullptr);

  // Registering the built-ins twice is an error, not a silent no-op.
  EXPECT_FALSE(fv::app::RegisterBuiltinOverlayTypes(r).ok());
}

TEST(BuiltinOverlayTypes, ToggleStaticOverlayThroughTheDescriptor) {
  // The static-overlay rule (plan §1.2): exists => close, doesn't => create.
  // A3's OverlaySession::ToggleStatic owns this flow; here it is done by hand
  // over the L4 stack to prove the descriptor carries everything it needs.
  OverlayTypeRegistry registry;
  ASSERT_TRUE(fv::app::RegisterBuiltinOverlayTypes(registry).ok());
  const OverlayTypeDesc* desc = registry.Find(fv::app::kGridTypeId);
  ASSERT_NE(desc, nullptr);

  fv::OverlayManager stack;

  // A2 gives the stack FirstOfType; until then, the scan is spelled out.
  auto first_of_type = [&stack](const std::string& id) {
    for (const auto& o : stack.Overlays()) {
      if (o->type_id() == id) return o;
    }
    return std::shared_ptr<fv::Overlay>();
  };

  auto toggle = [&] {
    auto existing = first_of_type(desc->id);
    if (existing) return stack.Remove(existing);
    auto made = desc->factory();
    made->set_type_id(desc->id);
    return stack.Add(made);
  };

  EXPECT_EQ(stack.Overlays().size(), 0u);
  ASSERT_TRUE(toggle().ok());  // on
  ASSERT_EQ(stack.Overlays().size(), 1u);
  EXPECT_EQ(stack.Overlays()[0]->type_id(), fv::app::kGridTypeId);
  EXPECT_EQ(stack.Overlays()[0]->Name(), "grid");

  ASSERT_TRUE(toggle().ok());  // off
  EXPECT_EQ(stack.Overlays().size(), 0u);

  ASSERT_TRUE(toggle().ok());  // and on again
  EXPECT_EQ(stack.Overlays().size(), 1u);
}

TEST(Overlay, TypeIdIsEmptyUntilStamped) {
  // An ad-hoc overlay (a pyfvw subclass, say) has no registered type, and that
  // is legal -- R7: the app layer is additive.
  PlainOverlay o;
  EXPECT_TRUE(o.type_id().empty());
  o.set_type_id("user.drawing");
  EXPECT_EQ(o.type_id(), "user.drawing");
}

// ---------------------------------------------------------------------------
// §3b — capabilities by accessor (R2)
// ---------------------------------------------------------------------------

TEST(OverlayCapabilities, DefaultToNullSoAnOldOverlayIsStillLegal) {
  PlainOverlay o;
  EXPECT_EQ(o.AsPersistence(), nullptr);
  EXPECT_EQ(o.AsHitTest(), nullptr);
  EXPECT_EQ(o.AsSnapTo(), nullptr);
  EXPECT_EQ(o.AsContextMenu(), nullptr);
  EXPECT_EQ(o.AsRoutingOverrides(), nullptr);
  EXPECT_EQ(o.AsEditTarget(), nullptr);

  // The built-in grid draws and nothing else.
  fv::GridOverlay grid;
  EXPECT_EQ(grid.AsPersistence(), nullptr);
  EXPECT_EQ(grid.AsHitTest(), nullptr);
}

// An overlay that opts into three capabilities the way a real file overlay does.
class DocOverlay : public fv::Overlay,
                   public fv::app::Persistence,
                   public fv::app::EditTarget,
                   public fv::app::RoutingOverrides {
 public:
  DocOverlay() : Overlay("doc") {}

  fv::app::Persistence* AsPersistence() override { return this; }
  fv::app::EditTarget* AsEditTarget() override { return this; }
  fv::app::RoutingOverrides* AsRoutingOverrides() override { return this; }

  fv::Status FileNew() override {
    set_file_spec("Doc1");
    set_has_been_saved(false);
    set_dirty(false);
    return fv::Status::Ok();
  }
  fv::Status FileOpen(const std::string& spec) override {
    set_file_spec(spec);
    set_has_been_saved(true);
    set_dirty(false);
    return fv::Status::Ok();
  }
  fv::Status FileSaveAs(const std::string& spec, int format_index) override {
    last_format_ = format_index;
    set_file_spec(spec);
    set_has_been_saved(true);
    set_dirty(false);
    return fv::Status::Ok();
  }

  void EnterEditFocus() override { ++entered_; }
  void ReleaseEditFocus() override { ++released_; }
  bool WantsDirectRouting() const override { return direct_; }

  int last_format() const { return last_format_; }
  int entered() const { return entered_; }
  int released() const { return released_; }
  void set_direct(bool v) { direct_ = v; }

 private:
  int last_format_ = -1;
  int entered_ = 0, released_ = 0;
  bool direct_ = false;
};

TEST(OverlayCapabilities, AnOverlayReturnsItselfForWhatItImplements) {
  DocOverlay doc;
  fv::Overlay& as_overlay = doc;

  EXPECT_EQ(as_overlay.AsPersistence(), static_cast<fv::app::Persistence*>(&doc));
  EXPECT_EQ(as_overlay.AsEditTarget(), static_cast<fv::app::EditTarget*>(&doc));
  EXPECT_EQ(as_overlay.AsRoutingOverrides(),
            static_cast<fv::app::RoutingOverrides*>(&doc));
  // ...and null for the ones it does not.
  EXPECT_EQ(as_overlay.AsHitTest(), nullptr);
  EXPECT_EQ(as_overlay.AsSnapTo(), nullptr);
  EXPECT_EQ(as_overlay.AsContextMenu(), nullptr);

  as_overlay.AsEditTarget()->ReleaseEditFocus();
  as_overlay.AsEditTarget()->EnterEditFocus();
  EXPECT_EQ(doc.released(), 1);
  EXPECT_EQ(doc.entered(), 1);

  EXPECT_FALSE(as_overlay.AsRoutingOverrides()->WantsDirectRouting());
  doc.set_direct(true);
  EXPECT_TRUE(as_overlay.AsRoutingOverrides()->WantsDirectRouting());

  // Undo/redo default to "cannot", so a UI greys them without asking twice.
  EXPECT_FALSE(as_overlay.AsEditTarget()->CanUndo());
  EXPECT_FALSE(as_overlay.AsEditTarget()->CanRedo());
}

TEST(Persistence, DocumentStateFollowsTheFileVerbs) {
  DocOverlay doc;
  fv::app::Persistence& p = doc;

  EXPECT_TRUE(p.file_spec().empty());
  EXPECT_FALSE(p.is_dirty());
  EXPECT_FALSE(p.has_been_saved());
  EXPECT_FALSE(p.is_read_only());
  EXPECT_FALSE(p.SupportsRevert());
  EXPECT_FALSE(p.Revert("x").ok());  // kUnsupported, not a crash

  ASSERT_TRUE(p.FileNew().ok());
  EXPECT_EQ(p.file_spec(), "Doc1");
  // A new document has a spec but has NOT reached disk -- which is what sends
  // its first Save through the Save As flow (A3).
  EXPECT_FALSE(p.has_been_saved());

  p.set_dirty(true);
  EXPECT_TRUE(p.is_dirty());

  ASSERT_TRUE(p.FileSaveAs("/tmp/doc.rte", 2).ok());
  EXPECT_EQ(doc.last_format(), 2);  // index into save_filters
  EXPECT_EQ(p.file_spec(), "/tmp/doc.rte");
  EXPECT_TRUE(p.has_been_saved());
  EXPECT_FALSE(p.is_dirty());
}

class RecordingPersistenceObserver : public fv::app::PersistenceObserver {
 public:
  void OnDirtyChanged(fv::app::Persistence&) override { ++dirty_calls; }
  void OnFileSpecChanged(fv::app::Persistence&) override { ++spec_calls; }
  int dirty_calls = 0, spec_calls = 0;
};

TEST(Persistence, NotifiesOnChangeOnly) {
  // "Dirty CHANGED" is the notification. A redundant set must not repaint an
  // overlay list, or every edit in a drawing session costs a UI refresh.
  DocOverlay doc;
  RecordingPersistenceObserver obs;
  fv::app::Persistence& p = doc;
  p.set_observer(&obs);

  p.set_dirty(true);
  p.set_dirty(true);
  p.set_dirty(true);
  EXPECT_EQ(obs.dirty_calls, 1);
  p.set_dirty(false);
  EXPECT_EQ(obs.dirty_calls, 2);

  p.set_file_spec("/tmp/a.rte");
  p.set_file_spec("/tmp/a.rte");
  EXPECT_EQ(obs.spec_calls, 1);
  p.set_file_spec("/tmp/b.rte");
  EXPECT_EQ(obs.spec_calls, 2);

  // Detaching stops the broadcast: A2 does this when an overlay leaves the
  // stack, and a dangling observer here would be a use-after-free.
  p.set_observer(nullptr);
  p.set_dirty(true);
  p.set_file_spec("/tmp/c.rte");
  EXPECT_EQ(obs.dirty_calls, 2);
  EXPECT_EQ(obs.spec_calls, 2);
}

}  // namespace
