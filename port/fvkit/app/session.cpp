// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/app/session.h"

#include "fvkit/app/properties.h"

#include <algorithm>
#include <string>
#include <utility>

#include "fvkit/app/editor.h"

namespace fv {
namespace app {

const char* ToString(FlowResult r) {
  switch (r) {
    case FlowResult::kDone: return "done";
    case FlowResult::kCanceled: return "canceled";
    case FlowResult::kFailed: return "failed";
  }
  return "?";
}

namespace {

// The extension of a file spec, without its dot and without a directory's
// own dots ("/a.dir/kiawah" has no extension). Both separators, because a
// Windows shell hands this layer Windows paths.
std::string ExtensionOf(const std::string& spec) {
  const size_t dot = spec.find_last_of('.');
  if (dot == std::string::npos) return std::string();
  const size_t sep = spec.find_last_of("/\\");
  if (sep != std::string::npos && dot < sep) return std::string();
  return spec.substr(dot + 1);
}

// What the user calls this overlay in a Save prompt: its document if it has
// one, else its display name.
std::string PromptName(Overlay& overlay) {
  if (Persistence* p = overlay.AsPersistence()) {
    if (!p->file_spec().empty()) return p->file_spec();
  }
  return overlay.Name();
}

// Worse of the two: kFailed beats kCanceled beats kDone. Used only where one
// flow covers several files and none of them may stop the others.
FlowResult Worse(FlowResult a, FlowResult b) {
  if (a == FlowResult::kFailed || b == FlowResult::kFailed)
    return FlowResult::kFailed;
  if (a == FlowResult::kCanceled || b == FlowResult::kCanceled)
    return FlowResult::kCanceled;
  return FlowResult::kDone;
}

std::string SessionPrefix(const std::string& name) {
  return "session." + name + ".";
}

}  // namespace

// A public flow's reentrancy guard (plan §6). Holds the flow's name so a
// nested entry can say which flow it was already inside.
class OverlaySession::Guard {
 public:
  Guard(OverlaySession& s, const char* name) : s_(s) {
    if (s_.in_flow_ != nullptr) {
      previous_ = s_.in_flow_;
      return;
    }
    s_.in_flow_ = name;
    held_ = true;
  }
  ~Guard() {
    if (held_) s_.in_flow_ = nullptr;
  }
  Guard(const Guard&) = delete;
  Guard& operator=(const Guard&) = delete;

  bool held() const { return held_; }
  const char* previous() const { return previous_; }

 private:
  OverlaySession& s_;
  const char* previous_ = nullptr;
  bool held_ = false;
};

OverlaySession::OverlaySession(OverlayTypeRegistry& registry,
                               OverlayManager& manager, AppShell& shell,
                               Settings& settings)
    : registry_(registry),
      manager_(manager),
      shell_(shell),
      settings_(settings) {}

FlowResult OverlaySession::Fail(const Status& s) {
  last_error_ = s;
  shell_.ReportError(s);
  return FlowResult::kFailed;
}

std::shared_ptr<Overlay> OverlaySession::Instantiate(
    const OverlayTypeDesc& desc) {
  std::shared_ptr<Overlay> overlay = desc.factory ? desc.factory() : nullptr;
  if (!overlay) return overlay;
  // Stamped at creation, the way InternalInitialize(guid) did it -- this is
  // what makes FirstOfType/OfType/FindByFileSpec answerable.
  overlay->set_type_id(desc.id);

  // AND ITS SETTINGS, HERE, FOR EVERY OVERLAY THAT DECLARES ANY.
  //
  // This is the single place an overlay comes into existence in the app layer,
  // which is why the hook belongs here and not in a factory or a shell. An
  // overlay that implements app::Properties gets its peregrine.ini section
  // applied the moment it is made -- no shell writes a line of code for it,
  // and a shell that forgot would otherwise silently show an overlay wearing
  // its compiled-in defaults while the user's file said otherwise. (That is
  // exactly what happened to the graticule the first time it was run from
  // PythonView: the [grid] section was read into Settings and nothing ever
  // asked for it.)
  //
  // Failures are WARNINGS, never a refused overlay: a typo in an .ini costs
  // the user that one key, not the map.
  if (app::Properties* props = overlay->AsProperties()) {
    std::vector<std::string> w;
    props->LoadFrom(settings_, SettingsPrefixForTypeId(desc.id), &w);
    for (const std::string& line : w) Warn(line);
  }
  return overlay;
}

std::shared_ptr<Overlay> OverlaySession::Shared(const Overlay& overlay) const {
  for (const auto& o : manager_.Overlays()) {
    if (o.get() == &overlay) return o;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Creating and opening
// ---------------------------------------------------------------------------

FlowResult OverlaySession::ToggleStatic(const TypeId& id) {
  Guard g(*this, "ToggleStatic");
  if (!g.held()) {
    return Fail(Status::Error(kInternal, std::string("ToggleStatic entered ") +
                                             "while " + g.previous() +
                                             " is still running"));
  }
  return ToggleStaticImpl(id);
}

FlowResult OverlaySession::ToggleStaticImpl(const TypeId& id) {
  const OverlayTypeDesc* desc = registry_.Find(id);
  if (desc == nullptr) {
    return Fail(Status::Error(kNotFound,
                              "no overlay type '" + id + "' is registered"));
  }
  if (desc->file.has_value()) {
    return Fail(Status::Error(
        kInvalidArg, "overlay type '" + id +
                         "' is a file type and cannot be toggled"));
  }

  if (std::shared_ptr<Overlay> existing = manager_.FirstOfType(id)) {
    // Off. Forced, because the toggle IS the user's control over this type --
    // user_controllable governs a close button, not the toggle that put it
    // there.
    return CloseImpl(*existing, /*force=*/true);
  }

  std::shared_ptr<Overlay> made = Instantiate(*desc);
  if (made == nullptr) {
    return Fail(Status::Error(
        kInternal, "factory for overlay type '" + id + "' returned nothing"));
  }
  Status s = manager_.Add(std::move(made));
  if (!s.ok()) return Fail(s);
  return FlowResult::kDone;
}

FlowResult OverlaySession::NewFileOverlay(const TypeId& id) {
  Guard g(*this, "NewFileOverlay");
  if (!g.held()) {
    return Fail(Status::Error(kInternal,
                              std::string("NewFileOverlay entered while ") +
                                  g.previous() + " is still running"));
  }
  return NewFileOverlayImpl(id);
}

FlowResult OverlaySession::NewFileOverlayImpl(const TypeId& id) {
  const OverlayTypeDesc* desc = registry_.Find(id);
  if (desc == nullptr) {
    return Fail(Status::Error(kNotFound,
                              "no overlay type '" + id + "' is registered"));
  }
  if (!desc->file.has_value()) {
    return Fail(Status::Error(kInvalidArg, "overlay type '" + id +
                                               "' is static; nothing to create"));
  }

  std::shared_ptr<Overlay> made = Instantiate(*desc);
  if (made == nullptr) {
    return Fail(Status::Error(
        kInternal, "factory for overlay type '" + id + "' returned nothing"));
  }
  Persistence* p = made->AsPersistence();
  if (p == nullptr) {
    // A file type whose overlay does not implement Persistence is a wiring
    // mistake, and it fails here rather than at the user's first Save.
    return Fail(Status::Error(kInternal,
                              "overlay type '" + id +
                                  "' is a file type but its overlay has no "
                                  "Persistence capability"));
  }

  Status s = p->FileNew();
  if (!s.ok()) return Fail(s);  // nothing was added: no half-built document

  s = manager_.Add(made);
  if (!s.ok()) return Fail(s);
  s = manager_.MakeCurrent(made);
  if (!s.ok()) return Fail(s);

  // ~ m_bAutoEnterOverlayEditor. A no-op without an EditorManager, for a type
  // with no editor, and for an editor that declines. Its failure does not undo
  // the document -- see the header.
  if (editors_ != nullptr) {
    if (editors_->AutoEnterFor(made) != FlowResult::kDone) {
      Warn("overlay type '" + id + "' was created but its editor could not "
           "be entered: " + editors_->last_error().message);
    }
  }
  return FlowResult::kDone;
}

FlowResult OverlaySession::OpenFileOverlays(const TypeId& hint) {
  Guard g(*this, "OpenFileOverlays");
  if (!g.held()) {
    return Fail(Status::Error(kInternal,
                              std::string("OpenFileOverlays entered while ") +
                                  g.previous() + " is still running"));
  }

  FileTypeDesc chooser;
  if (!hint.empty()) {
    const OverlayTypeDesc* desc = registry_.Find(hint);
    if (desc == nullptr) {
      return Fail(Status::Error(
          kNotFound, "no overlay type '" + hint + "' is registered"));
    }
    if (!desc->file.has_value()) {
      return Fail(Status::Error(
          kInvalidArg, "overlay type '" + hint + "' is static and opens no files"));
    }
    chooser = *desc->file;
  } else {
    // No hint: offer the union of every file type's open filters, in
    // registration order, and let the extension pick the type per file. This
    // is FalconView's "File > Open" against its "Open Overlay" -- the same
    // flow, one with the type decided in advance and one without.
    for (const OverlayTypeDesc* desc : registry_.All()) {
      if (!desc->file.has_value()) continue;
      for (const auto& f : desc->file->open_filters) {
        chooser.open_filters.push_back(f);
      }
    }
    if (chooser.open_filters.empty()) {
      return Fail(Status::Error(kNotFound,
                                "no file overlay type is registered"));
    }
  }

  const std::vector<std::string> specs = shell_.ChooseFilesToOpen(chooser);
  if (specs.empty()) return FlowResult::kCanceled;

  FlowResult result = FlowResult::kDone;
  for (const std::string& spec : specs) {
    result = Worse(result, OpenFileImpl(hint, spec));
  }
  return result;
}

FlowResult OverlaySession::OpenFile(const TypeId& type,
                                    const std::string& spec) {
  Guard g(*this, "OpenFile");
  if (!g.held()) {
    return Fail(Status::Error(kInternal, std::string("OpenFile entered while ") +
                                             g.previous() + " is still running"));
  }
  return OpenFileImpl(type, spec);
}

FlowResult OverlaySession::OpenFileImpl(const TypeId& type,
                                        const std::string& spec) {
  if (spec.empty()) {
    return Fail(Status::Error(kInvalidArg, "empty file spec"));
  }

  const OverlayTypeDesc* desc = nullptr;
  if (type.empty()) {
    const std::string ext = ExtensionOf(spec);
    desc = registry_.FindByExtension(ext);
    if (desc == nullptr) {
      return Fail(Status::Error(
          kNotFound, "no overlay type opens '" + spec + "'"));
    }
  } else {
    desc = registry_.Find(type);
    if (desc == nullptr) {
      return Fail(Status::Error(
          kNotFound, "no overlay type '" + type + "' is registered"));
    }
    if (!desc->file.has_value()) {
      return Fail(Status::Error(
          kInvalidArg, "overlay type '" + type + "' is static and opens no files"));
    }
  }

  // Dedup on (type, spec). Already open => make it current; and if it is
  // dirty, offer to throw the edits away and re-read, which is the only
  // reading of "open this file again" that means anything.
  if (std::shared_ptr<Overlay> open = manager_.FindByFileSpec(desc->id, spec)) {
    Persistence* p = open->AsPersistence();
    if (p != nullptr && p->is_dirty() && p->SupportsRevert()) {
      if (shell_.ConfirmRevert(spec)) {
        Status s = p->Revert(spec);
        if (!s.ok()) return Fail(s);
        p->set_dirty(false);
      }
      // Declining is NOT a cancel: the file the user asked for is open and
      // about to be current, which is what they asked for.
    }
    Status s = manager_.MakeCurrent(open);
    if (!s.ok()) return Fail(s);
    return FlowResult::kDone;
  }

  std::shared_ptr<Overlay> made = Instantiate(*desc);
  if (made == nullptr) {
    return Fail(Status::Error(kInternal, "factory for overlay type '" +
                                             desc->id + "' returned nothing"));
  }
  Persistence* p = made->AsPersistence();
  if (p == nullptr) {
    return Fail(Status::Error(kInternal,
                              "overlay type '" + desc->id +
                                  "' is a file type but its overlay has no "
                                  "Persistence capability"));
  }

  Status s = p->FileOpen(spec);
  if (!s.ok()) return Fail(s);  // a file that would not open is not in the stack

  // The session owns the document bookkeeping (capabilities.h says so): the
  // overlay parsed the file, the session records that it came from one.
  p->set_file_spec(spec);
  p->set_has_been_saved(true);
  p->set_dirty(false);

  s = manager_.Add(made);
  if (!s.ok()) return Fail(s);
  s = manager_.MakeCurrent(made);
  if (!s.ok()) return Fail(s);
  return FlowResult::kDone;
}

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------

FlowResult OverlaySession::Save(Overlay& overlay) {
  Guard g(*this, "Save");
  if (!g.held()) {
    return Fail(Status::Error(kInternal, std::string("Save entered while ") +
                                             g.previous() + " is still running"));
  }
  return SaveImpl(overlay);
}

FlowResult OverlaySession::SaveImpl(Overlay& overlay) {
  Persistence* p = overlay.AsPersistence();
  if (p == nullptr) {
    return Fail(Status::Error(kUnsupported, "overlay '" + overlay.Name() +
                                                "' has no Persistence"));
  }
  // Nowhere to write, or not allowed to write there: ask.
  if (!p->has_been_saved() || p->file_spec().empty() || p->is_read_only()) {
    return SaveAsImpl(overlay);
  }
  if (!p->is_dirty()) return FlowResult::kDone;  // nothing to write

  Status s = p->FileSaveAs(p->file_spec(), p->save_format_index());
  if (!s.ok()) return Fail(s);
  p->set_dirty(false);
  return FlowResult::kDone;
}

FlowResult OverlaySession::SaveAs(Overlay& overlay) {
  Guard g(*this, "SaveAs");
  if (!g.held()) {
    return Fail(Status::Error(kInternal, std::string("SaveAs entered while ") +
                                             g.previous() + " is still running"));
  }
  return SaveAsImpl(overlay);
}

FlowResult OverlaySession::SaveAsImpl(Overlay& overlay) {
  Persistence* p = overlay.AsPersistence();
  if (p == nullptr) {
    return Fail(Status::Error(kUnsupported, "overlay '" + overlay.Name() +
                                                "' has no Persistence"));
  }
  const OverlayTypeDesc* desc = registry_.Find(overlay.type_id());
  if (desc == nullptr || !desc->file.has_value()) {
    // No descriptor means no filters and no default extension, so there is
    // nothing to put in a file dialog. An overlay made outside the app layer
    // cannot be saved through it.
    return Fail(Status::Error(kNotFound,
                              "overlay '" + overlay.Name() +
                                  "' has no registered file type to save as"));
  }

  std::string suggested = p->file_spec();
  if (suggested.empty()) {
    suggested = overlay.Name();
    if (!desc->file->default_extension.empty()) {
      suggested += "." + desc->file->default_extension;
    }
  }

  const std::pair<std::string, int> chosen =
      shell_.ChooseSaveSpec(*desc->file, suggested);
  if (chosen.first.empty()) return FlowResult::kCanceled;

  Status s = p->FileSaveAs(chosen.first, chosen.second);
  if (!s.ok()) return Fail(s);

  p->set_file_spec(chosen.first);   // fires OverlayFileSpecChanged
  p->set_save_format_index(chosen.second);
  p->set_has_been_saved(true);
  p->set_dirty(false);
  // Saved somewhere the user chose, so it is writable now whatever it was.
  p->set_read_only(false);
  return FlowResult::kDone;
}

FlowResult OverlaySession::SaveAll() {
  Guard g(*this, "SaveAll");
  if (!g.held()) {
    return Fail(Status::Error(kInternal, std::string("SaveAll entered while ") +
                                             g.previous() + " is still running"));
  }
  // A copy: a Save can fire observers, and an observer may not mutate the
  // stack but the flow should not depend on that to stay valid.
  const std::vector<std::shared_ptr<Overlay>> stack = manager_.Overlays();
  for (const auto& o : stack) {
    Persistence* p = o->AsPersistence();
    if (p == nullptr || !p->is_dirty()) continue;
    const FlowResult r = SaveImpl(*o);
    if (r != FlowResult::kDone) return r;  // a cancel means "stop", not "skip"
  }
  return FlowResult::kDone;
}

// ---------------------------------------------------------------------------
// Closing
// ---------------------------------------------------------------------------

FlowResult OverlaySession::Close(Overlay& overlay) {
  Guard g(*this, "Close");
  if (!g.held()) {
    return Fail(Status::Error(kInternal, std::string("Close entered while ") +
                                             g.previous() + " is still running"));
  }
  return CloseImpl(overlay, /*force=*/false);
}

FlowResult OverlaySession::CloseImpl(Overlay& overlay, bool force) {
  std::shared_ptr<Overlay> owned = Shared(overlay);
  if (owned == nullptr) {
    return Fail(Status::Error(kNotFound, "overlay '" + overlay.Name() +
                                             "' is not in the stack"));
  }
  if (!force) {
    const OverlayTypeDesc* desc = registry_.Find(overlay.type_id());
    if (desc != nullptr && !desc->user_controllable) {
      return Fail(Status::Error(kUnsupported,
                                "overlay type '" + desc->id +
                                    "' is not user-controllable"));
    }
  }

  if (Persistence* p = overlay.AsPersistence()) {
    if (p->is_dirty()) {
      switch (shell_.AskSave(PromptName(overlay))) {
        case AppShell::SaveAnswer::kCancel:
          return FlowResult::kCanceled;
        case AppShell::SaveAnswer::kSave: {
          const FlowResult r = SaveImpl(overlay);
          // A save that failed or was cancelled aborts the close: closing
          // anyway is how a user loses a document.
          if (r != FlowResult::kDone) return r;
          break;
        }
        case AppShell::SaveAnswer::kDiscard:
          break;
      }
    }
  }

  // The per-instance half of the editor contract, and ONLY when nothing else
  // owns it. With an EditorManager wired, the release happens in its
  // OverlayRemoved hook together with the half this flow cannot do -- current
  // falling to the next overlay of the same type, or the mode exiting.
  if (editors_ == nullptr && manager_.current() == &overlay) {
    if (EditTarget* e = overlay.AsEditTarget()) e->ReleaseEditFocus();
  }

  Status s = manager_.Remove(owned);
  if (!s.ok()) return Fail(s);
  return FlowResult::kDone;
}

FlowResult OverlaySession::CloseAll() {
  Guard g(*this, "CloseAll");
  if (!g.held()) {
    return Fail(Status::Error(kInternal, std::string("CloseAll entered while ") +
                                             g.previous() + " is still running"));
  }
  return CloseAllImpl();
}

FlowResult OverlaySession::CloseAllImpl() {
  // Top-down, over a copy: the user is prompted about the overlay they can see
  // first, and each close mutates the real stack under us.
  std::vector<std::shared_ptr<Overlay>> stack = manager_.Overlays();
  std::reverse(stack.begin(), stack.end());
  for (const auto& o : stack) {
    const FlowResult r = CloseImpl(*o, /*force=*/true);
    // Whatever has already closed stays closed -- FalconView leaves them shut
    // too, and reopening them would be a second guess at the user's answer.
    if (r != FlowResult::kDone) return r;
  }
  return FlowResult::kDone;
}

FlowResult OverlaySession::Exit() {
  Guard g(*this, "Exit");
  if (!g.held()) {
    return Fail(Status::Error(kInternal, std::string("Exit entered while ") +
                                             g.previous() + " is still running"));
  }

  const bool autosave = settings_.GetBool("session.autosave", false);
  const std::string name =
      settings_.GetString("session.autosave_name", "default");

  // The snapshot is taken BEFORE anything closes, because by the time CloseAll
  // has finished there is no stack left to describe -- and it is APPLIED only
  // if the close completed, because a cancelled exit must leave the saved
  // session exactly as it was.
  std::vector<std::pair<std::string, std::string>> snapshot;
  if (autosave) {
    Status s = BuildConfiguration(name, &snapshot);
    if (!s.ok()) return Fail(s);
  }

  const FlowResult r = CloseAllImpl();
  if (r != FlowResult::kDone) return r;

  for (const auto& kv : snapshot) settings_.Set(kv.first, kv.second);
  return FlowResult::kDone;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

Status OverlaySession::BuildConfiguration(
    const std::string& name,
    std::vector<std::pair<std::string, std::string>>* out) {
  if (name.empty()) {
    return Status::Error(kInvalidArg, "configuration name is empty");
  }
  const std::string prefix = SessionPrefix(name);

  const std::vector<std::shared_ptr<Overlay>>& stack = manager_.Overlays();
  int written = 0;
  int current_index = -1;
  for (const auto& o : stack) {
    if (o->type_id().empty()) {
      Warn("overlay '" + o->Name() +
           "' has no type and was left out of configuration '" + name + "'");
      continue;
    }
    const std::string row = prefix + std::to_string(written) + ".";
    out->emplace_back(row + "type", o->type_id());
    std::string spec;
    if (Persistence* p = o->AsPersistence()) spec = p->file_spec();
    out->emplace_back(row + "file", spec);
    out->emplace_back(row + "visible", o->IsVisible() ? "true" : "false");
    if (o.get() == manager_.current()) current_index = written;
    ++written;
  }
  out->emplace_back(prefix + "count", std::to_string(written));
  out->emplace_back(prefix + "current", std::to_string(current_index));
  out->emplace_back(prefix + "declutter",
                    manager_.declutter() ? "true" : "false");
  return Status::Ok();
}

Status OverlaySession::SaveConfiguration(const std::string& name) {
  std::vector<std::pair<std::string, std::string>> keys;
  Status s = BuildConfiguration(name, &keys);
  if (!s.ok()) return s;
  for (const auto& kv : keys) settings_.Set(kv.first, kv.second);
  return Status::Ok();
}

Status OverlaySession::RestoreConfiguration(const std::string& name) {
  if (name.empty()) {
    return Status::Error(kInvalidArg, "configuration name is empty");
  }
  const std::string prefix = SessionPrefix(name);
  if (!settings_.Has(prefix + "count")) {
    return Status::Error(kNotFound,
                         "no session configuration '" + name + "'");
  }
  const int count = settings_.GetInt(prefix + "count", 0);

  // Bottom-up, the order they were written in.
  std::vector<std::shared_ptr<Overlay>> restored;
  int current_index = settings_.GetInt(prefix + "current", -1);
  std::shared_ptr<Overlay> want_current;

  for (int i = 0; i < count; ++i) {
    const std::string row = prefix + std::to_string(i) + ".";
    // `count` comes out of a file a human is invited to edit, so it is a hint
    // and the ROWS are the truth: a count left too high after some rows were
    // deleted by hand stops here instead of spinning through a billion
    // lookups. A count too LOW simply restores fewer, which is what it says.
    if (!settings_.Has(row + "type")) {
      Warn("configuration '" + name + "' claims " + std::to_string(count) +
           " overlays but row " + std::to_string(i) + " is missing");
      break;
    }
    const std::string type = settings_.GetString(row + "type");
    const std::string spec = settings_.GetString(row + "file");
    const OverlayTypeDesc* desc = registry_.Find(type);
    if (desc == nullptr) {
      Warn("configuration '" + name + "' names unregistered overlay type '" +
           type + "'");
      continue;
    }

    std::shared_ptr<Overlay> overlay;
    if (desc->file.has_value()) {
      if (spec.empty()) {
        // An untitled document. There is no file to reopen and inventing a
        // blank one would be a different document, so it is dropped.
        Warn("configuration '" + name + "' names an unsaved '" + type +
             "' overlay, which cannot be restored");
        continue;
      }
      if (OpenFileImpl(type, spec) != FlowResult::kDone) {
        Warn("configuration '" + name + "' could not reopen '" + spec + "'");
        continue;
      }
      overlay = manager_.FindByFileSpec(type, spec);
    } else {
      overlay = manager_.FirstOfType(type);
      if (overlay == nullptr) {
        if (ToggleStaticImpl(type) != FlowResult::kDone) {
          Warn("configuration '" + name + "' could not open static type '" +
               type + "'");
          continue;
        }
        overlay = manager_.FirstOfType(type);
      }
    }
    if (overlay == nullptr) continue;

    overlay->SetVisible(settings_.GetBool(row + "visible", true));
    if (i == current_index) want_current = overlay;
    restored.push_back(overlay);
  }

  // The saved order is reapplied only if these ARE the stack: Reorder takes a
  // total permutation by rule, and weaving the restored overlays around ones
  // the configuration never mentioned would be inventing an order nobody saved.
  if (restored.size() == manager_.Overlays().size()) {
    Status s = manager_.Reorder(restored);
    if (!s.ok()) {
      Warn("configuration '" + name + "' order not applied: " + s.message);
    }
  }
  if (want_current != nullptr) manager_.MakeCurrent(want_current);
  manager_.SetDeclutter(settings_.GetBool(prefix + "declutter", false));
  return Status::Ok();
}

Status OverlaySession::RestoreStartupOverlays() {
  for (const OverlayTypeDesc* desc : registry_.All()) {
    if (!desc->restore_at_startup) continue;
    if (desc->file.has_value()) {
      // A file type has no single document to restore at startup; that is what
      // a saved CONFIGURATION is for.
      Warn("overlay type '" + desc->id +
           "' is a file type and cannot restore at startup");
      continue;
    }
    if (manager_.FirstOfType(desc->id) != nullptr) continue;
    if (ToggleStaticImpl(desc->id) != FlowResult::kDone) {
      Warn("could not restore startup overlay type '" + desc->id + "'");
    }
  }
  return Status::Ok();
}

}  // namespace app
}  // namespace fv
