// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_fake.h"

#include "fv_desk_base_map.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/tools/png_write.h"

namespace fv {
namespace desk {

app::AppShell::SaveAnswer FakeDeskShell::AskSave(const std::string& name) {
  asked_save.push_back(name);
  return next_save_answer;
}

std::vector<std::string> FakeDeskShell::ChooseFilesToOpen(const app::FileTypeDesc& d) {
  last_open_chooser = d;
  return files_to_open;
}

std::pair<std::string, int> FakeDeskShell::ChooseSaveSpec(const app::FileTypeDesc&,
                                                          const std::string&) {
  return {save_spec, save_format};
}

std::optional<int> FakeDeskShell::ChooseFromList(const std::string&,
                                                 const std::vector<std::string>& rows) {
  list_rows = rows;
  return list_choice;
}

FakeDesk::FakeDesk(int width, int height, MapGroups groups) {
  desk_ = std::make_unique<Desk>(shell_, settings_, std::move(groups));
  desk_->map_view().Resize(width, height, 1.0, kNativeDisplayMmPerPixel);
}

Status FakeDesk::Run(const std::vector<std::string>& command_ids) {
  for (const std::string& id : command_ids) {
    const Status s = desk_->Execute(id);
    if (!s.ok()) return s;
  }
  return Status::Ok();
}

Status FakeDesk::Render(PixelBuffer* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "out is null");
  const view::Viewport& v = desk_->map_view().View();
  if (!v.HasSurface()) return Status::Error(kInvalidArg, "the view has no surface");
  CpuCanvas canvas(v.PixelWidth(), v.PixelHeight());
  canvas.Clear(FvColor{0, 0, 0, 255});

  const view::MapView& mv = desk_->map_view();
  if (mv.HasProduct() && desk_->catalog()) {
    BaseMapRenderer base;
    base.SetCatalog(desk_->catalog());
    const Status s = base.Render(v, mv.Product(), canvas);
    if (!s.ok()) return s;
  }
  const Status s = desk_->overlays().DrawAll(v.Projection(), canvas);
  if (!s.ok()) return s;
  *out = canvas.Buffer();
  return Status::Ok();
}

Status FakeDesk::RenderPng(const std::string& path) {
  PixelBuffer buf;
  const Status s = Render(&buf);
  if (!s.ok()) return s;
  return WritePng(buf, path);
}

}  // namespace desk
}  // namespace fv
