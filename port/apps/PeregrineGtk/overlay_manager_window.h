// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// overlay_manager_window.h — Tools ▸ Overlay Manager, the counterpart of
/// OverlayManagerWindow.swift.
#pragma once

#include <gtkmm.h>

#include <memory>
#include <string>
#include <vector>

#include "dialogs.h"
#include "fv_desk_host.h"

namespace peregrine {

/// The installed overlay packages, the selected one's details, and Install…,
/// Install from Folder…, Enable/Disable, Remove and Show in Files. The model
/// is DeskKit's PackageManager, read through `DeskHost`; the window re-reads
/// it after every change.
class OverlayManagerWindow : public Gtk::Window {
 public:
  /// `host` must outlive the window.
  OverlayManagerWindow(fv::desk::DeskHost& host, Gtk::Window& parent);

  /// Re-reads the packages, keeping the selected one selected.
  void Reload();
  /// Asks to install the .pgpkg or package folder at `path`, showing its
  /// publisher and the kinds of code it runs, then installs it on
  /// confirmation. `link` uses a folder in place.
  void ConfirmInstall(const std::string& path, bool link = false);
  /// Installs without asking; shows an error or the restart note.
  void Install(const std::string& path, bool link = false);
  /// Selects the package with `id`; false when it is not listed.
  bool Select(const std::string& id);
  /// Enables a disabled package or disables an enabled one.
  void ToggleEnabled();
  /// Removes the selected package after asking.
  void RemoveSelected();

  /// For tests: the listed packages, the detail text, the restart banner,
  /// the buttons and the confirmation shown last (null before the first).
  const std::vector<fv::desk::HostPackage>& rows() const { return rows_; }
  std::string detail_text() const;
  Gtk::Label& banner() { return banner_; }
  Gtk::Button& enable_button() { return enable_; }
  Gtk::Button& remove_button() { return remove_; }
  Gtk::Button& reveal_button() { return reveal_; }
  ChoiceDialog* confirmation() { return confirm_.get(); }

 private:
  void UpdateDetail();
  const fv::desk::HostPackage* SelectedPackage() const;
  void ChooseFile();
  void ChooseFolder();
  void Reveal();
  /// Asks with a modal ChoiceDialog; returns the button index.
  int Ask(const std::string& message, const std::string& detail,
          const std::vector<std::string>& buttons, int default_button, int cancel_button,
          int destructive_button);
  void Alert(const std::string& message, const std::string& detail);

  fv::desk::DeskHost& host_;
  Gtk::Box box_{Gtk::Orientation::VERTICAL, 10};
  Gtk::ScrolledWindow list_scroll_;
  Gtk::ListBox list_;
  Gtk::ScrolledWindow detail_scroll_;
  Gtk::TextView detail_;
  Gtk::Label banner_;
  Gtk::Button install_{"Install…"};
  Gtk::Button install_folder_{"Install from Folder…"};
  Gtk::Button enable_{"Disable"};
  Gtk::Button remove_{"Remove"};
  Gtk::Button reveal_{"Show in Files"};
  Gtk::Button close_{"Close"};
  std::vector<fv::desk::HostPackage> rows_;
  /// Set while the list is refilled, so its selection signal is not the user's.
  bool loading_ = false;
  std::unique_ptr<ChoiceDialog> confirm_;
};

}  // namespace peregrine
