// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/app/manager_host.h"

#include "fvkit/app/pick.h"
#include "fvkit/formats/source.h"
#include "fvkit/overlay/manager.h"

namespace fv {
namespace app {

ManagerHost::~ManagerHost() {
  for (const std::shared_ptr<Overlay>& o : manager_.Overlays()) {
    HostAware* h = o->AsHostAware();
    if (h != nullptr && h->host() == this) h->SetHost(nullptr);
  }
}

bool ManagerHost::Bind(Overlay& overlay) {
  HostAware* h = overlay.AsHostAware();
  if (h == nullptr) return false;
  h->SetHost(this);
  return true;
}

void ManagerHost::Unbind(Overlay& overlay) {
  if (HostAware* h = overlay.AsHostAware()) h->SetHost(nullptr);
  ReleaseMouse(overlay);
  undo_.RemoveOwner(&overlay);
}

Status ManagerHost::CaptureMouse(Overlay& overlay) { return manager_.CaptureMouse(&overlay); }

void ManagerHost::ReleaseMouse(Overlay& overlay) {
  if (manager_.mouse_capture() == &overlay) manager_.ReleaseMouse();
}

bool ManagerHost::ElevationAt(const GeoPoint& p, double* metres) {
  float m = 0;
  if (!elevation_ || !elevation_->GetElevation(p, &m).ok()) return false;
  *metres = m;
  return true;
}

std::vector<CoverageRow> ManagerHost::Coverage(const std::string& format, const GeoRect& area) {
  std::vector<CoverageRow> rows, out;
  if (!catalog_ || !catalog_->SelectByGeoRect(area, &rows).ok()) return out;
  for (CoverageRow& r : rows)
    if (r.format == format) out.push_back(std::move(r));
  return out;
}

std::vector<SnapToItem> ManagerHost::SnapCandidates(const MapProjection& proj, PixelPoint p,
                                                    double tolerance_px) {
  return app::SnapCandidates(manager_, proj, p, tolerance_px);
}

}  // namespace app
}  // namespace fv
