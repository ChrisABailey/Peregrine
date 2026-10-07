// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_options.h"

#include "fv_desk_user_settings.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/settings.h"

namespace fv {
namespace desk {
namespace {

/// The settings-file spelling of `v`: a choice by name, as `Properties::SaveTo` writes it.
std::string SettingText(const app::PropertySpec& spec, const app::PropertyValue& v) {
  if (spec.type == app::PropertyType::kChoice && v.i >= 0 &&
      v.i < static_cast<long long>(spec.choices.size()))
    return spec.choices[static_cast<size_t>(v.i)];
  return v.ToString();
}

}  // namespace

bool OptionsField::changed() const { return !app::SameValue(value, applied); }

// MARK: OptionsPage

std::unique_ptr<OptionsPage> OptionsPage::ForType(const app::OverlayTypeDesc& type,
                                                  const Settings& settings) {
  if (type.display_name.empty() || !type.factory) return nullptr;
  std::shared_ptr<Overlay> proto = type.factory();
  app::Properties* props = proto ? proto->AsProperties() : nullptr;
  if (props == nullptr || props->Describe().empty()) return nullptr;

  std::unique_ptr<OptionsPage> page(new OptionsPage());
  page->id_ = type.id;
  page->title_ = type.display_name;
  page->icon_ = type.icon;
  page->prefix_ = app::SettingsPrefixForTypeId(type.id);
  // The same load OverlaySession::Instantiate does, so the page opens on what
  // a new overlay of the type would show.
  props->LoadFrom(settings, page->prefix_);
  for (const app::PropertySpec& spec : props->Describe()) {
    OptionsField f;
    f.spec = spec;
    if (!props->GetProperty(spec.key, &f.value).ok()) f.value = spec.default_value;
    f.applied = f.value;
    page->fields_.push_back(std::move(f));

    auto section = page->sections_.begin();
    while (section != page->sections_.end() && section->title != spec.group) ++section;
    if (section == page->sections_.end()) {
      page->sections_.push_back(OptionsSection{spec.group, {}});
      section = page->sections_.end() - 1;
    }
    section->keys.push_back(spec.key);
  }
  page->prototype_ = std::move(proto);
  return page;
}

const OptionsField* OptionsPage::Field(const std::string& key) const {
  for (const OptionsField& f : fields_)
    if (f.spec.key == key) return &f;
  return nullptr;
}

OptionsField* OptionsPage::MutableField(const std::string& key) {
  for (OptionsField& f : fields_)
    if (f.spec.key == key) return &f;
  return nullptr;
}

Status OptionsPage::Set(const std::string& key, const app::PropertyValue& value) {
  OptionsField* f = MutableField(key);
  if (f == nullptr) return Status::Error(kNotFound, "no option '" + key + "' on " + title_);
  app::Properties* props = prototype_->AsProperties();
  const Status s = props->SetProperty(key, value);
  if (!s.ok()) return s;
  if (!props->GetProperty(key, &f->value).ok()) f->value = value;
  return Status::Ok();
}

Status OptionsPage::ResetToDefaults() {
  for (OptionsField& f : fields_) {
    const Status s = Set(f.spec.key, f.spec.default_value);
    if (!s.ok()) return s;
  }
  return Status::Ok();
}

void OptionsPage::Revert() {
  app::Properties* props = prototype_->AsProperties();
  for (OptionsField& f : fields_) {
    props->SetProperty(f.spec.key, f.applied);
    f.value = f.applied;
  }
}

bool OptionsPage::dirty() const {
  for (const OptionsField& f : fields_)
    if (f.changed()) return true;
  return false;
}

std::vector<std::pair<std::string, std::string>> OptionsPage::PendingSettings() const {
  std::vector<std::pair<std::string, std::string>> out;
  for (const OptionsField& f : fields_)
    if (f.changed()) out.emplace_back(prefix_ + f.spec.key, SettingText(f.spec, f.value));
  return out;
}

void OptionsPage::MarkApplied() {
  for (OptionsField& f : fields_) f.applied = f.value;
}

// MARK: OptionsModel

OptionsModel::OptionsModel(const app::OverlayTypeRegistry& types, const Settings& settings) {
  for (const app::OverlayTypeDesc* d : types.All())
    if (auto page = OptionsPage::ForType(*d, settings)) pages_.push_back(std::move(page));
}

OptionsPage* OptionsModel::Page(const app::TypeId& id) {
  for (const auto& p : pages_)
    if (p->id() == id) return p.get();
  return nullptr;
}

bool OptionsModel::dirty() const {
  for (const auto& p : pages_)
    if (p->dirty()) return true;
  return false;
}

void OptionsModel::Revert() {
  for (const auto& p : pages_) p->Revert();
}

Status OptionsModel::Apply(Settings& settings, OverlayManager& overlays, UserSettings* user,
                           std::vector<std::string>* warnings) {
  for (const auto& page : pages_) {
    if (!page->dirty()) continue;
    for (const auto& kv : page->PendingSettings()) {
      settings.Set(kv.first, kv.second);
      if (user != nullptr) user->Set(kv.first, kv.second);
    }
    for (const std::shared_ptr<Overlay>& o : overlays.OfType(page->id())) {
      app::Properties* props = o->AsProperties();
      if (props == nullptr) continue;
      for (const OptionsField& f : page->fields()) {
        if (!f.changed()) continue;
        const Status s = props->SetProperty(f.spec.key, f.value);
        if (!s.ok() && warnings != nullptr)
          warnings->push_back(o->Name() + ": " + f.spec.key + " rejected: " + s.message);
      }
    }
    page->MarkApplied();
  }
  return Status::Ok();
}

}  // namespace desk
}  // namespace fv
