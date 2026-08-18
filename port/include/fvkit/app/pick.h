// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/app/pick.h — hover, click deconfliction, snap-to and context-menu
// composition (fvkit-app-plan-COMPLETE.md §3e).
//
// This is the one place the plan REORGANISES FalconView rather than mirroring
// it. FalconView's test_select/selected pair gives each overlay a veto in stack
// order -- the first overlay to say "yes, mine" ends the walk -- and every
// richer answer (the snap-to chooser above all) was bolted on per feature.
// Here picking is a first-class AGGREGATION over the HitTest capability: every
// overlay on screen answers, the answers are ranked by a stated policy, and the
// tie is broken by a rule the caller chose instead of by stack order alone.
//
// PICKING DOES NOT REPLACE ROUTING, and the order between them is the shell's:
// routing runs FIRST (mouse capture, direct routing, an editor mid-gesture),
// and the pick session resolves the click only when no overlay consumed it.
// That is why nothing here consults capture or RoutingOverrides -- by the time
// a PickSession sees a click, the stack has already declined it.
//
// WHO IS ASKED, and it is the same set in every method here:
// OverlayManager::DrawOrder() reversed -- on-screen overlays, topmost first,
// honouring visibility and declutter and the top-most band. The pick index a
// vector overlay hit-tests against is built from the ink it EMITTED (L4's
// fvkit/vector/pick.h), so asking anything but the drawn set would let a tap
// land on something the user cannot see.
//
// TWO NAMING DEVIATIONS from the plan's code block, both forced:
//   * the verbs are `HitTestPoint` and `SnapToPoint`, never `HitTest` or
//     `SnapTo` -- those are the capability CLASSES in this same namespace, and
//     a member function of either name hides the type inside the class body,
//     so the very call the method has to make (`o->AsHitTest()` into a
//     `HitTest*`) stops compiling. Naming them after the capability's own
//     method instead is both legal and the more honest description.
//   * `UpdateHover` takes the policy too. A hover cannot ask a question, so
//     kAskWhenAmbiguous degrades to kTopMost for the cursor and hint; the
//     alternative (a hover that opens the chooser dialog) is not a choice.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "fvkit/app/capabilities.h"  // HitItem, SnapToItem
#include "fvkit/app/shell.h"         // AppShell, CursorId, HintText, MenuNode
#include "fvkit/geo.h"               // PixelPoint
#include "fvkit/proj.h"

namespace fv {

class OverlayManager;

namespace app {

// How several answers under one point become one answer.
enum class PickPolicy {
  // Stack order first, distance only within an overlay: the thing drawn ON TOP
  // wins even if something below it is nearer the cursor. FalconView's click
  // behaviour, and what a mouse user expects -- they aimed at what they saw.
  kTopMost,
  // Distance first, stack order as the tie-break. What a finger needs: the
  // tap point is a blunt instrument and the nearest ink is the intent.
  kNearest,
  // kTopMost, except that more than one candidate within tolerance is a
  // QUESTION rather than a ranking -- AppShell::ChooseFromList, which is the
  // shape FalconView's snap-to dialog already had. A single candidate is never
  // worth asking about and is returned directly.
  kAskWhenAmbiguous,
};

const char* ToString(PickPolicy p);

class PickSession {
 public:
  // Neither is owned; both must outlive the session. The session holds no
  // state about the stack -- it re-walks it per call -- so it survives any
  // amount of adding, removing and reordering with nothing to invalidate.
  PickSession(OverlayManager& manager, AppShell& shell);

  // ~ test_select: "what would a click here do". Aggregates, ranks, and gives
  // the shell the winner's cursor and hint (the defaults, kDefault and an
  // empty HintText, when nothing is under the point).
  //
  // ONLY ON A CHANGE. This runs on every mouse move, and a shell that rebuilt
  // a tooltip window sixty times a second because the cursor moved two pixels
  // along the same road would be the defect this call exists to avoid. The
  // same stance as Persistence::set_dirty: the notification is the CHANGE.
  void UpdateHover(const MapProjection& proj, PixelPoint p,
                   PickPolicy policy = PickPolicy::kTopMost);

  // What the last UpdateHover settled on; null when nothing was under it.
  const HitItem* hovered() const {
    return hovered_ ? &*hovered_ : nullptr;
  }
  // Forget the hover without asking the stack anything -- the cursor leaving
  // the map, or a redraw that invalidated every pick index. The shell is told
  // (cursor back to default, hint cleared) if there was anything to clear.
  void ClearHover();

  // Click resolution, for a click routing did not consume. nullopt = nothing
  // there, or the user cancelled the chooser -- which are the same outcome for
  // every caller and so are not distinguished, exactly as
  // AppShell::ChooseFilesToOpen's empty vector is.
  std::optional<HitItem> ResolveClick(const MapProjection& proj, PixelPoint p,
                                      PickPolicy policy = PickPolicy::kTopMost);

  // Every candidate under the point, ranked by the policy but never asking
  // anything. The building block the two calls above share, exposed because an
  // "N features here" readout is a real UI and should not have to re-implement
  // the walk. Appends nothing to the shell.
  std::vector<HitItem> HitTestPoint(
      const MapProjection& proj, PixelPoint p,
      PickPolicy policy = PickPolicy::kTopMost) const;

  // ~ test_snap_to/do_snap_to, and FalconView's snptodlg flow verbatim:
  // 0 answers => nullopt, 1 => it with no dialog, n => the chooser.
  //
  // "Aggregated across ALL overlays" (plan §3e) means every overlay that
  // ANSWERS rather than the first one -- not every overlay that exists. An
  // invisible or decluttered overlay is not on screen, and a cursor that
  // snapped to it would jump to something the user cannot see.
  std::optional<SnapToItem> SnapToPoint(const MapProjection& proj,
                                        PixelPoint p);

  // Composition, top-down: each overlay's ContextMenu capability appends its
  // own section, separated from the one above it. The returned root has an
  // empty label and is a pure container.
  MenuNode BuildContextMenu(const MapProjection& proj, PixelPoint p) const;
  // The same, then AppShell::ShowContextMenu. Returns false -- and does NOT
  // call the shell -- when no overlay contributed anything, because an empty
  // menu flashing open on a right-click is worse than no menu.
  bool ShowContextMenu(const MapProjection& proj, PixelPoint p);

  // The one knob (plan §3e). Pixels, and the shell scales it for the device:
  // the core has no business knowing a finger is wider than a mouse pointer.
  double tolerance_px = 8.0;

 private:
  // Candidates in stack order, each tagged with the rank of the overlay that
  // produced it (0 = topmost). Ranking is a stable sort on that plus distance.
  std::vector<HitItem> Gather(const MapProjection& proj, PixelPoint p,
                              std::vector<int>* ranks) const;

  OverlayManager& manager_;
  AppShell& shell_;
  std::optional<HitItem> hovered_;
};

// The chooser row for a hit: "<overlay>: <hint>", or just one of the two when
// the other is empty. Exposed because the ambiguity dialog is the one place a
// shell may want to render the rows itself.
std::string PickRowText(const HitItem& item);

}  // namespace app
}  // namespace fv
