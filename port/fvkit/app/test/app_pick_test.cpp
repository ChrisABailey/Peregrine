// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// App layer A5: hover, click deconfliction, snap-to and context-menu
// composition (fvkit-app-plan-COMPLETE.md §3e).
//
// The proof the plan asks for is "ambiguity tests (2 overlays, overlapping
// items, each policy); hover hint text", and the shape those tests take is
// forced by what the policies actually differ about: every ambiguity fixture
// here has the NEARER item LOWER in the stack, because that is the only
// arrangement in which kTopMost and kNearest disagree. A fixture where the
// topmost item is also the nearest would pass under either policy and under a
// session that had no policy at all.

#include "fvkit/app/pick.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "fake_shell.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/app/vector_hit_test.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/vector/pick.h"

namespace {

using fv::Overlay;
using fv::OverlayManager;
using fv::PixelPoint;
using fv::app::CursorId;
using fv::app::FakeShell;
using fv::app::HitItem;
using fv::app::MenuNode;
using fv::app::OverlayTypeDesc;
using fv::app::OverlayTypeRegistry;
using fv::app::PickPolicy;
using fv::app::PickSession;
using fv::app::SnapToItem;

// The projection is never consulted by any of the doubles -- and by the real
// vector adapter either, which is the point of §3e's "thin adapter": the pick
// index is already in canvas pixels. One default-constructed projection is
// therefore enough for the whole file.
const fv::MapProjection& Proj() {
  static const fv::MapProjection kProj;
  return kProj;
}

// ---------------------------------------------------------------------------
// Doubles
// ---------------------------------------------------------------------------

// An overlay with a scripted answer to "what is under this point". `distance`
// is what makes the policies differ; `hint`/`cursor` are what a hover shows.
class Pickable : public Overlay, public fv::app::HitTest {
 public:
  struct Item {
    uint64_t feature = 0;
    double distance = 0.0;
    std::string tip;
    CursorId cursor = CursorId::kHand;
  };

  explicit Pickable(std::string name) : Overlay(std::move(name)) {}

  fv::app::HitTest* AsHitTest() override { return this; }

  void HitTestPoint(const fv::MapProjection&, PixelPoint p, double tolerance,
                    std::vector<HitItem>& out) override {
    ++calls;
    last_tolerance = tolerance;
    last_point = p;
    for (const Item& i : items) {
      if (i.distance > tolerance) continue;
      HitItem hit;
      hit.overlay = this;
      hit.feature = i.feature;
      hit.distance_px = i.distance;
      hit.hint.tool_tip = i.tip;
      hit.hint.status = i.tip.empty() ? "" : i.tip + " (" + Name() + ")";
      hit.cursor = i.cursor;
      out.push_back(hit);
    }
  }

  std::vector<Item> items;
  int calls = 0;
  double last_tolerance = 0.0;
  PixelPoint last_point;
};

class Snappable : public Overlay, public fv::app::SnapTo {
 public:
  explicit Snappable(std::string name) : Overlay(std::move(name)) {}

  fv::app::SnapTo* AsSnapTo() override { return this; }

  void SnapToPoint(const fv::MapProjection&, PixelPoint p, double tolerance_px,
                   std::vector<SnapToItem>& out) override {
    ++calls;
    last_tolerance = tolerance_px;
    last_point = p;
    for (const SnapToItem& s : items) {
      SnapToItem copy = s;
      copy.overlay = this;
      out.push_back(copy);
    }
  }

  std::vector<SnapToItem> items;
  int calls = 0;
  double last_tolerance = 0.0;
  PixelPoint last_point;
};

class Menued : public Overlay, public fv::app::ContextMenu {
 public:
  Menued(std::string name, std::vector<std::string> labels)
      : Overlay(std::move(name)), labels_(std::move(labels)) {}

  fv::app::ContextMenu* AsContextMenu() override { return this; }

  void AppendMenuItems(const fv::MapProjection&, PixelPoint,
                       MenuNode& menu) override {
    ++calls;
    for (const std::string& l : labels_) {
      MenuNode node;
      node.label = l;
      menu.children.push_back(node);
    }
  }

  int calls = 0;

 private:
  std::vector<std::string> labels_;
};

std::shared_ptr<Pickable> AddPickable(OverlayManager& m, const std::string& name,
                                      std::vector<Pickable::Item> items) {
  auto o = std::make_shared<Pickable>(name);
  o->items = std::move(items);
  EXPECT_TRUE(m.Add(o).ok());
  return o;
}

// ---------------------------------------------------------------------------
// Hover  (~ test_select: "what would a click here do")
// ---------------------------------------------------------------------------

TEST(AppPickHover, TheTopMostItemSetsTheCursorAndTheHint) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);

  AddPickable(m, "low", {{1, 1.0, "buoy"}});
  AddPickable(m, "high", {{2, 4.0, "wreck", CursorId::kCrosshair}});

  pick.UpdateHover(Proj(), PixelPoint{10, 10});

  ASSERT_NE(pick.hovered(), nullptr);
  EXPECT_EQ(pick.hovered()->feature, 2u);  // topmost wins, though it is farther
  EXPECT_EQ(shell.cursor, CursorId::kCrosshair);
  ASSERT_EQ(shell.hint_calls.size(), 1u);
  EXPECT_EQ(shell.hint_calls[0].tool_tip, "wreck");
  EXPECT_EQ(shell.hint_calls[0].status, "wreck (high)");
}

TEST(AppPickHover, TheShellIsToldOnlyWhenTheHoverCHANGES) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  AddPickable(m, "chart", {{7, 2.0, "road"}});

  pick.UpdateHover(Proj(), PixelPoint{10, 10});
  pick.UpdateHover(Proj(), PixelPoint{11, 10});
  pick.UpdateHover(Proj(), PixelPoint{12, 11});

  // Three moves along one road: one hint, one cursor. This is the whole
  // reason the session keeps the last hit at all.
  EXPECT_EQ(shell.hint_calls.size(), 1u);
  EXPECT_EQ(shell.cursor_calls.size(), 1u);
}

TEST(AppPickHover, MovingOffEverythingClearsTheCursorAndTheHintOnce) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  auto o = AddPickable(m, "chart", {{7, 2.0, "road"}});

  pick.UpdateHover(Proj(), PixelPoint{10, 10});
  o->items.clear();  // the cursor has moved off the ink
  pick.UpdateHover(Proj(), PixelPoint{80, 80});
  pick.UpdateHover(Proj(), PixelPoint{81, 80});

  EXPECT_EQ(pick.hovered(), nullptr);
  EXPECT_EQ(shell.cursor, CursorId::kDefault);
  ASSERT_EQ(shell.hint_calls.size(), 2u);
  EXPECT_TRUE(shell.hint_calls[1].empty());  // and only ONE clear
  EXPECT_EQ(shell.cursor_calls.size(), 2u);
}

TEST(AppPickHover, AnAmbiguousPolicyDoesNotOpenAChooserOnAHover) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  AddPickable(m, "low", {{1, 1.0, "buoy"}});
  AddPickable(m, "high", {{2, 4.0, "wreck"}});

  pick.UpdateHover(Proj(), PixelPoint{10, 10}, PickPolicy::kAskWhenAmbiguous);

  EXPECT_EQ(shell.list_calls, 0);
  ASSERT_NE(pick.hovered(), nullptr);
  EXPECT_EQ(pick.hovered()->feature, 2u);  // degrades to top-most
}

TEST(AppPickHover, TheToleranceIsTheSessionsAndReachesTheOverlay) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  auto o = AddPickable(m, "chart", {{1, 12.0, "far"}});

  pick.UpdateHover(Proj(), PixelPoint{4, 5});
  EXPECT_EQ(o->last_tolerance, 8.0);  // the documented default
  EXPECT_EQ(o->last_point.x, 4);
  EXPECT_EQ(pick.hovered(), nullptr);  // 12 px away, outside it

  pick.tolerance_px = 16.0;
  pick.UpdateHover(Proj(), PixelPoint{4, 5});
  ASSERT_NE(pick.hovered(), nullptr);
}

// ---------------------------------------------------------------------------
// Who is asked
// ---------------------------------------------------------------------------

TEST(AppPickWhoIsAsked, AnInvisibleOverlayIsNotPickable) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  auto hidden = AddPickable(m, "hidden", {{1, 0.0, "under the cursor"}});
  hidden->SetVisible(false);

  EXPECT_TRUE(pick.HitTestPoint(Proj(), PixelPoint{1, 1}).empty());
  EXPECT_EQ(hidden->calls, 0);
}

TEST(AppPickWhoIsAsked, DeclutterLeavesOnlyTheCurrentOverlayPickable) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  auto bottom = AddPickable(m, "bottom", {{1, 0.0, "bottom"}});
  auto top = AddPickable(m, "top", {{2, 0.0, "top"}});

  EXPECT_TRUE(m.MakeCurrent(bottom).ok());
  m.SetDeclutter(true);

  const std::vector<HitItem> hits = pick.HitTestPoint(Proj(), PixelPoint{1, 1});
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].overlay, bottom.get());
  EXPECT_EQ(top->calls, 0);
}

TEST(AppPickWhoIsAsked, TheTopMostBandOutranksTheStackIndexJustAsItDoesInDraw) {
  // A crosshair overlay flagged top-most draws OVER everything however low it
  // sits (A2's second draw pass), so it must pick over everything too --
  // otherwise a click lands on the chart under a HUD the user can see.
  OverlayTypeRegistry registry;
  OverlayTypeDesc hud;
  hud.id = "test.hud";
  hud.display_name = "HUD";
  hud.is_top_most = true;
  hud.factory = [] { return std::make_shared<Pickable>("hud"); };
  ASSERT_TRUE(registry.Register(hud).ok());

  OverlayManager m;
  m.SetTypeRegistry(&registry);
  FakeShell shell;
  PickSession pick(m, shell);

  auto crosshair = std::make_shared<Pickable>("hud");
  crosshair->set_type_id("test.hud");
  crosshair->items = {{9, 6.0, "crosshair"}};
  ASSERT_TRUE(m.Add(crosshair).ok());
  auto chart = AddPickable(m, "chart", {{1, 0.0, "coastline"}});
  ASSERT_TRUE(m.MoveToTop(chart).ok());  // the chart is now the top of the list

  const std::vector<HitItem> hits = pick.HitTestPoint(Proj(), PixelPoint{1, 1});
  ASSERT_EQ(hits.size(), 2u);
  EXPECT_EQ(hits[0].overlay, crosshair.get());
}

TEST(AppPickWhoIsAsked, AnOverlayWithoutTheCapabilityIsSkipped) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  ASSERT_TRUE(m.Add(std::make_shared<Overlay>("plain")).ok());
  AddPickable(m, "chart", {{1, 0.0, "coastline"}});

  EXPECT_EQ(pick.HitTestPoint(Proj(), PixelPoint{1, 1}).size(), 1u);
}

// ---------------------------------------------------------------------------
// Click policies
// ---------------------------------------------------------------------------

// One fixture for the three policies: two overlays, both answering, and the
// NEARER item is the LOWER one -- the only arrangement that tells them apart.
class AppPickClick : public ::testing::Test {
 protected:
  void SetUp() override {
    low_ = AddPickable(manager_, "low", {{1, 1.0, "buoy"}});
    high_ = AddPickable(manager_, "high", {{2, 5.0, "wreck"}});
  }

  OverlayManager manager_;
  FakeShell shell_;
  PickSession pick_{manager_, shell_};
  std::shared_ptr<Pickable> low_, high_;
};

TEST_F(AppPickClick, TopMostTakesTheOverlayTheUserSawOnTop) {
  const auto hit = pick_.ResolveClick(Proj(), PixelPoint{5, 5},
                                      PickPolicy::kTopMost);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->overlay, high_.get());
  EXPECT_EQ(shell_.list_calls, 0);
}

TEST_F(AppPickClick, NearestTakesTheCloserInkFromLowerInTheStack) {
  const auto hit = pick_.ResolveClick(Proj(), PixelPoint{5, 5},
                                      PickPolicy::kNearest);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->overlay, low_.get());
  EXPECT_EQ(shell_.list_calls, 0);
}

TEST_F(AppPickClick, AskWhenAmbiguousAsksAndHonoursTheAnswer) {
  shell_.list_choice = 1;
  const auto hit = pick_.ResolveClick(Proj(), PixelPoint{5, 5},
                                      PickPolicy::kAskWhenAmbiguous);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->overlay, low_.get());  // row 1, the second of the two
  EXPECT_EQ(shell_.list_calls, 1);
  // Rows are ranked exactly as kTopMost would rank them, and each says WHICH
  // overlay it came from -- the whole reason the user is being asked.
  ASSERT_EQ(shell_.last_list_rows.size(), 2u);
  EXPECT_EQ(shell_.last_list_rows[0], "high: wreck");
  EXPECT_EQ(shell_.last_list_rows[1], "low: buoy");
}

TEST_F(AppPickClick, CancellingTheChooserPicksNothing) {
  shell_.list_choice = std::nullopt;
  EXPECT_FALSE(pick_.ResolveClick(Proj(), PixelPoint{5, 5},
                                  PickPolicy::kAskWhenAmbiguous));
}

TEST_F(AppPickClick, AnOutOfRangeAnswerIsTreatedAsACancel) {
  shell_.list_choice = 7;
  EXPECT_FALSE(pick_.ResolveClick(Proj(), PixelPoint{5, 5},
                                  PickPolicy::kAskWhenAmbiguous));
}

TEST_F(AppPickClick, ASingleCandidateIsNeverWorthAsking) {
  high_->items.clear();
  shell_.list_choice = std::nullopt;  // would be a cancel if it were asked
  const auto hit = pick_.ResolveClick(Proj(), PixelPoint{5, 5},
                                      PickPolicy::kAskWhenAmbiguous);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->overlay, low_.get());
  EXPECT_EQ(shell_.list_calls, 0);
}

TEST_F(AppPickClick, NothingUnderThePointIsNotAQuestion) {
  low_->items.clear();
  high_->items.clear();
  EXPECT_FALSE(pick_.ResolveClick(Proj(), PixelPoint{5, 5},
                                  PickPolicy::kAskWhenAmbiguous));
  EXPECT_EQ(shell_.list_calls, 0);
}

TEST_F(AppPickClick, TwoItemsOfONEOverlayAreOrderedByDistanceUnderTopMost) {
  low_->items.clear();
  high_->items = {{2, 5.0, "wreck"}, {3, 2.0, "buoy"}};
  const std::vector<HitItem> hits =
      pick_.HitTestPoint(Proj(), PixelPoint{5, 5}, PickPolicy::kTopMost);
  ASSERT_EQ(hits.size(), 2u);
  EXPECT_EQ(hits[0].feature, 3u);  // stack order first, distance WITHIN it
}

// ---------------------------------------------------------------------------
// Snap-to  (~ snptodlg)
// ---------------------------------------------------------------------------

std::shared_ptr<Snappable> AddSnappable(OverlayManager& m,
                                        const std::string& name,
                                        std::vector<std::string> descriptions) {
  auto o = std::make_shared<Snappable>(name);
  for (const std::string& d : descriptions) {
    SnapToItem item;
    item.description = d;
    o->items.push_back(item);
  }
  EXPECT_TRUE(m.Add(o).ok());
  return o;
}

TEST(AppPickSnapTo, NothingAnsweringIsNullopt) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  AddSnappable(m, "route", {});
  EXPECT_FALSE(pick.SnapToPoint(Proj(), PixelPoint{3, 3}));
  EXPECT_EQ(shell.list_calls, 0);
}

TEST(AppPickSnapTo, OneAnswerIsTakenWithoutADialog) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  AddSnappable(m, "route", {"turn point 3"});

  const auto snap = pick.SnapToPoint(Proj(), PixelPoint{3, 3});
  ASSERT_TRUE(snap.has_value());
  EXPECT_EQ(snap->description, "turn point 3");
  EXPECT_EQ(shell.list_calls, 0);
}

TEST(AppPickSnapTo, SeveralAnswersAreOneQuestionOverEVERYOverlayThatAnswered) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  AddSnappable(m, "route", {"turn point 3"});
  auto chart = AddSnappable(m, "chart", {"buoy 12", "light 4"});
  shell.list_choice = 2;

  const auto snap = pick.SnapToPoint(Proj(), PixelPoint{3, 3});
  ASSERT_TRUE(snap.has_value());
  // Top-down: the chart is above the route, so its two rows come first, and
  // unlike a hit the row text is the candidate's OWN description.
  EXPECT_EQ(shell.last_list_rows,
            (std::vector<std::string>{"buoy 12", "light 4", "turn point 3"}));
  EXPECT_EQ(snap->description, "turn point 3");
  EXPECT_EQ(chart->calls, 1);
}

TEST(AppPickSnapTo, CancellingSnapsNowhere) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  AddSnappable(m, "route", {"turn point 3"});
  AddSnappable(m, "chart", {"buoy 12"});
  shell.list_choice = std::nullopt;

  EXPECT_FALSE(pick.SnapToPoint(Proj(), PixelPoint{3, 3}));
}

TEST(AppPickSnapTo, AHiddenOverlayIsNotSnappedTo) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  auto hidden = AddSnappable(m, "hidden", {"buoy 12"});
  hidden->SetVisible(false);
  AddSnappable(m, "route", {"turn point 3"});

  const auto snap = pick.SnapToPoint(Proj(), PixelPoint{3, 3});
  ASSERT_TRUE(snap.has_value());
  EXPECT_EQ(snap->description, "turn point 3");
  EXPECT_EQ(shell.list_calls, 0);  // one answer, not two
  EXPECT_EQ(hidden->calls, 0);
}

// ---------------------------------------------------------------------------
// SnapCandidates — the same walk with no shell and no question (Pippin P19)
// ---------------------------------------------------------------------------

std::shared_ptr<Snappable> AddRanked(
    OverlayManager& m, const std::string& name,
    std::vector<std::pair<std::string, double>> rows) {
  auto o = std::make_shared<Snappable>(name);
  for (const auto& r : rows) {
    SnapToItem item;
    item.description = r.first;
    item.distance_px = r.second;
    o->items.push_back(item);
  }
  EXPECT_TRUE(m.Add(o).ok());
  return o;
}

TEST(AppSnapCandidates, RanksNearestFirstAcrossEveryOverlayThatAnswered) {
  // The phone's rule, and the reason the field was added: with no dialog to
  // show, "which one did you mean" has to be answered by the geometry. The
  // route is BELOW the chart in the stack and still wins, because its
  // candidate is nearer the finger.
  OverlayManager m;
  AddRanked(m, "route", {{"turn point 3", 2.0}});
  AddRanked(m, "chart", {{"buoy 12", 9.0}, {"light 4", 5.0}});

  const std::vector<SnapToItem> got =
      fv::app::SnapCandidates(m, Proj(), PixelPoint{3, 3}, 12.0);
  ASSERT_EQ(3u, got.size());
  EXPECT_EQ("turn point 3", got[0].description);
  EXPECT_EQ("light 4", got[1].description);
  EXPECT_EQ("buoy 12", got[2].description);
}

TEST(AppSnapCandidates, ATieKeepsStackOrderSoTheTopmostWins) {
  // Two markers stacked exactly -- which is what a route waypoint dropped ON a
  // named point looks like -- must not reorder run to run. The sort is stable
  // and the walk is topmost-first, so the overlay the user sees on top wins.
  OverlayManager m;
  AddRanked(m, "under", {{"below", 4.0}});
  AddRanked(m, "over", {{"above", 4.0}});

  const std::vector<SnapToItem> got =
      fv::app::SnapCandidates(m, Proj(), PixelPoint{3, 3}, 12.0);
  ASSERT_EQ(2u, got.size());
  EXPECT_EQ("above", got[0].description);
}

TEST(AppSnapCandidates, AnOverlayThatReportsNoDistanceSortsAsIfUnderTheCursor) {
  // Leaving distance_px at 0 is not an error -- it is an overlay written
  // before the field existed. It must still be offered, and 0 is the honest
  // reading of "did not say".
  OverlayManager m;
  AddRanked(m, "silent", {{"unranked", 0.0}});
  AddRanked(m, "chart", {{"buoy 12", 3.0}});

  const std::vector<SnapToItem> got =
      fv::app::SnapCandidates(m, Proj(), PixelPoint{3, 3}, 12.0);
  ASSERT_EQ(2u, got.size());
  EXPECT_EQ("unranked", got[0].description);
}

TEST(AppSnapCandidates, StampsTheAnsweringOverlayAndSkipsHiddenOnes) {
  OverlayManager m;
  auto hidden = AddRanked(m, "hidden", {{"buoy 12", 1.0}});
  hidden->SetVisible(false);
  auto route = AddRanked(m, "route", {{"turn point 3", 6.0}});

  const std::vector<SnapToItem> got =
      fv::app::SnapCandidates(m, Proj(), PixelPoint{3, 3}, 12.0);
  ASSERT_EQ(1u, got.size());
  EXPECT_EQ("turn point 3", got[0].description);
  EXPECT_EQ(route.get(), got[0].overlay);
  EXPECT_EQ(0, hidden->calls) << "an invisible overlay is not even asked";
}

TEST(AppSnapCandidates, TheToleranceReachesTheOverlayUnchanged) {
  // The one knob, and the shell is the only layer that knows a finger is
  // wider than a mouse pointer -- so it must arrive as sent.
  OverlayManager m;
  auto chart = AddRanked(m, "chart", {{"buoy 12", 3.0}});
  fv::app::SnapCandidates(m, Proj(), PixelPoint{7, 9}, 44.0);
  EXPECT_DOUBLE_EQ(44.0, chart->last_tolerance);
  EXPECT_EQ(7, chart->last_point.x);
  EXPECT_EQ(9, chart->last_point.y);
}

// ---------------------------------------------------------------------------
// Context menu composition
// ---------------------------------------------------------------------------

TEST(AppPickContextMenu, SectionsComeTopDownWithASeparatorBetween) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  ASSERT_TRUE(m.Add(std::make_shared<Menued>("chart", std::vector<std::string>{
                                                          "Identify"})).ok());
  ASSERT_TRUE(m.Add(std::make_shared<Menued>(
                        "route", std::vector<std::string>{"Insert", "Delete"}))
                  .ok());

  const MenuNode root = pick.BuildContextMenu(Proj(), PixelPoint{2, 2});
  ASSERT_EQ(root.children.size(), 4u);
  EXPECT_EQ(root.children[0].label, "Insert");  // the route is on top
  EXPECT_EQ(root.children[1].label, "Delete");
  EXPECT_TRUE(root.children[2].is_separator());
  EXPECT_EQ(root.children[3].label, "Identify");
}

TEST(AppPickContextMenu, AnOverlayThatAppendsNothingCostsNoSeparator) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  ASSERT_TRUE(m.Add(std::make_shared<Menued>("chart", std::vector<std::string>{
                                                          "Identify"})).ok());
  auto quiet = std::make_shared<Menued>("quiet", std::vector<std::string>{});
  ASSERT_TRUE(m.Add(quiet).ok());

  const MenuNode root = pick.BuildContextMenu(Proj(), PixelPoint{2, 2});
  ASSERT_EQ(root.children.size(), 1u);
  EXPECT_EQ(root.children[0].label, "Identify");
  EXPECT_EQ(quiet->calls, 1);  // it was asked; it simply had nothing to say
}

TEST(AppPickContextMenu, AnEmptyMenuIsNeverShown) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  ASSERT_TRUE(m.Add(std::make_shared<Menued>("quiet",
                                             std::vector<std::string>{})).ok());

  EXPECT_FALSE(pick.ShowContextMenu(Proj(), PixelPoint{2, 2}));
  EXPECT_TRUE(shell.context_menus.empty());
}

TEST(AppPickContextMenu, ANonEmptyMenuGoesToTheShellAtThePoint) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  ASSERT_TRUE(m.Add(std::make_shared<Menued>("chart", std::vector<std::string>{
                                                          "Identify"})).ok());

  EXPECT_TRUE(pick.ShowContextMenu(Proj(), PixelPoint{12, 34}));
  ASSERT_EQ(shell.context_menus.size(), 1u);
  ASSERT_EQ(shell.context_menus[0].children.size(), 1u);
  EXPECT_EQ(shell.context_menu_points[0].x, 12);
  EXPECT_EQ(shell.context_menu_points[0].y, 34);
}

// ---------------------------------------------------------------------------
// The vector adapter — HitTest over the L4 PickIndex
// ---------------------------------------------------------------------------

fv::FeatureRef Ref(int32_t layer, int32_t tile, int32_t feature) {
  fv::FeatureRef r;
  r.layer = layer;
  r.tile = tile;
  r.feature = feature;
  return r;
}

// A source that describes exactly what it is asked about, and counts.
class DescribingSource : public fv::IVectorSource {
 public:
  fv::Status Open(const std::string&) override { return fv::Status::Ok(); }
  bool IsOpen() const override { return true; }
  fv::GeoRect Bounds() const override { return fv::GeoRect(); }
  std::vector<std::string> Layers() const override { return {"hydline"}; }
  fv::Status Query(const fv::VectorQuery&,
                   std::vector<fv::VectorFeature>*) override {
    return fv::Status::Ok();
  }
  fv::Status Describe(const fv::FeatureRef& ref,
                      fv::FeatureDescription* out) override {
    ++describe_calls;
    out->ref = ref;
    out->title = "feature " + std::to_string(ref.feature);
    out->class_name = "Hydrography";
    return fv::Status::Ok();
  }
  int describe_calls = 0;
};

TEST(AppVectorHitTest, TheIndexIsHitInCanvasPixelsAndNeedsNoProjection) {
  Overlay owner("dnc");
  fv::PickIndex index;
  index.AddBox(Ref(0, 7, 42), 10, fv::PixelRect{20, 20, 30, 30});
  fv::app::VectorHitTest adapter(owner);
  adapter.SetIndex(&index);

  std::vector<HitItem> out;
  adapter.HitTestPoint(Proj(), PixelPoint{25, 25}, 3.0, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].overlay, &owner);
  EXPECT_EQ(out[0].distance_px, 0.0);
  EXPECT_EQ(out[0].cursor, CursorId::kHand);

  out.clear();
  adapter.HitTestPoint(Proj(), PixelPoint{200, 200}, 3.0, out);
  EXPECT_TRUE(out.empty());
}

TEST(AppVectorHitTest, WithNoIndexItSimplyAnswersNothing) {
  Overlay owner("dnc");
  fv::app::VectorHitTest adapter(owner);
  std::vector<HitItem> out;
  adapter.HitTestPoint(Proj(), PixelPoint{25, 25}, 3.0, out);
  EXPECT_TRUE(out.empty());
}

TEST(AppVectorHitTest, TheIdIsAStableHandleBecauseAFeatureRefDoesNotFitIn64Bits) {
  // Four int32s are 128 bits and HitItem::feature is 64, so the id CANNOT be
  // a packing -- it is a handle this adapter translates back. What matters is
  // that it is stable: the same feature keeps its id across hit tests, so a
  // selection held over a redraw still names the same row.
  Overlay owner("dnc");
  fv::PickIndex index;
  index.AddBox(Ref(1, 7, 42), 10, fv::PixelRect{20, 20, 30, 30});
  index.AddBox(Ref(1, 7, 43), 10, fv::PixelRect{20, 20, 30, 30});
  fv::app::VectorHitTest adapter(owner);
  adapter.SetIndex(&index);

  std::vector<HitItem> first, second;
  adapter.HitTestPoint(Proj(), PixelPoint{25, 25}, 3.0, first);
  adapter.HitTestPoint(Proj(), PixelPoint{26, 26}, 3.0, second);

  ASSERT_EQ(first.size(), 2u);
  ASSERT_EQ(second.size(), 2u);
  EXPECT_NE(first[0].feature, first[1].feature);
  EXPECT_EQ(first[0].feature, second[0].feature);
  EXPECT_EQ(adapter.id_count(), 2u);  // and no new handle for the second look

  // The round trip, which is the whole contract -- WHICH of the two is on top
  // is the pick index's business and is pinned in the L4 tests.
  const int32_t a = adapter.RefFor(first[0].feature).feature;
  const int32_t b = adapter.RefFor(first[1].feature).feature;
  EXPECT_NE(a, b);
  EXPECT_TRUE((a == 42 && b == 43) || (a == 43 && b == 42));
  EXPECT_EQ(adapter.RefFor(first[0].feature).tile, 7);
  EXPECT_FALSE(adapter.RefFor(0).valid());       // 0 is "no feature"
  EXPECT_FALSE(adapter.RefFor(999).valid());     // and an unknown id is not one
}

TEST(AppVectorHitTest, TheHintIsTheProductsOwnDescriptionAndIsAskedOnceEach) {
  Overlay owner("dnc");
  fv::PickIndex index;
  index.AddBox(Ref(1, 7, 42), 10, fv::PixelRect{20, 20, 30, 30});
  DescribingSource source;
  fv::app::VectorHitTest adapter(owner);
  adapter.SetIndex(&index);
  adapter.SetSource(&source);

  std::vector<HitItem> out;
  adapter.HitTestPoint(Proj(), PixelPoint{25, 25}, 3.0, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].hint.tool_tip, "feature 42");
  EXPECT_EQ(out[0].hint.status, "feature 42 (Hydrography)");

  // Hover runs on every mouse move; Describe re-reads a row from the product.
  for (int i = 0; i < 20; ++i) {
    out.clear();
    adapter.HitTestPoint(Proj(), PixelPoint{25, 25}, 3.0, out);
  }
  EXPECT_EQ(source.describe_calls, 1);
}

TEST(AppVectorHitTest, WithNoSourceTheHitIsStillRealAndSimplyHasNoWords) {
  Overlay owner("dnc");
  fv::PickIndex index;
  index.AddBox(Ref(1, 7, 42), 10, fv::PixelRect{20, 20, 30, 30});
  fv::app::VectorHitTest adapter(owner);
  adapter.SetIndex(&index);

  std::vector<HitItem> out;
  adapter.HitTestPoint(Proj(), PixelPoint{25, 25}, 3.0, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_TRUE(out[0].hint.empty());
}

TEST(AppVectorHitTest, MaxItemsCapsWhatOneClickOffersTheUser) {
  Overlay owner("contours");
  fv::PickIndex index;
  for (int32_t i = 0; i < 40; ++i) {
    index.AddBox(Ref(1, 7, i), 10, fv::PixelRect{20, 20, 30, 30});
  }
  fv::app::VectorHitTest adapter(owner);
  adapter.SetIndex(&index);

  std::vector<HitItem> out;
  adapter.HitTestPoint(Proj(), PixelPoint{25, 25}, 3.0, out);
  EXPECT_EQ(out.size(), 8u);  // the default

  adapter.max_items = 0;  // 0 = every one of them
  out.clear();
  adapter.HitTestPoint(Proj(), PixelPoint{25, 25}, 3.0, out);
  EXPECT_EQ(out.size(), 40u);
}

// And the whole point of the adapter: it plugs into the session unchanged.
class VectorOverlay : public Overlay {
 public:
  VectorOverlay() : Overlay("dnc"), hit_(*this) {}
  fv::app::HitTest* AsHitTest() override { return &hit_; }
  fv::app::VectorHitTest hit_;
};

TEST(AppVectorHitTest, ItIsAPlainHitTestCapabilityAsFarAsTheSessionIsConcerned) {
  OverlayManager m;
  FakeShell shell;
  PickSession pick(m, shell);
  DescribingSource source;
  fv::PickIndex index;
  index.AddBox(Ref(1, 7, 42), 10, fv::PixelRect{20, 20, 30, 30});

  auto overlay = std::make_shared<VectorOverlay>();
  overlay->hit_.SetIndex(&index);
  overlay->hit_.SetSource(&source);
  ASSERT_TRUE(m.Add(overlay).ok());

  pick.UpdateHover(Proj(), PixelPoint{25, 25});
  ASSERT_NE(pick.hovered(), nullptr);
  EXPECT_EQ(shell.cursor, CursorId::kHand);
  ASSERT_EQ(shell.hint_calls.size(), 1u);
  EXPECT_EQ(shell.hint_calls[0].tool_tip, "feature 42");

  const auto hit = pick.ResolveClick(Proj(), PixelPoint{25, 25});
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(overlay->hit_.RefFor(hit->feature).feature, 42);
}

}  // namespace
