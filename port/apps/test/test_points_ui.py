# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""PE4 — the point palette and the two shell policies that surround it.

The gestures are pinned in C++ and the bindings in `test_pyfvw_points.py`. What
is only testable here is the SHELL's half, which is the half with no tk in it:

  * a built-in type gains a palette without being re-declared, so the Tools
    menu and the button bar get a Points mode from one line;
  * the palette reads the overlay the EditorManager says is being edited, not
    one it remembered;
  * the dimming rule — every open point set except the edited one, and only
    while the point mode is on;
  * bringing a dimmed set forward puts it above the other point sets and hands
    it the edit, without climbing over a route or a crosshair;
  * the point sheet's two faces — the symbol set it offers, the document's own
    artwork in it, and the fields it writes back.

The sheet's tests are the only ones here that open a window, and they skip
where there is no display; everything above them is tk-free.

Run via ctest (pythonview_pytest) or:
  PYTHONPATH=build/port/bindings/pyfvw:port/apps pytest -q port/apps/test
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

pyfvw = pytest.importorskip("pyfvw")

import points as points_mod  # noqa: E402

app = pyfvw.app
ovl = pyfvw.overlay


class FakeShell(app.AppShell):
    """The least shell the app layer will drive: it answers nothing and
    records nothing, because these tests are about the stack and the mode."""

    def report_error(self, code, message):
        pass

    def request_invalidate(self):
        pass


class Bench:
    """A registry, a stack, a session and an editor manager — PythonView's app
    layer with the window taken off."""

    def __init__(self):
        self.shell = FakeShell()
        self.settings = pyfvw.Settings()
        self.mgr = ovl.OverlayManager()
        self.registry = app.OverlayTypeRegistry()
        app.register_builtin_types(self.registry)
        points_mod.attach_editor(self.registry,
                                 lambda: points_mod.PointEditor(self))
        self.mgr.set_type_registry(self.registry)
        self.session = app.OverlaySession(self.registry, self.mgr, self.shell,
                                          self.settings)
        self.editors = app.EditorManager(self.registry, self.mgr, self.shell)
        self.editors.set_session(self.session)
        self.session.set_editor_manager(self.editors)
        self.pick = app.PickSession(self.mgr, self.shell)
        self.dialogs = []

    # The two hooks PointEditor and the dimming policy reach for.
    def edit_point_dialog(self, overlay, point_id):
        self.dialogs.append((overlay, point_id))

    sync_point_dimming = None      # replaced below; see PythonView's own

    def add_points(self, name):
        """Through the FLOW, not by hand. A PointOverlay constructed directly
        carries no type id, so the registry-driven half of the app layer —
        first_of_type, and with it the whole "edit the topmost one" invariant —
        would never see it."""
        self.session.new_file_overlay(points_mod.POINTS_TYPE_ID)
        o = self.mgr.first_of_type(points_mod.POINTS_TYPE_ID)
        o.name = name
        return o


def _sync(bench):
    """PythonView.sync_point_dimming, transcribed. The shell method itself
    touches `self.mgr`, `self.editors` and `self.pick` and nothing else, which
    is what makes it testable off the window."""
    edited = bench.editors.edited
    editing = bench.editors.current_mode == points_mod.POINTS_TYPE_ID
    for o in bench.mgr.overlays:
        if not isinstance(o, ovl.PointOverlay):
            continue
        o.set_manager(bench.mgr)
        dim = bool(editing and o is not edited)
        o.dimmed = dim
        if dim:
            o.edit.release_edit_focus()


def test_the_builtin_type_gains_a_palette_without_being_redeclared():
    bench = Bench()
    ids = [d.id for d in bench.registry.all()]
    assert ids.count(points_mod.POINTS_TYPE_ID) == 1
    desc = bench.registry.find(points_mod.POINTS_TYPE_ID)
    assert desc.has_editor
    # And the type is still fvkit's: extension, filters and order untouched.
    assert desc.file.default_extension == "fvpoints"
    assert desc.default_display_order == 950
    assert points_mod.POINTS_TYPE_ID in [d.id for d in bench.registry.with_editors()]


def test_attaching_to_a_type_that_is_not_there_says_so():
    reg = app.OverlayTypeRegistry()
    assert reg.set_editor_factory("fv.nope", lambda: None) is False


def test_the_palette_reads_the_edited_overlay():
    bench = Bench()
    editor = points_mod.PointEditor(bench)
    a = bench.add_points("A")
    b = bench.add_points("B")

    bench.editors.set_mode(points_mod.POINTS_TYPE_ID)
    assert bench.editors.edited is b        # the topmost of its type

    labels = [n.label for n in editor.tools()]
    assert "Add point (a)" in labels
    # Nothing selected, so the per-point verbs are off.
    by_label = {n.label: n for n in editor.tools()}
    assert by_label["Edit point..."].enabled is False
    assert by_label["Delete point (d)"].enabled is False

    pid = b.edit.add_at(ovl.MapPoint(name="One"), pyfvw.geo.GeoPoint(32.6, -80.1))
    by_label = {n.label: n for n in editor.tools()}
    assert by_label["Edit point..."].enabled is True
    assert by_label["Undo (u)"].enabled is True

    # The "Edit point..." button asks the shell, with the overlay and the row.
    by_label["Edit point..."].invoke()
    assert bench.dialogs == [(b, pid)]

    # Making the other set current moves the palette with it — the editor asks
    # the manager rather than remembering.
    bench.mgr.make_current(a)
    assert bench.editors.edited is a
    assert {n.label: n for n in editor.tools()}["Edit point..."].enabled is False


def test_the_add_toggle_is_the_sessions_armed_mode():
    bench = Bench()
    editor = points_mod.PointEditor(bench)
    o = bench.add_points("A")
    bench.editors.set_mode(points_mod.POINTS_TYPE_ID)

    assert o.edit.adding is False
    {n.label: n for n in editor.tools()}["Add point (a)"].invoke()
    assert o.edit.adding is True
    assert {n.label: n for n in editor.tools()}["Add point (a)"].checked is True


def test_entering_the_mode_with_nothing_open_creates_a_set():
    bench = Bench()
    assert bench.mgr.first_of_type(points_mod.POINTS_TYPE_ID) is None
    bench.editors.set_mode(points_mod.POINTS_TYPE_ID)
    # auto_enter_on_create: a mode with nothing to edit is not a state the user
    # asked for, so the flow made one.
    assert bench.mgr.first_of_type(points_mod.POINTS_TYPE_ID) is not None
    assert bench.editors.edited is not None


def test_only_the_unedited_sets_dim_and_only_while_editing():
    bench = Bench()
    a = bench.add_points("A")
    b = bench.add_points("B")
    c = bench.add_points("C")

    # Creating a set auto-enters the editor, so the mode is already on.
    assert bench.editors.current_mode == points_mod.POINTS_TYPE_ID
    _sync(bench)
    assert bench.editors.edited is c
    assert [o.dimmed for o in (a, b, c)] == [True, True, False]
    # A dimmed set has given up edit focus, so it declines the press that the
    # edited one is entitled to.
    assert a.edit.has_edit_focus is False
    assert c.edit.has_edit_focus is True

    # Leaving the mode un-dims everything: outside an edit session the sets are
    # equally live.
    bench.editors.set_mode("")
    _sync(bench)
    assert [o.dimmed for o in (a, b, c)] == [False, False, False]


def test_dimming_does_not_dirty_the_document():
    bench = Bench()
    a = bench.add_points("A")
    b = bench.add_points("B")
    a.dirty = False
    b.dirty = False
    bench.editors.set_mode(points_mod.POINTS_TYPE_ID)
    _sync(bench)
    assert a.dimmed is True
    assert a.dirty is False


def test_raising_a_dimmed_set_hands_it_the_edit():
    """The shell's whole move is `make_current`. Where the stack ends up is
    EditorManager's fifth invariant and is pinned in C++ (app_editor_test);
    what is checked here is that the shell asks for nothing else."""
    bench = Bench()
    a = bench.add_points("A")
    b = bench.add_points("B")
    _sync(bench)
    assert bench.editors.edited is b
    assert a.dimmed is True

    # PythonView.raise_point_overlay, transcribed.
    bench.mgr.make_current(a)
    _sync(bench)

    assert bench.editors.edited is a
    assert a.dimmed is False
    assert b.dimmed is True
    assert a.edit.has_edit_focus is True
    # And it did come forward: the edited set is the topmost of its type.
    assert bench.mgr.first_of_type(points_mod.POINTS_TYPE_ID) is a


# ---------------------------------------------------------------------------
# PE5 — the dialog's colour half, and the add path without a window
# ---------------------------------------------------------------------------

def test_colour_round_trips_through_both_spellings():
    # Opaque loses its alpha, because "#c82828" is what a document holds and
    # what Pippin's palette offers.
    assert points_mod.color_to_hex((200, 40, 40, 255)) == "#c82828"
    assert points_mod.color_to_hex((200, 40, 40, 0)) == "#c8282800"
    assert points_mod.hex_to_color("#c82828") == (200, 40, 40, 255)
    assert points_mod.hex_to_color("c82828") == (200, 40, 40, 255)
    # `aa` 00 is how a document asks for a bare icon with no badge.
    assert points_mod.hex_to_color("#c8282800") == (200, 40, 40, 0)
    # One bad cell keeps the point's own colour rather than failing.
    assert points_mod.hex_to_color("nonsense", (1, 2, 3, 4)) == (1, 2, 3, 4)
    assert points_mod.hex_to_color("#12345", (1, 2, 3, 4)) == (1, 2, 3, 4)
    for _name, hexed in points_mod.SWATCHES:
        assert points_mod.color_to_hex(points_mod.hex_to_color(hexed)) == hexed


def test_every_swatch_and_shape_is_one_the_core_knows():
    o = ovl.PointOverlay("A")
    for shape in points_mod.SHAPES:
        p = ovl.MapPoint(name=shape, shape=shape)
        # An unknown name would read back as "circle"; these all survive.
        assert p.shape == shape
    o.add_point(ovl.MapPoint(name="x"))


def test_add_without_a_window_places_the_prototype():
    """`PythonView.add_point_at` transcribed to the point it branches on. With
    no tk there is no dialog to accept, so the prototype is placed as it
    stands — which is what makes the shell's add path testable at all."""
    bench = Bench()
    o = bench.add_points("A")
    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(800, 600)
    proj.set_center(pyfvw.geo.GeoPoint(32.74, -79.89))
    proj.set_scale(50000.0)
    canvas = pyfvw.canvas.CpuCanvas(800, 600)
    o.on_draw(proj, canvas)

    where = o.edit.resolve_pixel(400, 300)
    assert where.valid and not where.snapped
    proto = ovl.MapPoint(name="Shelter", shape="triangle", category="shelter")
    pid = o.edit.add_at(proto, where.position)

    placed = o.find(pid)
    assert placed.name == "Shelter"
    assert placed.shape == "triangle"
    assert placed.category == "shelter"
    # Within a pixel of the centre: (400, 300) is not the exact centre of an
    # 800x600 surface, and un-projecting a pixel lands on its own centre.
    assert abs(placed.position.lat - 32.74) < 1e-3


# ---------------------------------------------------------------------------
# The symbol set — the shapes, the swatches and the document's own artwork
# ---------------------------------------------------------------------------

def test_shape_ring_matches_the_overlays_own_proportions():
    """`shape_ring` is a transcription of `ShapeRing` in `point_overlay.cpp`,
    so a marker previewed in the dialog is the size it draws on the map."""
    # Circle and cross are not rings in either place.
    assert points_mod.shape_ring("circle", 0, 0, 20) is None
    assert points_mod.shape_ring("cross", 0, 0, 20) is None

    assert points_mod.shape_ring("square", 0, 0, 20) == [-10, -10, 10, -10,
                                                        10, 10, -10, 10]
    assert points_mod.shape_ring("diamond", 0, 0, 20) == [0, -10, 10, 0,
                                                         0, 10, -10, 0]
    tri = points_mod.shape_ring("triangle", 0, 0, 20)
    assert tri[0:2] == [0, -10]
    assert abs(tri[2] - 8.66) < 1e-9 and tri[3] == 5.0

    star = points_mod.shape_ring("star", 0, 0, 20)
    assert len(star) == 20
    radii = [round((star[i] ** 2 + star[i + 1] ** 2) ** 0.5, 4)
             for i in range(0, 20, 2)]
    # Alternating outer and inner, the inner 0.42 of the outer.
    assert radii[0::2] == [10.0] * 5
    assert radii[1::2] == [4.2] * 5

    # Every shape the core knows is one this can draw.
    for shape in points_mod.SHAPES:
        points_mod.shape_ring(shape, 5, 5, 12)


def test_swatch_name_names_the_palette_and_passes_a_stray_through():
    assert points_mod.swatch_name("#c82828") == "Red"
    assert points_mod.swatch_name("#C82828") == "Red"
    # A document may hold any colour, and one off the palette is still legal.
    assert points_mod.swatch_name("#123456") == "#123456"


def test_the_info_face_opens_what_a_document_wrote():
    """An author writes "kiawah.com" as often as "https://kiawah.com", and
    only one of those opens; the same for a number with brackets in it."""
    assert points_mod.dial_string("(843) 555-0100") == "8435550100"
    assert points_mod.dial_string("+44 20 7946 0000") == "+442079460000"
    assert points_mod.dial_string("call the office") == ""
    assert points_mod.web_url("kiawah.com") == "https://kiawah.com"
    assert points_mod.web_url("http://kiawah.com") == "http://kiawah.com"
    assert points_mod.web_url("  ") == ""


def _sample_symbols():
    d = os.path.join(os.environ.get("FVW_TESTDATA_DIR", "TestData"),
                     "GeoSymbol", "makiPng")
    if not os.path.isdir(d):
        d = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
            os.path.dirname(os.path.abspath(__file__))))),
            "testdata", "GeoSymbol", "makiPng")
    return list(ovl.PointOverlay.sample_symbols(d)) if os.path.isdir(d) else []


def test_symbol_images_name_a_row_and_refuse_a_missing_one():
    """The name half of `SymbolImages` needs no window, which is the half the
    info face reads: a point wearing an icon says which one."""
    symbols = _sample_symbols()
    if not symbols:
        pytest.skip("no icon directory in this checkout")
    images = points_mod.SymbolImages(symbols)
    assert images.name_of(symbols[0].id) == symbols[0].name
    # An id from a document whose palette was edited out from under it.
    assert images.name_of(9999) == ""
    assert images.name_of(0) == ""


# ---------------------------------------------------------------------------
# The dialog's two faces. These DO open a window, so they are skipped where
# there is no display -- which is the only reason they are not with the rest.
# ---------------------------------------------------------------------------

@pytest.fixture
def tk_root():
    tk = pytest.importorskip("tkinter")
    try:
        root = tk.Tk()
    except tk.TclError:
        pytest.skip("no display")
    root.withdraw()
    yield root
    root.destroy()


def _a_point():
    p = ovl.MapPoint(name="Fort Sumter", shape="star")
    p.position = pyfvw.geo.GeoPoint(32.7522, -79.8747)
    p.category = "fort"
    p.phone = "(843) 555-0100"
    p.url = "nps.gov/fosu"
    p.remarks = "Boat access only."
    p.elevation_ft = 12.0
    return p


def test_both_faces_build_and_the_edit_button_swaps_between_them(tk_root):
    symbols = _sample_symbols()
    dlg = points_mod.PointDialog(tk_root, _a_point(), symbols, mode="info")
    dlg.win = None
    # run() would block on wait_window; the window and the faces are what is
    # under test, so they are built the way run() builds them.
    import tkinter as tk
    dlg.win = tk.Toplevel(tk_root)
    dlg.body = tk.Frame(dlg.win)
    dlg.body.pack()
    dlg._make_vars()

    dlg._build("info")
    assert dlg.mode == "info"

    dlg._build("edit")
    # Six shapes, eight swatches, and one icon cell per document row plus the
    # "no icon" cell -- every choice Pippin offers, previewed as a marker.
    expected = len(points_mod.SHAPES) + len(points_mod.SWATCHES)
    if symbols:
        expected += len(symbols) + 1
    assert len(dlg._previews) == expected

    # Cancel from an edit that began on the info face goes BACK to it.
    dlg._cancel()
    assert dlg.mode == "info"
    dlg.win.destroy()


def test_the_form_writes_every_field_back(tk_root):
    import tkinter as tk

    symbols = _sample_symbols()
    p = _a_point()
    dlg = points_mod.PointDialog(tk_root, p, symbols, mode="edit")
    dlg.win = tk.Toplevel(tk_root)
    dlg.body = tk.Frame(dlg.win)
    dlg.body.pack()
    dlg._make_vars()
    dlg._build("edit")

    v = dlg._vars
    v["name"].set("Fort Moultrie")
    v["phone"].set("843 555 0111")
    v["url"].set("nps.gov/fosu/moultrie")
    v["remarks"].set("Drive-up.")
    v["category"].set("fort")
    v["shape"].set("diamond")
    v["color"].set("#1e8c46")
    v["size"].set("18")
    v["elevation"].set("22")
    if symbols:
        v["symbol_id"].set(symbols[0].id)
    dlg._repaint()
    dlg._apply()

    assert p.name == "Fort Moultrie"
    assert p.phone == "843 555 0111"
    assert p.url == "nps.gov/fosu/moultrie"
    assert p.remarks == "Drive-up."
    assert p.shape == "diamond"
    assert points_mod.color_to_hex(p.color) == "#1e8c46"
    assert p.size_px == 18.0
    assert p.elevation_ft == 22.0
    if symbols:
        assert p.symbol_id == symbols[0].id

    # A number that will not parse keeps what the point had: the field is the
    # only thing wrong, and failing the dialog over it would lose the rest.
    v["size"].set("wide")
    v["elevation"].set("high")
    dlg._apply()
    assert p.size_px == 18.0 and p.elevation_ft == 22.0
    dlg.win.destroy()


def test_a_document_with_no_artwork_gets_no_icon_row(tk_root):
    """Pippin's rule, and the honest one: a `.fvpoints` file that embeds no
    artwork has no icons to offer."""
    import tkinter as tk

    dlg = points_mod.PointDialog(tk_root, _a_point(), (), mode="edit")
    dlg.win = tk.Toplevel(tk_root)
    dlg.body = tk.Frame(dlg.win)
    dlg.body.pack()
    dlg._make_vars()
    dlg._build("edit")
    assert not any(kind == "icon" for kind, _v, _c, _f in dlg._previews)
    dlg.win.destroy()


# ---------------------------------------------------------------------------
# The symbol library — the icon set a document can import from
# ---------------------------------------------------------------------------

def _icon_dir():
    """The maki directory, which is the set the sample and Pippin documents
    were authored from and the shell's default icon library."""
    for root in (os.environ.get("FVW_TESTDATA_DIR", ""),
                 os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
                     os.path.dirname(os.path.abspath(__file__))))),
                     "testdata")):
        d = os.path.join(root, "GeoSymbol", "makiPng")
        if root and os.path.isdir(d):
            return d
    return ""


@pytest.fixture
def library():
    path = _icon_dir()
    if not path:
        pytest.skip("no icon directory in this checkout")
    lib = pyfvw.symbol.PngSymbolLibrary()
    lib.open_directory(path)
    return points_mod.SymbolLibrary(lib)


def test_a_sprite_becomes_the_row_a_document_would_carry(library):
    """The preview and the import are the same conversion — the chooser shows
    what `add_symbol_from_library` will write, not a second reading of the
    icon set."""
    ids = library.ids
    assert len(ids) > 100
    sym = library.symbol(ids[0])
    assert sym is not None
    assert sym.name == ids[0]
    assert sym.image[:8] == b"\x89PNG\r\n\x1a\n"
    # Cached: a chooser asks for the same cell on every repaint.
    assert library.symbol(ids[0]).image == sym.image
    # An icon the library has never heard of is None, not a blank row.
    assert library.symbol("no_such_sprite") is None


def test_the_document_and_the_library_agree_about_a_sprite(library):
    """What the dialog previews and what the document ends up holding are the
    same bytes, because they are made by the same call."""
    o = ovl.PointOverlay("A")
    sprite = library.symbol(library.ids[0])
    row_id = o.add_symbol_from_library(library.library, sprite.name)
    row = next(s for s in o.symbols if s.id == row_id)
    assert row.name == sprite.name
    assert row.image == sprite.image
    # Deduped by name, so a shell may import on every save without asking.
    assert o.add_symbol_from_library(library.library, sprite.name) == row_id
    assert len(o.symbols) == 1


def test_a_chosen_sprite_is_written_only_on_save(tk_root, library):
    """Nothing is imported when the dialog is backed out of: the pick queues
    a placeholder and Save is what asks the document for a real row."""
    import tkinter as tk

    imported = []
    dlg = points_mod.PointDialog(
        tk_root, _a_point(), (), mode="edit", library=library,
        on_import=lambda name: (imported.append(name), 77)[1])
    dlg.win = tk.Toplevel(tk_root)
    dlg.body = tk.Frame(dlg.win)
    dlg.body.pack()
    dlg._make_vars()
    dlg._build("edit")

    # A document with no artwork of its own still gets the row, because the
    # library can supply some.
    assert any(kind == "add" for kind, _v, _c, _f in dlg._previews)

    sprite = library.symbol(library.ids[0])
    placeholder = dlg.take_sprite(sprite)
    assert placeholder < 0, "not the document's id until it is in the document"
    assert dlg.pending[placeholder].name == sprite.name
    assert imported == [], "the pick imported nothing"
    # It previews from the pending row, so the user sees what they chose.
    assert dlg.images.get(placeholder, 18) is not None

    dlg._apply()
    assert imported == [sprite.name]
    assert dlg.point.symbol_id == 77
    dlg.win.destroy()


def test_a_sprite_the_document_already_has_selects_that_row(tk_root, library):
    import tkinter as tk

    o = ovl.PointOverlay("A")
    sprite = library.symbol(library.ids[0])
    row_id = o.add_symbol_from_library(library.library, sprite.name)

    dlg = points_mod.PointDialog(tk_root, _a_point(), o.symbols, mode="edit",
                                 library=library, on_import=lambda n: 0)
    dlg.win = tk.Toplevel(tk_root)
    dlg.body = tk.Frame(dlg.win)
    dlg.body.pack()
    dlg._make_vars()
    dlg._build("edit")

    assert dlg.take_sprite(library.symbol(sprite.name)) == row_id
    assert dlg.pending == {}, "nothing queued for a row the document has"
    # And choosing it twice is still one cell.
    other = library.symbol(library.ids[1])
    first = dlg.take_sprite(other)
    assert dlg.take_sprite(library.symbol(other.name)) == first
    assert len(dlg.pending) == 1
    dlg.win.destroy()


def test_with_no_shell_to_import_through_a_pending_sprite_is_dropped(tk_root,
                                                                    library):
    """A script or a test has no document to import into, so the point goes
    back to wearing no icon rather than a row nothing can resolve."""
    import tkinter as tk

    dlg = points_mod.PointDialog(tk_root, _a_point(), (), mode="edit",
                                 library=library)
    dlg.win = tk.Toplevel(tk_root)
    dlg.body = tk.Frame(dlg.win)
    dlg.body.pack()
    dlg._make_vars()
    dlg._build("edit")
    dlg.take_sprite(library.symbol(library.ids[0]))
    dlg._apply()
    assert dlg.point.symbol_id == 0
    dlg.win.destroy()


def test_the_chooser_filters_the_library_by_name(tk_root, library):
    """The grid is built from the library and the filter is a substring over
    its ids, which are words -- a maki file's stem, a sheet sprite's name."""
    chooser = points_mod.SymbolLibraryChooser(tk_root, library,
                                              points_mod.SymbolImages())
    picked = {}

    def drive():
        grid = chooser.win.winfo_children()
        # The filter entry is the first thing in the window's top frame.
        chooser.win.update_idletasks()
        picked["all"] = _cells(chooser)
        _filter_entry(chooser).delete(0, "end")
        _filter_entry(chooser).insert(0, "harbor")
        chooser.win.update_idletasks()
        picked["some"] = _cells(chooser)
        _filter_entry(chooser).delete(0, "end")
        _filter_entry(chooser).insert(0, "zzz_not_a_sprite")
        chooser.win.update_idletasks()
        picked["none"] = _cells(chooser)
        del grid
        chooser.win.destroy()

    tk_root.after(50, drive)
    assert chooser.run() is None, "closing without a pick returns nothing"
    assert picked["all"] == len(library.ids)
    assert 0 < picked["some"] < picked["all"]
    assert picked["none"] == 0


def _filter_entry(chooser):
    import tkinter as tk

    for frame in chooser.win.winfo_children():
        for child in frame.winfo_children():
            if isinstance(child, tk.Entry):
                return child
    raise AssertionError("no filter entry")


def _cells(chooser):
    """How many sprite cells the grid is showing."""
    import tkinter as tk

    for frame in chooser.win.winfo_children():
        for child in frame.winfo_children():
            if isinstance(child, tk.Canvas):
                grid = child.winfo_children()[0]
                return sum(1 for c in grid.winfo_children()
                           if isinstance(c, tk.Frame))
    raise AssertionError("no grid")


def test_the_documents_own_icons_are_names_in_the_library(library):
    """The library must be the set the documents were AUTHORED from, or the
    chooser offers a second drawing style and a file ends up mixing two.

    Pippin's own `kiawah.fvpoints` is the case to check: every row in it is a
    maki stem, so choosing one the document already carries selects that row
    rather than importing a different picture under the same idea."""
    doc = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(
        __file__))), "Pippin", "Data", "kiawah.fvpoints")
    if not os.path.isfile(doc):
        pytest.skip("no Pippin data file in this checkout")
    o = ovl.PointOverlay("kiawah")
    o.file_open(doc)
    assert o.symbols, "the document carries artwork"
    known = set(library.ids)
    unknown = sorted(s.name for s in o.symbols if s.name not in known)
    assert unknown == [], "the library does not carry %r" % (unknown,)

    # And re-importing one is the row the document already has, not a second.
    before = len(o.symbols)
    name = o.symbols[0].name
    assert o.add_symbol_from_library(library.library, name) == o.symbols[0].id
    assert len(o.symbols) == before
