// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "PPPointStore.h"

#include <algorithm>
#include <filesystem>
#include <utility>
#include <vector>

#include "fvkit/app/capabilities.h"

namespace pippin {

PointStore::PointStore(std::string seed_path, std::string document_path)
    : seed_path_(std::move(seed_path)),
      document_path_(std::move(document_path)),
      overlay_(std::make_shared<fv::PointOverlay>("Points")) {}

void PointStore::Persist() {
  last_write_error_.clear();
  if (document_path_.empty()) return;

  // An empty set is still a document, and this is where points differ from
  // the route. `RouteStore::Persist` deletes the file when the last waypoint
  // goes, because no route and an empty route are the same state. Points are
  // not: a user who deleted every point wants an empty map next launch, and a
  // deleted document would be re-seeded from the pack and hand them all back.
  // So an empty set is written, and the file records that somebody was here.
  const fv::Status s = overlay_->FileSaveAs(document_path_, 0);
  if (!s.ok()) {
    last_write_error_ = s.message;
    return;
  }
  overlay_->set_dirty(false);
}

fv::Status PointStore::LoadAtLaunch() {
  if (document_path_.empty()) {
    // No disk at all: a store that edits in memory. Still seed it, so a test
    // (and a pack read before Foundation has been asked where `Documents/` is)
    // has the shipped set to look at.
    if (seed_path_.empty()) return fv::Status::Ok();
    std::error_code ec;
    if (!std::filesystem::exists(seed_path_, ec)) return fv::Status::Ok();
    return overlay_->FileOpen(seed_path_);
  }

  std::error_code ec;
  if (std::filesystem::exists(document_path_, ec)) {
    const fv::Status s = overlay_->FileOpen(document_path_);
    if (!s.ok()) {
      // Reported, and the app still comes up. The seed is deliberately NOT
      // tried here — see the header: replacing a file we could not read with
      // the factory set would destroy whatever is still in it.
      return s;
    }
    overlay_->set_dirty(false);
    return fv::Status::Ok();
  }

  // A first launch. Seed it if the pack has one, and WRITE the copy now rather
  // than at the first edit: from this moment the document exists, the branch
  // above is the one every later launch takes, and the bundle's copy is never
  // read again.
  if (!seed_path_.empty() && std::filesystem::exists(seed_path_, ec)) {
    const fv::Status s = overlay_->FileOpen(seed_path_);
    if (!s.ok()) {
      // A pack whose points file will not open is a staging bug, and it is
      // worth saying — but it is not worth refusing to start over, and the app
      // comes up with an empty set that the user can add to.
      overlay_->FileNew();
      return s;
    }
    seeded_ = true;
  }
  Persist();
  if (!last_write_error_.empty())
    return fv::Status::Error(fv::kIoError, last_write_error_);
  return fv::Status::Ok();
}

int64_t PointStore::AddPoint(fv::MapPoint point) {
  const int64_t id = overlay_->AddPoint(std::move(point));
  Persist();
  return id;
}

bool PointStore::UpdatePoint(const fv::MapPoint& point) {
  if (!overlay_->UpdatePoint(point)) return false;
  Persist();
  return true;
}

bool PointStore::RemovePoint(int64_t id) {
  if (!overlay_->RemovePoint(id)) return false;
  Persist();
  return true;
}

int64_t PointStore::HitTest(const fv::MapProjection& proj, fv::PixelPoint p,
                            double tolerance_px) const {
  std::vector<fv::app::HitItem> hits;
  overlay_->HitTestPoint(proj, p, tolerance_px, hits);
  if (hits.empty()) return 0;
  // NEAREST WINS. `HitTestPoint` emits in document order and ranks nothing —
  // A5's ranking belongs to the pick session, which a phone does not run — so
  // the choice is made here, once, and it is the smallest `distance_px`.
  const auto it = std::min_element(
      hits.begin(), hits.end(),
      [](const fv::app::HitItem& a, const fv::app::HitItem& b) {
        return a.distance_px < b.distance_px;
      });
  return (int64_t)it->feature;
}

}  // namespace pippin
