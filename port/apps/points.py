# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""The point overlay's tool palette, as PythonView renders it.

The sibling of `route.py`, and for the same reason: the document, the drawing,
the pick, the snap and the EDITING all live in C++ (`fvkit/overlay/point_*`,
reached through `pyfvw.overlay`), because Pippin draws the same `.fvpoints`
file and edits it through its own sheet. What is left here is shell furniture —
a palette is a statement about a toolbar, and a phone would render none of it.

The type itself is a BUILT-IN (`pyfvw.app.register_builtin_types`), so this
module does not re-declare it. It attaches a palette to the one already
registered, which is what `registry.set_editor_factory` exists for: fvkit says
what a point set is and where it sits in the stack, the desktop says what
editing one looks like.

Editing, from the user's side (the keys are `PointEditSession`'s):
    click       select the point under the cursor
    drag        move it, once the cursor has left the marker it grabbed -- so
                selecting never nudges. The drop SNAPS to any exact position
                under it: a point in another `.fvpoints` set, or a route's
                waypoint
    a           arm "add a point"; the next click on the map places one
    d / Delete  delete the selected point
    u           undo (ctrl-Z in the app; shift for redo)
    Esc         cancel a drag, disarm add, deselect

A right-click on a marker opens the same dialog on its INFO face, which needs
no editor and no mode: reading what a point says is not editing it.

The icons the sheet offers are the DOCUMENT's own, plus whatever the shell's
configured icon set can supply: "+" on the icon row browses it, and an icon
chosen there is imported into the document on Save.
"""

import math

import pyfvw

app = pyfvw.app
overlay = pyfvw.overlay

#: The registered type id and the document extension, taken from the C++
#: constants rather than restated.
POINTS_TYPE_ID = app.POINTS_TYPE_ID
POINTS_EXTENSION = overlay.PointOverlay.EXTENSION

PointOverlay = overlay.PointOverlay


class PointEditor:
    """The point TOOL. One instance however many point sets are open, which is
    what the app layer keeps editors per type for: "I am placing points" is a
    statement about the user, not about a document.

    Duck-typed, like `RouteEditor`: activate() and deactivate(), plus whichever
    of tools(), default_cursor(), ui_constraints() and auto_enter_on_create()
    it cares to define. It holds no editing state — the gestures, the armed
    mode and the undo stack are the overlay's `edit` session, which a phone
    shares while sharing none of this.
    """

    def __init__(self, app_window=None):
        # The shell, so a palette button can do what a key press does, and so
        # "Edit point..." can open a dialog. None in a test.
        self.app = app_window
        self.active = False

    # --- the bracket the mode dance calls ---------------------------------

    def activate(self):
        self.active = True
        self._sync_dimming()

    def deactivate(self):
        self.active = False
        self._sync_dimming()

    def default_cursor(self):
        return app.CursorId.CROSSHAIR

    def ui_constraints(self):
        return app.EditorUiConstraints()

    def auto_enter_on_create(self):
        # Creating a point set means you are about to put points in it.
        return True

    # --- the palette -------------------------------------------------------

    def tools(self):
        o = self._points()
        edit = o.edit if o is not None else None
        selected = bool(o is not None and o.selected)
        return [
            app.MenuNode(label="Add point (a)",
                         checked=bool(edit is not None and edit.adding),
                         action=self._toggle_add),
            app.MenuNode(label="Edit point...", enabled=selected,
                         action=self._edit_selected),
            app.MenuNode(label="Delete point (d)", enabled=selected,
                         action=self._delete),
            app.MenuNode(label=""),                       # separator
            app.MenuNode(label="Labels",
                         checked=bool(o is not None and o.show_labels),
                         action=self._toggle_labels),
            app.MenuNode(label=""),
            app.MenuNode(label="Undo (u)",
                         enabled=bool(edit is not None and edit.can_undo()),
                         action=lambda: edit is not None and edit.undo()),
        ]

    # --- what the tools act on --------------------------------------------

    def _points(self):
        """The point set being edited. The EditorManager knows it — asking IT
        rather than remembering is what keeps the tools correct when the user
        makes a different set current."""
        if self.app is None or self.app.editors is None:
            return None
        edited = self.app.editors.edited
        return edited if isinstance(edited, PointOverlay) else None

    def _toggle_add(self):
        o = self._points()
        if o is not None:
            o.edit.adding = not o.edit.adding

    def _delete(self):
        o = self._points()
        if o is not None and o.selected:
            o.edit.delete(o.selected)

    def _toggle_labels(self):
        o = self._points()
        if o is not None:
            o.show_labels = not o.show_labels

    def _edit_selected(self):
        o = self._points()
        fn = getattr(self.app, "edit_point_dialog", None)
        if o is not None and o.selected and fn is not None:
            fn(o, o.selected)

    def _sync_dimming(self):
        fn = getattr(self.app, "sync_point_dimming", None)
        if fn is not None:
            fn()


def attach_editor(registry, editor_factory):
    """Gives the built-in point type this shell's palette.

    Not a `register` call: the type is fvkit's and re-declaring it here would
    put its extension, its file filters and its display order in two places to
    drift apart. `pyfvw.app.register_builtin_types` registers it WITHOUT an
    editor, which is what a shell with no toolbar (Pippin) wants; the two
    differ by this module's palette and by nothing else."""
    return registry.set_editor_factory(POINTS_TYPE_ID, editor_factory)


# ---------------------------------------------------------------------------
# The symbol set — the same one Pippin offers, drawn the way the map draws it
# ---------------------------------------------------------------------------

#: The fixed colour palette, the same eight Pippin offers. A palette rather
#: than a colour wheel: what matters on a map read at arm's length is that two
#: points are tellable apart. No alpha — `#rrggbbaa` with `aa` 00 is how a
#: document asks for a bare icon with no badge, which is not a choice to put in
#: front of a user.
SWATCHES = [
    ("Red", "#c82828"), ("Amber", "#e1a01e"), ("Green", "#1e8c46"),
    ("Blue", "#285ad2"), ("Plum", "#8246a0"), ("Slate", "#4a5a6e"),
    ("Rust", "#b45a10"), ("Black", "#1a1a1a"),
]

#: The six shapes `PointShape` spells, in its own order.
SHAPES = ["circle", "square", "triangle", "diamond", "cross", "star"]

#: How much of the badge the icon covers, `kIconFractionOfBadge` in
#: `point_overlay.cpp`.
ICON_FRACTION = 0.62


def color_to_hex(color):
    """An (r, g, b, a) tuple as `#rrggbb`, dropping a fully opaque alpha."""
    r, g, b = color[0], color[1], color[2]
    a = color[3] if len(color) > 3 else 255
    text = "#%02x%02x%02x" % (r, g, b)
    return text if a == 255 else text + "%02x" % a


def hex_to_color(text, fallback=(200, 40, 40, 255)):
    """`#rrggbb` or `#rrggbbaa` as an (r, g, b, a) tuple, or `fallback`.

    Both spellings, because `fv::PointColorFromString` accepts both and a
    document may hold either."""
    s = text.strip().lstrip("#")
    if len(s) not in (6, 8):
        return fallback
    try:
        v = [int(s[i:i + 2], 16) for i in range(0, len(s), 2)]
    except ValueError:
        return fallback
    return (v[0], v[1], v[2], v[3] if len(v) == 4 else 255)


def swatch_name(hexed):
    """The palette's name for a colour, or the hex itself for one off it."""
    for name, value in SWATCHES:
        if value.lower() == hexed.lower():
            return name
    return hexed


def shape_ring(shape, cx, cy, size):
    """A shape's outline as a flat [x0, y0, x1, y1, ...] list, or None for the
    two shapes that are not polygons (circle and cross).

    The proportions are `ShapeRing` in `point_overlay.cpp`: triangle and
    diamond share the circle's circumscribed radius, and the star's inner
    radius is 0.42 of its outer, so a marker previewed here is the size it
    will draw on the map."""
    r = size / 2.0
    if shape == "square":
        pts = [(-r, -r), (r, -r), (r, r), (-r, r)]
    elif shape == "triangle":
        pts = [(0, -r), (r * 0.866, r * 0.5), (-r * 0.866, r * 0.5)]
    elif shape == "diamond":
        pts = [(0, -r), (r, 0), (0, r), (-r, 0)]
    elif shape == "star":
        pts = []
        for i in range(10):
            a = -math.pi / 2.0 + i * math.pi / 5.0
            rr = r if i % 2 == 0 else r * 0.42
            pts.append((rr * math.cos(a), rr * math.sin(a)))
    else:
        return None
    return [c for x, y in pts for c in (cx + x, cy + y)]


class SymbolImages:
    """The document's embedded artwork as tk images, decoded once each.

    Tk 8.6 reads PNG bytes straight into a `PhotoImage`, which is the same
    format `EmbeddedSymbolLibrary` decodes, so the picker shows the artwork
    the map will stamp rather than a stand-in. A blob that will not decode is
    cached as None and the marker falls back to its bare shape, which is the
    overlay's own rule for a bad row.

    Scaling is by integer `subsample` because that is all Tk offers; a 32px
    tile in a 17px slot comes back at 16. Good enough for a chooser cell, and
    the map still draws the tile at its full resolution.
    """

    def __init__(self, symbols=()):
        self.symbols = {s.id: s for s in symbols}
        self._cache = {}

    def get(self, symbol_id, target_px):
        """The artwork for a symbol id, subsampled to about `target_px`, or
        None for id 0, an id the document has no row for, and a bad blob."""
        import tkinter as tk

        if not symbol_id or symbol_id not in self.symbols:
            return None
        key = (symbol_id, int(target_px))
        if key in self._cache:
            return self._cache[key]
        sym = self.symbols[symbol_id]
        image = None
        try:
            full = tk.PhotoImage(data=sym.image)
            # `pixel_ratio` is tile pixels per nominal pixel, so artwork drawn
            # at 2x wants twice the subsample to come out the same size.
            step = int(round(full.width() / max(1.0, target_px)))
            image = full.subsample(step, step) if step > 1 else full
            # The subsampled copy does not keep the original alive.
            self._cache[(symbol_id, "full")] = full
        except Exception:
            image = None
        self._cache[key] = image
        return image

    def add(self, symbol):
        """Registers a row that is not (yet) in the document, which is how a
        sprite chosen from a library previews before it is imported."""
        self.symbols[symbol.id] = symbol

    def name_of(self, symbol_id):
        sym = self.symbols.get(symbol_id)
        return sym.name if sym is not None else ""


class SymbolLibrary:
    """An icon set a document can import from, and the sprites it offers as
    the rows a document would carry.

    Turning a sprite into a `.fvpoints` row is
    `PointOverlay.add_symbol_from_library` -- the tile re-encoded as a PNG
    with its pixel ratio and any off-centre pivot. This holds a SCRATCH
    overlay and imports into that, so the picture the chooser shows is made by
    the same call that will write the document rather than by a second reading
    of the sheet. Nothing here touches the document being edited.

    `library` is a `pyfvw.symbol.PngSymbolLibrary` -- a directory of loose
    PNGs through `open_directory`, or a sprite sheet through `open_sheet`.
    """

    def __init__(self, library):
        self.library = library
        self._scratch = PointOverlay("library scratch")
        self._rows = {}

    @property
    def ids(self):
        """Every sprite the library can answer, sorted."""
        return list(self.library.ids)

    def symbol(self, sprite_id):
        """The sprite as a `PointSymbol`, or None for one that will not
        convert. Cached: a chooser asks for the same cell on every repaint."""
        if sprite_id in self._rows:
            return self._rows[sprite_id]
        row = None
        try:
            rid = self._scratch.add_symbol_from_library(self.library,
                                                        sprite_id)
            row = self._scratch.find_symbol(rid)
        except Exception:
            row = None
        # A copy, and not the scratch overlay's live row: the caller keeps it
        # for as long as its dialog lives and may renumber it.
        if row is not None:
            row = overlay.PointSymbol(name=row.name, image=row.image,
                                      pixel_ratio=row.pixel_ratio,
                                      pivot=row.pivot, id=row.id)
        self._rows[sprite_id] = row
        return row


class SymbolLibraryChooser:
    """The library's icons as a filterable grid, one modal window.

    A grid and not a list because what is being chosen is a picture. The
    filter is a plain substring over the library's ids, which are words --
    a maki file's stem ("harbor", "picnic-site") or a sheet sprite's name
    ("harbor_11") -- so typing "har" is how a set of two hundred becomes a
    set of two.

    `run()` returns the chosen `PointSymbol` (the row a document would carry),
    or None.
    """

    CELL_PX = 24
    COLUMNS = 8

    def __init__(self, parent, library, images, title="Add icon"):
        self.parent = parent
        self.library = library
        #: Shared with the dialog, so a sprite decoded here is not decoded
        #: again when it lands in the icon row.
        self.images = images
        self.title = title
        self.result = None

    def run(self):
        import tkinter as tk

        win = tk.Toplevel(self.parent)
        self.win = win
        win.title(self.title)
        win.transient(self.parent)

        v_filter = tk.StringVar()
        top = tk.Frame(win)
        top.pack(fill="x", padx=10, pady=(10, 4))
        tk.Label(top, text="Filter:").pack(side="left")
        entry = tk.Entry(top, textvariable=v_filter, width=24)
        entry.pack(side="left", padx=(4, 0))

        # A canvas and an inner frame is the only scrolling container tk has.
        middle = tk.Frame(win)
        middle.pack(fill="both", expand=True, padx=10)
        canvas = tk.Canvas(middle, width=self.COLUMNS * (self.CELL_PX + 14),
                           height=340, highlightthickness=0,
                           bg=middle.cget("bg"))
        bar = tk.Scrollbar(middle, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=bar.set)
        bar.pack(side="right", fill="y")
        canvas.pack(side="left", fill="both", expand=True)
        grid = tk.Frame(canvas, bg=canvas.cget("bg"))
        window = canvas.create_window(0, 0, window=grid, anchor="nw")

        def fit(_e=None):
            canvas.configure(scrollregion=canvas.bbox("all"))
        grid.bind("<Configure>", fit)
        canvas.bind("<Configure>",
                    lambda e: canvas.itemconfigure(window, width=e.width))
        canvas.bind_all("<MouseWheel>",
                        lambda e: canvas.yview_scroll(-e.delta, "units"))

        status = tk.Label(win, text="", anchor="w", fg="#555555")
        status.pack(fill="x", padx=10)

        def choose(symbol):
            self.result = symbol
            win.destroy()

        def repopulate(*_a):
            for child in grid.winfo_children():
                child.destroy()
            needle = v_filter.get().strip().lower()
            shown = 0
            for sprite_id in self.library.ids:
                if needle and needle not in sprite_id.lower():
                    continue
                sym = self.library.symbol(sprite_id)
                if sym is None:
                    continue
                self.images.add(sym)
                icon = self.images.get(sym.id, self.CELL_PX)
                cell = tk.Frame(grid, bg=grid.cget("bg"))
                cell.grid(row=shown // self.COLUMNS,
                          column=shown % self.COLUMNS, padx=3, pady=3)
                button = tk.Label(cell, image=icon, width=self.CELL_PX,
                                  height=self.CELL_PX, cursor="hand2",
                                  bg=grid.cget("bg"))
                button.pack()
                button.bind("<Button-1>", lambda _e, s=sym: choose(s))
                # The id is the tooltip a desktop has no tooltips for: the
                # status line under the grid says what is under the cursor.
                button.bind("<Enter>",
                            lambda _e, t=sprite_id: status.configure(text=t))
                shown += 1
            if shown == 0:
                tk.Label(grid, text="No icon matches that.",
                         fg="#555555", bg=grid.cget("bg")).grid(row=0,
                                                                column=0)
            fit()

        v_filter.trace_add("write", repopulate)
        repopulate()

        buttons = tk.Frame(win)
        buttons.pack(fill="x", padx=10, pady=10)
        tk.Button(buttons, text="Cancel", command=win.destroy).pack(
            side="right")
        win.bind("<Escape>", lambda _e: win.destroy())
        entry.focus_set()
        win.grab_set()
        self.parent.wait_window(win)
        canvas.unbind_all("<MouseWheel>")
        return self.result


def draw_marker(canvas, cx, cy, size, shape, color, icon=None):
    """Stamps one marker on a tk canvas the way the overlay draws it: the
    shape filled in the point's colour with a black edge, and the embedded
    icon centred on top.

    `color` is `#rrggbb`; `icon` is a `PhotoImage` or None. This duplicates
    the overlay's rendering, as `PointShapeMark` does on the phone, because
    sharing it would mean rasterising through `CpuCanvas` for every cell."""
    r = size / 2.0
    edge = "#000000"
    if shape == "circle":
        canvas.create_oval(cx - r, cy - r, cx + r, cy + r, fill=color,
                           outline=edge, width=1.5)
    elif shape == "cross":
        # Two bars rather than two strokes, so the fill draws it and the arms
        # read at any size.
        w = r * 0.34
        canvas.create_rectangle(cx - w, cy - r, cx + w, cy + r, fill=color,
                                outline=edge, width=1.0)
        canvas.create_rectangle(cx - r, cy - w, cx + r, cy + w, fill=color,
                                outline=edge, width=1.0)
    else:
        ring = shape_ring(shape, cx, cy, size)
        canvas.create_polygon(ring, fill=color, outline=edge, width=1.5)
    if icon is not None:
        canvas.create_image(cx, cy, image=icon)


# ---------------------------------------------------------------------------
# The dialog — the desktop's answer to Pippin's PointInfoSheet and
# PointEditSheet, which are two faces of one window here
# ---------------------------------------------------------------------------


def dial_string(phone):
    """A phone number as a `tel:` URL wants it: digits, and a leading `+` if
    the author wrote one. Empty when there is nothing to dial."""
    text = phone.strip()
    digits = "".join(c for c in text if c.isdigit())
    if not digits:
        return ""
    return ("+" if text.startswith("+") else "") + digits


def web_url(url):
    """A URL as a browser wants it, or "". An author writes "kiawah.com" as
    often as "https://kiawah.com", and only one of those opens."""
    text = url.strip()
    if not text:
        return ""
    if "://" in text:
        return text
    return "https://" + text


class PointDialog:
    """A point's whole row, as a modal window with two faces.

    INFO is what a right-click on a marker opens: everything the document
    holds about that point, laid out to be read, with an Edit button that
    turns the same window into the form. EDIT is Pippin's `PointEditSheet` —
    the same fields in the same order, so a point authored on the desktop and
    one authored on the phone are the same row with the same things filled in.
    Location is absent from the form: the desktop places a point by clicking
    the map, and a coordinate the user already pointed at does not need a
    second widget.

    The symbol pickers are the phone's, not a set of combo boxes: six shapes
    drawn as themselves, eight colour swatches, and the DOCUMENT's own icons
    decoded out of its `symbols` table. An empty table hides the icon row
    rather than showing an empty one, which is Pippin's rule and the honest
    one — a `.fvpoints` file that embeds no artwork has no icons to offer.

    `run()` returns the edited `MapPoint`, or None if nothing was accepted.
    Cancelling an edit that began in info mode goes back to the info face
    rather than closing, which is what the phone's sheet does.
    """

    #: The badge size the chooser cells and the info header draw at. Bigger
    #: than a marker on the map, because a cell is being looked at directly.
    CELL_PX = 30

    def __init__(self, parent, point, symbols=(), title="Point", mode="edit",
                 library=None, on_import=None):
        self.parent = parent
        self.point = point
        self.symbols = list(symbols)
        self.title = title
        self.mode = mode
        #: The face the window opened on, which is what Cancel returns to.
        self.home = mode
        #: The icon set "Add icon..." browses, a `SymbolLibrary` or None. None
        #: hides the button: a shell with no library configured has nothing to
        #: add from.
        self.library = library
        #: `callable(sprite_id) -> symbol_id`, the document's own import. It is
        #: called on Save and not on the pick, so a cancelled dialog leaves the
        #: document exactly as it found it.
        self.on_import = on_import
        self.result = None
        self.images = SymbolImages(self.symbols)
        #: Sprites chosen from the library and not yet in the document, keyed
        #: by the NEGATIVE id they wear until Save gives them a real one.
        self.pending = {}
        self.win = None
        self.body = None
        self._vars = None

    # --- the window --------------------------------------------------------

    def run(self):
        import tkinter as tk

        win = tk.Toplevel(self.parent)
        self.win = win
        win.title(self.title)
        win.transient(self.parent)
        self.body = tk.Frame(win)
        self.body.pack(fill="both", expand=True)
        self._make_vars()
        self._build(self.mode)
        win.bind("<Escape>", lambda _e: self._cancel())
        win.grab_set()
        self.parent.wait_window(win)
        return self.result

    def _build(self, mode):
        """Swaps the window's face, rebuilding it from the variables."""
        self.mode = mode
        for child in self.body.winfo_children():
            child.destroy()
        if mode == "info":
            self._build_info()
        else:
            self._build_edit()

    def _cancel(self):
        if self.mode == "edit" and self.home == "info":
            self._reset_vars()
            self._build("info")
        else:
            self.win.destroy()

    # --- the variables both faces share ------------------------------------

    def _make_vars(self):
        import tkinter as tk

        self._vars = {
            "name": tk.StringVar(), "phone": tk.StringVar(),
            "url": tk.StringVar(), "category": tk.StringVar(),
            "elevation": tk.StringVar(), "shape": tk.StringVar(),
            "color": tk.StringVar(), "size": tk.StringVar(),
            "symbol_id": tk.IntVar(), "remarks": tk.StringVar(),
        }
        self._reset_vars()
        # A hex nobody's palette has is still legal, because a document may
        # hold any colour; typing one repaints the swatches and the badges.
        self._vars["color"].trace_add("write", lambda *_a: self._repaint())

    def _reset_vars(self):
        p = self.point
        v = self._vars
        v["name"].set(p.name)
        v["phone"].set(p.phone)
        v["url"].set(p.url)
        v["category"].set(p.category)
        v["elevation"].set("" if p.elevation_ft == 0.0 else "%g" % p.elevation_ft)
        v["shape"].set(p.shape)
        v["color"].set(color_to_hex(p.color))
        v["size"].set("%g" % p.size_px)
        v["symbol_id"].set(int(p.symbol_id))
        v["remarks"].set(p.remarks)

    def _apply(self):
        """Writes the form back into the point. A number that will not parse
        keeps what the point already had: the field is the only thing wrong,
        and failing the whole dialog over it would lose the rest."""
        p, v = self.point, self._vars
        p.name = v["name"].get().strip()
        p.phone = v["phone"].get().strip()
        p.url = v["url"].get().strip()
        p.category = v["category"].get().strip()
        p.remarks = v["remarks"].get().strip()
        p.shape = v["shape"].get()
        p.color = hex_to_color(v["color"].get(), p.color)
        p.symbol_id = self._resolve_symbol(int(v["symbol_id"].get()))
        try:
            p.size_px = float(v["size"].get())
        except ValueError:
            pass
        try:
            p.elevation_ft = float(v["elevation"].get() or 0.0)
        except ValueError:
            pass

    def _resolve_symbol(self, symbol_id):
        """A chosen icon as an id the document holds.

        A pending sprite is imported HERE, on Save, so nothing is written for
        a dialog the user backs out of. With no shell to import through (a
        script, a test) the point goes back to wearing no icon rather than a
        row nothing can resolve."""
        if symbol_id >= 0:
            return symbol_id
        sprite = self.pending.get(symbol_id)
        if sprite is None or self.on_import is None:
            return 0
        return int(self.on_import(sprite.name)) or 0

    def _add_from_library(self):
        """Browses the library and takes what comes back."""
        if self.library is None:
            return
        chosen = SymbolLibraryChooser(self.win, self.library,
                                      self.images).run()
        if chosen is not None:
            self.take_sprite(chosen)

    def take_sprite(self, sprite):
        """Selects a sprite the chooser returned, queueing its import.

        A sprite the document ALREADY carries selects that row instead of
        queueing a second import of it -- the dedup `add_symbol_from_library`
        would do anyway, done early enough that the icon row shows one cell
        rather than two identical ones. The same for a sprite chosen twice
        before Save.

        Returns the id the icon row now shows, which is the document's for a
        row it has and a negative placeholder for one it does not."""
        existing = next((s for s in self.symbols if s.name == sprite.name),
                        None)
        if existing is not None:
            chosen_id = existing.id
        else:
            chosen_id = next((i for i, s in self.pending.items()
                              if s.name == sprite.name), None)
            if chosen_id is None:
                chosen_id = -(len(self.pending) + 1)
                sprite.id = chosen_id
                self.pending[chosen_id] = sprite
                self.images.add(sprite)
        self._vars["symbol_id"].set(chosen_id)
        # The icon row may have grown a cell, so it is rebuilt not repainted.
        if self.mode == "edit":
            self._build("edit")
        return chosen_id

    # --- the info face -----------------------------------------------------

    def _build_info(self):
        """Everything the document holds, laid out to be read.

        An empty field is ABSENT rather than blank, which is the phone's rule:
        a row of empty labels reads as missing data, and what a point does not
        have is not data. The phone and website rows open through the system's
        own handler, so a number dials and a link opens."""
        import tkinter as tk
        import webbrowser

        p = self.point
        win = self.body
        self.win.title(p.name or "Point")

        header = tk.Frame(win)
        header.pack(fill="x", padx=12, pady=(12, 6))
        badge = tk.Canvas(header, width=36, height=36, highlightthickness=0,
                          bg=header.cget("bg"))
        badge.pack(side="left")
        draw_marker(badge, 18, 18, self.CELL_PX, p.shape,
                    color_to_hex(p.color)[:7],
                    self.images.get(p.symbol_id, self.CELL_PX * ICON_FRACTION))
        titles = tk.Frame(header)
        titles.pack(side="left", padx=(10, 0), anchor="w")
        tk.Label(titles, text=p.name or "Unnamed", font=("", 13, "bold"),
                 anchor="w").pack(anchor="w")
        if p.category:
            tk.Label(titles, text=p.category, font=("", 10),
                     fg="#555555", anchor="w").pack(anchor="w")

        rows = tk.Frame(win)
        rows.pack(fill="both", expand=True, padx=12)
        r = [0]

        def row(label, text, command=None):
            if not text:
                return
            i = r[0]
            r[0] += 1
            tk.Label(rows, text=label, anchor="ne", fg="#555555").grid(
                row=i, column=0, sticky="ne", padx=(0, 8), pady=2)
            if command is None:
                tk.Label(rows, text=text, anchor="w", justify="left",
                         wraplength=320).grid(row=i, column=1, sticky="w",
                                              pady=2)
            else:
                link = tk.Label(rows, text=text, anchor="w", fg="#1a5fb4",
                                cursor="hand2", wraplength=320)
                link.grid(row=i, column=1, sticky="w", pady=2)
                link.bind("<Button-1>", lambda _e: command())

        dial = dial_string(p.phone)
        row("Phone", p.phone,
            (lambda: webbrowser.open("tel:" + dial)) if dial else None)
        web = web_url(p.url)
        row("Website", p.url,
            (lambda: webbrowser.open(web)) if web else None)
        row("Remarks", p.remarks)
        row("Position", "%.5f, %.5f" % (p.position.lat, p.position.lon))
        icon = self.images.name_of(p.symbol_id)
        row("Symbol", "%s, %s, %g px%s" % (
            p.shape, swatch_name(color_to_hex(p.color)[:7]), p.size_px,
            (" · " + icon) if icon else ""))
        row("Elevation", "" if p.elevation_ft == 0.0
            else "%g ft" % p.elevation_ft)
        # The row id, last and quiet: it is what a bug report and a `sqlite3`
        # session need and what nobody reads on the way past.
        row("Id", str(p.id) if p.id else "")

        buttons = tk.Frame(win)
        buttons.pack(fill="x", padx=12, pady=12)
        tk.Button(buttons, text="Edit",
                  command=lambda: self._build("edit")).pack(side="right")
        tk.Button(buttons, text="Close",
                  command=self.win.destroy).pack(side="right", padx=(0, 6))

    # --- the edit face -----------------------------------------------------

    def _build_edit(self):
        import tkinter as tk

        v = self._vars
        win = self.body
        self.win.title(self.title)
        #: Everything that previews the marker, repainted whenever a choice
        #: changes: the shape row, the colour row and every icon cell all show
        #: the WHOLE marker, so a busy icon inside a star is seen here rather
        #: than on the map.
        self._previews = []

        row = [0]

        def field(label, var, width=32, **kw):
            r = row[0]
            row[0] += 1
            tk.Label(win, text=label).grid(row=r, column=0, sticky="e",
                                           padx=(10, 4), pady=2)
            e = tk.Entry(win, textvariable=var, width=width, **kw)
            e.grid(row=r, column=1, sticky="w", padx=(0, 10), pady=2)
            return e

        def heading(text):
            r = row[0]
            row[0] += 1
            tk.Label(win, text=text, anchor="w", font=("", 10, "bold")).grid(
                row=r, column=0, columnspan=2, sticky="w", padx=10,
                pady=(10, 2))

        def picker_row(label):
            r = row[0]
            row[0] += 1
            tk.Label(win, text=label).grid(row=r, column=0, sticky="ne",
                                           padx=(10, 4), pady=2)
            frame = tk.Frame(win)
            frame.grid(row=r, column=1, sticky="w", padx=(0, 10), pady=2)
            return frame

        e_name = field("Name:", v["name"])

        heading("Contact")
        field("Phone:", v["phone"])
        field("Website:", v["url"])

        heading("Remarks")
        r = row[0]
        row[0] += 1
        remarks = tk.Text(win, width=32, height=3, wrap="word")
        remarks.insert("1.0", v["remarks"].get())
        remarks.grid(row=r, column=1, sticky="w", padx=(0, 10), pady=2)

        heading("Symbol")
        self._build_shape_row(picker_row("Shape:"))
        self._build_color_row(picker_row("Colour:"))
        # The row is there when the document has artwork OR a library can
        # supply some; only a shell with neither hides it.
        if self.symbols or self.pending or self.library is not None:
            self._build_icon_row(picker_row("Icon:"))
        field("Colour hex:", v["color"], width=12)
        field("Size (px):", v["size"], width=12)

        heading("Attributes")
        field("Category:", v["category"])
        field("Elevation (ft):", v["elevation"], width=12)

        def ok():
            v["remarks"].set(remarks.get("1.0", "end-1c"))
            self._apply()
            self.result = self.point
            self.win.destroy()

        r = row[0]
        buttons = tk.Frame(win)
        buttons.grid(row=r, column=0, columnspan=2, sticky="e", padx=10,
                     pady=10)
        tk.Button(buttons, text="Cancel",
                  command=self._cancel).pack(side="right")
        tk.Button(buttons, text="Save",
                  command=ok).pack(side="right", padx=(0, 6))
        self.win.bind("<Return>", lambda _e: ok())
        e_name.focus_set()
        e_name.selection_range(0, "end")
        self._repaint()

    def _chosen(self):
        """(shape, `#rrggbb`, icon id) as the form currently stands."""
        v = self._vars
        return (v["shape"].get(), color_to_hex(hex_to_color(v["color"].get(),
                                                            self.point.color)),
                int(v["symbol_id"].get()))

    def _repaint(self):
        """Redraws every preview and moves the selection rings.

        A chosen cell is ringed rather than recoloured: a swatch that changed
        colour to say "chosen" would misreport what it looks like on the map.

        Nothing to do on the info face, which the colour variable's own trace
        can reach after a swap back.
        """
        if not getattr(self, "_previews", None) or self.mode != "edit":
            return
        shape, color, symbol_id = self._chosen()
        icon_px = self.CELL_PX * ICON_FRACTION
        for kind, value, canvas, frame in self._previews:
            canvas.delete("all")
            c = self.CELL_PX / 2 + 3
            if kind == "shape":
                draw_marker(canvas, c, c, self.CELL_PX, value, color[:7],
                            self.images.get(symbol_id, icon_px))
                chosen = value == shape
            elif kind == "color":
                canvas.create_oval(c - 13, c - 13, c + 13, c + 13, fill=value,
                                   outline="#666666")
                chosen = value.lower() == color[:7].lower()
            elif kind == "add":
                canvas.create_text(c, c, text="+", font=("", 18),
                                   fill="#1a5fb4")
                chosen = False
            else:
                if value == 0:
                    canvas.create_text(c, c, text="⌀", font=("", 16),
                                       fill="#777777")
                else:
                    draw_marker(canvas, c, c, self.CELL_PX, shape, color[:7],
                                self.images.get(value, icon_px))
                chosen = value == symbol_id
            frame.configure(highlightbackground="#1a5fb4" if chosen else
                            frame.cget("bg"),
                            highlightcolor="#1a5fb4" if chosen else
                            frame.cget("bg"))

    def _cell(self, parent, kind, value, on_click, tooltip=""):
        """One clickable preview: a canvas in a frame whose border is the
        selection ring."""
        import tkinter as tk

        frame = tk.Frame(parent, highlightthickness=2, bd=0)
        frame.pack(side="left", padx=2)
        side = self.CELL_PX + 6
        canvas = tk.Canvas(frame, width=side, height=side, highlightthickness=0,
                           bg=parent.cget("bg"), cursor="hand2")
        canvas.pack()
        canvas.bind("<Button-1>", lambda _e: (on_click(), self._repaint()))
        self._previews.append((kind, value, canvas, frame))
        return frame

    def _build_shape_row(self, parent):
        """The six shapes drawn as themselves, since a list of the words
        "circle" and "diamond" is a worse way to choose a picture."""
        v = self._vars
        for shape in SHAPES:
            self._cell(parent, "shape", shape,
                       lambda s=shape: v["shape"].set(s))

    def _build_color_row(self, parent):
        v = self._vars
        for _name, hexed in SWATCHES:
            self._cell(parent, "color", hexed,
                       lambda h=hexed: v["color"].set(h))

    def _build_icon_row(self, parent):
        """The icons this point can wear, as themselves.

        The first cell is "no icon" and is a cell rather than a checkbox,
        because clearing is the same kind of choice as picking and needs its
        own hit target. Then the document's own rows, then any sprite chosen
        from the library and not yet saved, then "+" -- which browses the
        library. Cells wrap at eight per line rather than scrolling: a desktop
        window can be tall, and a scroller hides half the palette behind a
        gesture."""
        import tkinter as tk

        v = self._vars
        line = [tk.Frame(parent)]
        line[0].pack(anchor="w")
        cells = [("icon", 0)]
        cells += [("icon", s.id)
                  for s in sorted(self.symbols, key=lambda s: s.name)]
        cells += [("icon", i)
                  for i in sorted(self.pending, reverse=True)]
        if self.library is not None:
            cells.append(("add", 0))
        for n, (kind, value) in enumerate(cells):
            if n and n % 8 == 0:
                line[0] = tk.Frame(parent)
                line[0].pack(anchor="w")
            if kind == "add":
                self._cell(line[0], "add", 0, self._add_from_library)
            else:
                self._cell(line[0], "icon", value,
                           lambda i=value: v["symbol_id"].set(i))
