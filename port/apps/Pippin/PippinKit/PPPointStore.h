// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPPointStore.h — the point document's life in the app.
//
// `PPRouteStore` for points, and the same shape: everything about the points
// testable on the mac is in this header, and everything else is in
// `PPMap`/`PPPoint` above it.
//
// `fv::PointOverlay` already reads, writes, draws and picks a `.fvpoints`
// document but has no editor. This is the editor's other half: not the
// gestures, which are Swift, but three rules underneath them.
//
// The seed, which `RouteStore` has no equivalent of. A route is something the
// user makes; a point set is something the app ships and the user then edits.
// The pack carries `kiawah.fvpoints` in a read-only bundle, so on the first
// launch the document is copied — opened and saved forward — into
// `Documents/`, and the bundle's copy is never read again. Drawing the
// bundle's set and writing edits elsewhere would be two sources of truth for
// one map, and the first app update would silently revert somebody's edits.
//
// Every edit writes, for `RouteStore`'s reason: a write ordered by the shell
// would race the next edit and could leave an older set on disk than the one
// on screen. So `AddPoint`/`UpdatePoint`/`RemovePoint` each end in a save, as
// part of the same call.
//
// A failed write is not a refused edit. The point is on screen and correct;
// what the user loses is the next launch, and `last_write_error()` is how a
// shell says so. The alternative is a map that undoes a rider's edit the
// moment their disk fills up.
//
// Deliberately absent: a queue, a thread, a lock and a callback. This class is
// as thread-hostile as `PPMap`, which owns it, and belongs to the same queue.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fvkit/geo.h"
#include "fvkit/overlay/point_overlay.h"
#include "fvkit/proj.h"

namespace pippin {

class PointStore {
 public:
  // Neither path has to exist. A missing seed is a pack that ships no points,
  // which starts the app empty rather than failing, and a missing document is
  // what a first launch looks like.
  //
  // `document_path` may be empty at construction and set later: `PPMap` builds
  // its store before anything draws and learns where `Documents/` is
  // afterwards, from the one layer allowed to ask Foundation. A store with no
  // document path edits happily and persists nothing, which is also what a
  // test that does not care about disk wants.
  PointStore(std::string seed_path, std::string document_path);

  // The overlay, for the caller to put in its stack.
  const std::shared_ptr<fv::PointOverlay>& overlay() const { return overlay_; }

  const std::string& document_path() const { return document_path_; }
  void set_document_path(std::string path) { document_path_ = std::move(path); }

  // Opens the user's document, seeding it from the pack's copy the first time.
  //
  // Three outcomes, only one an error:
  //   * the document exists         -> it is opened;
  //   * it does not and a seed does -> the seed is opened and saved to the
  //                                    document path, so the next launch
  //                                    takes the first branch;
  //   * neither                     -> an empty set, and `Ok`.
  //
  // A document that exists and will not read is reported, and the app still
  // comes up with no points rather than refusing to start. The seed is
  // deliberately not tried as a fallback: overwriting a corrupt-looking file
  // with the factory set would destroy whatever the user still had, and the
  // rescue for that is a copy off the phone.
  fv::Status LoadAtLaunch();

  // --- the set ------------------------------------------------------------

  const std::vector<fv::MapPoint>& Points() const {
    return overlay_->points();
  }
  const fv::MapPoint* Find(int64_t id) const { return overlay_->Find(id); }

  // The palette the document carries, for a symbol picker to offer.
  const std::vector<fv::PointSymbol>& Symbols() const {
    return overlay_->symbols();
  }

  // Adds, and persists. An id of 0, the usual case for a point made in the UI,
  // gets the next free one, and the id it got is returned; that is what the
  // shell edits and deletes by.
  int64_t AddPoint(fv::MapPoint point);

  // Replaces the row with `point.id`, and persists. False, and nothing
  // written, when there is no such row: a sheet holding an id deleted on
  // another screen gets a refusal rather than a resurrection.
  bool UpdatePoint(const fv::MapPoint& point);

  bool RemovePoint(int64_t id);

  // --- what is under a finger ---------------------------------------------

  // The nearest point within `tolerance_px` of `p`, or 0.
  //
  // Nearest wins with no ambiguity question, which is a phone decision rather
  // than a disagreement with A5: the desktop pick session can ask which one
  // you meant because it has a mouse, a context menu and a status bar, while
  // a rider with one thumb gets the closest and taps again. The full
  // aggregation is still in `PointOverlay::HitTestPoint`.
  //
  // `tolerance_px` is in surface pixels, like everything the projection
  // speaks. The caller converts from points, knowing the backing scale.
  int64_t HitTest(const fv::MapProjection& proj, fv::PixelPoint p,
                  double tolerance_px) const;

  // --- display state (not document state) ---------------------------------

  // Drawn or not: the points button. Not persisted in the document, which is
  // a set of places rather than a record of whether somebody had them
  // switched on. The startup value comes from `pippin.ini`.
  void SetVisible(bool on) { overlay_->SetVisible(on); }
  bool visible() const { return overlay_->IsVisible(); }

  void SetShowLabels(bool on) { overlay_->SetShowLabels(on); }
  bool show_labels() const { return overlay_->show_labels(); }

  // The selected point draws highlighted. Selection is display state too, and
  // `PointOverlay` does not dirty the document for it, so this does not
  // write.
  void SetSelected(int64_t id) { overlay_->SetSelected(id); }
  int64_t selected() const { return overlay_->selected(); }

  // --- what went wrong ----------------------------------------------------

  // The error from the last write, or empty. A failed write is not a refused
  // edit; see the header.
  const std::string& last_write_error() const { return last_write_error_; }

  // True once `LoadAtLaunch` copied the pack's set into `Documents/`. Worth
  // one line on screen the first time, because it distinguishes points the app
  // shipped from points the user made.
  bool seeded() const { return seeded_; }

 private:
  // Writes the document, or does nothing when there is no path. Records a
  // failure rather than returning it; see the header.
  void Persist();

  std::string seed_path_;
  std::string document_path_;
  std::shared_ptr<fv::PointOverlay> overlay_;

  std::string last_write_error_;
  bool seeded_ = false;
};

}  // namespace pippin
