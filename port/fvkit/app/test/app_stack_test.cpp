// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// App layer A2: the stack, grown in place (fvkit-app-plan-COMPLETE.md §3c).
//
// The code under test is fv::OverlayManager, which lives in fvkit/overlay --
// the plan's `stack.h` IS manager.h. The tests live here because A2 is what
// they pin: observers, the current overlay, the reorder verbs, insertion by
// the TYPE's display order, declutter, mouse capture and the three-phase
// event route. port/fvkit/test/overlay_test.cpp keeps the pre-A2 L4 semantics
// (draw bottom-up, route top-down, KeyEvent) and both must stay green -- A2 is
// additive by rule R7.
//
// These are FalconView's OverlayEventRouter_UnitTests / C_ovl_mgr behaviours
// re-derived as gtest, not transliterated: the Windows tests are MFC-bound.

#include "fvkit/app/capabilities.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/overlay/manager.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "fvkit/canvas/cpu_canvas.h"

namespace {

using fv::app::OverlayTypeDesc;
using fv::app::OverlayTypeRegistry;

// ---------------------------------------------------------------------------
// Doubles
// ---------------------------------------------------------------------------

// Records what it is offered and handles when told to.
class Recorder : public fv::Overlay {
 public:
  Recorder(std::string name, std::vector<std::string>* log, bool handles)
      : Overlay(std::move(name)), log_(log), handles_(handles) {}

  fv::Status OnDraw(const fv::MapProjection&, fv::ICanvas&) override {
    log_->push_back(Name() + ":draw");
    return fv::Status::Ok();
  }
  bool OnMouseDown(const fv::MouseEvent&) override {
    log_->push_back(Name() + ":down");
    return handles_;
  }
  bool OnMouseMove(const fv::MouseEvent&) override {
    log_->push_back(Name() + ":move");
    return handles_;
  }
  bool OnMouseUp(const fv::MouseEvent&) override {
    log_->push_back(Name() + ":up");
    return handles_;
  }
  bool OnKeyDown(const fv::KeyEvent&) override {
    log_->push_back(Name() + ":key");
    return handles_;
  }

  void set_handles(bool v) { handles_ = v; }

 private:
  std::vector<std::string>* log_;
  bool handles_;
};

// An overlay that claims the selection lock (~ is_selection_locked).
class LockingRecorder : public Recorder, public fv::app::RoutingOverrides {
 public:
  LockingRecorder(std::string name, std::vector<std::string>* log, bool handles)
      : Recorder(std::move(name), log, handles) {}

  fv::app::RoutingOverrides* AsRoutingOverrides() override { return this; }
  bool WantsDirectRouting() const override { return direct_; }
  void set_direct(bool v) { direct_ = v; }

 private:
  bool direct_ = true;
};

// A file overlay: enough Persistence to exercise the stack's broadcast and the
// (type, file spec) dedup lookup.
class DocOverlay : public fv::Overlay, public fv::app::Persistence {
 public:
  explicit DocOverlay(std::string name) : Overlay(std::move(name)) {}
  fv::app::Persistence* AsPersistence() override { return this; }

  fv::Status FileNew() override { return fv::Status::Ok(); }
  fv::Status FileOpen(const std::string& spec) override {
    set_file_spec(spec);
    set_has_been_saved(true);
    return fv::Status::Ok();
  }
  fv::Status FileSaveAs(const std::string& spec, int) override {
    set_file_spec(spec);
    return fv::Status::Ok();
  }
};

class RecordingStackObserver : public fv::StackObserver {
 public:
  void OverlayAdded(fv::Overlay& o) override {
    log.push_back("added:" + o.Name());
  }
  void OverlayRemoved(fv::Overlay& o) override {
    log.push_back("removed:" + o.Name());
  }
  void OverlayOrderChanged() override { log.push_back("order"); }
  void CurrentChanged(fv::Overlay* now, fv::Overlay* was) override {
    log.push_back("current:" + std::string(was ? was->Name() : "-") + ">" +
                  std::string(now ? now->Name() : "-"));
  }
  void OverlayDirtyChanged(fv::Overlay& o) override {
    log.push_back("dirty:" + o.Name());
  }
  void OverlayFileSpecChanged(fv::Overlay& o) override {
    log.push_back("spec:" + o.Name());
  }

  std::vector<std::string> log;
};

std::shared_ptr<Recorder> Make(const std::string& name,
                               std::vector<std::string>* log,
                               bool handles = false,
                               const std::string& type_id = "") {
  auto o = std::make_shared<Recorder>(name, log, handles);
  o->set_type_id(type_id);
  return o;
}

std::vector<std::string> NamesBottomUp(const fv::OverlayManager& m) {
  std::vector<std::string> out;
  for (const auto& o : m.Overlays()) out.push_back(o->Name());
  return out;
}

// A registry with three placement shapes: low, high and top-most.
OverlayTypeDesc Desc(const std::string& id, int order, bool top_most = false) {
  OverlayTypeDesc d;
  d.id = id;
  d.display_name = id;
  d.default_display_order = order;
  d.is_top_most = top_most;
  d.factory = [] { return std::shared_ptr<fv::Overlay>(); };
  return d;
}

void FillRegistry(OverlayTypeRegistry* r) {
  ASSERT_TRUE(r->Register(Desc("t.low", 100)).ok());
  ASSERT_TRUE(r->Register(Desc("t.high", 900)).ok());
  ASSERT_TRUE(r->Register(Desc("t.hud", 500, /*top_most=*/true)).ok());
}

// ---------------------------------------------------------------------------
// Insertion by display order (~ AddOverlayToStack)
// ---------------------------------------------------------------------------

TEST(StackInsertion, WithNoRegistryAddIsStillAPlainAppend) {
  // R7: a pyfvw user who never touches the app layer must see the pre-A2
  // stack. With no registry every overlay is order 0 and not top-most, and
  // "insert above the topmost overlay whose order is <= mine" is push_back.
  std::vector<std::string> log;
  fv::OverlayManager m;
  ASSERT_TRUE(m.Add(Make("a", &log)).ok());
  ASSERT_TRUE(m.Add(Make("b", &log)).ok());
  ASSERT_TRUE(m.Add(Make("c", &log)).ok());
  EXPECT_EQ(NamesBottomUp(m), (std::vector<std::string>{"a", "b", "c"}));
}

TEST(StackInsertion, ANewOverlayLandsWhereItsTypesDisplayOrderSays) {
  OverlayTypeRegistry r;
  FillRegistry(&r);
  std::vector<std::string> log;
  fv::OverlayManager m;
  m.SetTypeRegistry(&r);

  ASSERT_TRUE(m.Add(Make("high1", &log, false, "t.high")).ok());
  // 100 < 900, so the low one goes UNDER the high one even though it was
  // added later -- this is the whole point of the display order.
  ASSERT_TRUE(m.Add(Make("low1", &log, false, "t.low")).ok());
  EXPECT_EQ(NamesBottomUp(m), (std::vector<std::string>{"low1", "high1"}));

  // Equal orders stack newest-on-top (FalconView's "<=", the top-biased rule).
  ASSERT_TRUE(m.Add(Make("low2", &log, false, "t.low")).ok());
  EXPECT_EQ(NamesBottomUp(m),
            (std::vector<std::string>{"low1", "low2", "high1"}));

  // An untyped overlay is order 0, so it belongs above nothing here and sinks
  // to the bottom. That is legal -- an ad-hoc overlay has no registered type.
  ASSERT_TRUE(m.Add(Make("adhoc", &log)).ok());
  EXPECT_EQ(NamesBottomUp(m),
            (std::vector<std::string>{"adhoc", "low1", "low2", "high1"}));
}

TEST(StackInsertion, ATopMostOverlayStaysAboveEverythingThatIsNot) {
  // Its display order (500) is BELOW t.high's 900, and it still draws over it:
  // the top-most flag is not a display order, it is a separate band.
  OverlayTypeRegistry r;
  FillRegistry(&r);
  std::vector<std::string> log;
  fv::OverlayManager m;
  m.SetTypeRegistry(&r);

  ASSERT_TRUE(m.Add(Make("hud", &log, false, "t.hud")).ok());
  ASSERT_TRUE(m.Add(Make("high", &log, false, "t.high")).ok());
  ASSERT_TRUE(m.Add(Make("low", &log, false, "t.low")).ok());
  EXPECT_EQ(NamesBottomUp(m),
            (std::vector<std::string>{"low", "high", "hud"}));

  // A second top-most one joins its own band, above the first.
  ASSERT_TRUE(m.Add(Make("hud2", &log, false, "t.hud")).ok());
  EXPECT_EQ(NamesBottomUp(m),
            (std::vector<std::string>{"low", "high", "hud", "hud2"}));
}

// ---------------------------------------------------------------------------
// The current overlay
// ---------------------------------------------------------------------------

TEST(StackCurrent, AnOverlayThatLandsOnTopBecomesCurrentButAHudNeverDoes) {
  OverlayTypeRegistry r;
  FillRegistry(&r);
  std::vector<std::string> log;
  fv::OverlayManager m;
  m.SetTypeRegistry(&r);
  RecordingStackObserver obs;
  m.AddObserver(&obs);

  auto high = Make("high", &log, false, "t.high");
  ASSERT_TRUE(m.Add(high).ok());
  EXPECT_EQ(m.current(), high.get());

  // Lands underneath => current does not move.
  auto low = Make("low", &log, false, "t.low");
  ASSERT_TRUE(m.Add(low).ok());
  EXPECT_EQ(m.current(), high.get());

  // Lands on top, but the user does not work in a crosshair.
  auto hud = Make("hud", &log, false, "t.hud");
  ASSERT_TRUE(m.Add(hud).ok());
  EXPECT_EQ(m.Overlays().back(), hud);
  EXPECT_EQ(m.current(), high.get());

  EXPECT_EQ(obs.log, (std::vector<std::string>{"added:high", "current:->high",
                                               "added:low", "added:hud"}));
}

TEST(StackCurrent, AnOpenHudDoesNotFreezeTheCurrentOverlay) {
  // The one deviation from C_ovl_mgr in A2. FalconView asks "is this the head
  // of the list", and a top-most overlay always IS the head -- so with a
  // crosshair open, nothing added afterwards ever became current. The rule
  // means "topmost of the overlays the user works in", and reads identically
  // whenever no top-most overlay is open.
  OverlayTypeRegistry r;
  FillRegistry(&r);
  std::vector<std::string> log;
  fv::OverlayManager m;
  m.SetTypeRegistry(&r);

  auto hud = Make("hud", &log, false, "t.hud");
  ASSERT_TRUE(m.Add(hud).ok());
  EXPECT_EQ(m.current(), nullptr);  // a HUD is never current

  auto high = Make("high", &log, false, "t.high");
  ASSERT_TRUE(m.Add(high).ok());
  ASSERT_EQ(m.Overlays().back(), hud);  // the HUD is still literally on top
  EXPECT_EQ(m.current(), high.get());
}

TEST(StackCurrent, MakeCurrentBroadcastsOnlyOnAChangeAndNullClears) {
  std::vector<std::string> log;
  fv::OverlayManager m;
  RecordingStackObserver obs;
  auto a = Make("a", &log);
  auto b = Make("b", &log);
  ASSERT_TRUE(m.Add(a).ok());
  ASSERT_TRUE(m.Add(b).ok());
  m.AddObserver(&obs);  // after the adds, so only MakeCurrent shows up

  ASSERT_TRUE(m.MakeCurrent(a).ok());
  ASSERT_TRUE(m.MakeCurrent(a).ok());  // no second event
  EXPECT_EQ(m.current(), a.get());
  ASSERT_TRUE(m.MakeCurrent(nullptr).ok());
  EXPECT_EQ(m.current(), nullptr);
  EXPECT_EQ(obs.log, (std::vector<std::string>{"current:b>a", "current:a>-"}));

  // An overlay that is not in the stack cannot be made current.
  auto stranger = Make("stranger", &log);
  EXPECT_EQ(m.MakeCurrent(stranger).code, fv::kNotFound);
  EXPECT_EQ(m.current(), nullptr);
}

TEST(StackCurrent, RemovingTheCurrentOverlayDropsToTheOneBelowIt) {
  OverlayTypeRegistry r;
  FillRegistry(&r);
  std::vector<std::string> log;
  fv::OverlayManager m;
  m.SetTypeRegistry(&r);

  auto low = Make("low", &log, false, "t.low");
  auto high = Make("high", &log, false, "t.high");
  auto hud = Make("hud", &log, false, "t.hud");
  ASSERT_TRUE(m.Add(low).ok());
  ASSERT_TRUE(m.Add(high).ok());
  ASSERT_TRUE(m.Add(hud).ok());
  ASSERT_EQ(m.current(), high.get());

  ASSERT_TRUE(m.Remove(high).ok());
  // Not the hud, which is topmost in the stack but never current.
  EXPECT_EQ(m.current(), low.get());

  ASSERT_TRUE(m.Remove(low).ok());
  EXPECT_EQ(m.current(), nullptr);  // only the hud is left
}

// ---------------------------------------------------------------------------
// Observers
// ---------------------------------------------------------------------------

TEST(StackObservers, RemovedFiresAfterTheRemovalAndBeforeTheOverlayDies) {
  // The contract that makes an overlay-list row implementable: the stack it
  // reads is already correct, and the overlay it is handed is still alive.
  class Checker : public fv::StackObserver {
   public:
    explicit Checker(fv::OverlayManager* m) : m_(m) {}
    void OverlayRemoved(fv::Overlay& o) override {
      name_seen = o.Name();
      size_seen = m_->Overlays().size();
    }
    fv::OverlayManager* m_;
    std::string name_seen;
    size_t size_seen = 999;
  };

  std::vector<std::string> log;
  fv::OverlayManager m;
  Checker checker(&m);
  m.AddObserver(&checker);
  auto a = Make("a", &log);
  ASSERT_TRUE(m.Add(a).ok());
  ASSERT_TRUE(m.Add(Make("b", &log)).ok());

  ASSERT_TRUE(m.Remove(a).ok());
  EXPECT_EQ(checker.name_seen, "a");
  EXPECT_EQ(checker.size_seen, 1u);
}

TEST(StackObservers, AnObserverMayDetachItselfFromInsideItsOwnCallback) {
  // Notification walks a copy. An overlay-list row that closes on the event it
  // is handling would otherwise invalidate the iterator underneath us.
  class SelfDetaching : public fv::StackObserver {
   public:
    explicit SelfDetaching(fv::OverlayManager* m) : m_(m) {}
    void OverlayAdded(fv::Overlay&) override {
      ++calls;
      m_->RemoveObserver(this);
    }
    fv::OverlayManager* m_;
    int calls = 0;
  };

  std::vector<std::string> log;
  fv::OverlayManager m;
  SelfDetaching obs(&m);
  m.AddObserver(&obs);
  ASSERT_TRUE(m.Add(Make("a", &log)).ok());
  ASSERT_TRUE(m.Add(Make("b", &log)).ok());
  EXPECT_EQ(obs.calls, 1);

  // Adding the same observer twice is a no-op, not a double broadcast.
  RecordingStackObserver twice;
  m.AddObserver(&twice);
  m.AddObserver(&twice);
  ASSERT_TRUE(m.Add(Make("c", &log)).ok());
  EXPECT_EQ(twice.log.size(), 2u);  // added:c + current:b>c
}

TEST(StackObservers, DirtyAndFileSpecReachTheStackThroughThePersistenceHook) {
  // A1 left Persistence::set_observer with a null hook and the state correct;
  // A2 is what fills it in, and this is the join.
  fv::OverlayManager m;
  RecordingStackObserver obs;
  m.AddObserver(&obs);

  auto doc = std::make_shared<DocOverlay>("doc");
  doc->set_type_id("t.doc");
  ASSERT_TRUE(m.Add(doc).ok());
  obs.log.clear();

  doc->set_dirty(true);
  doc->set_dirty(true);  // change-only, still one event
  ASSERT_TRUE(doc->FileOpen("/tmp/a.rte").ok());
  EXPECT_EQ(obs.log, (std::vector<std::string>{"dirty:doc", "spec:doc"}));

  // Leaving the stack detaches the hook: an overlay outlives the manager
  // (shared_ptr, D1), so a stale one would be a use-after-free.
  obs.log.clear();
  ASSERT_TRUE(m.Remove(doc).ok());
  doc->set_dirty(false);
  doc->set_file_spec("/tmp/b.rte");
  // Removal itself is announced (and it WAS the current overlay); the two
  // document changes after it are not.
  EXPECT_EQ(obs.log,
            (std::vector<std::string>{"removed:doc", "current:doc>-"}));
}

TEST(StackObservers, ThePersistenceHookIsDroppedWhenTheManagerDies) {
  auto doc = std::make_shared<DocOverlay>("doc");
  {
    fv::OverlayManager m;
    ASSERT_TRUE(m.Add(doc).ok());
    doc->set_dirty(true);
  }
  doc->set_dirty(false);  // must not call into the dead manager
  EXPECT_FALSE(doc->is_dirty());
}

// ---------------------------------------------------------------------------
// Reorder verbs
// ---------------------------------------------------------------------------

class ReorderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    a_ = Make("a", &log_);
    b_ = Make("b", &log_);
    c_ = Make("c", &log_);
    d_ = Make("d", &log_);
    for (const auto& o : {a_, b_, c_, d_}) ASSERT_TRUE(m_.Add(o).ok());
    m_.AddObserver(&obs_);
  }
  std::vector<std::string> log_;
  std::shared_ptr<Recorder> a_, b_, c_, d_;
  fv::OverlayManager m_;
  RecordingStackObserver obs_;
};

TEST_F(ReorderTest, MoveAboveAndBelowPlaceTheOverlayNextToItsAnchor) {
  ASSERT_TRUE(m_.MoveAbove(a_, c_).ok());
  EXPECT_EQ(NamesBottomUp(m_), (std::vector<std::string>{"b", "c", "a", "d"}));

  ASSERT_TRUE(m_.MoveBelow(d_, b_).ok());
  EXPECT_EQ(NamesBottomUp(m_), (std::vector<std::string>{"d", "b", "c", "a"}));

  // Downward and upward moves must both land exactly beside the anchor -- the
  // index shifts when the moved overlay came from below it.
  ASSERT_TRUE(m_.MoveAbove(d_, c_).ok());
  EXPECT_EQ(NamesBottomUp(m_), (std::vector<std::string>{"b", "c", "d", "a"}));
  ASSERT_TRUE(m_.MoveBelow(a_, b_).ok());
  EXPECT_EQ(NamesBottomUp(m_), (std::vector<std::string>{"a", "b", "c", "d"}));

  EXPECT_EQ(obs_.log, (std::vector<std::string>(4, "order")));
}

TEST_F(ReorderTest, TopBottomAndTheNoOpCases) {
  ASSERT_TRUE(m_.MoveToTop(b_).ok());
  EXPECT_EQ(NamesBottomUp(m_), (std::vector<std::string>{"a", "c", "d", "b"}));
  ASSERT_TRUE(m_.MoveToBottom(d_).ok());
  EXPECT_EQ(NamesBottomUp(m_), (std::vector<std::string>{"d", "a", "c", "b"}));

  // Already there: no move, and no order event to repaint a list over.
  obs_.log.clear();
  ASSERT_TRUE(m_.MoveToTop(b_).ok());
  ASSERT_TRUE(m_.MoveToBottom(d_).ok());
  EXPECT_TRUE(obs_.log.empty());

  auto stranger = Make("stranger", &log_);
  EXPECT_EQ(m_.MoveToTop(stranger).code, fv::kNotFound);
  EXPECT_EQ(m_.MoveAbove(a_, stranger).code, fv::kNotFound);
  EXPECT_EQ(m_.MoveAbove(a_, a_).code, fv::kInvalidArg);
}

TEST_F(ReorderTest, ReorderTakesTheWholePermutationOrNothing) {
  ASSERT_TRUE(m_.Reorder({d_, c_, b_, a_}).ok());
  EXPECT_EQ(NamesBottomUp(m_), (std::vector<std::string>{"d", "c", "b", "a"}));
  EXPECT_EQ(obs_.log, (std::vector<std::string>{"order"}));

  // A stale dialog must not half-apply: each of these is rejected whole.
  obs_.log.clear();
  EXPECT_EQ(m_.Reorder({d_, c_, b_}).code, fv::kInvalidArg);      // short
  EXPECT_EQ(m_.Reorder({d_, c_, b_, b_}).code, fv::kInvalidArg);  // twice
  EXPECT_EQ(m_.Reorder({d_, c_, b_, nullptr}).code, fv::kInvalidArg);
  EXPECT_EQ(m_.Reorder({d_, c_, b_, Make("stranger", &log_)}).code,
            fv::kNotFound);
  EXPECT_EQ(NamesBottomUp(m_), (std::vector<std::string>{"d", "c", "b", "a"}));
  EXPECT_TRUE(obs_.log.empty());

  // The identity permutation is legal and silent.
  ASSERT_TRUE(m_.Reorder({d_, c_, b_, a_}).ok());
  EXPECT_TRUE(obs_.log.empty());
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

TEST(StackQueries, OfTypeIsTopDownSoItsFrontIsFirstOfType) {
  std::vector<std::string> log;
  fv::OverlayManager m;
  auto r1 = Make("r1", &log, false, "t.route");
  auto d1 = Make("d1", &log, false, "t.draw");
  auto r2 = Make("r2", &log, false, "t.route");
  for (const auto& o : {r1, d1, r2}) ASSERT_TRUE(m.Add(o).ok());

  // FirstOfType is the TOPMOST of the type, because that is the one an editor
  // adopts when the mode changes (plan §3d.1).
  EXPECT_EQ(m.FirstOfType("t.route"), r2);
  EXPECT_EQ(m.FirstOfType("t.draw"), d1);
  EXPECT_EQ(m.FirstOfType("t.nothing"), nullptr);

  auto routes = m.OfType("t.route");
  ASSERT_EQ(routes.size(), 2u);
  EXPECT_EQ(routes[0], r2);  // == FirstOfType
  EXPECT_EQ(routes[1], r1);
  EXPECT_TRUE(m.OfType("").empty());  // untyped overlays are not a "type"
}

TEST(StackQueries, FindByFileSpecIsTheOpenDedupKey) {
  fv::OverlayManager m;
  auto a = std::make_shared<DocOverlay>("a");
  auto b = std::make_shared<DocOverlay>("b");
  a->set_type_id("t.route");
  b->set_type_id("t.draw");
  ASSERT_TRUE(m.Add(a).ok());
  ASSERT_TRUE(m.Add(b).ok());
  ASSERT_TRUE(a->FileOpen("/tmp/kiawah.rte").ok());
  ASSERT_TRUE(b->FileOpen("/tmp/kiawah.drw").ok());

  EXPECT_EQ(m.FindByFileSpec("t.route", "/tmp/kiawah.rte"), a);
  // The key is the PAIR: the same path under another type is not a match.
  EXPECT_EQ(m.FindByFileSpec("t.draw", "/tmp/kiawah.rte"), nullptr);
  EXPECT_EQ(m.FindByFileSpec("", "/tmp/kiawah.drw"), b);  // empty = any type

  // An unsaved document has an empty spec, and "no file" is not an identity --
  // two brand-new routes must not dedup onto each other.
  auto fresh = std::make_shared<DocOverlay>("fresh");
  fresh->set_type_id("t.route");
  ASSERT_TRUE(m.Add(fresh).ok());
  EXPECT_EQ(m.FindByFileSpec("t.route", ""), nullptr);

  // An overlay with no Persistence at all never matches.
  std::vector<std::string> log;
  ASSERT_TRUE(m.Add(Make("plain", &log, false, "t.route")).ok());
  EXPECT_EQ(m.FindByFileSpec("t.route", "/tmp/kiawah.rte"), a);
}

// ---------------------------------------------------------------------------
// Draw: the top-most band, and declutter
// ---------------------------------------------------------------------------

TEST(StackDraw, TopMostOverlaysDrawLastEvenWhenTheUserSinksThem) {
  OverlayTypeRegistry r;
  FillRegistry(&r);
  std::vector<std::string> log;
  fv::OverlayManager m;
  m.SetTypeRegistry(&r);
  auto low = Make("low", &log, false, "t.low");
  auto hud = Make("hud", &log, false, "t.hud");
  auto high = Make("high", &log, false, "t.high");
  for (const auto& o : {low, hud, high}) ASSERT_TRUE(m.Add(o).ok());

  // Drag the HUD to the bottom of the stack: it still draws over everything,
  // which is what the flag means (~ OnDrawTopMostOverlays as a second pass).
  ASSERT_TRUE(m.MoveToBottom(hud).ok());
  EXPECT_EQ(NamesBottomUp(m), (std::vector<std::string>{"hud", "low", "high"}));

  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(64, 64).ok());
  ASSERT_TRUE(proj.SetCenter({0, 0}).ok());
  ASSERT_TRUE(proj.SetScale(1000000.0).ok());
  fv::CpuCanvas canvas(64, 64);
  ASSERT_TRUE(m.DrawAll(proj, canvas).ok());
  EXPECT_EQ(log, (std::vector<std::string>{"low:draw", "high:draw",
                                           "hud:draw"}));
}

TEST(StackDeclutter, OnlyTheCurrentOverlayDrawsAndRoutes) {
  std::vector<std::string> log;
  fv::OverlayManager m;
  auto a = Make("a", &log);
  auto b = Make("b", &log);
  auto c = Make("c", &log);
  for (const auto& o : {a, b, c}) ASSERT_TRUE(m.Add(o).ok());
  ASSERT_TRUE(m.MakeCurrent(b).ok());
  m.SetDeclutter(true);

  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(64, 64).ok());
  ASSERT_TRUE(proj.SetCenter({0, 0}).ok());
  ASSERT_TRUE(proj.SetScale(1000000.0).ok());
  fv::CpuCanvas canvas(64, 64);
  ASSERT_TRUE(m.DrawAll(proj, canvas).ok());
  EXPECT_EQ(log, (std::vector<std::string>{"b:draw"}));

  log.clear();
  EXPECT_FALSE(m.RouteMouseDown({1, 1, 0}));  // b declines; nobody else is asked
  EXPECT_EQ(log, (std::vector<std::string>{"b:down"}));

  // Nothing is hidden -- turning declutter off restores the whole stack.
  log.clear();
  m.SetDeclutter(false);
  ASSERT_TRUE(m.DrawAll(proj, canvas).ok());
  EXPECT_EQ(log, (std::vector<std::string>{"a:draw", "b:draw", "c:draw"}));

  // A hidden current overlay under declutter means nothing draws or routes.
  log.clear();
  m.SetDeclutter(true);
  b->SetVisible(false);
  ASSERT_TRUE(m.DrawAll(proj, canvas).ok());
  EXPECT_FALSE(m.RouteMouseDown({1, 1, 0}));
  EXPECT_TRUE(log.empty());
}

// ---------------------------------------------------------------------------
// Three-phase routing
// ---------------------------------------------------------------------------

TEST(StackRouting, DirectRoutingBeatsStackOrder) {
  // The selection lock: an overlay mid-gesture must see the mouse first even
  // though it is not topmost (~ get_m_bDirectlyRouteMouseLeftButtonDown).
  std::vector<std::string> log;
  fv::OverlayManager m;
  auto locked = std::make_shared<LockingRecorder>("locked", &log, true);
  ASSERT_TRUE(m.Add(locked).ok());
  ASSERT_TRUE(m.Add(Make("above1", &log, true)).ok());
  ASSERT_TRUE(m.Add(Make("above2", &log, true)).ok());

  EXPECT_TRUE(m.RouteMouseDown({1, 1, 0}));
  EXPECT_EQ(log, (std::vector<std::string>{"locked:down"}));
}

TEST(StackRouting, ADeclinedDirectRouteFallsBackButIsNotOfferedTwice) {
  // FalconView re-offered the event to the locked overlay in its second walk.
  // Deliberate deviation (see manager.cpp): one event, one delivery.
  std::vector<std::string> log;
  fv::OverlayManager m;
  auto locked = std::make_shared<LockingRecorder>("locked", &log, false);
  auto top = Make("top", &log, true);
  ASSERT_TRUE(m.Add(locked).ok());
  ASSERT_TRUE(m.Add(Make("mid", &log, false)).ok());
  ASSERT_TRUE(m.Add(top).ok());

  EXPECT_TRUE(m.RouteMouseDown({1, 1, 0}));
  EXPECT_EQ(log, (std::vector<std::string>{"locked:down", "top:down"}));

  // With the lock released it is just another overlay, in stack order.
  log.clear();
  locked->set_direct(false);
  EXPECT_TRUE(m.RouteMouseDown({1, 1, 0}));
  EXPECT_EQ(log, (std::vector<std::string>{"top:down"}));
}

TEST(StackRouting, TheLockOutranksDeclutter) {
  // C_ovl_mgr::select runs the pre-pass over the whole list without consulting
  // the display mode, and it must: a gesture in a background overlay cannot be
  // abandoned because the user pressed declutter mid-drag.
  std::vector<std::string> log;
  fv::OverlayManager m;
  auto locked = std::make_shared<LockingRecorder>("locked", &log, true);
  auto other = Make("other", &log, true);
  ASSERT_TRUE(m.Add(locked).ok());
  ASSERT_TRUE(m.Add(other).ok());
  ASSERT_TRUE(m.MakeCurrent(other).ok());
  m.SetDeclutter(true);

  EXPECT_TRUE(m.RouteMouseDown({1, 1, 0}));
  EXPECT_EQ(log, (std::vector<std::string>{"locked:down"}));

  // ...and when the lock declines, declutter still limits the fallback to the
  // current overlay.
  log.clear();
  locked->set_handles(false);
  EXPECT_TRUE(m.RouteMouseDown({1, 1, 0}));
  EXPECT_EQ(log, (std::vector<std::string>{"locked:down", "other:down"}));
}

TEST(StackRouting, AnInvisibleOverlayIsSkippedInEveryPhase) {
  std::vector<std::string> log;
  fv::OverlayManager m;
  auto locked = std::make_shared<LockingRecorder>("locked", &log, true);
  ASSERT_TRUE(m.Add(locked).ok());
  ASSERT_TRUE(m.Add(Make("top", &log, true)).ok());
  locked->SetVisible(false);

  EXPECT_TRUE(m.RouteMouseDown({1, 1, 0}));
  EXPECT_EQ(log, (std::vector<std::string>{"top:down"}));
}

// ---------------------------------------------------------------------------
// Mouse capture
// ---------------------------------------------------------------------------

TEST(StackCapture, ACapturedGestureCannotBeStolen) {
  std::vector<std::string> log;
  fv::OverlayManager m;
  auto dragger = Make("dragger", &log, true);
  auto top = Make("top", &log, true);
  ASSERT_TRUE(m.Add(dragger).ok());
  ASSERT_TRUE(m.Add(top).ok());

  ASSERT_TRUE(m.CaptureMouse(dragger.get()).ok());
  EXPECT_EQ(m.mouse_capture(), dragger.get());
  EXPECT_TRUE(m.RouteMouseMove({2, 2, 0}));
  EXPECT_TRUE(m.RouteMouseUp({2, 2, 0}));
  EXPECT_EQ(log, (std::vector<std::string>{"dragger:move", "dragger:up"}));

  // Even when the capturing overlay DECLINES, nobody else is offered it: the
  // gesture owns the mouse, it does not merely get first refusal.
  log.clear();
  dragger->set_handles(false);
  EXPECT_FALSE(m.RouteMouseMove({3, 3, 0}));
  EXPECT_EQ(log, (std::vector<std::string>{"dragger:move"}));

  m.ReleaseMouse();
  log.clear();
  EXPECT_TRUE(m.RouteMouseMove({4, 4, 0}));
  EXPECT_EQ(log, (std::vector<std::string>{"top:move"}));
}

TEST(StackCapture, EscapeReachesTheCapturingOverlayFirstButKeysStillRoute) {
  std::vector<std::string> log;
  fv::OverlayManager m;
  auto dragger = Make("dragger", &log, false);
  auto top = Make("top", &log, true);
  ASSERT_TRUE(m.Add(dragger).ok());
  ASSERT_TRUE(m.Add(top).ok());
  ASSERT_TRUE(m.CaptureMouse(dragger.get()).ok());

  // First refusal, not exclusivity: a key the gesture does not want (a menu
  // shortcut) still belongs to the rest of the stack, and the capturing
  // overlay is not offered it twice on the way past.
  fv::KeyEvent esc;
  esc.key = fv::Key::kEscape;
  EXPECT_TRUE(m.RouteKeyDown(esc));
  EXPECT_EQ(log, (std::vector<std::string>{"dragger:key", "top:key"}));

  log.clear();
  dragger->set_handles(true);
  EXPECT_TRUE(m.RouteKeyDown(esc));
  EXPECT_EQ(log, (std::vector<std::string>{"dragger:key"}));
}

TEST(StackCapture, CaptureIsDroppedByRemovalAndIgnoredWhenHidden) {
  std::vector<std::string> log;
  fv::OverlayManager m;
  auto dragger = Make("dragger", &log, true);
  auto top = Make("top", &log, true);
  ASSERT_TRUE(m.Add(dragger).ok());
  ASSERT_TRUE(m.Add(top).ok());

  // Hidden: the capture is stale rather than a black hole -- routing skips
  // invisible overlays everywhere else, so it must here too.
  ASSERT_TRUE(m.CaptureMouse(dragger.get()).ok());
  dragger->SetVisible(false);
  EXPECT_TRUE(m.RouteMouseMove({1, 1, 0}));
  EXPECT_EQ(log, (std::vector<std::string>{"top:move"}));

  dragger->SetVisible(true);
  log.clear();
  ASSERT_TRUE(m.Remove(dragger).ok());
  EXPECT_EQ(m.mouse_capture(), nullptr);
  EXPECT_TRUE(m.RouteMouseMove({1, 1, 0}));
  EXPECT_EQ(log, (std::vector<std::string>{"top:move"}));

  // An overlay outside the stack cannot capture.
  EXPECT_EQ(m.CaptureMouse(dragger.get()).code, fv::kNotFound);
  EXPECT_EQ(m.CaptureMouse(nullptr).code, fv::kInvalidArg);
}

}  // namespace
