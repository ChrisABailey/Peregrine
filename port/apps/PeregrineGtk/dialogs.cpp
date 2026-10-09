// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "dialogs.h"

#include <utility>

namespace peregrine {
namespace {

/// Runs `start` with a completion slot and a nested main loop until the slot
/// has run. GTK's dialogs are asynchronous; the host's questions are not.
void RunNested(const std::function<void(const Gio::SlotAsyncReady&)>& start,
               const std::function<void(const Glib::RefPtr<Gio::AsyncResult>&)>& finish) {
  auto loop = Glib::MainLoop::create();
  bool done = false;
  start([&](const Glib::RefPtr<Gio::AsyncResult>& result) {
    finish(result);
    done = true;
    loop->quit();
  });
  if (!done) loop->run();
}

/// A file dialog starting in the request's directory with its filters.
Glib::RefPtr<Gtk::FileDialog> FileDialogFor(const fv::desk::DeskHost& host,
                                            const fv::desk::HostRequest& r) {
  auto dialog = Gtk::FileDialog::create();
  dialog->set_modal(true);
  if (!r.directory.empty()) dialog->set_initial_folder(Gio::File::create_for_path(r.directory));
  if (auto filters = RequestFilters(host)) {
    dialog->set_filters(filters);
    dialog->set_default_filter(filters->get_item(0));
  }
  return dialog;
}

/// The local path of a chosen file; "" for a URI with no local path.
std::string PathOf(const Glib::RefPtr<Gio::File>& file) {
  return file ? file->get_path() : std::string();
}

void ChooseOpen(fv::desk::DeskHost& host, Gtk::Window& parent, const fv::desk::HostRequest& r) {
  auto dialog = FileDialogFor(host, r);
  std::vector<std::string> paths;
  if (r.multiple) {
    RunNested([&](const Gio::SlotAsyncReady& slot) { dialog->open_multiple(parent, slot); },
              [&](const Glib::RefPtr<Gio::AsyncResult>& result) {
                try {
                  for (const auto& file : dialog->open_multiple_finish(result))
                    paths.push_back(PathOf(file));
                } catch (const Glib::Error&) {
                  // Dismissed: no answer is a cancel.
                }
              });
  } else {
    RunNested([&](const Gio::SlotAsyncReady& slot) { dialog->open(parent, slot); },
              [&](const Glib::RefPtr<Gio::AsyncResult>& result) {
                try {
                  paths.push_back(PathOf(dialog->open_finish(result)));
                } catch (const Glib::Error&) {
                }
              });
  }
  for (const std::string& path : paths)
    if (!path.empty()) host.AnswerPath(path);
}

void ChooseSave(fv::desk::DeskHost& host, Gtk::Window& parent, const fv::desk::HostRequest& r) {
  auto dialog = FileDialogFor(host, r);
  if (!r.suggested_name.empty()) dialog->set_initial_name(r.suggested_name);
  std::string path;
  RunNested([&](const Gio::SlotAsyncReady& slot) { dialog->save(parent, slot); },
            [&](const Glib::RefPtr<Gio::AsyncResult>& result) {
              try {
                path = PathOf(dialog->save_finish(result));
              } catch (const Glib::Error&) {
              }
            });
  if (path.empty()) return;
  // FileDialog does not report the filter in use; the core then goes by the
  // file's extension, as on macOS.
  host.AnswerPath(path);
  host.AnswerIndex(0);
}

void ChooseDirectory(fv::desk::DeskHost& host, Gtk::Window& parent,
                     const fv::desk::HostRequest& r) {
  auto dialog = Gtk::FileDialog::create();
  dialog->set_modal(true);
  dialog->set_title(r.title);
  dialog->set_accept_label("Choose");
  std::string path;
  RunNested([&](const Gio::SlotAsyncReady& slot) { dialog->select_folder(parent, slot); },
            [&](const Glib::RefPtr<Gio::AsyncResult>& result) {
              try {
                path = PathOf(dialog->select_folder_finish(result));
              } catch (const Glib::Error&) {
              }
            });
  if (!path.empty()) host.AnswerPath(path);
}

/// The text in curly quotes.
std::string Quoted(const std::string& text) { return "“" + text + "”"; }

}  // namespace

int AskSaveAnswer(int index) {
  switch (index) {
    case 0: return 1;
    case 2: return 0;
    default: return 2;
  }
}

Glib::RefPtr<Gio::ListStore<Gtk::FileFilter>> RequestFilters(const fv::desk::DeskHost& host) {
  const fv::desk::HostRequest r = host.PendingRequest();
  if (r.item_count <= 0) return {};
  auto store = Gio::ListStore<Gtk::FileFilter>::create();
  for (int i = 0; i < r.item_count; ++i) {
    auto filter = Gtk::FileFilter::create();
    filter->set_name(host.RequestItemLabel(i));
    const std::string patterns = host.RequestItemPattern(i);
    size_t start = 0;
    while (start <= patterns.size()) {
      size_t end = patterns.find(';', start);
      if (end == std::string::npos) end = patterns.size();
      std::string p = patterns.substr(start, end - start);
      p.erase(0, p.find_first_not_of(' '));
      p.erase(p.find_last_not_of(' ') + 1);
      if (p == "*.*") p = "*";
      if (!p.empty()) filter->add_pattern(p);
      start = end + 1;
    }
    store->append(filter);
  }
  return store;
}

void AnswerRequest(fv::desk::DeskHost& host, Gtk::Window& parent) {
  const fv::desk::HostRequest r = host.PendingRequest();
  switch (r.kind) {
    case fv::desk::kRequestAskSave: {
      // Buttons left to right; the host's answer is 0 save, 1 discard, 2 cancel.
      ChoiceDialog d(parent, "Save changes to " + Quoted(r.title) + "?",
                     "Your changes will be lost if you don’t save them.",
                     {"Don’t Save", "Cancel", "Save"}, 2, 1, 0);
      static constexpr int kAnswer[] = {1, 2, 0};
      host.AnswerIndex(kAnswer[d.Run()]);
      break;
    }
    case fv::desk::kRequestChooseOpen:
      ChooseOpen(host, parent, r);
      break;
    case fv::desk::kRequestChooseSave:
      ChooseSave(host, parent, r);
      break;
    case fv::desk::kRequestChooseFromList: {
      std::vector<std::string> rows;
      for (int i = 0; i < r.item_count; ++i) rows.push_back(host.RequestItemLabel(i));
      ChoiceDialog d(parent, r.title, std::string(), {"Cancel", "OK"}, 1, 0);
      d.SetRows(rows);
      if (d.Run() == 1 && d.selected_row() >= 0) host.AnswerIndex(d.selected_row());
      break;
    }
    case fv::desk::kRequestConfirmRevert: {
      ChoiceDialog d(parent, "Revert " + Quoted(r.title) + " to the saved version?",
                     "Changes since the last save will be lost.", {"Cancel", "Revert"}, 1, 0, 1);
      host.AnswerIndex(d.Run() == 1 ? 1 : 0);
      break;
    }
    case fv::desk::kRequestChooseDirectory:
      ChooseDirectory(host, parent, r);
      break;
    default:
      break;
  }
}

// MARK: ChoiceDialog

ChoiceDialog::ChoiceDialog(Gtk::Window& parent, const std::string& message,
                           const std::string& detail, const std::vector<std::string>& buttons,
                           int default_button, int cancel_button, int destructive_button)
    : cancel_button_(cancel_button) {
  set_transient_for(parent);
  set_modal(true);
  set_resizable(false);
  set_hide_on_close(true);
  set_title(std::string());
  add_css_class("message");
  box_.set_margin(20);
  message_.set_text(message);
  message_.add_css_class("title-4");
  message_.set_wrap(true);
  message_.set_max_width_chars(50);
  message_.set_xalign(0);
  box_.append(message_);
  if (!detail.empty()) {
    detail_.set_text(detail);
    detail_.set_wrap(true);
    detail_.set_max_width_chars(50);
    detail_.set_xalign(0);
    box_.append(detail_);
  }
  buttons_.set_halign(Gtk::Align::END);
  buttons_.set_margin_top(10);
  for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
    auto* b = Gtk::make_managed<Gtk::Button>(buttons[static_cast<size_t>(i)]);
    b->set_use_underline(false);
    if (i == default_button) {
      b->add_css_class("suggested-action");
      set_default_widget(*b);
    }
    if (i == destructive_button) b->add_css_class("destructive-action");
    b->signal_clicked().connect([this, i] { Respond(i); });
    buttons_.append(*b);
  }
  box_.append(buttons_);
  set_child(box_);

  signal_close_request().connect(
      [this] {
        if (!response_) Respond(cancel_button_);
        return false;
      },
      false);
  auto keys = Gtk::EventControllerKey::create();
  keys->signal_key_pressed().connect(
      [this](guint keyval, guint, Gdk::ModifierType) {
        if (keyval != GDK_KEY_Escape) return false;
        Respond(cancel_button_);
        return true;
      },
      false);
  add_controller(keys);
}

void ChoiceDialog::SetRows(const std::vector<std::string>& rows) {
  std::vector<Glib::ustring> labels(rows.begin(), rows.end());
  if (!rows_) {
    rows_ = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create(labels));
    rows_->set_size_request(420, -1);
    box_.insert_child_after(*rows_, message_);
  } else {
    rows_->set_model(Gtk::StringList::create(labels));
  }
  rows_->set_selected(rows.empty() ? GTK_INVALID_LIST_POSITION : 0);
}

int ChoiceDialog::selected_row() const {
  if (!rows_) return -1;
  const guint selected = rows_->get_selected();
  return selected == GTK_INVALID_LIST_POSITION ? -1 : static_cast<int>(selected);
}

void ChoiceDialog::Respond(int index) {
  if (response_) return;
  response_ = index;
  set_visible(false);
  if (loop_) loop_->quit();
}

int ChoiceDialog::Run() {
  response_.reset();
  present();
  if (Gtk::Widget* d = get_default_widget()) d->grab_focus();
  loop_ = Glib::MainLoop::create();
  if (!response_) loop_->run();
  loop_.reset();
  return *response_;
}

// MARK: JobDialog

JobDialog::JobDialog(Gtk::Window& parent, const std::string& title,
                     std::function<void()> on_cancel)
    : on_cancel_(std::move(on_cancel)) {
  set_transient_for(parent);
  set_modal(true);
  set_resizable(false);
  set_deletable(false);
  set_title(title);
  box_.set_margin_top(16);
  box_.set_margin_bottom(16);
  box_.set_margin_start(20);
  box_.set_margin_end(20);
  heading_.set_text(title);
  heading_.add_css_class("heading");
  heading_.set_xalign(0);
  bar_.set_size_request(400, -1);
  text_.set_xalign(0);
  text_.set_ellipsize(Pango::EllipsizeMode::MIDDLE);
  text_.set_single_line_mode(true);
  text_.set_max_width_chars(1);
  text_.set_hexpand(true);
  cancel_.set_halign(Gtk::Align::END);
  cancel_.set_margin_top(4);
  cancel_.signal_clicked().connect([this] { Cancel(); });
  box_.append(heading_);
  box_.append(bar_);
  box_.append(text_);
  box_.append(cancel_);
  set_child(box_);

  // The window manager's close and Escape cancel the job; the dialog stays
  // until the job has stopped.
  signal_close_request().connect(
      [this] {
        Cancel();
        return true;
      },
      false);
  auto keys = Gtk::EventControllerKey::create();
  keys->signal_key_pressed().connect(
      [this](guint keyval, guint, Gdk::ModifierType) {
        if (keyval != GDK_KEY_Escape) return false;
        Cancel();
        return true;
      },
      false);
  add_controller(keys);
}

void JobDialog::Update(double fraction, const std::string& text) {
  bar_.set_fraction(fraction < 0 ? 0 : fraction > 1 ? 1 : fraction);
  if (!cancelled_) text_.set_text(text);
}

void JobDialog::Cancel() {
  if (cancelled_) return;
  cancelled_ = true;
  cancel_.set_sensitive(false);
  text_.set_text("Cancelling after the current data source…");
  if (on_cancel_) on_cancel_();
}

}  // namespace peregrine
