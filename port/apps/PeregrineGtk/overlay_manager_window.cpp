// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "overlay_manager_window.h"

#include <filesystem>
#include <iostream>

namespace peregrine {
namespace {

/// The table's columns: title and width in pixels, as the macOS table's; the
/// last one takes the slack.
struct Column {
  const char* title;
  int width;
};
constexpr Column kColumns[] = {{"Name", 230}, {"Version", 80}, {"Location", 80}, {"State", 200}};

/// `text` with each newline replaced by `separator`.
std::string JoinLines(const std::string& text, const std::string& separator) {
  std::string out;
  for (char c : text) {
    if (c == '\n') out += separator;
    else out += c;
  }
  return out;
}

/// One table row: a label per column, the last one expanding. The heading
/// row sits outside the list, so it is indented by the list's frame and row
/// padding.
Gtk::Box* MakeRow(const std::vector<std::string>& cells, bool heading) {
  auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
  row->set_margin_start(heading ? 9 : 6);
  row->set_margin_end(6);
  row->set_margin_top(3);
  row->set_margin_bottom(3);
  for (size_t i = 0; i < cells.size(); ++i) {
    auto* label = Gtk::make_managed<Gtk::Label>(cells[i]);
    label->set_xalign(0);
    label->set_ellipsize(Pango::EllipsizeMode::END);
    // Without a character width an ellipsizing label would shrink to "…".
    label->set_width_chars(1);
    if (i + 1 == cells.size()) label->set_hexpand(true);
    else label->set_size_request(kColumns[i].width, -1);
    if (heading) label->add_css_class("heading");
    row->append(*label);
  }
  return row;
}

}  // namespace

OverlayManagerWindow::OverlayManagerWindow(fv::desk::DeskHost& host, Gtk::Window& parent)
    : host_(host) {
  set_title("Overlay Manager");
  set_transient_for(parent);
  set_default_size(720, 520);
  set_size_request(560, 380);
  set_hide_on_close(true);

  auto* header = MakeRow({kColumns[0].title, kColumns[1].title, kColumns[2].title,
                          kColumns[3].title}, true);
  list_.set_selection_mode(Gtk::SelectionMode::SINGLE);
  list_.signal_selected_rows_changed().connect([this] {
    if (!loading_) UpdateDetail();
  });
  list_scroll_.set_child(list_);
  list_scroll_.set_has_frame(true);
  list_scroll_.set_vexpand(true);
  list_scroll_.set_size_request(-1, 140);
  list_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

  detail_.set_editable(false);
  detail_.set_cursor_visible(false);
  detail_.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
  detail_.set_left_margin(6);
  detail_.set_right_margin(6);
  detail_.set_top_margin(6);
  detail_.set_bottom_margin(6);
  detail_scroll_.set_child(detail_);
  detail_scroll_.set_has_frame(true);
  detail_scroll_.set_vexpand(true);
  detail_scroll_.set_size_request(-1, 112);
  detail_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

  banner_.set_text("Restart Peregrine to finish the changes marked “at relaunch”.");
  banner_.set_xalign(0);
  banner_.set_wrap(true);
  banner_.add_css_class("warning");

  install_.signal_clicked().connect([this] { ChooseFile(); });
  install_folder_.signal_clicked().connect([this] { ChooseFolder(); });
  enable_.signal_clicked().connect([this] { ToggleEnabled(); });
  remove_.signal_clicked().connect([this] { RemoveSelected(); });
  reveal_.signal_clicked().connect([this] { Reveal(); });
  close_.signal_clicked().connect([this] { close(); });
  auto* buttons = ButtonRow({&install_, &install_folder_, &enable_, &remove_, &reveal_}, {&close_});

  box_.append(*header);
  box_.append(list_scroll_);
  box_.append(detail_scroll_);
  box_.append(banner_);
  box_.append(*buttons);
  SetDialogMargins(box_);
  set_child(box_);
  CloseOnEscape(*this);
  Reload();
}

void OverlayManagerWindow::Reload() {
  const fv::desk::HostPackage* old = SelectedPackage();
  const std::string selected = old ? old->id : std::string();
  loading_ = true;
  while (Gtk::Widget* row = list_.get_first_child()) list_.remove(*row);
  rows_.clear();
  const int count = host_.PackageCount();
  for (int i = 0; i < count; ++i) rows_.push_back(host_.PackageAt(i));
  for (const fv::desk::HostPackage& p : rows_) {
    const std::string state = p.pending.empty() ? p.state : p.state + " · " + p.pending;
    Gtk::Box* row = MakeRow({p.name, p.version, p.location, state}, false);
    auto* state_label = dynamic_cast<Gtk::Label*>(row->get_last_child());
    if (state_label && (p.state == "Failed" || p.state == "Unavailable"))
      state_label->add_css_class("error");
    else if (state_label && (p.state == "Disabled" || p.state == "Shadowed"))
      state_label->add_css_class("dim-label");
    row->set_tooltip_text(p.id);
    list_.append(*row);
  }
  loading_ = false;
  if (selected.empty() || !Select(selected)) list_.unselect_all();
  banner_.set_visible(host_.PackagesRestartPending());
  UpdateDetail();
}

bool OverlayManagerWindow::Select(const std::string& id) {
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].id != id) continue;
    list_.select_row(*list_.get_row_at_index(static_cast<int>(i)));
    return true;
  }
  return false;
}

const fv::desk::HostPackage* OverlayManagerWindow::SelectedPackage() const {
  const Gtk::ListBoxRow* row = list_.get_selected_row();
  if (!row) return nullptr;
  const int index = row->get_index();
  return index >= 0 && index < static_cast<int>(rows_.size()) ? &rows_[index] : nullptr;
}

std::string OverlayManagerWindow::detail_text() const {
  return detail_.get_buffer()->get_text().raw();
}

/// The detail pane and the buttons for the selected package.
void OverlayManagerWindow::UpdateDetail() {
  const fv::desk::HostPackage* p = SelectedPackage();
  if (!p) {
    detail_.get_buffer()->set_text(
        rows_.empty() ? "No overlay packages are installed. Install… adds a .pgpkg file."
                      : "Select a package to see its details.");
    enable_.set_label("Disable");
    enable_.set_sensitive(false);
    remove_.set_sensitive(false);
    reveal_.set_sensitive(false);
    return;
  }
  std::string text = p->name + " " + p->version + "\n" + p->id + "\n\n";
  if (!p->description.empty()) text += p->description + "\n\n";
  const std::pair<const char*, const std::string*> fields[] = {
      {"Publisher", &p->publisher}, {"License", &p->license},
      {"Runs code", &p->code_kinds}, {"Location", &p->dir}};
  for (const auto& [label, value] : fields)
    if (!value->empty()) text += std::string(label) + ": " + *value + "\n";
  if (!p->overlays.empty()) text += "\nOverlays:\n  " + JoinLines(p->overlays, "\n  ") + "\n";
  if (!p->tools.empty()) text += "\nTools:\n  " + JoinLines(p->tools, "\n  ") + "\n";
  if (!p->reason.empty()) text += "\n" + p->state + ": " + p->reason + "\n";
  detail_.get_buffer()->set_text(text);
  enable_.set_label(p->enabled ? "Disable" : "Enable");
  enable_.set_sensitive(p->state != "Shadowed");
  remove_.set_sensitive(p->removable && p->location != "System");
  reveal_.set_sensitive(true);
}

void OverlayManagerWindow::ConfirmInstall(const std::string& path, bool link) {
  const fv::desk::HostPackage p = host_.InspectPackage(path);
  if (!p.error.empty()) {
    Alert("“" + std::filesystem::path(path).filename().string() + "” is not an overlay package",
          p.error);
    return;
  }
  std::vector<std::string> lines;
  lines.push_back("Publisher: " + (p.publisher.empty() ? std::string("unknown") : p.publisher));
  if (!p.code_kinds.empty())
    lines.push_back("Runs code: " + p.code_kinds +
                    ". It runs with your permissions; install it only if you trust the publisher.");
  if (!p.overlays.empty()) lines.push_back("Overlays: " + JoinLines(p.overlays, ", "));
  if (!p.tools.empty()) lines.push_back("Tools: " + JoinLines(p.tools, ", "));
  if (link) lines.push_back("The folder is used in place; removing the package leaves it untouched.");
  std::string detail;
  for (const std::string& line : lines) detail += (detail.empty() ? "" : "\n\n") + line;
  if (Ask("Install “" + p.name + "” " + p.version + "?", detail, {"Cancel", "Install"}, 1, 0, -1) != 1)
    return;
  Install(path, link);
}

void OverlayManagerWindow::Install(const std::string& path, bool link) {
  const fv::desk::HostInstallOutcome o = link ? host_.LinkPackage(path) : host_.InstallPackage(path);
  Reload();
  if (!o.error.empty()) {
    Alert("Could not install the package", o.error);
    return;
  }
  Select(o.id);
  if (!o.live) {
    const fv::desk::HostPackage* p = SelectedPackage();
    const std::string name = p ? "“" + p->name + "”" : o.id;
    Alert(name + " is ready", "It replaces version " + o.replaced_version +
                                  " when Peregrine restarts.");
  }
}

void OverlayManagerWindow::ToggleEnabled() {
  const fv::desk::HostPackage* p = SelectedPackage();
  if (!p) return;
  const std::string error = host_.SetPackageEnabled(p->id, !p->enabled);
  if (!error.empty()) Alert("Could not change the package", error);
  Reload();
}

void OverlayManagerWindow::RemoveSelected() {
  const fv::desk::HostPackage* selected = SelectedPackage();
  if (!selected) return;
  const fv::desk::HostPackage p = *selected;
  const std::string detail =
      p.location == "Linked"
          ? "Peregrine stops using the folder; its files are left in place."
          : "Its files are deleted. A package in use is removed when Peregrine restarts.";
  if (Ask("Remove “" + p.name + "”?", detail, {"Cancel", "Remove"}, 1, 0, 1) != 1) return;
  const std::string error = host_.RemovePackage(p.id);
  if (!error.empty()) Alert("Could not remove the package", error);
  Reload();
}

void OverlayManagerWindow::ChooseFile() {
  auto dialog = Gtk::FileDialog::create();
  dialog->set_modal(true);
  dialog->set_title("Choose an overlay package (.pgpkg)");
  dialog->set_accept_label("Install");
  auto filters = Gio::ListStore<Gtk::FileFilter>::create();
  auto packages = Gtk::FileFilter::create();
  packages->set_name("Overlay packages");
  packages->add_pattern("*.pgpkg");
  filters->append(packages);
  dialog->set_filters(filters);
  dialog->set_default_filter(packages);
  dialog->open(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& r) {
    try {
      auto file = dialog->open_finish(r);
      if (!file || file->get_path().empty()) return;
      const std::string path = file->get_path();
      Glib::signal_idle().connect_once([this, path] { ConfirmInstall(path); });
    } catch (const Glib::Error&) {
      // dismissed
    }
  });
}

void OverlayManagerWindow::ChooseFolder() {
  auto dialog = Gtk::FileDialog::create();
  dialog->set_modal(true);
  dialog->set_title("Choose a package folder (it holds peregrine-package.json); it is used in place");
  dialog->set_accept_label("Install");
  dialog->select_folder(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& r) {
    try {
      auto folder = dialog->select_folder_finish(r);
      if (!folder || folder->get_path().empty()) return;
      const std::string path = folder->get_path();
      Glib::signal_idle().connect_once([this, path] { ConfirmInstall(path, true); });
    } catch (const Glib::Error&) {
      // dismissed
    }
  });
}

/// Opens the file manager on the package's directory.
void OverlayManagerWindow::Reveal() {
  const fv::desk::HostPackage* p = SelectedPackage();
  if (!p) return;
  auto launcher = Gtk::FileLauncher::create(Gio::File::create_for_path(p->dir));
  launcher->open_containing_folder(*this, [this, launcher](const Glib::RefPtr<Gio::AsyncResult>& r) {
    try {
      launcher->open_containing_folder_finish(r);
    } catch (const Glib::Error& e) {
      Alert("Could not show the package", e.what());
    }
  });
}

int OverlayManagerWindow::Ask(const std::string& message, const std::string& detail,
                              const std::vector<std::string>& buttons, int default_button,
                              int cancel_button, int destructive_button) {
  // The window may not be showing yet when a .pgpkg is opened at launch.
  Gtk::Window& parent = get_visible() ? static_cast<Gtk::Window&>(*this) : *get_transient_for();
  confirm_ = std::make_unique<ChoiceDialog>(parent, message, detail, buttons, default_button,
                                            cancel_button, destructive_button);
  return confirm_->Run();
}

void OverlayManagerWindow::Alert(const std::string& message, const std::string& detail) {
  std::cerr << message << ": " << detail << "\n";
  auto alert = Gtk::AlertDialog::create(message);
  alert->set_detail(detail);
  alert->show(get_visible() ? static_cast<Gtk::Window&>(*this) : *get_transient_for());
}

}  // namespace peregrine
