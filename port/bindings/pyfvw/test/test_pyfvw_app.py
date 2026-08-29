# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""pyfvw.app — the app layer through the Python surface (A6).

These are not a translation of the C++ app tests; those already pin the flows
(port/fvkit/app/test/). What is only testable HERE is the seam itself:

  * a Python class really is an AppShell — the core calls back into it, and a
    cancel it returns really does abort the flow that asked;
  * a capability really is discovered from the methods a Python subclass
    defines, with no registration and no base class;
  * an overlay made by a PYTHON FACTORY keeps its overrides after C++ takes
    ownership of it, which is the trampoline-lifetime trap contract D1 exists
    for and the one thing most likely to break silently;
  * a Python EDITOR is the same object on the way back out.

Run via ctest (pyfvw_pytest) or:
  PYTHONPATH=build/port/bindings/pyfvw pytest -q port/bindings/pyfvw/test
"""

import gc
import os

import pytest

import pyfvw

app = pyfvw.app


# ---------------------------------------------------------------------------
# A scripted shell — the Python peer of the C++ FakeShell
# ---------------------------------------------------------------------------

class ScriptedShell(app.AppShell):
    def __init__(self):
        super().__init__()
        self.save_answer = app.AppShell.SaveAnswer.DISCARD
        self.files_to_open = []
        self.save_spec = ("", 0)
        self.list_choice = 0
        self.revert = True
        self.cursor = app.CursorId.DEFAULT
        self.hint = app.HintText()
        self.errors = []
        self.menus = []
        self.editor_changes = []
        self.invalidations = 0
        self.asked = []

    def ask_save(self, name):
        self.asked.append(name)
        return self.save_answer

    def choose_files_to_open(self, file_type):
        return list(self.files_to_open)

    def choose_save_spec(self, file_type, suggested):
        return self.save_spec

    def choose_from_list(self, title, rows):
        self.asked.append((title, list(rows)))
        return self.list_choice

    def confirm_revert(self, spec):
        return self.revert

    def set_cursor(self, cursor):
        self.cursor = cursor

    def show_hint(self, hint):
        self.hint = hint

    def show_context_menu(self, x, y, menu):
        self.menus.append([c.label for c in menu.children])

    def request_invalidate(self):
        self.invalidations += 1

    def on_editor_changed(self, type_id, editor):
        self.editor_changes.append((type_id, editor))

    def report_error(self, code, message):
        self.errors.append((code, message))


class Notes(pyfvw.overlay.Overlay):
    """A Python file overlay. Defining these methods is the ONLY thing that
    makes it one — there is no base class and nothing to register."""

    def __init__(self, name="Notes"):
        super().__init__(name)
        self.lines = []
        self.focus = 0

    # Persistence
    def file_new(self):
        self.lines = []

    def file_open(self, spec):
        with open(spec) as f:
            self.lines = [ln.strip() for ln in f if ln.strip()]

    def file_save_as(self, spec, format_index):
        with open(spec, "w") as f:
            f.write("\n".join(self.lines))

    def revert(self, spec):
        self.file_open(spec)
        self.dirty = False

    # HitTest
    def hit_test_point(self, proj, x, y, tolerance_px):
        d = ((x - 100) ** 2 + (y - 100) ** 2) ** 0.5
        if d > tolerance_px:
            return []
        return [app.HitItem(feature=7, distance_px=d,
                            hint=app.HintText("note", "the note at 100,100"),
                            cursor=app.CursorId.HAND)]

    # ContextMenu. Contributes only where it has something to say, which is
    # what lets "no overlay contributed" be testable at all.
    def menu_items(self, proj, x, y):
        if not self.hit_test_point(proj, x, y, 8.0):
            return []
        return [app.MenuNode(label="Add a line",
                             action=lambda: self.lines.append("added"))]

    # SearchProvider (S4). Defining `search` is what makes the overlay
    # searchable, exactly as defining `hit_test_point` makes it pickable — and
    # the label field is this overlay's own business, which is the whole point
    # of the seam: the caller never learns that a note's title is its text.
    def search(self, query):
        rows = []
        for i, line in enumerate(self.lines):
            q = app.text_match_quality(query.text, line)
            if q < 0:
                continue
            rows.append(app.SearchResult(
                title=line, detail="note", match_quality=q, feature=i,
                position=pyfvw.geo.GeoPoint(32.6 + i * 0.001, -80.11)))
        return rows

    # EditTarget
    def enter_edit_focus(self):
        self.focus += 1

    def release_edit_focus(self):
        self.focus -= 1


class NotesEditor:
    """Duck-typed: activate/deactivate is the whole contract."""

    def __init__(self):
        self.activations = 0
        self.active = False

    def activate(self):
        self.activations += 1
        self.active = True

    def deactivate(self):
        self.active = False

    def tools(self):
        return [app.MenuNode(label="Pen"), app.MenuNode(label="Eraser")]

    def ui_constraints(self):
        return app.EditorUiConstraints(disable_rotation=True)


class Fixture:
    """Registry + stack + shell + session + editors + pick, wired the way a
    shell wires them, including both directions of the session/editor link."""

    def __init__(self, notes_factory=None):
        self.shell = ScriptedShell()
        self.registry = app.OverlayTypeRegistry()
        app.register_builtin_types(self.registry)
        self.editor = NotesEditor()
        self.registry.register(app.OverlayTypeDesc(
            id="user.notes", display_name="Notes",
            factory=notes_factory or (lambda: Notes()),
            editor_factory=lambda: self.editor,
            file=app.FileTypeDesc(default_extension="notes",
                                  open_filters=[("Notes (*.notes)", "*.notes")]),
            default_display_order=1000))
        self.manager = pyfvw.overlay.OverlayManager()
        self.manager.set_type_registry(self.registry)
        self.settings = pyfvw.Settings()
        self.session = app.OverlaySession(self.registry, self.manager,
                                          self.shell, self.settings)
        self.editors = app.EditorManager(self.registry, self.manager, self.shell)
        self.editors.set_session(self.session)
        self.session.set_editor_manager(self.editors)
        self.pick = app.PickSession(self.manager, self.shell)


def _proj():
    p = pyfvw.engine.MapProjection()
    p.set_surface_size(800, 600)
    p.set_center(pyfvw.geo.GeoPoint(32.74, -79.89))
    p.set_scale(50000.0)
    return p


# ---------------------------------------------------------------------------
# The registry
# ---------------------------------------------------------------------------

def test_builtin_types_are_the_grid_and_the_point_overlay():
    reg = app.OverlayTypeRegistry()
    app.register_builtin_types(reg)
    ids = [d.id for d in reg.all()]
    assert app.GRID_TYPE_ID in ids and app.POINTS_TYPE_ID in ids
    assert reg.is_static(app.GRID_TYPE_ID)
    assert reg.is_file(app.POINTS_TYPE_ID)
    # A leading dot is tolerated and case does not matter -- a file spec has
    # both and neither is the user's problem.
    assert reg.find_by_extension(".FVPOINTS").id == app.POINTS_TYPE_ID


def test_a_type_without_a_factory_is_rejected_at_registration():
    reg = app.OverlayTypeRegistry()
    with pytest.raises(pyfvw.FvError):
        reg.register(app.OverlayTypeDesc(id="user.broken", display_name="X"))
    with pytest.raises(pyfvw.FvError):
        reg.register(app.OverlayTypeDesc(id="", factory=lambda: None))


def test_registering_the_same_id_twice_is_rejected():
    fx = Fixture()
    with pytest.raises(pyfvw.FvError):
        fx.registry.register(app.OverlayTypeDesc(
            id="user.notes", factory=lambda: Notes()))


# ---------------------------------------------------------------------------
# Capabilities are the methods you defined
# ---------------------------------------------------------------------------

def test_defining_file_methods_is_what_makes_an_overlay_a_document():
    plain = pyfvw.overlay.Overlay("plain")
    assert not plain.is_file_overlay
    assert plain.file_spec == "" and plain.dirty is False
    with pytest.raises(pyfvw.FvError):
        plain.dirty = True          # loud, not a silent no-op

    notes = Notes()
    assert notes.is_file_overlay
    notes.dirty = True
    assert notes.dirty


def test_a_python_overlay_answers_picks_and_the_binding_stamps_the_overlay():
    fx = Fixture()
    notes = Notes()
    fx.manager.add(notes)
    hits = fx.pick.hit_test_point(_proj(), 100, 100)
    assert len(hits) == 1
    assert hits[0].feature == 7
    # Stamped by the binding: an overlay cannot attribute a hit to somebody
    # else's overlay even if it tried.
    assert hits[0].overlay.name == "Notes"
    assert hits[0].hint.status == "the note at 100,100"

    fx.pick.update_hover(_proj(), 100, 100)
    assert fx.shell.cursor == app.CursorId.HAND
    assert fx.shell.hint.status == "the note at 100,100"
    # Only on a CHANGE: hovering the same thing again says nothing new.
    fx.shell.hint = app.HintText()
    fx.pick.update_hover(_proj(), 101, 100)
    assert fx.shell.hint.status == ""


def test_a_python_overlay_contributes_a_context_menu_section():
    fx = Fixture()
    notes = Notes()
    fx.manager.add(notes)
    assert fx.pick.show_context_menu(_proj(), 100, 100)
    assert fx.shell.menus == [["Add a line"]]
    menu = fx.pick.build_context_menu(_proj(), 100, 100)
    menu.children[0].invoke()
    assert notes.lines == ["added"]
    # Nothing under the point contributes nothing, and the shell is not even
    # called -- an empty menu flashing open is worse than no menu.
    assert not fx.pick.show_context_menu(_proj(), 400, 400)
    assert len(fx.shell.menus) == 1


# ---------------------------------------------------------------------------
# The flows, against a Python shell
# ---------------------------------------------------------------------------

def test_new_open_save_close_round_trip(tmp_path):
    fx = Fixture()
    assert fx.session.new_file_overlay("user.notes") == app.FlowResult.DONE
    notes = fx.manager.first_of_type("user.notes")
    assert notes is not None and notes.type_id == "user.notes"
    assert fx.manager.current is notes

    notes.lines = ["one", "two"]
    notes.dirty = True
    spec = str(tmp_path / "a.notes")
    fx.shell.save_spec = (spec, 0)
    assert fx.session.save(notes) == app.FlowResult.DONE
    assert notes.file_spec == spec and not notes.dirty and notes.has_been_saved
    assert os.path.exists(spec)

    # Re-opening the SAME (type, spec) makes it current rather than doubling.
    assert fx.session.open_file("user.notes", spec) == app.FlowResult.DONE
    assert len(fx.manager.of_type("user.notes")) == 1

    assert fx.session.close(notes) == app.FlowResult.DONE
    assert fx.manager.overlays == []


def test_a_cancel_in_the_save_prompt_aborts_the_close(tmp_path):
    fx = Fixture()
    fx.session.new_file_overlay("user.notes")
    notes = fx.manager.first_of_type("user.notes")
    notes.dirty = True

    fx.shell.save_answer = app.AppShell.SaveAnswer.CANCEL
    assert fx.session.close(notes) == app.FlowResult.CANCELED
    assert fx.manager.contains(notes)        # still open, still dirty
    assert fx.shell.asked                    # and the user really was asked

    fx.shell.save_answer = app.AppShell.SaveAnswer.DISCARD
    assert fx.session.close(notes) == app.FlowResult.DONE
    assert not fx.manager.contains(notes)


def test_a_failing_open_is_reported_and_leaves_nothing_in_the_stack(tmp_path):
    fx = Fixture()
    missing = str(tmp_path / "nope.notes")
    assert fx.session.open_file("user.notes", missing) == app.FlowResult.FAILED
    assert fx.manager.overlays == []
    # A Python overlay reports a failure by RAISING, and the message survives
    # all the way out to the shell.
    assert fx.shell.errors
    assert "nope.notes" in fx.shell.errors[-1][1]
    assert fx.session.last_error is not None


def test_open_dispatches_by_extension_when_no_type_is_named(tmp_path):
    fx = Fixture()
    spec = tmp_path / "b.notes"
    spec.write_text("hello\n")
    assert fx.session.open_file("", str(spec)) == app.FlowResult.DONE
    notes = fx.manager.first_of_type("user.notes")
    assert notes.lines == ["hello"]
    # The document renames nothing by itself; the session owns the spec.
    assert notes.file_spec == str(spec)


def test_the_chooser_drives_open_file_overlays(tmp_path):
    fx = Fixture()
    a, b = tmp_path / "a.notes", tmp_path / "b.notes"
    a.write_text("one\n")
    b.write_text("two\n")
    fx.shell.files_to_open = [str(a), str(b)]
    assert fx.session.open_file_overlays("") == app.FlowResult.DONE
    assert len(fx.manager.of_type("user.notes")) == 2
    # Choosing no files IS the cancel; there is no second signal.
    fx.shell.files_to_open = []
    assert fx.session.open_file_overlays("") == app.FlowResult.CANCELED


def test_static_types_toggle_rather_than_open():
    fx = Fixture()
    assert fx.session.toggle_static(app.GRID_TYPE_ID) == app.FlowResult.DONE
    assert fx.manager.first_of_type(app.GRID_TYPE_ID) is not None
    assert fx.session.toggle_static(app.GRID_TYPE_ID) == app.FlowResult.DONE
    assert fx.manager.first_of_type(app.GRID_TYPE_ID) is None
    # A file type has many instances and nothing to toggle.
    assert fx.session.toggle_static("user.notes") == app.FlowResult.FAILED


# ---------------------------------------------------------------------------
# Editors
# ---------------------------------------------------------------------------

def test_the_mode_dance_from_python():
    fx = Fixture()
    # Entering a mode with nothing of its type open CREATES one, because the
    # editor auto-enters by default.
    assert fx.editors.set_mode("user.notes") == app.FlowResult.DONE
    notes = fx.manager.first_of_type("user.notes")
    assert notes is not None
    # The editor comes back as the object the factory returned, not a wrapper.
    assert fx.editors.current_editor is fx.editor
    assert fx.editor.active
    assert fx.editors.edited.name == notes.name
    assert notes.focus == 1                      # the per-instance bracket
    assert fx.editors.active_constraints.disable_rotation
    assert [t.label for t in fx.editors.current_editor.tools()] == ["Pen", "Eraser"]
    # The shell heard about it, and heard about the same object.
    assert fx.shell.editor_changes[-1] == ("user.notes", fx.editor)

    assert fx.editors.set_mode("") == app.FlowResult.DONE
    assert fx.editors.current_mode == ""
    assert not fx.editor.active
    assert notes.focus == 0
    assert fx.editors.current_editor is None

    # One editor instance per TYPE: re-entering reuses it, so tool state
    # survives leaving and coming back.
    fx.editors.set_mode("user.notes")
    assert fx.editors.current_editor is fx.editor
    assert fx.editor.activations == 2


def test_creating_a_document_auto_enters_its_editor():
    fx = Fixture()
    assert fx.session.new_file_overlay("user.notes") == app.FlowResult.DONE
    assert fx.editors.current_mode == "user.notes"
    # ...on CREATE only. Opening a file does not drag the user into an editor.
    fx.editors.set_mode("")
    assert fx.editors.current_mode == ""


def test_closing_the_edited_overlay_leaves_the_mode():
    fx = Fixture()
    fx.editors.set_mode("user.notes")
    notes = fx.manager.first_of_type("user.notes")
    assert fx.session.close(notes) == app.FlowResult.DONE
    assert fx.editors.current_mode == ""
    assert not fx.editor.active


def test_an_editor_that_refuses_to_activate_fails_the_mode():
    class Refuses:
        def activate(self):
            raise RuntimeError("no tools today")

        def deactivate(self):
            pass

    fx = Fixture()
    fx.registry.register(app.OverlayTypeDesc(
        id="user.refuses", display_name="Refuses",
        factory=lambda: Notes("Refuses"), editor_factory=lambda: Refuses(),
        file=app.FileTypeDesc(default_extension="ref")))
    assert fx.editors.set_mode("user.refuses") == app.FlowResult.FAILED
    assert fx.editors.current_mode == ""
    assert any("no tools today" in msg for _c, msg in fx.shell.errors)


# ---------------------------------------------------------------------------
# The trampoline-lifetime trap (contract D1) — the reason for the alias holder
# ---------------------------------------------------------------------------

def test_a_factory_made_overlay_keeps_its_python_overrides():
    """The factory is called from inside a flow with no Python reference in
    scope. If the C++ shared_ptr did not keep the Python half alive, this
    overlay would still be in the stack, still draw, and silently answer no
    picks -- which is the failure mode this test exists to catch."""
    fx = Fixture()
    fx.session.new_file_overlay("user.notes")
    for _ in range(3):
        gc.collect()
    notes = fx.manager.first_of_type("user.notes")
    assert isinstance(notes, Notes)
    hits = fx.pick.hit_test_point(_proj(), 100, 100)
    assert len(hits) == 1 and hits[0].feature == 7
    assert notes.is_file_overlay


def test_the_python_half_is_released_when_the_overlay_is_closed():
    fx = Fixture()
    fx.session.new_file_overlay("user.notes")
    notes = fx.manager.first_of_type("user.notes")
    fx.session.close(notes)
    del notes
    for _ in range(3):
        gc.collect()
    assert not any(isinstance(o, Notes) for o in fx.manager.overlays)


# ---------------------------------------------------------------------------
# The C++ point overlay through the same seams
# ---------------------------------------------------------------------------

def test_the_point_overlay_opens_draws_and_picks(tmp_path):
    spec = str(tmp_path / "sample.fvpoints")
    pyfvw.overlay.PointOverlay.write_sample_file(spec)
    fx = Fixture()
    assert fx.session.open_file("", spec) == app.FlowResult.DONE
    points = fx.manager.first_of_type(app.POINTS_TYPE_ID)
    assert len(points.points) == 26
    assert points.name == "Sample Points"      # the document's own name
    # Written with no symbol directory, so the palette is empty and every
    # point draws as its shape -- the schema-1 picture, still supported.
    assert points.symbols == []

    proj = _proj()
    canvas = pyfvw.canvas.CpuCanvas(800, 600)
    canvas.clear((255, 255, 255, 255))
    fx.manager.draw_all(proj, canvas)

    light = points.find(8)
    assert light.name == "Charleston Light" and light.category == "light"
    x, y = proj.geo_to_surface(light.position)
    hits = fx.pick.hit_test_point(proj, int(x), int(y))
    assert [h.feature for h in hits] == [8]
    assert "163 ft" in hits[0].hint.status

    # The sample's paired points really are ambiguous, so the chooser runs.
    r2 = points.find(10)
    x, y = proj.geo_to_surface(r2.position)
    fx.shell.list_choice = 1
    chosen = fx.pick.resolve_click(proj, int(x), int(y),
                                   app.PickPolicy.ASK_WHEN_AMBIGUOUS)
    assert chosen.feature == 11                # the row the user picked
    assert isinstance(fx.shell.asked[-1], tuple)
    assert len(fx.shell.asked[-1][1]) == 2

    # Cancelling the chooser and finding nothing are the same answer.
    fx.shell.list_choice = None
    assert fx.pick.resolve_click(proj, int(x), int(y),
                                 app.PickPolicy.ASK_WHEN_AMBIGUOUS) is None


def test_point_documents_round_trip_through_python(tmp_path):
    spec = str(tmp_path / "made.fvpoints")
    o = pyfvw.overlay.PointOverlay("Made In Python")
    o.set_points([
        pyfvw.overlay.MapPoint("Alpha", 32.7, -79.9, shape="star", size_px=12,
                               color=(10, 200, 30), category="test",
                               elevation_ft=99.0, remarks="round trip", id=1),
    ])
    o.file_save_as(spec)

    back = pyfvw.overlay.PointOverlay()
    back.file_open(spec)
    assert len(back.points) == 1
    p = back.points[0]
    assert (p.name, p.shape, p.category, p.elevation_ft) == \
        ("Alpha", "star", "test", 99.0)
    assert p.color == (10, 200, 30, 255)
    assert back.name == "Made In Python"


def test_a_point_documents_symbols_are_embedded_and_shared(tmp_path):
    """Schema 2 from Python: the artwork lives in the file, and many points
    reference one row of it."""
    png = tmp_path / "castle.png"
    # A 1x1 PNG, written by hand -- the binding stores bytes and does not
    # decode until something draws, so this never has to be real artwork.
    import base64
    png.write_bytes(base64.b64decode(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP8"
        "z8DwHwAFAAH/q842iQAAAABJRU5ErkJggg=="))

    spec = str(tmp_path / "forts.fvpoints")
    o = pyfvw.overlay.PointOverlay("Forts")
    sym_id = o.add_symbol_from_png(str(png))
    assert sym_id > 0
    # The same name is the same row: that is what makes sharing the default.
    assert o.add_symbol_from_png(str(png)) == sym_id
    assert len(o.symbols) == 1
    assert o.find_symbol_by_name("castle").id == sym_id

    o.set_points([
        pyfvw.overlay.MapPoint("Sumter", 32.7522, -79.8747,
                               symbol_id=sym_id, id=1),
        pyfvw.overlay.MapPoint("Moultrie", 32.7594, -79.8577,
                               symbol_id=sym_id, id=2),
    ])
    o.file_save_as(spec)

    back = pyfvw.overlay.PointOverlay()
    back.file_open(spec)
    assert len(back.symbols) == 1
    assert back.symbols[0].name == "castle"
    assert back.symbols[0].image == png.read_bytes()
    assert back.symbols[0].pivot is None        # unset stays unset
    assert [p.symbol_id for p in back.points] == [sym_id, sym_id]


# ---------------------------------------------------------------------------
# The stack, A2 surface
# ---------------------------------------------------------------------------

def test_display_order_places_a_new_overlay_and_the_top_most_band_draws_last():
    fx = Fixture()
    fx.registry.register(app.OverlayTypeDesc(
        id="user.hud", display_name="HUD", factory=lambda: Notes("HUD"),
        is_top_most=True))
    fx.session.toggle_static("user.hud")
    fx.session.toggle_static(app.GRID_TYPE_ID)      # order 900
    fx.session.new_file_overlay("user.notes")       # order 1000

    names = [o.name for o in fx.manager.draw_order()]
    assert names[-1] == "HUD"            # top-most band draws over everything
    assert names.index("grid") < names.index("Notes")


def test_a_stack_observer_hears_what_the_flows_do(tmp_path):
    seen = []

    class Watcher(app.StackObserver):
        def overlay_added(self, o):
            seen.append(("add", o.name))

        def overlay_removed(self, o):
            seen.append(("remove", o.name))

        def current_changed(self, now, was):
            seen.append(("current", now.name if now else None))

        def dirty_changed(self, o):
            seen.append(("dirty", o.dirty))

    fx = Fixture()
    watcher = Watcher()
    fx.manager.add_observer(watcher)
    fx.session.new_file_overlay("user.notes")
    notes = fx.manager.first_of_type("user.notes")
    notes.dirty = True
    notes.dirty = True                    # a redundant set says nothing
    fx.shell.save_answer = app.AppShell.SaveAnswer.DISCARD
    fx.session.close(notes)
    fx.manager.remove_observer(watcher)

    assert ("add", "Notes") in seen
    assert ("remove", "Notes") in seen
    assert seen.count(("dirty", True)) == 1


# ---------------------------------------------------------------------------
# Search (S4) — the second aggregating capability, from Python
# ---------------------------------------------------------------------------


def _searchable_notes():
    notes = Notes()
    notes.lines = ["Ruddy Turnstone", "Beach Walk", "Governors Drive"]
    return notes


def test_defining_search_is_what_makes_an_overlay_searchable():
    fx = Fixture()
    notes = _searchable_notes()
    fx.manager.add(notes)

    r = app.SearchSession(fx.manager).search(app.SearchQuery(text="rud tur"))
    assert len(r) == 1
    assert r[0].title == "Ruddy Turnstone"
    assert r[0].detail == "note"
    assert r[0].feature == 0
    # Stamped by the binding, for the reason a hit is: an overlay must not be
    # able to attribute a result to somebody else's overlay.
    assert r[0].overlay.name == "Notes"
    # The shared rule decided the quality, not the provider.
    assert r[0].match_quality == 2


def test_an_overlay_without_a_search_method_is_simply_not_asked():
    fx = Fixture()
    fx.manager.add(pyfvw.overlay.Overlay("Plain"))
    assert app.SearchSession(fx.manager).search(app.SearchQuery(text="x")) == []


def test_search_finds_things_in_a_hidden_overlay_and_visible_only_does_not():
    """The deliberate opposite of picking. 'Where is X' is a fair question
    about a layer the user switched off an hour ago, and the answer names the
    overlay so a shell can say where it was."""
    fx = Fixture()
    notes = _searchable_notes()
    fx.manager.add(notes)
    notes.visible = False

    assert len(app.SearchSession(fx.manager).search(
        app.SearchQuery(text="beach"))) == 1
    assert app.SearchSession(fx.manager).search(
        app.SearchQuery(text="beach", visible_only=True)) == []


def test_a_search_crosses_the_stack_and_the_caller_names_no_field(tmp_path):
    """The plan's acceptance criterion, in Python: one query, two overlay
    types that have never heard of each other, and a caller that knows neither
    the column a point document stores a name in nor what a note calls its
    text."""
    fx = Fixture()
    doc = tmp_path / "points.fvpoints"
    points = pyfvw.overlay.PointOverlay("Points")
    points.file_new()
    points.add_point(pyfvw.overlay.MapPoint("Ruddy Turnstone", 32.6044,
                                            -80.1083))
    points.file_save_as(str(doc), 0)
    fx.manager.add(points)
    fx.manager.add(_searchable_notes())

    r = app.SearchSession(fx.manager).search(
        app.SearchQuery(text="Ruddy Turnstone"))
    # BOTH answers, and both are true. Cross-provider dedup is deferred on
    # purpose (search-plan-COMPLETE.md): the caller tells them apart by
    # `detail`.
    assert len(r) == 2
    assert {x.detail for x in r} == {"note", "point"}
    assert {x.overlay.name for x in r} == {"Notes", "Points"}


def test_ordering_by_distance_is_an_enum_value_and_not_a_grammar():
    fx = Fixture()
    fx.manager.add(_searchable_notes())
    q = app.SearchQuery(text="", area=pyfvw.geo.GeoRect(
        pyfvw.geo.GeoPoint(32.59, -80.12), pyfvw.geo.GeoPoint(32.61, -80.10)))
    # The third note is the northernmost; asking from the north puts it first.
    q.near = pyfvw.geo.GeoPoint(32.61, -80.11)
    q.order = app.SearchOrder.NEAREST
    r = app.SearchSession(fx.manager).search(q)
    assert [x.title for x in r] == ["Governors Drive", "Beach Walk",
                                    "Ruddy Turnstone"]
    q.near = pyfvw.geo.GeoPoint(32.59, -80.11)
    assert app.SearchSession(fx.manager).search(q)[0].title == "Ruddy Turnstone"
    assert app.search_order_name(app.SearchOrder.NEAREST) == "nearest"


def test_a_radius_is_a_cut_the_session_makes_and_no_provider_implements():
    fx = Fixture()
    fx.manager.add(_searchable_notes())
    q = app.SearchQuery(near=pyfvw.geo.GeoPoint(32.6, -80.11), radius_m=150.0)
    r = app.SearchSession(fx.manager).search(q)
    # The notes are 111 m apart in latitude, so 150 m reaches exactly two.
    assert [x.title for x in r] == ["Ruddy Turnstone", "Beach Walk"]


def test_a_cancelled_search_returns_what_it_had():
    fx = Fixture()
    fx.manager.add(_searchable_notes())
    flag = app.CancelFlag()
    flag.set()
    assert flag.cancelled
    assert app.SearchSession(fx.manager).search(
        app.SearchQuery(text="beach"), flag) == []
    flag.clear()
    assert len(app.SearchSession(fx.manager).search(
        app.SearchQuery(text="beach"), flag)) == 1
