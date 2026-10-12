// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_layer_properties.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

#include "fv_desk_desk.h"

namespace fv {
namespace desk {
namespace {

LayerGeneral Read(const Overlay& o) {
  LayerGeneral g;
  g.name = o.Name();
  g.description = o.description();
  g.credits = o.credits();
  g.min_scale = o.min_scale();
  g.max_scale = o.max_scale();
  return g;
}

std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

}  // namespace

std::string PropertiesTabTitle(PropertiesTab tab) {
  switch (tab) {
    case PropertiesTab::kSource: return "Source";
    case PropertiesTab::kGeneral: return "General";
    case PropertiesTab::kSymbology: return "Symbology";
    case PropertiesTab::kLabels: return "Labels";
    case PropertiesTab::kFields: return "Fields";
  }
  return std::string();
}

Status ParseScale(const std::string& text, double* scale_denom) {
  std::string s = Trim(text);
  if (s.empty()) {
    *scale_denom = 0;
    return Status::Ok();
  }
  if (s.size() > 2 && s[0] == '1' && s[1] == ':') s = Trim(s.substr(2));
  std::string digits;
  for (char c : s)
    if (c != ',') digits += c;
  char* end = nullptr;
  const double v = digits.empty() ? 0 : std::strtod(digits.c_str(), &end);
  if (digits.empty() || *end != '\0' || !std::isfinite(v) || v < 1)
    return Status::Error(kInvalidArg, "“" + Trim(text) + "” is not a scale; type one such as 1:50,000");
  *scale_denom = v;
  return Status::Ok();
}

Status LayerGeneralModel::Open(const std::shared_ptr<Overlay>& overlay, std::shared_ptr<LayerGeneralModel>* out) {
  if (!overlay) return Status::Error(kInvalidArg, "no overlay");
  std::shared_ptr<LayerGeneralModel> m(new LayerGeneralModel());
  m->overlay_ = overlay;
  m->overlay_id_ = overlay.get();
  m->applied_ = Read(*overlay);
  m->draft_ = m->applied_;
  *out = std::move(m);
  return Status::Ok();
}

Status LayerGeneralModel::Set(const LayerGeneral& general) {
  if (Trim(general.name).empty()) return Status::Error(kInvalidArg, "a layer needs a name");
  if (general.min_scale < 0 || general.max_scale < 0) return Status::Error(kInvalidArg, "a scale cannot be negative");
  if (general.min_scale > 0 && general.max_scale > 0 && general.min_scale <= general.max_scale)
    return Status::Error(kInvalidArg, "the minimum scale (" + FormatScale(general.min_scale) +
                                          ") must be smaller than the maximum scale (" +
                                          FormatScale(general.max_scale) + "), or the layer never draws");
  draft_ = general;
  return Status::Ok();
}

Status LayerGeneralModel::Apply() {
  const std::shared_ptr<Overlay> o = overlay_.lock();
  if (!o) return Status::Error(kNotFound, "the layer has closed");
  o->SetName(draft_.name);
  o->SetDescription(draft_.description);
  o->SetCredits(draft_.credits);
  o->SetScaleRange(draft_.min_scale, draft_.max_scale);
  applied_ = Read(*o);
  draft_ = applied_;
  return Status::Ok();
}

Status LayerGeneralModel::Revert() {
  const std::shared_ptr<Overlay> o = overlay_.lock();
  if (!o) return Status::Error(kNotFound, "the layer has closed");
  applied_ = Read(*o);
  draft_ = applied_;
  return Status::Ok();
}

}  // namespace desk
}  // namespace fv
