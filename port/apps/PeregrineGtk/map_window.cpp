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

/// The version the log's first line names; the app has no release number yet.
constexpr char kAppVersion[] = "(unversioned)";

}  // namespace

MapWindow::MapWindow()
    : host_(fv::desk::DeskHost::Create()),
      canvas_(*host_),
      menus_(*host_, [this](const std::string& id) { Execute(id); }) {
  host_->SetPropertiesTabs(true);
  const std::string settings_error = host_->LoadSettings(std::string());
  const std::string log_error = host_->StartLog(std::string(), kAppVersion);
  if (!log_error.empty()) std::cerr << "Peregrine: no log file: " << log_error << "\n";
  InstallCss();
  set_title("Peregrine");
  set_default_size(1100, 750);
  set_size_request(400, 300);
  set_titlebar(header_);
  header_.pack_start(toolbar_);
  insert_action_group(CommandMenus::kActionPrefix, menus_.actions());
  // DeskKit's menus, then Help, which the macOS app also adds in its shell.
  add_action("show-log", sigc::mem_fun(*this, &MapWindow::ShowLog));
  auto help = Gio::Menu::create();
  help->append("Show Log", "win.show-log");
  auto bar = Gio::Menu::create();
  bar->append_section(menus_.menu_bar());
  bar->append_submenu("Help", help);
  menu_bar_.set_menu_model(bar);
  menu_bar_.add_css_class("menu-bar-row");

  // Three equal thirds keep the scale centred whatever the sides hold.
  for (Gtk::Label* l : {&feedback_label_, &scale_label_, &product_label_, &position_label_,
                        &elevation_label_})
    SetUpLabel(*l);
  for (Gtk::Label* l : {&position_label_, &elevation_label_}) l->add_css_class("numeric");
  status_left_.append(feedback_label_);
  status_centre_.set_halign(Gtk::Align::CENTER);
  status_centre_.append(scale_label_);
  status_centre_.append(product_label_);
  status_right_.set_halign(Gtk::Align::END);
  status_right_.append(position_label_);
  status_right_.append(elevation_label_);
  status_bar_.set_homogeneous(true);
  status_bar_.add_css_class("status-bar");
  status_bar_.append(status_left_);
  status_bar_.append(status_centre_);
  status_bar_.append(status_right_);
  content_.append(menu_bar_);
  content_.append(canvas_);
  content_.append(status_bar_);
  set_child(content_);
  canvas_.grab_focus();

  canvas_.signal_status_changed().connect([this] { RefreshStatus(); });
  canvas_.signal_error().connect([this](const std::string& e) { PresentError("Peregrine", e); });
  canvas_.signal_tick().connect([this](const fv::desk::HostTick& t) { Handle(t); });
  signal_close_request().connect(sigc::mem_fun(*this, &MapWindow::OnCloseRequest), false);
  host_->SetRequestHandler(
      [](void* context) { static_cast<MapWindow*>(context)->AnswerRequest(); }, this);
  RefreshStatus();
  UpdateMenus();
  if (!settings_error.empty()) {
    Glib::signal_idle().connect_once(
        [this, settings_error] { PresentError("Settings could not be read", settings_error); });
  }
}

MapWindow::~MapWindow() { host_->SetRequestHandler(nullptr, nullptr); }

/// Copies the host's status bar text and hover tool tip into the window.
void MapWindow::RefreshStatus() {
  feedback_label_.set_text(host_->StatusFeedback());
  scale_label_.set_text(host_->StatusScale());
  product_label_.set_text(host_->StatusProduct());
  position_label_.set_text(host_->StatusPosition());
  elevation_label_.set_text(host_->StatusElevation());
  const std::string tip = host_->HoverToolTip();
  if (tip != tooltip_) {
    tooltip_ = tip;
    canvas_.set_tooltip_text(tip);
    canvas_.set_has_tooltip(!tip.empty());
  }
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

/// Remembers the catalog's name for the title.
void MapWindow::CatalogDidChange() {
  const std::string path = host_->CatalogPath();
  if (path.empty() || path == ":memory:") return;
  catalog_title_ = "Peregrine — " + std::filesystem::path(path).stem().string();
  UpdateTitle();
}

/// Titles the window after the current or topmost overlay file, or the
/// catalog when no overlay file is open.
void MapWindow::UpdateTitle() {
  const std::string document = host_->DocumentName();
  const std::string title = document.empty() ? catalog_title_ : document;
  if (get_title().raw() != title) set_title(title);
}

/// Reacts to what the host reports each frame: menus, the catalog, notices,
/// and anything that can change a command's enabled or checked state.
void MapWindow::Handle(const fv::desk::HostTick& t) {
  UpdateTitle();
  if (t.menus_changed) UpdateMenus();
  if (t.catalog_changed) CatalogDidChange();
  if (t.job_changed) UpdateJob();
  if (t.options_requested) {
    const int kind = host_->TakeOptionsRequest();
    if (kind == fv::desk::kOptionsMap || kind == fv::desk::kOptionsOverlay) {
      ShowPanel(kind == fv::desk::kOptionsMap ? map_options_ : overlay_options_,
                [this, kind] { return std::make_unique<OptionsWindow>(*host_, kind, *this); });
    }
  }
  if (t.sources_requested && host_->TakeSourcesRequest()) ShowDataSources();
  if (t.profile_requested && host_->TakeRouteProfileRequest())
    ShowPanel(profile_, [this] { return std::make_unique<ProfileWindow>(*host_, *this); });
  if (t.profile_changed && profile_ && profile_->get_visible()) profile_->Reload();
  if (t.layers_requested && host_->TakeLayersRequest())
    ShowPanel(layers_, [this] {
      return std::make_unique<LayersWindow>(*host_, *this, [this](const std::string& id) { Execute(id); });
    });
  if (t.layers_changed && layers_ && layers_->get_visible()) layers_->Reload();
  if (t.form_requested && host_->TakeFeatureFormRequest())
    ShowPanel(feature_form_, [this] { return std::make_unique<FeatureFormWindow>(*host_, *this); });
  if (t.table_requested && host_->TakeAttributeTableRequest())
    ShowPanel(attribute_table_, [this] { return std::make_unique<AttributeTableWindow>(*host_, *this); });
  if (t.style_requested && host_->TakeLayerStyleRequest())
    ShowPanel(layer_style_, [this] { return std::make_unique<LayerStyleWindow>(*host_, *this); });
  if (t.new_layer_requested && host_->TakeNewLayerRequest())
    ShowPanel(new_layer_, [this] { return std::make_unique<NewLayerWindow>(*host_, *this); });
  if (t.properties_requested && host_->TakeLayerPropertiesRequest())
    ShowPanel(properties_, [this] { return std::make_unique<PropertiesWindow>(*host_, *this); });
  if (t.features_changed) FeaturesDidChange();
  if (t.new_frame || t.redraw || t.status_changed || t.catalog_changed || t.job_changed)
    menus_.Refresh();
  const std::string notice = host_->TakeNotice();
  if (!notice.empty()) PresentNotice(notice);
}

template <typename W, typename Make>
void MapWindow::ShowPanel(std::unique_ptr<W>& window, Make make) {
  if (window) window->Reload();
  else window = make();
  window->present();
}

/// Hides a form, table or style dialog whose model closed, follows the tabs of
/// Properties, and refreshes the table. The form is not rebuilt here: it holds
/// the user's edits.
void MapWindow::FeaturesDidChange() {
  if (feature_form_ && !host_->FeatureFormOpen()) feature_form_->set_visible(false);
  // The style dialog shows one feature's style; a layer's style is a tab of Properties.
  if (layer_style_ && !(host_->LayerStyleOpen() && host_->LayerStyleIsFeature())) layer_style_->set_visible(false);
  if (properties_ && properties_->get_visible() && !properties_->Refresh()) properties_->set_visible(false);
  if (attribute_table_) {
    if (!host_->AttributeTableOpen()) attribute_table_->set_visible(false);
    else if (attribute_table_->get_visible()) attribute_table_->Reload();
  }
}

/// Answers the host's question with a modal dialog. The map holds still
/// meanwhile: the host is inside the call that asked.
void MapWindow::AnswerRequest() {
  canvas_.SetHeld(true);
  peregrine::AnswerRequest(*host_, *this);
  canvas_.SetHeld(false);
}

/// Shows, advances or dismisses the progress dialog of a catalog build.
void MapWindow::UpdateJob() {
  if (data_sources_) data_sources_->Reload();
  if (!host_->JobActive()) {
    job_dialog_.reset();
    return;
  }
  if (!job_dialog_) {
    job_dialog_ = std::make_unique<JobDialog>(*this, "Building the map catalog",
                                              [this] { host_->CancelJob(); });
    job_dialog_->present();
  }
  job_dialog_->Update(host_->JobFraction(), host_->JobText());
}

void MapWindow::ShowDataSources() {
  if (data_sources_) {
    data_sources_->Reload();
    data_sources_->present();
    return;
  }
  data_sources_ = std::make_unique<DataSourcesWindow>(
      *host_, *this,
      [this](const std::string& message, const std::string& detail, Gtk::Window& parent) {
        PresentError(message, detail, parent);
      });
  data_sources_->present();
}

void MapWindow::ShowLog() {
  const std::string path = host_->LogFilePath();
  if (path.empty()) {
    PresentError("No log file", "The application log could not be started.");
    return;
  }
  auto launcher = Gtk::FileLauncher::create(Gio::File::create_for_path(path));
  launcher->launch(*this, [this, launcher](const Glib::RefPtr<Gio::AsyncResult>& result) {
    try {
      launcher->launch_finish(result);
    } catch (const Glib::Error& e) {
      PresentError("Could not open the log", e.what());
    }
  });
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
    const std::string icon = IconNameFor(item.icon);
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
  PresentError(message, detail, *this);
}

void MapWindow::PresentError(const std::string& message, const std::string& detail,
                             Gtk::Window& parent) {
  std::cerr << message << ": " << detail << "\n";
  auto alert = Gtk::AlertDialog::create(message);
  alert->set_detail(detail);
  alert->show(parent);
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
