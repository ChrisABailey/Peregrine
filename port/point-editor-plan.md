# The point editor — plan (PE1–PE7)

An editing mode for `fv.points`, the SQLite point overlay
(`fvkit/overlay/point_overlay.h`), asked for by Chris on 2026-09-04 for
PythonView. Read `port/PORTING.md` §1a-bis and `port/apps/route.py`'s docstring
first; this is the same shape as the route editor and deliberately so.

## 0. What is being asked for

1. A toolbar button turns the point editor **on**, and turning it on turns any
   other editor off.
2. Entering the mode edits the **topmost** `.fvpoints` overlay, or creates one
   when none is open.
3. With two or more point overlays open, the edited one draws normally and the
   rest draw **dimmed**. Clicking a dimmed one makes it current, which moves it
   to the top and un-dims it.
4. Clicking the map **adds** a point, through a dialog that chooses the symbol
   and fills in the rest of the row — the fields Pippin's `PointEditSheet`
   offers.
5. An existing point **drags**: the drag starts when the press hit a point and
   the cursor has left that marker's own bounds. The drop **snaps** through
   `fv::app::SnapCandidates`.

### What is already there, and must not be rebuilt

`EditorManager` (`fvkit/app/editor.h`) already delivers 1 and 2 whole: its
invariant 1 is "SetMode makes the current overlay match the mode — topmost of
that type, or created through FileNew when the editor auto-enters", and its
invariant 2 is "making a different overlay current makes the mode match the
overlay", which is 3's second half. `OverlayManager::make_current` moves an
overlay within its band, and `PickSession::resolve_click` already tells the
shell which overlay was clicked. `PointOverlay` already implements `SnapTo`,
`HitTest` and `EditTarget`, and `route.py` already proves a palette is a list
of `MenuNode` that `toolbar.py` renders for free.

So the new code is: a **`PointEditSession`** (the gestures), a **dimmed** draw
state on `PointOverlay`, the **bindings** for both, a **palette** module, and
the **dialog**.

### Where the code goes, and why C++ rather than Python

The same reason the route editor moved (`RouteKit/fv_route_edit.h`): Pippin
draws `.fvpoints` and edits it through its own sheet, so a second answer to
"what does dragging a point mean" is the thing the port cannot afford. The
gestures, the undo stack and the snap go in `fvkit/`. The dialog and the
palette are shell furniture and stay in Python.

---

## PE1 — `PointEditSession` (C++, fvkit) — BUILT

`fvkit/overlay/point_edit.{h,cpp}`, transcribed from `RouteKit/fv_route_edit`
rather than redesigned. `PointOverlay` owns one (`edit()`) and forwards the
Overlay input SPI to it, exactly as `RouteOverlay` does.

State: `selected` (already on the overlay), `adding`, `pick_tolerance_px`,
`snap_tolerance_px`, a drag, and an undo stack of whole `std::vector<MapPoint>`
lists — the route editor's history shape, for the route editor's reason.

    EditPosition ResolvePixel(PixelPoint) const;   // SnapCandidates, then unproject
    int64_t AddAt(const MapPoint& prototype, const GeoPoint&);
    bool Delete(int64_t id);
    bool MoveTo(int64_t id, const GeoPoint&);
    bool BeginDrag(int64_t id, PixelPoint at);     // takes the mouse capture
    bool DragTo(PixelPoint), EndDrag(PixelPoint), CancelDrag();
    bool OnMouseDown/Move/Up(const MouseEvent&), OnKeyDown(const KeyEvent&);

`EditPosition` is `fv::EditPosition` re-used, not a second copy of it; it moves
out of `RouteKit/fv_route_edit.h` into a header both can include.

**The drag threshold is the marker, not a pixel count**, which is the one thing
here the route editor does not already say. A press inside a point's drawn
bounds selects it and arms a *pending* drag; the drag begins on the first move
that leaves those bounds (`size_px * dpi_scale / 2`, the same half-width
`HitTestPoint` adds to its tolerance). A press-move-release that never left the
marker is a click, so selecting a point never nudges it.

**Adding is two gestures**, route.py's rule unchanged: the click supplies a
position and the shell supplies the row. `AddAt` takes a prototype `MapPoint`
so the dialog's answer (symbol, colour, name, phone, …) arrives as one whole
row rather than as a set of field setters — the same bargain `UpdatePoint`
already struck.

Tests: `fvkit/test/point_edit_test.cpp` — select/add/delete/undo, the drag
threshold in both directions, a snapped drop, and edit focus cancelling a drag
in flight.

## PE2 — the dimmed state (C++, fvkit) — BUILT

`PointOverlay::SetDimmed(bool)` / `dimmed()`. Dimmed, every ink the overlay
lays down is alpha-scaled by one factor (`kDimmedAlpha`, 0.35): the badge, its
black edge, the label, and the embedded icon.

The icon is the only one that is not a colour. `ICanvas::DrawPixmap` carries no
alpha, so the dimmed tile is a **second cached decode** with its alpha scaled,
keyed alongside the normal one in `EmbeddedSymbolLibrary` — the same shape as
the per-colour `libraries_` cache the overlay already keeps, and contained
entirely in `point_overlay.cpp`. No new canvas call, no new `RenderState`, no
golden outside this overlay moves.

Selection still draws while dimmed: a dimmed overlay is not an inert one, it is
one that is not being edited.

Tests: extend `fvkit/test/point_overlay_test.cpp` — a dimmed draw emits the
same stamp count as a normal one and lands less ink.

## PE3 — the bindings — BUILT

`bindings/pyfvw/pyfvw_module.cpp`: `PointOverlay.update_point` (bound at last —
`UpdatePoint` has existed in C++ since schema 3 and nothing could call it),
`dimmed`, and `edit` returning the session. `PointEditSession` gets the class
binding `RouteEditSession` has, including `EditPosition`.

Tests: `bindings/pyfvw/test/test_pyfvw_points.py`.

## PE4 — the palette and the shell policy (PythonView) — BUILT

`port/apps/points.py`, the sibling of `route.py`: a `PointEditor` holding no
state, whose `tools()` reads the edited overlay's session, and a
`point_type_desc(factory, editor_factory)`.

Neither of the plan's two options was taken. `OverlayTypeRegistry::
SetEditorFactory` attaches a palette to a type that is ALREADY registered,
which is smaller than both and leaves one definition of what a point set is:
fvkit says the extension, the filters and the display order, the desktop says
what editing one looks like.

The shell's two policies, and they are the shell's because dimming is a
statement about a window and not about a document:

* After any change to the mode or the stack, walk `mgr` and set `dimmed` on
  every `fv.points` overlay that is not the one being edited. Off entirely when
  the point editor is not active.
* `_on_release`'s existing pick already selects into a `PointOverlay`. When the
  point editor is active and the hit overlay is not the edited one, call
  `mgr.make_current` on it instead — `EditorManager`'s invariants 2 and 5 do
  the rest, and the dim walk above follows.

**Invariant 5, which this plan first got wrong** (Chris, 2026-09-04). The
overlay being edited goes on top of EVERYTHING, not to the top of its own band:
a route being edited draws over a point set and a point set being edited draws
over the route, because "on top" is a statement about what the user is working
on and not about which type outranks which. The top-most band (the crosshair)
is the exception and stays above the lot. Leaving the mode puts the stack back
in its default order.

It is therefore `EditorManager`'s and not this shell's — it is true of every
editor and every shell, Pippin included — so it lives in `TakeFocus` and
`ExitMode` over two new `OverlayManager` calls, `MoveToTopOfWorking` and
`RestoreDefaultOrder`. The restore is a STABLE sort, so overlays sharing a
display order keep the arrangement they have and the one most recently edited
stays the upper of its peers.

## PE5 — the point dialog (PythonView) — BUILT

A modal `Toplevel`, the desktop's answer to `PointEditSheet.swift`, opened by a
click in add mode and by "Edit point..." on the palette. The same fields, in
Pippin's order: name; phone and url; remarks; shape, colour and icon; category
and elevation. Location is the click, not a row — the desktop has a cursor.

The icon chooser lists the document's own `symbols` palette and is hidden when
the document has none, which is Pippin's rule. Cancelling an add adds nothing.

---

## Order, and what a session is

PE1–PE5 all landed 2026-09-04; PE6 and PE7 followed on 2026-09-07.

One thing the plan did not anticipate: the SHELL takes the armed-add click, in
`_on_press`, before the stack routes it. The overlay's own handler would have
placed a default point and spent the armed mode before the shell saw the click,
and a point is not placed until its sheet is accepted. `resolve_pixel` and
`add_at` are the same functions that handler calls, entered one level up —
which is what the session exposes them for.

Three hazards on the Python surface were found by these tests and closed in
passing, all of them the same shape — the bindings handed out live views of the
document. `find`, `points` and `symbols` now return COPIES (a row goes back
through `update_point`), and `EditorManager.edited` returns the STACK's
shared_ptr rather than a wrapper minted around the raw pointer, so
`editors.edited is my_overlay` is true for the overlay actually being edited.

---

## PE6 — the symbol set and the info face (PythonView) — BUILT

Asked for by Chris on 2026-09-07: PythonView must fully edit a point file
Pippin wrote and the other way round, with the same symbol set and the
document's own icons; and a right-click on a marker must show everything the
document holds about it, with an Edit button on that view.

**The symbol pickers are the phone's, not three combo boxes.** PE5 chose a
shape from a list of the words "circle" and "diamond", a colour by name, and
an icon by NAME — which is the one thing a document's artwork cannot usefully
be reduced to. They are now `PointEditSheet`'s own controls: six shapes drawn
as themselves, eight colour swatches, and one cell per `symbols` row, every
cell previewing the WHOLE marker so a busy icon inside a star is seen here
rather than on the map. A chosen cell is RINGED and not recoloured — a swatch
that changed colour to say "chosen" would misreport what it draws.

The artwork comes out of SQLite: `PointSymbol.image` is PNG bytes, which Tk
reads straight into a `PhotoImage`, so the picker shows what the map will
stamp. `SymbolImages` decodes each row once, subsampled to the cell (integer
`subsample` is all Tk offers, and a chooser cell can afford it). A blob that
will not decode caches as None and the cell falls back to the bare shape,
which is `EmbeddedSymbolLibrary`'s own rule for a bad row.

`shape_ring` and `draw_marker` transcribe `ShapeRing` and
`kIconFractionOfBadge` from `point_overlay.cpp`, as `PointShapeMark` does on
the phone and for the same reason: sharing them would mean rasterising
through `CpuCanvas` for every cell.

**One window, two faces.** `PointDialog` gained a `mode`. INFO is the
right-click sheet — the marker, the name and category, whichever of phone,
website, remarks, position, symbol, elevation and id the point actually has,
an empty field being ABSENT rather than blank; the phone and website rows open
through `webbrowser`. EDIT is PE5's form with the new pickers. Edit swaps the
face in place, and Cancel from an edit that began on the info face goes back
to it rather than closing, which is what the phone's sheet does. The form's
variables outlive the swap.

**The right-click section is the SHELL's**, appended above whatever the
overlays composed, because the core opens no dialogs (rule R1) and
`PointOverlay::AppendMenuItems` can only offer to select. One row when a
single marker is under the cursor and a submenu when several are; opening the
sheet selects the point, because the dialog is about that point.

Nothing moved to C++. The pickers are furniture and the info sheet is a
window, and Pippin already has both.

Tests: `port/apps/test/test_points_ui.py` — the ring proportions against
`ShapeRing`'s numbers, the swatch names, the phone and URL the info face
opens, and the two faces themselves (skipped where there is no display).
The selftest's point step builds the right-click row over a marker and
asserts there is none over empty sky.

## PE7 — the symbol library (fvkit + PythonView) — BUILT

Asked for by Chris on 2026-09-07: any icon in the port's own icon set must be
usable on a point, and one the document does not already carry must be added
to its SQLite palette.

**WHICH icon set, corrected the same day.** The first build browsed the
OSM-Liberty sprite sheet. It is the wrong set: the artwork already in a
`.fvpoints` document is the **maki** set (`testdata/GeoSymbol/makiPng`), which
`points.symbol_dir` has always named and which `WriteSampleFile` embeds from.
Every one of `kiawah.fvpoints`'s 43 rows is a maki file stem. Offering a
different drawing style would let one document mix two, so the library is
`points.symbol_dir` and the sheet key invented for it is gone. Nothing else
moved: `PngSymbolLibrary` reads a directory and a sheet through the same
interface, so the shell tells them apart by the path and the layers below it
never learn which is which.

**One new call, because `AddSymbol` already did the hard half.** It dedups by
name and returns the row a document already has, which is exactly "add it if
it is not already stored". What was missing was only the conversion from a
sprite to a row, so `PointOverlay::AddSymbolFromLibrary(ISymbolLibrary&, id,
name, out_id)` sits beside `AddSymbolFromPngFile` and takes the library's
tile, its pixel ratio and any off-centre pivot. A pivot equal to the tile
centre is left UNSET, because unset already means the centre.

It is C++ and not Python for this plan's standing reason: Pippin edits the
same documents and would otherwise need a second answer to "what does
importing an icon mean". `PngSymbolLibrary` already reads both forms an icon
set ships in — a sprite sheet (`OpenSheet`) and a directory of loose PNGs
(`OpenDirectory`) — so nothing here knows what a sheet is.

`EncodePng` (`fvkit/tools/png_write.h`) is the one supporting piece: the blob
form of `WritePng`, over `png_set_write_fn`. A caller that STORES a PNG should
not have to write a temporary file to read it back.

**The preview and the import are the same call.** `points.py`'s
`SymbolLibrary` holds a SCRATCH `PointOverlay` and imports into that, so the
picture the chooser shows is made by the code that will write the document
rather than by a second reading of the icon set. The maki directory's 215
icons convert in about as long as it takes to decode them into tk images,
paid once when the chooser opens.

**Nothing is written until Save.** "+" on the icon row opens
`SymbolLibraryChooser` — a filterable grid, because what is being chosen is a
picture and two hundred of them need a needle. A pick queues the icon under a
NEGATIVE placeholder id and previews from it; `_apply` calls the shell's
`on_import`, which is the document's own `add_symbol_from_library`. So a
dialog the user backs out of leaves the palette exactly as it found it, and an
icon the document already carries selects that row instead of queueing a
second copy.

`points.symbol_dir` is now BOTH the set `File > Sample points` embeds from and
the set "+" browses, which is the point: they have to be one set or a document
ends up holding two drawing styles. A path that is not there costs the sample
its icons and the sheet its "+" button, and nothing else.

Tests: `fvkit/test/point_overlay_test.cpp` (the import, the dedup, the
decoded tile, an id the library has never heard of) and seven more in
`test_points_ui.py` — the conversion, that the document and the chooser agree
byte for byte, the deferred write, the already-carried row, the no-shell
fallback, the chooser's filter, and the correction itself: every row in
Pippin's `kiawah.fvpoints` is a name the library carries. The selftest opens
the configured set and imports through it.
