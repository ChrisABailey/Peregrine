// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_options.h"

#include "fv_desk_map_groups.h"
#include "fv_desk_user_settings.h"
#include "fvkit/log.h"
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

bool OptionsPage::AddPart(std::shared_ptr<app::Properties> props, const std::string& prefix,
                          std::function<void(const app::Properties&)> apply,
                          const Settings& settings) {
  if (props == nullptr || props->Describe().empty()) return false;
  const size_t part = parts_.size();
  // The same load OverlaySession::Instantiate does, so the page opens on what
  // a new overlay of the type would show.
  props->LoadFrom(settings, prefix);
  for (const app::PropertySpec& spec : props->Describe()) {
    if (Field(spec.key) != nullptr) {
      FV_LOG_WARNING(title_ + ": option '" + spec.key + "' appears twice; the second is skipped");
      continue;
    }
    OptionsField f;
    f.spec = spec;
    if (!props->GetProperty(spec.key, &f.value).ok()) f.value = spec.default_value;
    f.applied = f.value;
    fields_.push_back(std::move(f));
    field_part_.push_back(part);

    auto section = sections_.begin();
    while (section != sections_.end() && section->title != spec.group) ++section;
    if (section == sections_.end()) {
      sections_.push_back(OptionsSection{spec.group, {}});
      section = sections_.end() - 1;
    }
    section->keys.push_back(spec.key);
  }
  parts_.push_back(Part{prefix, std::move(props), std::move(apply)});
  return true;
}

std::unique_ptr<OptionsPage> OptionsPage::ForType(const app::OverlayTypeDesc& type,
                                                  const Settings& settings,
                                                  const std::vector<MapOptionsSource>& extra) {
  if (type.display_name.empty() || !type.factory) return nullptr;
  std::unique_ptr<OptionsPage> page(new OptionsPage());
  page->id_ = type.id;
  page->title_ = type.display_name;
  page->icon_ = type.icon;
  std::shared_ptr<Overlay> proto = type.factory();
  if (app::Properties* props = proto ? proto->AsProperties() : nullptr) {
    // Aliases the overlay, which owns the Properties it returns.
    page->AddPart(std::shared_ptr<app::Properties>(proto, props),
                  app::SettingsPrefixForTypeId(type.id), nullptr, settings);
  }
  for (const MapOptionsSource& src : extra)
    if (src.group_id == type.id && src.make)
      page->AddPart(std::shared_ptr<app::Properties>(src.make()), src.prefix, src.apply,
                    settings);
  if (page->fields_.empty()) return nullptr;
  return page;
}

std::unique_ptr<OptionsPage> OptionsPage::ForMapGroup(const std::string& title,
                                                      const MapOptionsSource& source,
                                                      const Settings& settings) {
  if (!source.make) return nullptr;
  std::unique_ptr<OptionsPage> page(new OptionsPage());
  page->id_ = source.group_id;
  page->title_ = title;
  if (!page->AddPart(std::shared_ptr<app::Properties>(source.make()), source.prefix,
                     source.apply, settings))
    return nullptr;
  return page;
}

const OptionsField* OptionsPage::Field(const std::string& key) const {
  for (const OptionsField& f : fields_)
    if (f.spec.key == key) return &f;
  return nullptr;
}

OptionsField* OptionsPage::MutableField(const std::string& key, size_t* part) {
  for (size_t i = 0; i < fields_.size(); ++i) {
    if (fields_[i].spec.key != key) continue;
    if (part != nullptr) *part = field_part_[i];
    return &fields_[i];
  }
  return nullptr;
}

bool OptionsPage::PartDirty(size_t part) const {
  for (size_t i = 0; i < fields_.size(); ++i)
    if (field_part_[i] == part && fields_[i].changed()) return true;
  return false;
}

Status OptionsPage::Set(const std::string& key, const app::PropertyValue& value) {
  size_t part = 0;
  OptionsField* f = MutableField(key, &part);
  if (f == nullptr) return Status::Error(kNotFound, "no option '" + key + "' on " + title_);
  app::Properties* props = parts_[part].prototype.get();
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
  for (size_t i = 0; i < fields_.size(); ++i) {
    OptionsField& f = fields_[i];
    parts_[field_part_[i]].prototype->SetProperty(f.spec.key, f.applied);
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
  for (size_t i = 0; i < fields_.size(); ++i) {
    const OptionsField& f = fields_[i];
    if (f.changed())
      out.emplace_back(parts_[field_part_[i]].prefix + f.spec.key, SettingText(f.spec, f.value));
  }
  return out;
}

void OptionsPage::MarkApplied() {
  for (OptionsField& f : fields_) f.applied = f.value;
}

// MARK: OptionsModel

OptionsModel::OptionsModel(const app::OverlayTypeRegistry& types, const Settings& settings,
                           const std::vector<MapOptionsSource>& extra) {
  for (const app::OverlayTypeDesc* d : types.All())
    if (auto page = OptionsPage::ForType(*d, settings, extra)) pages_.push_back(std::move(page));
}

OptionsModel::OptionsModel(const MapGroups& groups,
                           const std::vector<MapOptionsSource>& sources,
                           const Settings& settings) {
  for (const MapGroup& g : groups.All())
    for (const MapOptionsSource& src : sources)
      if (src.group_id == g.id)
        if (auto page = OptionsPage::ForMapGroup(g.title, src, settings))
          pages_.push_back(std::move(page));
}

OptionsPage* OptionsModel::Page(const app::TypeId& id) {
  for (const auto& p : pages_)
    if (p->id() == id) return p.get();
  return nullptr;
}

bool OptionsModel::SelectPage(const app::TypeId& id) {
  for (size_t i = 0; i < pages_.size(); ++i) {
    if (pages_[i]->id() != id) continue;
    initial_page_ = static_cast<int>(i);
    return true;
  }
  return false;
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
    for (size_t part = 0; part < page->parts_.size(); ++part) {
      const OptionsPage::Part& p = page->parts_[part];
      if (p.apply && page->PartDirty(part)) p.apply(*p.prototype);
    }
    for (const std::shared_ptr<Overlay>& o : overlays.OfType(page->id())) {
      app::Properties* props = o->AsProperties();
      if (props == nullptr) continue;
      for (size_t i = 0; i < page->fields().size(); ++i) {
        const OptionsField& f = page->fields()[i];
        if (!f.changed() || page->parts_[page->field_part_[i]].apply) continue;
        const Status s = props->SetProperty(f.spec.key, f.value);
        if (s.ok()) continue;
        const std::string w = o->Name() + ": " + f.spec.key + " rejected: " + s.message;
        FV_LOG_WARNING(w);
        if (warnings != nullptr) warnings->push_back(w);
      }
    }
    page->MarkApplied();
  }
  return Status::Ok();
}

}  // namespace desk
}  // namespace fv
