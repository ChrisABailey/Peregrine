// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// App layer A3: the shell seam and the session flows
// (fvkit-app-plan.md §3f/§3g).
//
// The proof the plan asks for is "every FlowResult path unit-tested against a
// scripted FakeShell", and that is what these are. No file is written: a
// document here is a counter of how many times it was told to save, because
// the flows are what is under test and a real file would only test the fake
// overlay's parser. The one thing a test must never do is let a flow reach a
// dialog -- FakeShell is the whole reason it cannot.

#include "fvkit/app/session.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "fake_shell.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/settings.h"

namespace {

using fv::Status;
using fv::app::AppShell;
using fv::app::FakeShell;
using fv::app::FileTypeDesc;
using fv::app::FlowResult;
using fv::app::OverlaySession;
using fv::app::OverlayTypeDesc;
using fv::app::OverlayTypeRegistry;

// ---------------------------------------------------------------------------
// Doubles
// ---------------------------------------------------------------------------

// A document overlay. Records every persistence call and can be told to fail
// any of them, which is how the "a failed save aborts the close" path is
// reached without a read-only filesystem.
class DocOverlay : public fv::Overlay, public fv::app::Persistence {
 public:
  explicit DocOverlay(std::string name) : Overlay(std::move(name)) {}

  fv::app::Persistence* AsPersistence() override { return this; }

  Status FileNew() override {
    ++new_calls;
    return new_fails ? Status::Error(fv::kIoError, "FileNew refused")
                     : Status::Ok();
  }
  Status FileOpen(const std::string& spec) override {
    opened.push_back(spec);
    return open_fails ? Status::Error(fv::kIoError, "cannot read " + spec)
                      : Status::Ok();
  }
  Status FileSaveAs(const std::string& spec, int format) override {
    saved.push_back(spec);
    saved_formats.push_back(format);
    return save_fails ? Status::Error(fv::kIoError, "cannot write " + spec)
                      : Status::Ok();
  }
  bool SupportsRevert() const override { return supports_revert; }
  Status Revert(const std::string& spec) override {
    reverted.push_back(spec);
    return Status::Ok();
  }

  int new_calls = 0;
  std::vector<std::string> opened, saved, reverted;
  std::vector<int> saved_formats;
  bool new_fails = false, open_fails = false, save_fails = false;
  bool supports_revert = true;
};

// A static overlay that also happens to be an edit target, so the close
// bracket (ReleaseEditFocus before Remove) has something to record.
class StaticOverlay : public fv::Overlay, public fv::app::EditTarget {
 public:
  explicit StaticOverlay(std::string name) : Overlay(std::move(name)) {}
  fv::app::EditTarget* AsEditTarget() override { return this; }
  void EnterEditFocus() override { ++entered; }
  void ReleaseEditFocus() override { ++released; }
  int entered = 0, released = 0;
};

// Counts the stack broadcasts a flow produces -- the observable half of
// "and then the overlay was actually removed".
class CountingObserver : public fv::StackObserver {
 public:
  void OverlayAdded(fv::Overlay&) override { ++added; }
  void OverlayRemoved(fv::Overlay&) override { ++removed; }
  void OverlayFileSpecChanged(fv::Overlay&) override { ++spec_changed; }
  void OverlayDirtyChanged(fv::Overlay&) override { ++dirty_changed; }
  int added = 0, removed = 0, spec_changed = 0, dirty_changed = 0;
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

constexpr const char kDocType[] = "test.doc";
constexpr const char kNoteType[] = "test.note";
constexpr const char kStaticType[] = "test.static";
constexpr const char kFixedType[] = "test.fixed";  // not user-controllable

class SessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    OverlayTypeDesc doc;
    doc.id = kDocType;
    doc.display_name = "Test Document";
    doc.default_display_order = 100;
    FileTypeDesc doc_file;
    doc_file.default_extension = "doc";
    doc_file.open_filters = {{"Test Documents (*.doc)", "*.doc"}};
    doc_file.save_filters = {{"Test Documents (*.doc)", "*.doc"},
                             {"Test Documents, old (*.doc)", "*.doc"}};
    doc.file = doc_file;
    doc.factory = [] { return std::make_shared<DocOverlay>("Untitled"); };
    ASSERT_TRUE(registry_.Register(doc).ok());

    OverlayTypeDesc note;
    note.id = kNoteType;
    note.display_name = "Test Note";
    note.default_display_order = 100;
    FileTypeDesc note_file;
    note_file.default_extension = "note";
    note_file.open_filters = {{"Test Notes (*.note)", "*.note"}};
    note_file.save_filters = {{"Test Notes (*.note)", "*.note"}};
    note.file = note_file;
    note.factory = [] { return std::make_shared<DocOverlay>("Untitled Note"); };
    ASSERT_TRUE(registry_.Register(note).ok());

    OverlayTypeDesc stat;
    stat.id = kStaticType;
    stat.display_name = "Test Static";
    stat.default_display_order = 900;
    stat.restore_at_startup = true;
    stat.factory = [] { return std::make_shared<StaticOverlay>("Static"); };
    ASSERT_TRUE(registry_.Register(stat).ok());

    OverlayTypeDesc fixed;
    fixed.id = kFixedType;
    fixed.display_name = "Test Fixed";
    fixed.user_controllable = false;
    fixed.factory = [] { return std::make_shared<StaticOverlay>("Fixed"); };
    ASSERT_TRUE(registry_.Register(fixed).ok());

    manager_.SetTypeRegistry(&registry_);
  }

  // The overlay a flow just created, as its concrete type.
  DocOverlay* Doc(size_t index) {
    return static_cast<DocOverlay*>(manager_.Overlays()[index].get());
  }

  OverlayTypeRegistry registry_;
  fv::OverlayManager manager_;
  FakeShell shell_;
  fv::Settings settings_;
  OverlaySession session_{registry_, manager_, shell_, settings_};
};

// ---------------------------------------------------------------------------
// ToggleStatic
// ---------------------------------------------------------------------------

TEST_F(SessionTest, TogglingAStaticTypeOnAndOffIsOneVerb) {
  EXPECT_EQ(session_.ToggleStatic(kStaticType), FlowResult::kDone);
  ASSERT_EQ(manager_.Overlays().size(), 1u);
  EXPECT_EQ(manager_.Overlays()[0]->type_id(), kStaticType);

  EXPECT_EQ(session_.ToggleStatic(kStaticType), FlowResult::kDone);
  EXPECT_TRUE(manager_.Overlays().empty());
}

// A graticule is not a document; nothing is being worked ON in it. So the
// session never CALLS MakeCurrent for a static overlay -- whether the new
// overlay ends up current is A2's insertion rule and nothing else, which for a
// type that lands below the working overlay means it does not.
// (What deliberately makes an editable overlay current is entering its
// editor's mode, and that is A4's job.)
TEST_F(SessionTest, TogglingAStaticOverlayOnDoesNotItselfMakeItCurrent) {
  ASSERT_EQ(session_.NewFileOverlay(kDocType), FlowResult::kDone);
  fv::Overlay* doc = manager_.current();
  ASSERT_NE(doc, nullptr);

  // kFixedType sits at display order 0, below the document at 100.
  ASSERT_EQ(session_.ToggleStatic(kFixedType), FlowResult::kDone);
  ASSERT_EQ(manager_.Overlays().size(), 2u);
  ASSERT_EQ(manager_.Overlays()[0]->type_id(), kFixedType);
  EXPECT_EQ(manager_.current(), doc);
}

TEST_F(SessionTest, AFileTypeCannotBeToggled) {
  EXPECT_EQ(session_.ToggleStatic(kDocType), FlowResult::kFailed);
  EXPECT_EQ(session_.last_error().code, fv::kInvalidArg);
  EXPECT_EQ(shell_.errors.size(), 1u);
  EXPECT_TRUE(manager_.Overlays().empty());
}

TEST_F(SessionTest, AnUnregisteredTypeFailsRatherThanDoingNothing) {
  EXPECT_EQ(session_.ToggleStatic("test.nope"), FlowResult::kFailed);
  EXPECT_EQ(session_.last_error().code, fv::kNotFound);
}

// user_controllable governs the CLOSE button, not the toggle that opened it:
// a user who can toggle a type on must be able to toggle it back off.
TEST_F(SessionTest, ToggleClosesEvenATypeTheUserMayNotClose) {
  ASSERT_EQ(session_.ToggleStatic(kFixedType), FlowResult::kDone);
  ASSERT_EQ(manager_.Overlays().size(), 1u);
  EXPECT_EQ(session_.Close(*manager_.Overlays()[0]), FlowResult::kFailed);
  EXPECT_EQ(session_.last_error().code, fv::kUnsupported);
  EXPECT_EQ(session_.ToggleStatic(kFixedType), FlowResult::kDone);
  EXPECT_TRUE(manager_.Overlays().empty());
}

// ---------------------------------------------------------------------------
// New
// ---------------------------------------------------------------------------

TEST_F(SessionTest, NewFileOverlayCreatesAnUntitledCurrentDocument) {
  CountingObserver obs;
  manager_.AddObserver(&obs);

  EXPECT_EQ(session_.NewFileOverlay(kDocType), FlowResult::kDone);
  ASSERT_EQ(manager_.Overlays().size(), 1u);
  DocOverlay* d = Doc(0);
  EXPECT_EQ(d->new_calls, 1);
  EXPECT_EQ(manager_.current(), d);
  EXPECT_EQ(obs.added, 1);
  // Untitled: no spec, never saved -- which is what sends the first Save
  // through the Save As flow.
  EXPECT_TRUE(d->file_spec().empty());
  EXPECT_FALSE(d->has_been_saved());

  manager_.RemoveObserver(&obs);
}

// A document that could not initialise must not become something the user has
// to close.
TEST_F(SessionTest, AFailedFileNewLeavesNothingInTheStack) {
  OverlayTypeDesc bad;
  bad.id = "test.badnew";
  bad.file = FileTypeDesc{};
  bad.factory = [] {
    auto d = std::make_shared<DocOverlay>("Bad");
    d->new_fails = true;
    return d;
  };
  ASSERT_TRUE(registry_.Register(bad).ok());

  EXPECT_EQ(session_.NewFileOverlay("test.badnew"), FlowResult::kFailed);
  EXPECT_TRUE(manager_.Overlays().empty());
  EXPECT_EQ(session_.last_error().code, fv::kIoError);
}

// A file type wired to an overlay with no Persistence is a programming
// mistake, and it says so at creation rather than at the user's first Save.
TEST_F(SessionTest, AFileTypeWhoseOverlayHasNoPersistenceFailsLoudly) {
  OverlayTypeDesc bad;
  bad.id = "test.nopersist";
  bad.file = FileTypeDesc{};
  bad.factory = [] { return std::make_shared<StaticOverlay>("No persist"); };
  ASSERT_TRUE(registry_.Register(bad).ok());

  EXPECT_EQ(session_.NewFileOverlay("test.nopersist"), FlowResult::kFailed);
  EXPECT_EQ(session_.last_error().code, fv::kInternal);
  EXPECT_TRUE(manager_.Overlays().empty());
}

TEST_F(SessionTest, AStaticTypeCannotBeCreatedAsADocument) {
  EXPECT_EQ(session_.NewFileOverlay(kStaticType), FlowResult::kFailed);
  EXPECT_EQ(session_.last_error().code, fv::kInvalidArg);
}

// ---------------------------------------------------------------------------
// Open
// ---------------------------------------------------------------------------

TEST_F(SessionTest, OpenFileReadsTheDocumentAndRecordsWhereItCameFrom) {
  EXPECT_EQ(session_.OpenFile(kDocType, "/tmp/kiawah.doc"), FlowResult::kDone);
  ASSERT_EQ(manager_.Overlays().size(), 1u);
  DocOverlay* d = Doc(0);
  ASSERT_EQ(d->opened.size(), 1u);
  EXPECT_EQ(d->opened[0], "/tmp/kiawah.doc");
  EXPECT_EQ(d->file_spec(), "/tmp/kiawah.doc");
  EXPECT_TRUE(d->has_been_saved());
  EXPECT_FALSE(d->is_dirty());
  EXPECT_EQ(manager_.current(), d);
}

TEST_F(SessionTest, AnEmptyTypeIsResolvedByExtension) {
  EXPECT_EQ(session_.OpenFile("", "/tmp/a.note"), FlowResult::kDone);
  ASSERT_EQ(manager_.Overlays().size(), 1u);
  EXPECT_EQ(manager_.Overlays()[0]->type_id(), kNoteType);
}

TEST_F(SessionTest, AnExtensionNobodyClaimsFails) {
  EXPECT_EQ(session_.OpenFile("", "/tmp/a.xyz"), FlowResult::kFailed);
  EXPECT_EQ(session_.last_error().code, fv::kNotFound);
  EXPECT_TRUE(manager_.Overlays().empty());
}

TEST_F(SessionTest, AFileThatWillNotOpenIsNotLeftInTheStack) {
  OverlayTypeDesc bad;
  bad.id = "test.badopen";
  FileTypeDesc f;
  f.default_extension = "bad";
  bad.file = f;
  bad.factory = [] {
    auto d = std::make_shared<DocOverlay>("Bad");
    d->open_fails = true;
    return d;
  };
  ASSERT_TRUE(registry_.Register(bad).ok());

  EXPECT_EQ(session_.OpenFile("", "/tmp/a.bad"), FlowResult::kFailed);
  EXPECT_TRUE(manager_.Overlays().empty());
  EXPECT_EQ(session_.last_error().code, fv::kIoError);
}

// The dedup key is (type, spec): opening it again makes the open copy current
// and does NOT read the file a second time.
TEST_F(SessionTest, OpeningAnAlreadyOpenFileMakesItCurrentInstead) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/b.doc"), FlowResult::kDone);
  DocOverlay* a = Doc(0);
  ASSERT_EQ(a->file_spec(), "/tmp/a.doc");
  ASSERT_NE(manager_.current(), a);

  EXPECT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  EXPECT_EQ(manager_.Overlays().size(), 2u);
  EXPECT_EQ(manager_.current(), a);
  EXPECT_EQ(a->opened.size(), 1u);  // not re-read
}

// The same path under a DIFFERENT type is a different document: two overlays
// may legitimately read one file.
TEST_F(SessionTest, DedupIsPerTypeNotPerPath) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/shared.dat"), FlowResult::kDone);
  ASSERT_EQ(session_.OpenFile(kNoteType, "/tmp/shared.dat"), FlowResult::kDone);
  EXPECT_EQ(manager_.Overlays().size(), 2u);
}

TEST_F(SessionTest, ReopeningADirtyDocumentOffersToRevert) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  d->set_dirty(true);

  shell_.revert_answer = true;
  EXPECT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  ASSERT_EQ(shell_.asked_revert.size(), 1u);
  EXPECT_EQ(shell_.asked_revert[0], "/tmp/a.doc");
  ASSERT_EQ(d->reverted.size(), 1u);
  EXPECT_FALSE(d->is_dirty());
}

// Declining the revert is not a cancel: the file the user asked for is open
// and current, which is what they asked for. Their edits simply survive.
TEST_F(SessionTest, DecliningTheRevertKeepsTheEditsAndStillSucceeds) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  d->set_dirty(true);

  shell_.revert_answer = false;
  EXPECT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  EXPECT_TRUE(d->reverted.empty());
  EXPECT_TRUE(d->is_dirty());
  EXPECT_EQ(manager_.current(), d);
}

TEST_F(SessionTest, AnOverlayThatCannotRevertIsNotAsked) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  d->supports_revert = false;
  d->set_dirty(true);

  EXPECT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  EXPECT_TRUE(shell_.asked_revert.empty());
  EXPECT_TRUE(d->is_dirty());
}

TEST_F(SessionTest, TheOpenChooserGetsTheTypesOwnFiltersWhenTypeIsKnown) {
  shell_.files_to_open = {"/tmp/a.doc"};
  EXPECT_EQ(session_.OpenFileOverlays(kDocType), FlowResult::kDone);
  EXPECT_EQ(shell_.open_chooser_calls, 1);
  ASSERT_EQ(shell_.last_open_filters.size(), 1u);
  EXPECT_EQ(shell_.last_open_filters[0].second, "*.doc");
}

// No hint: the chooser gets every file type's filters and each chosen path is
// dispatched by its own extension.
TEST_F(SessionTest, WithNoTypeHintTheFiltersAreTheUnionAndTheExtensionDecides) {
  shell_.files_to_open = {"/tmp/a.doc", "/tmp/b.note"};
  EXPECT_EQ(session_.OpenFileOverlays(""), FlowResult::kDone);
  EXPECT_EQ(shell_.last_open_filters.size(), 2u);
  ASSERT_EQ(manager_.Overlays().size(), 2u);
  std::vector<std::string> types;
  for (const auto& o : manager_.Overlays()) types.push_back(o->type_id());
  EXPECT_NE(std::find(types.begin(), types.end(), kDocType), types.end());
  EXPECT_NE(std::find(types.begin(), types.end(), kNoteType), types.end());
}

TEST_F(SessionTest, AnEmptyFileChoiceIsACancel) {
  shell_.files_to_open.clear();
  EXPECT_EQ(session_.OpenFileOverlays(kDocType), FlowResult::kCanceled);
  EXPECT_TRUE(manager_.Overlays().empty());
  EXPECT_TRUE(shell_.errors.empty());  // a cancel is never reported as an error
}

// One bad file in a multi-select must not cost the user the good ones.
TEST_F(SessionTest, OneUnopenableFileDoesNotStopTheOthers) {
  shell_.files_to_open = {"/tmp/a.doc", "/tmp/b.xyz", "/tmp/c.doc"};
  EXPECT_EQ(session_.OpenFileOverlays(""), FlowResult::kFailed);
  EXPECT_EQ(manager_.Overlays().size(), 2u);
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

TEST_F(SessionTest, SavingAnUntitledDocumentGoesThroughSaveAs) {
  ASSERT_EQ(session_.NewFileOverlay(kDocType), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  d->set_dirty(true);

  shell_.save_spec = "/tmp/new.doc";
  EXPECT_EQ(session_.Save(*d), FlowResult::kDone);
  EXPECT_EQ(shell_.save_chooser_calls, 1);
  ASSERT_EQ(d->saved.size(), 1u);
  EXPECT_EQ(d->saved[0], "/tmp/new.doc");
  EXPECT_EQ(d->file_spec(), "/tmp/new.doc");
  EXPECT_TRUE(d->has_been_saved());
  EXPECT_FALSE(d->is_dirty());
}

TEST_F(SessionTest, TheSuggestedNameCarriesTheTypesDefaultExtension) {
  ASSERT_EQ(session_.NewFileOverlay(kDocType), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  d->set_dirty(true);
  shell_.save_spec = "/tmp/new.doc";
  ASSERT_EQ(session_.Save(*d), FlowResult::kDone);
  ASSERT_EQ(shell_.save_suggestions.size(), 1u);
  EXPECT_EQ(shell_.save_suggestions[0], "Untitled.doc");
}

TEST_F(SessionTest, SavingASavedDocumentWritesItsOwnSpecWithNoDialog) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  d->set_dirty(true);

  EXPECT_EQ(session_.Save(*d), FlowResult::kDone);
  EXPECT_EQ(shell_.save_chooser_calls, 0);
  ASSERT_EQ(d->saved.size(), 1u);
  EXPECT_EQ(d->saved[0], "/tmp/a.doc");
}

// The A3 addition beyond the plan: without a remembered format index, every
// plain Save after a Save As would silently rewrite the document in format 0.
TEST_F(SessionTest, SaveReusesTheFormatTheUserChoseInSaveAs) {
  ASSERT_EQ(session_.NewFileOverlay(kDocType), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  d->set_dirty(true);
  shell_.save_spec = "/tmp/old-format.doc";
  shell_.save_format = 1;
  ASSERT_EQ(session_.SaveAs(*d), FlowResult::kDone);
  ASSERT_EQ(d->saved_formats.size(), 1u);
  EXPECT_EQ(d->saved_formats[0], 1);

  d->set_dirty(true);
  ASSERT_EQ(session_.Save(*d), FlowResult::kDone);
  ASSERT_EQ(d->saved_formats.size(), 2u);
  EXPECT_EQ(d->saved_formats[1], 1);
}

TEST_F(SessionTest, SavingACleanDocumentTouchesNothing) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  EXPECT_EQ(session_.Save(*d), FlowResult::kDone);
  EXPECT_TRUE(d->saved.empty());
  EXPECT_EQ(shell_.save_chooser_calls, 0);
}

TEST_F(SessionTest, AReadOnlyDocumentIsSavedElsewhere) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  d->set_read_only(true);
  d->set_dirty(true);

  shell_.save_spec = "/tmp/copy.doc";
  EXPECT_EQ(session_.Save(*d), FlowResult::kDone);
  EXPECT_EQ(shell_.save_chooser_calls, 1);
  EXPECT_EQ(d->file_spec(), "/tmp/copy.doc");
  EXPECT_FALSE(d->is_read_only());
}

TEST_F(SessionTest, CancellingTheSaveDialogCancelsTheSave) {
  ASSERT_EQ(session_.NewFileOverlay(kDocType), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  d->set_dirty(true);
  shell_.save_spec.clear();  // cancel

  EXPECT_EQ(session_.Save(*d), FlowResult::kCanceled);
  EXPECT_TRUE(d->saved.empty());
  EXPECT_TRUE(d->is_dirty());
  EXPECT_TRUE(shell_.errors.empty());
}

TEST_F(SessionTest, ANewSpecIsBroadcastToTheStack) {
  CountingObserver obs;
  ASSERT_EQ(session_.NewFileOverlay(kDocType), FlowResult::kDone);
  manager_.AddObserver(&obs);
  DocOverlay* d = Doc(0);
  d->set_dirty(true);
  EXPECT_EQ(obs.dirty_changed, 1);

  shell_.save_spec = "/tmp/named.doc";
  ASSERT_EQ(session_.SaveAs(*d), FlowResult::kDone);
  EXPECT_EQ(obs.spec_changed, 1);
  EXPECT_EQ(obs.dirty_changed, 2);
  manager_.RemoveObserver(&obs);
}

TEST_F(SessionTest, SaveAllWritesEveryDirtyDocumentAndSkipsTheRest) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/b.doc"), FlowResult::kDone);
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/c.doc"), FlowResult::kDone);
  Doc(0)->set_dirty(true);
  Doc(2)->set_dirty(true);

  EXPECT_EQ(session_.SaveAll(), FlowResult::kDone);
  EXPECT_EQ(Doc(0)->saved.size(), 1u);
  EXPECT_TRUE(Doc(1)->saved.empty());
  EXPECT_EQ(Doc(2)->saved.size(), 1u);
}

// A user who cancelled one Save As did not mean "carry on with the rest".
TEST_F(SessionTest, ACancelInSaveAllStopsTheRest) {
  ASSERT_EQ(session_.NewFileOverlay(kDocType), FlowResult::kDone);
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/b.doc"), FlowResult::kDone);
  for (const auto& o : manager_.Overlays()) {
    static_cast<DocOverlay*>(o.get())->set_dirty(true);
  }
  shell_.save_spec.clear();  // the untitled one cancels

  EXPECT_EQ(session_.SaveAll(), FlowResult::kCanceled);
  // The untitled document is at the bottom (both types share display order
  // 100 and it was added first), so the walk stops before the second.
  EXPECT_TRUE(Doc(1)->saved.empty());
}

// ---------------------------------------------------------------------------
// Close
// ---------------------------------------------------------------------------

TEST_F(SessionTest, ClosingACleanDocumentAsksNothing) {
  CountingObserver obs;
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  manager_.AddObserver(&obs);

  EXPECT_EQ(session_.Close(*manager_.Overlays()[0]), FlowResult::kDone);
  EXPECT_TRUE(shell_.asked_save.empty());
  EXPECT_EQ(obs.removed, 1);
  EXPECT_TRUE(manager_.Overlays().empty());
  manager_.RemoveObserver(&obs);
}

TEST_F(SessionTest, ClosingADirtyDocumentPromptsAndSaveWrites) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  d->set_dirty(true);
  shell_.next_save_answer = AppShell::SaveAnswer::kSave;

  EXPECT_EQ(session_.Close(*d), FlowResult::kDone);
  ASSERT_EQ(shell_.asked_save.size(), 1u);
  EXPECT_EQ(shell_.asked_save[0], "/tmp/a.doc");  // the file, not the name
  EXPECT_TRUE(manager_.Overlays().empty());
}

TEST_F(SessionTest, DiscardClosesWithoutWriting) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  d->set_dirty(true);
  shell_.next_save_answer = AppShell::SaveAnswer::kDiscard;

  EXPECT_EQ(session_.Close(*d), FlowResult::kDone);
  EXPECT_TRUE(manager_.Overlays().empty());
}

TEST_F(SessionTest, CancelAtTheSavePromptAbortsTheClose) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  d->set_dirty(true);
  shell_.next_save_answer = AppShell::SaveAnswer::kCancel;

  EXPECT_EQ(session_.Close(*d), FlowResult::kCanceled);
  EXPECT_EQ(manager_.Overlays().size(), 1u);
  EXPECT_TRUE(d->is_dirty());
  EXPECT_TRUE(shell_.errors.empty());
}

// Closing anyway is how a user loses a document.
TEST_F(SessionTest, AFailedSaveAbortsTheClose) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  d->set_dirty(true);
  d->save_fails = true;
  shell_.next_save_answer = AppShell::SaveAnswer::kSave;

  EXPECT_EQ(session_.Close(*d), FlowResult::kFailed);
  EXPECT_EQ(manager_.Overlays().size(), 1u);
  EXPECT_EQ(session_.last_error().code, fv::kIoError);
}

// The Save-As inside a close can itself be cancelled, and that cancel is the
// close's answer -- not a failure.
TEST_F(SessionTest, CancellingTheSaveAsInsideACloseCancelsTheClose) {
  ASSERT_EQ(session_.NewFileOverlay(kDocType), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  d->set_dirty(true);
  shell_.next_save_answer = AppShell::SaveAnswer::kSave;
  shell_.save_spec.clear();

  EXPECT_EQ(session_.Close(*d), FlowResult::kCanceled);
  EXPECT_EQ(manager_.Overlays().size(), 1u);
}

TEST_F(SessionTest, TheEditedOverlayReleasesFocusBeforeItIsRemoved) {
  ASSERT_EQ(session_.ToggleStatic(kStaticType), FlowResult::kDone);
  // A close drops the stack's last reference, so the overlay is destroyed
  // inside Close unless the test holds one of its own.
  std::shared_ptr<fv::Overlay> keep = manager_.Overlays()[0];
  auto* s = static_cast<StaticOverlay*>(keep.get());
  ASSERT_TRUE(manager_.MakeCurrent(keep).ok());

  EXPECT_EQ(session_.Close(*s), FlowResult::kDone);
  EXPECT_EQ(s->released, 1);
}

TEST_F(SessionTest, ClosingAnOverlayThatIsNotInTheStackFails) {
  DocOverlay orphan("Orphan");
  EXPECT_EQ(session_.Close(orphan), FlowResult::kFailed);
  EXPECT_EQ(session_.last_error().code, fv::kNotFound);
}

TEST_F(SessionTest, CloseAllPromptsOncePerDirtyDocument) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/b.doc"), FlowResult::kDone);
  ASSERT_EQ(session_.ToggleStatic(kStaticType), FlowResult::kDone);
  Doc(0)->set_dirty(true);
  Doc(1)->set_dirty(true);
  shell_.next_save_answer = AppShell::SaveAnswer::kDiscard;

  EXPECT_EQ(session_.CloseAll(), FlowResult::kDone);
  EXPECT_EQ(shell_.asked_save.size(), 2u);
  EXPECT_TRUE(manager_.Overlays().empty());
}

// Any cancel aborts the rest -- and what has already closed stays closed,
// exactly as FalconView leaves it.
TEST_F(SessionTest, ACancelAbortsCloseAllAndTheAlreadyClosedStayClosed) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/b.doc"), FlowResult::kDone);
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/c.doc"), FlowResult::kDone);
  for (const auto& o : manager_.Overlays()) {
    static_cast<DocOverlay*>(o.get())->set_dirty(true);
  }
  // Top-down: c discards, b cancels, a is never reached.
  shell_.save_answers = {AppShell::SaveAnswer::kDiscard,
                         AppShell::SaveAnswer::kCancel};

  EXPECT_EQ(session_.CloseAll(), FlowResult::kCanceled);
  ASSERT_EQ(manager_.Overlays().size(), 2u);
  EXPECT_EQ(Doc(0)->file_spec(), "/tmp/a.doc");
  EXPECT_EQ(Doc(1)->file_spec(), "/tmp/b.doc");
}

TEST_F(SessionTest, CloseAllClosesEvenTypesTheUserMayNotClose) {
  ASSERT_EQ(session_.ToggleStatic(kFixedType), FlowResult::kDone);
  EXPECT_EQ(session_.CloseAll(), FlowResult::kDone);
  EXPECT_TRUE(manager_.Overlays().empty());
}

// ---------------------------------------------------------------------------
// Exit
// ---------------------------------------------------------------------------

TEST_F(SessionTest, ACancelledSaveCancelsTheWholeExit) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  Doc(0)->set_dirty(true);
  shell_.next_save_answer = AppShell::SaveAnswer::kCancel;

  EXPECT_EQ(session_.Exit(), FlowResult::kCanceled);
  EXPECT_EQ(manager_.Overlays().size(), 1u);
}

TEST_F(SessionTest, ExitAutosavesTheConfigurationOnlyWhenAsked) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  EXPECT_EQ(session_.Exit(), FlowResult::kDone);
  EXPECT_FALSE(settings_.Has("session.default.count"));

  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  settings_.Set("session.autosave", "true");
  EXPECT_EQ(session_.Exit(), FlowResult::kDone);
  EXPECT_EQ(settings_.GetInt("session.default.count", -1), 1);
}

// A cancelled exit must not overwrite the saved session with a half-closed one.
TEST_F(SessionTest, ACancelledExitDoesNotOverwriteTheSavedSession) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  Doc(0)->set_dirty(true);
  settings_.Set("session.autosave", "true");
  shell_.next_save_answer = AppShell::SaveAnswer::kCancel;

  EXPECT_EQ(session_.Exit(), FlowResult::kCanceled);
  EXPECT_FALSE(settings_.Has("session.default.count"));
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

TEST_F(SessionTest, AConfigurationRoundTripsThroughSettings) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  ASSERT_EQ(session_.OpenFile(kNoteType, "/tmp/b.note"), FlowResult::kDone);
  ASSERT_EQ(session_.ToggleStatic(kStaticType), FlowResult::kDone);
  manager_.Overlays()[0]->SetVisible(false);
  ASSERT_TRUE(manager_.MakeCurrent(manager_.Overlays()[1]).ok());
  manager_.SetDeclutter(true);

  ASSERT_TRUE(session_.SaveConfiguration("work").ok());
  EXPECT_EQ(settings_.GetInt("session.work.count", -1), 3);
  EXPECT_EQ(settings_.GetString("session.work.0.type"), kDocType);
  EXPECT_EQ(settings_.GetString("session.work.0.file"), "/tmp/a.doc");
  EXPECT_EQ(settings_.GetInt("session.work.current", -1), 1);

  ASSERT_EQ(session_.CloseAll(), FlowResult::kDone);
  manager_.SetDeclutter(false);
  ASSERT_TRUE(manager_.Overlays().empty());

  ASSERT_TRUE(session_.RestoreConfiguration("work").ok());
  ASSERT_EQ(manager_.Overlays().size(), 3u);
  EXPECT_EQ(manager_.Overlays()[0]->type_id(), kDocType);
  EXPECT_EQ(manager_.Overlays()[1]->type_id(), kNoteType);
  EXPECT_EQ(manager_.Overlays()[2]->type_id(), kStaticType);
  EXPECT_FALSE(manager_.Overlays()[0]->IsVisible());
  EXPECT_EQ(manager_.current(), manager_.Overlays()[1].get());
  EXPECT_TRUE(manager_.declutter());
  EXPECT_TRUE(session_.warnings().empty());
}

// Restore goes through the same flows, so an already-open file is deduped.
TEST_F(SessionTest, RestoringOverAnOpenSessionDoesNotDoubleAnything) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  ASSERT_TRUE(session_.SaveConfiguration("work").ok());
  ASSERT_TRUE(session_.RestoreConfiguration("work").ok());
  EXPECT_EQ(manager_.Overlays().size(), 1u);
}

TEST_F(SessionTest, RestoringAConfigurationThatWasNeverSavedIsNotFound) {
  EXPECT_EQ(session_.RestoreConfiguration("nope").code, fv::kNotFound);
}

// An overlay made outside the app layer has no descriptor to remake it from.
TEST_F(SessionTest, AnOverlayWithNoTypeIsSkippedWithAWarning) {
  ASSERT_TRUE(manager_.Add(std::make_shared<DocOverlay>("Ad hoc")).ok());
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);

  ASSERT_TRUE(session_.SaveConfiguration("work").ok());
  EXPECT_EQ(settings_.GetInt("session.work.count", -1), 1);
  EXPECT_EQ(session_.warnings().size(), 1u);
}

// A plugin that is gone, or a downgrade: skipped with a note, not an aborted
// restore.
TEST_F(SessionTest, AnUnknownTypeInAConfigurationIsSkipped) {
  settings_.Set("session.work.count", "2");
  settings_.Set("session.work.0.type", "test.gone");
  settings_.Set("session.work.0.file", "/tmp/x.gone");
  settings_.Set("session.work.1.type", kDocType);
  settings_.Set("session.work.1.file", "/tmp/a.doc");

  ASSERT_TRUE(session_.RestoreConfiguration("work").ok());
  ASSERT_EQ(manager_.Overlays().size(), 1u);
  EXPECT_EQ(manager_.Overlays()[0]->type_id(), kDocType);
  EXPECT_EQ(session_.warnings().size(), 1u);
}

TEST_F(SessionTest, AnUntitledDocumentCannotBeRestored) {
  ASSERT_EQ(session_.NewFileOverlay(kDocType), FlowResult::kDone);
  ASSERT_TRUE(session_.SaveConfiguration("work").ok());
  ASSERT_EQ(session_.CloseAll(), FlowResult::kDone);
  shell_.next_save_answer = AppShell::SaveAnswer::kDiscard;

  ASSERT_TRUE(session_.RestoreConfiguration("work").ok());
  EXPECT_TRUE(manager_.Overlays().empty());
  EXPECT_EQ(session_.warnings().size(), 1u);
}

// The file is one a human is invited to edit, so `count` is a hint and the
// rows are the truth -- a count left too high after rows were deleted by hand
// must not spin through a billion settings lookups.
TEST_F(SessionTest, ACountThatOutrunsTheRowsStopsAtTheLastRow) {
  settings_.Set("session.work.count", "1000000000");
  settings_.Set("session.work.0.type", kDocType);
  settings_.Set("session.work.0.file", "/tmp/a.doc");

  ASSERT_TRUE(session_.RestoreConfiguration("work").ok());
  EXPECT_EQ(manager_.Overlays().size(), 1u);
  EXPECT_EQ(session_.warnings().size(), 1u);
}

TEST_F(SessionTest, StartupOverlaysAreTheStaticTypesFlaggedForIt) {
  ASSERT_TRUE(session_.RestoreStartupOverlays().ok());
  ASSERT_EQ(manager_.Overlays().size(), 1u);
  EXPECT_EQ(manager_.Overlays()[0]->type_id(), kStaticType);

  // Idempotent: a second call must not add a second graticule.
  ASSERT_TRUE(session_.RestoreStartupOverlays().ok());
  EXPECT_EQ(manager_.Overlays().size(), 1u);
}

// ---------------------------------------------------------------------------
// Reentrancy (plan §6)
// ---------------------------------------------------------------------------

// A shell whose dialog pumps the message loop can deliver a click while a Save
// prompt is up. The nested flow fails loudly instead of corrupting the stack.
TEST_F(SessionTest, AFlowEnteredFromInsideAFlowFailsLoudly) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  DocOverlay* d = Doc(0);
  d->set_dirty(true);

  // A shell that starts a second flow from inside AskSave.
  class ReentrantShell : public FakeShell {
   public:
    OverlaySession* session = nullptr;
    FlowResult nested = FlowResult::kDone;
    SaveAnswer AskSave(const std::string& name) override {
      if (session != nullptr) {
        OverlaySession* s = session;
        session = nullptr;  // once
        nested = s->ToggleStatic(kStaticType);
      }
      return FakeShell::AskSave(name);
    }
  };
  ReentrantShell reentrant;
  OverlaySession session(registry_, manager_, reentrant, settings_);
  reentrant.session = &session;
  reentrant.next_save_answer = AppShell::SaveAnswer::kDiscard;

  EXPECT_EQ(session.Close(*d), FlowResult::kDone);
  EXPECT_EQ(reentrant.nested, FlowResult::kFailed);
  EXPECT_EQ(session.last_error().code, fv::kInternal);
  // The nested flow did nothing: no stray static overlay.
  EXPECT_TRUE(manager_.Overlays().empty());
}

// Close calls Save internally, and that must NOT trip the guard.
TEST_F(SessionTest, TheGuardDoesNotFireOnAFlowCallingAnotherInternally) {
  ASSERT_EQ(session_.OpenFile(kDocType, "/tmp/a.doc"), FlowResult::kDone);
  std::shared_ptr<fv::Overlay> keep = manager_.Overlays()[0];  // outlive Close
  auto* d = static_cast<DocOverlay*>(keep.get());
  d->set_dirty(true);
  shell_.next_save_answer = AppShell::SaveAnswer::kSave;

  EXPECT_EQ(session_.Close(*d), FlowResult::kDone);
  EXPECT_EQ(d->saved.size(), 1u);
  EXPECT_TRUE(shell_.errors.empty());
}

}  // namespace
