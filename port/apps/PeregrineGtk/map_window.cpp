// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "map_window.h"

#include <filesystem>
#include <iostream>
#include <map>

namespace peregrine {
namespace {

/// The menu bar and status bar paint the window background, and the status
/// bar a top rule, so a snapshot of the content shows them as the window does.
constexpr char kCss[] =
    ".menu-bar-row { background-color: @window_bg_color; }"
    ".status-bar { background-color: @window_bg_color; border-top: 1px solid @borders;"
    " padding: 4px 10px; }";

void InstallCss() {
  static bool installed = false;
  if (installed) return;
  installed = true;
  auto provider = Gtk::CssProvider::create();
  provider->load_from_data(kCss);
  Gtk::StyleContext::add_provider_for_display(Gdk::Display::get_default(), provider,
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void SetUpLabel(Gtk::Label& label) {
  label.set_xalign(0);
  label.set_ellipsize(Pango::EllipsizeMode::END);
  label.set_single_line_mode(true);
}

/// The freedesktop symbolic icon for a DeskKit icon name; "" when there is
/// none, so the button shows its label instead.
std::string IconName(const std::string& icon) {
  static const std::map<std::string, std::string> kIcons = {
      {"zoom_in", "zoom-in-symbolic"},
      {"zoom_out", "zoom-out-symbolic"},
      {"recenter", "find-location-symbolic"},
      {"grid", "view-grid-symbolic"},
      {"points", "mark-location-symbolic"},
      {"tamask", "dialog-warning-symbolic"},
      {"pencil", "document-edit-symbolic"},
  };
  const auto it = kIcons.find(icon);
  return it == kIcons.end() ? std::string() : it->second;
}

}  // namespace

MapWindow::MapWindow()
    : host_(fv::desk::DeskHost::Create()),
      canvas_(*host_),
      menus_(*host_, [this](const std::string& id) { Execute(id); }) {
  const std::string settings_error = host_->LoadSettings(std::string());
  InstallCss();
  set_title("Peregrine");
  set_default_size(1100, 750);
  set_size_request(400, 300);
  set_titlebar(header_);
  header_.pack_start(toolbar_);
  insert_action_group(CommandMenus::kActionPrefix, menus_.actions());
  menu_bar_.set_menu_model(menus_.menu_bar());
  menu_bar_.add_css_class("menu-bar-row");

  for (Gtk::Label* l : {&scale_label_, &product_label_, &message_label_, &position_label_})
    SetUpLabel(*l);
  position_label_.add_css_class("numeric");
  position_label_.set_hexpand(true);
  position_label_.set_xalign(1);
  status_bar_.add_css_class("status-bar");
  status_bar_.append(scale_label_);
  status_bar_.append(product_label_);
  status_bar_.append(message_label_);
  status_bar_.append(position_label_);
  content_.append(menu_bar_);
  content_.append(canvas_);
  content_.append(status_bar_);
  set_child(content_);
  canvas_.grab_focus();

  canvas_.signal_status_changed().connect([this] { RefreshStatus(); });
  canvas_.signal_error().connect([this](const std::string& e) { PresentError("Peregrine", e); });
  canvas_.signal_tick().connect([this](const fv::desk::HostTick& t) { Handle(t); });
  signal_close_request().connect(sigc::mem_fun(*this, &MapWindow::OnCloseRequest), false);
  RefreshStatus();
  UpdateMenus();
  if (!settings_error.empty()) {
    Glib::signal_idle().connect_once(
        [this, settings_error] { PresentError("Settings could not be read", settings_error); });
  }
}

MapWindow::~MapWindow() = default;

void MapWindow::RefreshStatus() {
  scale_label_.set_text(host_->StatusScale());
  product_label_.set_text(host_->StatusProduct());
  message_label_.set_text(host_->StatusMessage());
  position_label_.set_text(host_->StatusPosition());
}

// MARK: Catalog and commands

bool MapWindow::OpenCatalog(const std::string& path) {
  const std::string error = host_->OpenCatalog(path);
  if (!error.empty()) {
    PresentError("Could not open the map catalog", error);
    return false;
  }
  CatalogDidChange();
  RefreshStatus();
  return true;
}

/// Titles the window after the open catalog.
void MapWindow::CatalogDidChange() {
  const std::string path = host_->CatalogPath();
  if (path.empty() || path == ":memory:") return;
  set_title("Peregrine — " + std::filesystem::path(path).stem().string());
}

/// Reacts to what the host reports each frame: menus, the catalog, notices,
/// and anything that can change a command's enabled or checked state.
void MapWindow::Handle(const fv::desk::HostTick& t) {
  if (t.menus_changed) UpdateMenus();
  if (t.catalog_changed) CatalogDidChange();
  if (t.new_frame || t.redraw || t.status_changed || t.catalog_changed || t.job_changed)
    menus_.Refresh();
  const std::string notice = host_->TakeNotice();
  if (!notice.empty()) PresentNotice(notice);
}

void MapWindow::GoTo(double lat, double lon, double scale) {
  host_->GoTo(lat, lon, scale > 0 ? scale : host_->ScaleDenom());
}

void MapWindow::Execute(const std::string& command_id) {
  const std::string error = host_->Execute(command_id);
  if (!error.empty()) PresentError("Command failed", error);
  if (host_->QuitRequested()) {
    close();
    return;
  }
  menus_.Refresh();
}

void MapWindow::UpdateMenus() {
  if (menus_.Rebuild()) RebuildToolbar();
  menus_.Refresh();
  // The window is added to the application after construction; the
  // shortcuts are installed on the first update after that.
  Glib::RefPtr<Gtk::Application> app = get_application();
  if (!app) return;
  for (const std::string& action : accelerated_actions_) app->unset_accels_for_action(action);
  accelerated_actions_.clear();
  for (const auto& [action, accel] : menus_.accelerators()) {
    app->set_accels_for_action(action, {accel});
    accelerated_actions_.push_back(action);
  }
}

/// Replaces the header bar's buttons when the toolbar's commands changed: one
/// icon button per command, a toggle for a checkable one, and a gap between
/// groups.
void MapWindow::RebuildToolbar() {
  if (menus_.toolbar() == toolbar_items_ && toolbar_.get_first_child() != nullptr) return;
  toolbar_items_ = menus_.toolbar();
  while (Gtk::Widget* child = toolbar_.get_first_child()) toolbar_.remove(*child);
  auto theme = Gtk::IconTheme::get_for_display(get_display());
  for (const ToolbarItem& item : toolbar_items_) {
    if (item.command.empty()) {
      auto* gap = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
      gap->add_css_class("spacer");
      toolbar_.append(*gap);
      continue;
    }
    Gtk::Button* button = item.checkable ? Gtk::make_managed<Gtk::ToggleButton>()
                                         : Gtk::make_managed<Gtk::Button>();
    const std::string icon = IconName(item.icon);
    if (!icon.empty() && theme->has_icon(icon)) button->set_icon_name(icon);
    else button->set_label(item.label);
    button->set_tooltip_text(item.label);
    button->set_action_name(item.action);
    button->set_focus_on_click(false);
    toolbar_.append(*button);
  }
}

/// Closing the window runs DeskKit's quit, which asks about each unsaved
/// overlay; a cancel keeps the window open.
bool MapWindow::OnCloseRequest() {
  if (!host_->QuitRequested()) host_->Execute("app.quit");
  return !host_->QuitRequested();
}

void MapWindow::PresentError(const std::string& message, const std::string& detail) {
  std::cerr << message << ": " << detail << "\n";
  auto alert = Gtk::AlertDialog::create(message);
  alert->set_detail(detail);
  alert->show(*this);
}

void MapWindow::PresentNotice(const std::string& notice) {
  Gtk::AlertDialog::create(notice)->show(*this);
}

// MARK: Snapshot

std::string MapWindow::WriteSnapshot(const std::string& path) {
  canvas_.RenderNow();
  RefreshStatus();
  const int w = content_.get_width();
  const int h = content_.get_height();
  GtkNative* native = gtk_widget_get_native(GTK_WIDGET(gobj()));
  GskRenderer* renderer = native ? gtk_native_get_renderer(native) : nullptr;
  if (w <= 0 || h <= 0 || !renderer) return "the window is not shown";
  const double scale = canvas_.get_scale_factor();

  // The content is drawn through the window, as in a frame, then moved to
  // the origin; a widget paintable would hold only the last painted frame.
  graphene_rect_t bounds;
  if (!gtk_widget_compute_bounds(GTK_WIDGET(content_.gobj()), GTK_WIDGET(gobj()), &bounds))
    return "the window is not laid out";
  GtkSnapshot* snapshot = gtk_snapshot_new();
  gtk_snapshot_scale(snapshot, static_cast<float>(scale), static_cast<float>(scale));
  const graphene_point_t origin = GRAPHENE_POINT_INIT(-bounds.origin.x, -bounds.origin.y);
  gtk_snapshot_translate(snapshot, &origin);
  gtk_widget_snapshot_child(GTK_WIDGET(gobj()), GTK_WIDGET(content_.gobj()), snapshot);
  GskRenderNode* node = gtk_snapshot_free_to_node(snapshot);
  if (!node) return "nothing was drawn";
  const graphene_rect_t viewport = GRAPHENE_RECT_INIT(
      0.f, 0.f, static_cast<float>(w * scale), static_cast<float>(h * scale));
  GdkTexture* texture = gsk_renderer_render_texture(renderer, node, &viewport);
  gsk_render_node_unref(node);
  const bool saved = texture && gdk_texture_save_to_png(texture, path.c_str());
  if (texture) g_object_unref(texture);
  if (!saved) return "could not write " + path;

  std::cerr << std::filesystem::path(path).filename().string() << ": " << host_->StatusScale()
            << " | " << host_->StatusProduct() << " | " << host_->StatusMessage() << "\n";
  return std::string();
}

}  // namespace peregrine
