# pyfvw — a short guide to the Python interface

`pyfvw` is the Python binding over **FvKit**, the portable core of the FalconView
port. It gives you the map stack without COM, MFC, or a Windows display: catalog
a directory of map data, render a base map into a buffer, and draw your own
geospatial features on top — from a script, a notebook, or a service.

Everything below was run against `TestData/` before it was written down.

---

## 1. Getting the module

The binding is built as part of the normal port build (it is skipped gracefully
if no Python dev environment is present):

```bash
cmake -B build && cmake --build build -j
```

That produces `build/port/bindings/pyfvw/pyfvw.cpython-*.so`. There is no
`pip install` step — put its directory on `sys.path`:

```python
import os, sys
REPO = "/path/to/FVW"
sys.path.insert(0, os.path.join(REPO, "build", "port", "bindings", "pyfvw"))
os.environ.setdefault("MSPCCS_DATA",
                      os.path.join(REPO, "fvw_core", "PdfLib", "sdk", "lib"))

import numpy as np
import pyfvw
```

Two environment variables matter. **`MSPCCS_DATA`** points at the GEOTRANS
datum tables and is required for anything that shifts datums — GeoTIFF headers
and `geo.parse_location`. **`FVW_GEODATA_DIR`** is only needed for magnetic
variation. `numpy` is optional but is how you get at pixels.

### Conventions worth knowing before the first call

| | |
|---|---|
| **Latitude before longitude**, always, in WGS-84 decimal degrees | `GeoPoint(33.74, -84.38)` |
| Longitude is canonical `(-180, +180]`; ±180 → +180 | `geo.normalize_lon(-180.0) == 180.0` |
| `ll.lon > ur.lon` means the rect **crosses the antimeridian** | `rect.crosses_antimeridian` |
| Errors are **exceptions**, never return codes — `pyfvw.FvError` carries `.code` and `.message` | `except pyfvw.FvError as e: e.code == pyfvw.OUT_OF_COVERAGE` |
| Out-parameters became return values; parameterless getters became properties | `src.info.width`, not `src.GetInfo(&info)` |
| Pixels are interleaved **RGBA8**, top-down, zero-copy to numpy as `(h, w, 4)` | `np.asarray(canvas.buffer)` |
| Decode and I/O release the GIL | threads actually overlap |

---

## 2. The map catalog

The catalog is the port's replacement for the Windows Map Data Manager: a
SQLite database with an R-tree over frame coverage. You point it at directories,
it identifies what is there through the format registry, and afterwards it
answers "what covers this viewport, at this scale?" in well under a millisecond.

```python
pyfvw.catalog.register_builtin_formats()      # idempotent; call before scan
print(pyfvw.catalog.registered_format_keys())
# ['cadrg', 'dted', 'dted-shaded', 'enc', 'geotiff', 'gpkg', 'tiros', 'vpf']

cat = pyfvw.catalog.Catalog("maps.sqlite")    # omit the path for ":memory:"

ds = cat.add_data_source("TestData/rpf", "cadrg")
print(cat.scan(ds), "frames")                 # 524 frames

ds2 = cat.add_data_source("TestData/geotiff", "geotiff")
print(cat.scan(ds2), "frames")                # 32 frames
```

Scanning groups frames into **series** — one row per
**(format, series key, scale, scale units)**, with the scale also normalized to
a 1:N denominator so series of different products can be compared. `series_key`
says what the pixels are ("Color") and the scale says how big they are, so a key
alone does NOT identify a series: use `display_name`, which is FalconView's own
map-type label:

```python
for s in sorted(cat.series(), key=lambda s: s.scale_denom):
    print(f"id={s.id:<3} {s.format:<8} {s.display_name:<18} 1:{int(s.scale_denom):,}")

# id=5   geotiff  Color 0.300 meter  1:2,014
# id=10  geotiff  Color 0.600 meter  1:4,028
# id=6   geotiff  B&W 1 meter        1:6,714
# id=8   geotiff  Color 1 meter      1:6,714
# id=9   geotiff  GeoTIFF 1:30 K     1:30,000
# id=4   cadrg    TLM 1:50 K         1:50,000
# id=11  geotiff  Color 10 meter     1:67,141
# id=7   geotiff  Color 50 meter     1:335,707
# id=3   cadrg    LFC 1:500 K        1:500,000
# id=2   cadrg    JNC 1:2 M          1:2,000,000
# id=1   cadrg    GNC 1:5 M          1:5,000,000
```

A catalog written before this (schema 1) keyed a series on the format and key
alone, so every GeoTIFF resolution merged into one row. Opening such a file
REBUILDS it — the data sources survive, their coverage does not — and
`cat.needs_rescan` is then `True`, meaning `cat.scan(...)` each source again.

Two queries do most of the work — *what covers this box* and *which series suits
this scale*:

```python
atlanta = pyfvw.geo.GeoRect(ll=pyfvw.geo.GeoPoint(33.6, -84.5),
                            ur=pyfvw.geo.GeoPoint(33.9, -84.2))

for r in cat.select_by_geo_rect(atlanta):      # optional series_id= filter
    print(r.series_key, os.path.basename(r.path), r.bounds)

best = cat.best_series_for_scale(500_000)      # -> SeriesRow(cadrg:LFC, 1:500000)
```

The R-tree is antimeridian-aware: crossing coverage is stored as two boxes and
queries are split and de-duplicated, so a viewport over the dateline needs no
special-casing from you. `remove_data_source(id)` drops a source and its rows.

---

## 3. Rendering a base map

`MapEngine` is the compositing loop that a viewer would otherwise write by hand:
catalog query → per-frame raster source (through an LRU cache) → resample →
canvas. You configure a viewport and call `render`.

```python
center = pyfvw.geo.parse_location("33 44 55.7 N 84 23 17.5 W")
# also accepts "33.7488 -84.3882" and MGRS: "16SGB 47342 34212"

eng = pyfvw.engine.MapEngine(cat)
eng.set_surface(800, 600)
eng.set_center(center)
eng.set_scale(500_000)                         # 1:500,000

canvas = pyfvw.canvas.CpuCanvas(800, 600)
canvas.clear((255, 255, 255))
drawn = eng.render(canvas, best.id)            # series_id=0 renders all series
print(f"composited {drawn} frames")            # composited 2 frames

arr = np.asarray(canvas.buffer)                # zero-copy (600, 800, 4) uint8
```

`arr` is a live view of the canvas, so it is ready for Pillow, OpenCV, an
`imshow`, a Qt/tk widget, or a hand-rolled PNG writer (`PythonView.py`'s
`_save_png` is a dependency-free one, in about fifteen lines).

The engine's projection is exposed for coordinate work — this is what turns a
mouse click into a position:

```python
print(eng.proj.bounds)              # GeoRect(ll=(33.546, -84.956), ur=(33.950, -83.819))
print(eng.proj.deg_per_pixel_lat, eng.proj.deg_per_pixel_lon)

p = eng.proj.surface_to_geo(400, 300)          # pixel -> GeoPoint
sx, sy = eng.proj.geo_to_surface(p)            # and back
```

Attach an elevation source and the engine answers terrain queries at the same
coordinates — meters, `NaN` for a DTED void post, `FvError(OUT_OF_COVERAGE)`
outside the data:

```python
eng.set_elevation_source(pyfvw.formats.DtedElevationSource("TestData/dted"))
print(eng.get_elevation(center.lat, center.lon), "m")     # 338.0 m
```

**Scale vs. physical scale.** `set_scale(N)` is the FalconView-compatible
equal-arc path. When you want a series drawn at its *native* size on a known
display pitch — a 1:500k chart actually at 1:500k, imagery at 100% — use
`set_physical_scale`, which also gives the projection physically correct
latitude-dependent aspect:

```python
lfc = next(s for s in cat.series() if s.series_key == "LFC")
eng.set_physical_scale(lfc.scale, lfc.scale_units,
                       pyfvw.engine.NATIVE_DISPLAY_MM_PER_PIXEL)  # 0.25 mm/px
```

`mm_per_pixel` is then your zoom knob: larger means zoomed out.

To pre-render a region for offline use, `pyfvw.store.TilePackWriter` drives the
same engine into an OGC GeoPackage pyramid, which the catalog reads back as the
`gpkg` format — pyramid levels become series.

---

## 4. Overlays that draw geospatial features

An overlay is your own drawing on top of the map. Subclass
`pyfvw.overlay.Overlay`, override the handlers you care about, and let the
`OverlayManager` own the stack. `on_draw` receives the **projection** and the
**canvas**: converting geography to pixels with `proj.geo_to_surface` is the
whole job.

> There is a **real** route overlay — `pyfvw.route.RouteOverlay`, with a
> document, a road planner, a pick, a snap and an editor. What follows is not
> it. It is the smallest overlay that shows the three things an overlay does
> (draw, remember what it drew, answer a click), written from scratch so you
> can see all of it at once; see §5 and `pyfvw.route` for the real one.

```python
class LegOverlay(pyfvw.overlay.Overlay):
    """A named route: leg lines, waypoint diamonds, labels."""

    def __init__(self, name, waypoints, color=(220, 30, 30)):
        super().__init__(name)
        self.waypoints = waypoints          # [(label, lat, lon), ...]
        self.color = color
        self.selected = None
        self._drawn = []                    # where the last on_draw put them

    def on_draw(self, proj, canvas):
        self._drawn = []
        for label, lat, lon in self.waypoints:
            try:
                x, y = proj.geo_to_surface(pyfvw.geo.GeoPoint(lat, lon))
            except pyfvw.FvError:
                continue                    # not on this projection; skip it
            self._drawn.append((label, int(round(x)), int(round(y))))

        if len(self._drawn) >= 2:
            canvas.draw_lines([(x, y) for _, x, y in self._drawn],
                              color=self.color, width=2)

        for label, x, y in self._drawn:
            fill = (255, 220, 0) if label == self.selected else self.color
            canvas.fill_polygon([[(x, y - 7), (x + 7, y), (x, y + 7), (x - 7, y)]],
                                fill=fill, outline=(0, 0, 0), outline_width=1)
            canvas.draw_text(label, x + 10, y - 10, color=(0, 0, 0), size=12.0)

    def on_mouse_down(self, e):
        # Hit-test what was DRAWN, so a click agrees with the screen.
        for label, x, y in self._drawn:
            if abs(e.x - x) <= 8 and abs(e.y - y) <= 8:
                self.selected = label
                return True                 # handled -> stops routing
        return False
```

Stack it under the built-in graticule and draw. Overlays draw **bottom-up** and
receive events **top-down until one returns `True`**:

```python
route = LegOverlay("KATL departure", [
    ("KATL",  33.6407, -84.4277),
    ("VULCN", 33.75,   -84.30),
    ("ROME",  34.35,   -85.16),
])

mgr = pyfvw.overlay.OverlayManager()
mgr.add(pyfvw.overlay.GridOverlay())        # lat/lon graticule; .set_color(...)
mgr.add(route)

canvas.set_default_font("/System/Library/Fonts/Supplemental/Arial.ttf")
canvas.clear((255, 255, 255))
eng.render(canvas, best.id)                 # base map first
mgr.draw_all(eng.proj, canvas)              # then overlays, bottom-up

sx, sy = eng.proj.geo_to_surface(pyfvw.geo.GeoPoint(33.75, -84.30))
mgr.route_mouse_down(pyfvw.overlay.MouseEvent(int(sx), int(sy)))
print(route.selected)                       # VULCN
```

Notes that save time:

- **Keep a reference or don't** — `add()` keeps the Python half of a subclassed
  overlay alive for the manager's lifetime, so `mgr.add(MyOverlay())` retains
  its overrides even with no other reference.
- **`overlay.visible = False`** skips both drawing and event routing.
- **Exceptions are contained.** An exception in `on_draw` surfaces as
  `FvError` from `draw_all`, naming the overlay
  (`overlay 'broken': python overlay: RuntimeError: boom`); an exception in an
  event handler is logged and treated as unhandled. Nothing raw crosses the SPI.
- `set_default_font` takes a TTF path — text is stb_truetype and ASCII-only for
  now; without a font, `draw_text` has nothing to draw with.
- The other canvas primitives are `clear`, `draw_lines` (with `width=` and a
  `dash=` pattern), `fill_polygon` (even-odd over multiple rings), `draw_text`,
  and `draw_pixmap` for alpha-blending a `PixelBuffer`.

### Terrain contours

`pyfvw.overlay.ContourOverlay` is the ported Contour Lines overlay: it traces
contours from an elevation source and draws them, with major lines heavier than
minor ones and the elevation labelled on the majors.

```python
c = pyfvw.overlay.ContourOverlay()
c.set_elevation_source(pyfvw.formats.DtedElevationSource("/data/dted"))
c.set_property("major_interval", 500.0)     # in `interval_unit`s
c.set_property("interval_unit", "meters")   # a choice takes its NAME or index
c.set_property("show_labels", True)
mgr.add(c)
```

Three things are worth knowing:

- **Nothing is drawn without an elevation source**, and nothing is drawn on a
  map smaller in scale than `display_threshold` (1:250 K by default) — contours
  at 1:5 M are a smear. `c.last_draw` says which of the two it was
  (`no_source`, `below_threshold`) along with everything else the draw did.
- **The tracing is cached on a geographic lattice**, so a pan is nearly free and
  only a change of interval, of sampling, or of source re-reads the terrain.
- **`smoothing`** (`none` / `chaikin` / `spline`) rounds the staircase a
  contour traced off elevation posts has at large scales. It works on the
  projected line, so it costs what is on screen and never invalidates the
  cache — `last_draw["vertices"]` versus `["shaped_vertices"]` is what it costs
  in ink.

### Features from a real product

When the features are a *map product* rather than your own data, don't hand-roll
it — the vector seam reads, styles, and draws DNC and S-57 ENC through the same
three classes:

```python
src = pyfvw.vector.VpfVectorSource()
src.open("TestData/vpf/dnc17/h1707300")       # a DNC library directory
style = pyfvw.vector.GeoSymStyleEngine()
style.open("TestData", pyfvw.vector.GEOSYM_DNC)

proj = pyfvw.engine.MapProjection()           # standalone, no catalog needed
proj.set_surface_size(512, 512)
proj.set_center(pyfvw.geo.GeoPoint(41.70, -69.90))
proj.set_physical_scale(300_000, 0.25)

cv = pyfvw.canvas.CpuCanvas(512, 512); cv.clear((255, 255, 255))
vr = pyfvw.vector.VectorRenderer(src, style)
vr.render(proj, cv)                           # does NOT clear the canvas
print(vr.features_queried, vr.draws_emitted, vr.query_ms, vr.draw_ms)

for hit in vr.pick_index.hit_test(256, 256, 6.0):   # topmost first
    d = src.describe(hit.ref)
    print(d.title, d.layer_name, [(a.code, a.display) for a in d.attributes[:3]])
    # Depth Curve hydline [('f_code', 'Depth Curve'), ('acc', 'Accurate'), ...]
```

Swap `EncVectorSource` + `S52StyleEngine` for the same call sequence on ENC
cells — including the mariner settings that change *what* is drawn
(`style.mariner().safety_contour = 10.0`), `set_color_scheme(S52_NIGHT)`, and
`src.staleness_warning` when cells have unapplied updates. Every engine exposes
`rules()` (a small text rule language for show/hide/priority) and
`viewing_groups()` for decluttering.

The mariner settings are **shared by both chart products**: on DNC they are
GeoSym's `ssdc`/`msdc`/`mssc`/`idsm`/`isdm` under their S-52 names, so
`GeoSymStyleEngine.mariner()` moves the depth ramp the same way. Their defaults
differ on purpose (DNC 10 m with the shallow pattern on, ENC 30 m without), and
DNC ignores `safety_depth` — its `ssdc` is both the contour and the sounding
threshold. Use `set_mariner()` in a loop: reading through `mariner()` hands back
a live reference and bumps the style epoch, which rebuilds a retained scene.

Groups of features switch off together through `vector.FamilySet` — a JSON file
of named families, each a list of rule selectors, with an `enabled` flag.
Starters for all three products ship in `port/families/`:

```python
fams = pyfvw.vector.FamilySet()
fams.load_file("port/families/dnc-families.json")
fams.set_enabled("bottom", False)      # hide the bottom characteristics
fams.append_rules(style.rules())       # families first, your own rules after
```

OSM vector tiles are the third product, and the same three classes again — with
one extra step, because a tile pyramid is the one product where the SCALE picks
which data is read:

```python
src = pyfvw.vector.OsmVectorSource()
src.open("TestData/OSM/mbtiles/us-south.mbtiles")
style = pyfvw.vector.OsmStyleEngine()
style.load_file("port/Osm/styles/peregrine-osm.json")   # a MapLibre GL style

# The zoom<->scale relation is latitude-dependent and BOTH halves derive it.
# Give the style engine the viewport's centre latitude and the same pixel
# pitch the source has, or its minzoom will switch layers on at a different
# scale than the tile level the source read.
style.set_reference_latitude(33.755)
style.set_display_mm_per_pixel(src.display_mm_per_pixel)

cv.clear(style.background(25_000)[:3])   # a GL `background` is not a feature
```

`src.last_query_zoom` says which level was read and `src.last_query_overzoom`
how far past the pyramid the display has gone (z14 geometry under z15+ rules —
intended, and how a slippy map keeps growing after it runs out of levels).
A style sheet outside the supported subset raises rather than loading half of
itself; `style.ignored_icons` lists what a sprite sheet would have drawn.

The pick index is built from what the renderer actually inked, and is valid only
for the **last** `render()`.

---

## 5. Overlays as documents: `pyfvw.app`

Section 4's overlay draws. `pyfvw.app` is what turns a drawing into an
**application**: overlay types the user can create and open, documents that
know whether they are unsaved, an editor mode, and one pick that the hover, the
click and the right-click menu all share. It is the Python face of the C++
`fv::app` layer, so a shell written here and a shell written in C++ answer the
same interface.

Four ideas, and everything else follows from them.

**A capability is a method you defined.** There is nothing to register and no
base class to inherit. Define `file_open`/`file_new`/`file_save_as` and your
overlay IS a document — it gets `.dirty`, `.file_spec`, and the New/Open/Save/
Close flows. Define `hit_test_point` and it answers picks. Define `menu_items`
and it contributes a right-click section. The full list is in
`help(pyfvw.overlay.Overlay)`.

```python
class Notes(pyfvw.overlay.Overlay):
    def __init__(self):
        super().__init__("Notes")
        self.lines = []

    def file_new(self):
        self.lines = []

    def file_open(self, spec):              # raise to report a failure
        self.lines = open(spec).read().split("\n")

    def file_save_as(self, spec, format_index):
        open(spec, "w").write("\n".join(self.lines))

    def hit_test_point(self, proj, x, y, tolerance_px):
        return [pyfvw.app.HitItem(feature=7, distance_px=0.0,
                                  hint=pyfvw.app.HintText("a note", "line 7"))]
```

**A type is data, not a class.** An `OverlayTypeDesc` carries the identity, the
menu text, where new instances land in the stack, the file sub-descriptor and
the factory. A descriptor WITHOUT a `file` is *static*: at most one instance,
toggled on and off (the lat/lon grid). One WITH a file has as many instances as
the user opens documents.

```python
app = pyfvw.app
registry = app.OverlayTypeRegistry()
app.register_builtin_types(registry)        # fv.grid (static), fv.points (file)
registry.register(app.OverlayTypeDesc(
    id="user.notes", display_name="Notes",
    factory=lambda: Notes(),
    file=app.FileTypeDesc(default_extension="notes",
                          open_filters=[("Notes (*.notes)", "*.notes")]),
    default_display_order=1000))
```

**You are the dialogs.** The core never opens one: it calls back into an
`AppShell` you subclass, and the flows cannot tell your tk dialog from a
scripted table in a test. Five decisions and six presentation calls, all
listed in `help(pyfvw.app.AppShell)`.

```python
class MyShell(app.AppShell):
    def ask_save(self, name):               # -> SaveAnswer.SAVE/DISCARD/CANCEL
        return app.AppShell.SaveAnswer.DISCARD
    def choose_files_to_open(self, file_type):
        return ["/tmp/a.notes"]             # [] means the user cancelled
    def choose_save_spec(self, file_type, suggested):
        return ("/tmp/a.notes", 0)          # "" means the user cancelled
    def choose_from_list(self, title, rows):
        return 0                            # None means the user cancelled
    def confirm_revert(self, spec): return False
    def set_cursor(self, cursor): pass
    def show_hint(self, hint): pass
    def show_context_menu(self, x, y, menu): pass
    def request_invalidate(self): pass
    def on_editor_changed(self, type_id, editor): pass
    def report_error(self, code, message): print(message)
```

**A flow reports what happened, and a cancel is not an error.** Every verb
returns a `FlowResult`: `DONE`, `CANCELED` (the user said no — say nothing,
they know) or `FAILED` (already reported through `report_error`). One cancel in
one save prompt aborts the whole `close_all`, and with it the application exit.

```python
manager = pyfvw.overlay.OverlayManager()
manager.set_type_registry(registry)         # insertion by display order
session = app.OverlaySession(registry, manager, MyShell(), pyfvw.Settings())

session.toggle_static(app.GRID_TYPE_ID)     # static: on, then off again
session.new_file_overlay("user.notes")      # a fresh untitled document
session.open_file("", "/tmp/a.notes")       # "" dispatches by EXTENSION
notes = manager.first_of_type("user.notes")
notes.dirty = True
session.save(notes)                         # Save As when never saved
session.close(notes)                        # prompts, because it is dirty
```

Two more pieces sit on top of that.

**Editors** are per TYPE, not per overlay — the editor is the tool state ("I am
drawing routes") and the overlay being edited is whichever instance is current.
An editor is duck-typed: any object with `activate()` and `deactivate()`, plus
whichever of `tools()`, `default_cursor()`, `ui_constraints()` and
`auto_enter_on_create()` it wants. `EditorManager.set_mode(t)` makes the current
overlay match the mode — creating one through the session if none of that type
is open — and making a different overlay current makes the mode match the
overlay, which happens however the change was made.

**Picking** aggregates: every overlay on screen answers, and a policy decides.
`TOP_MOST` is what a mouse user expects (they aimed at what they saw),
`NEAREST` is what a finger needs, `ASK_WHEN_AMBIGUOUS` turns a crowded point
into `choose_from_list`. Who is asked is exactly the draw order reversed, so a
tap always agrees with the screen.

```python
pick = app.PickSession(manager, shell)
pick.update_hover(proj, x, y)               # tells the shell cursor + hint,
                                            # only when the hit CHANGES
hit = pick.resolve_click(proj, x, y, app.PickPolicy.ASK_WHEN_AMBIGUOUS)
if hit is not None:
    print(hit.overlay.name, hit.feature, hit.hint.status)
pick.show_context_menu(proj, x, y)          # False = nobody contributed
```

`pyfvw.overlay.PointOverlay` is a complete worked example in C++: a point set
in a SQLite document (`.fvpoints`), persistent, pickable and with a context
menu. `PointOverlay.write_sample_file(path, symbol_dir="")` writes an arbitrary
starter document to pick at.

A point is drawn as its `shape` in its `color` — and, when it names one, with
a raster symbol stamped on top of that badge. **The artwork is IN the
document**, in a second table, so a `.fvpoints` file opens with its symbology
intact on a machine that has never seen the icon set; and one row serves as
many points as reference it, which is what makes an icon set affordable:

```python
o = pyfvw.overlay.PointOverlay("Forts")
castle = o.add_symbol_from_png("testdata/GeoSymbol/makiPng/castle.png")
o.add_symbol_from_png(".../castle.png") == castle   # same NAME, same row
o.set_points([
    pyfvw.overlay.MapPoint("Sumter",   32.7522, -79.8747, symbol_id=castle),
    pyfvw.overlay.MapPoint("Moultrie", 32.7594, -79.8577, symbol_id=castle),
])
o.file_save_as("forts.fvpoints")        # the PNG is written once
```

A `color` with alpha 0 suppresses the badge, which is how a document asks for
the bare icon; an unset `pivot` centres the tile; an id with no row (or a blob
that will not decode) falls back to the shape rather than costing a point.
Passing `symbol_dir` to `write_sample_file` embeds the ~20 maki icons the
sample's points name — the three forts share one `castle` between them.

### Changing an overlay's settings: the declared property page

Every overlay can *declare* what a user may change about it, and the four calls below are bound on
**`Overlay`** rather than on any particular overlay class. So they work the same for the graticule
today and for the scale bar or the contours tomorrow, with no new binding code — which is the whole
reason the schema exists instead of a per-overlay property dialog.

```python
g = pyfvw.overlay.GridOverlay()

for row in g.describe_properties():
    print(row["group"], row["key"], row["type"], row["default"])
# Lines line_color   color (255, 255, 255, 96)
# Lines line_width   int   1                     (also row["min"], row["max"])
# Lines show_casing  bool  True
# ...

g.get_property("show_ticks")            # True
g.set_property("show_ticks", False)
g.set_property("line_color", (255, 0, 0))     # (r, g, b) or (r, g, b, a)
g.reset_properties()                    # everything back to its declared default
```

A row carries what a UI needs to build a control: `key`, `label`, `group`, `type`
(`bool`/`int`/`float`/`str`/`color`/`choice`), `default`, `help`, plus `min`/`max` or `choices`
where they apply. `set_property` **raises** on the wrong type or a value outside the declared
range — it does not clamp, because a script and a settings file both reach it and neither is a
spinner. An overlay that declares nothing answers `[]` rather than raising, so iterating a stack is
always safe.

The same declaration is what backs the `[grid]` section of `peregrine.ini`; see
`port/peregrine.ini.sample`.

### Finding things: one question, every source

Picking asks *what is under this pixel*; **searching asks *where is X*, of the whole stack at
once**. It is a different capability on purpose — geo-space rather than pixel-space, on demand
rather than per frame, and it deliberately looks in overlays that are switched off, because "in
Points, which is hidden" is a useful answer.

```python
q = app.SearchQuery(text="rud tur")          # case-insensitive token prefix
q.near = proj.center                          # ranks by distance after quality
q.area = proj.bounds                          # optional: confine it to the view
for r in app.SearchSession(manager).search(q):
    print(r.title, "|", r.detail, "|", r.overlay.name, "|", r.bounds)
```

`SearchQuery` is two independently optional filters plus an ordering: an `area` (or `near` +
`radius_m`, which the session turns into a box and then cuts exactly), and `text`.
`SearchOrder.NEAREST` is "order by distance"; `AUTO`, the default, is best-match when there is
text and nearest when there is not. `max_results` caps at both ends — no provider builds a longer
list, and the merged list is cut after ranking.

**An overlay becomes searchable by DEFINING `search`** — the same rule that makes `hit_test_point`
a pickable overlay:

```python
class Notes(pyfvw.overlay.Overlay):
    def search(self, query):
        return [app.SearchResult(title=line, detail="note", feature=i,
                                 position=pyfvw.geo.GeoPoint(lat, lon))
                for i, line in enumerate(self.lines)
                if app.text_match_quality(query.text, line) >= 0]
```

Your provider decides WHICH string it matches; `app.text_match_quality` decides what matching
means, so "rud tur" cannot mean two things in one stack. The binding stamps `overlay` for you.

**Two things that are not overlays can be put in the stack to be asked.**
`pyfvw.overlay.VectorMapOverlay` wraps any `pyfvw.vector` source so the chart itself answers
(globally when the pack carries a name index — `fvnames build` — and inside an area otherwise),
and `pyfvw.route.RoadGraphOverlay` answers with the ROUTABLE road. Both can be held
`visible = False`: they exist to be asked, not to be drawn.

```python
chart = pyfvw.overlay.VectorMapOverlay("Chart", source)
chart.visible = False
manager.add(chart)
```

**A `VectorMapOverlay` also DRAWS**, once it is given a style engine as well as a source — the
same source/style/renderer chain the base map uses, in the overlay stack. That is how a chart is
laid over another map: OSM roads and names over shaded relief, a DNC library over imagery.

```python
osm = pyfvw.overlay.VectorMapOverlay("OSM", source)
osm.set_style(style)                 # None again = search-only
manager.add(osm)
manager.move_to_bottom(osm)          # a map goes under the documents drawn on it
```

The style's `background` layer is never painted here — a GL background is a canvas clear, and an
overlay that cleared the canvas would erase the map under it. A sheet meant for this use declares
none and keeps its area fills transparent; `port/Osm/styles/peregrine-osm-overlay.json` is the
worked example. `scene_margin`, `simplify_pixels`, `symbol_scale`, `device_dpi`,
`label_reference_scale` and `max_draw_features` forward to the renderer and survive a source or
style change.

A long search can run on a worker thread — `search()` releases the GIL — and be cut short by the
next keystroke with `app.CancelFlag`:

```python
flag = app.CancelFlag()
...                                  # another thread: flag.set()
rows = app.SearchSession(manager).search(q, flag)   # returns what it had, ranked
```

---

## 6. Measuring and seeing: `pyfvw.analysis`

Four tools, no overlay and no shell. `pyfvw.analysis` is FalconView's Range & Bearing and
Intervisibility work as **values a Python UI can hold** — a path, the terrain profile under it,
a viewshed, and the labels the Windows overlay would have drawn.

A path is the spine, and it is the same object for a two-point line, a polyline and a route:

```python
an = pyfvw.analysis
path = an.GeoPath([pyfvw.geo.GeoPoint(31.2, -81.8),
                   pyfvw.geo.GeoPoint(31.8, -81.2)])     # GREAT_CIRCLE by default
path.total_length_m                                       # 60 nm to the degree
path.point_at_distance(path.total_length_m / 2)           # on the great circle, not the rhumb
```

The line type belongs to the **path**, so `an.LineType.RHUMB` changes both the range and where the
midpoint is. A leg the geodesy refuses is zero-length and `leg(i).ok` is false — never dropped, so
every index still lines up with the vertices.

**The elevation profile of anything, including a route**, is one call, because AN2 walks the path
rather than reading a diagonal out of a DTED block: N elevation reads for N samples, correct at
every bearing.

```python
src  = pyfvw.formats.DtedElevationSource(dted_root)
opts = an.ProfileOptions(); opts.sample_count = 200
r    = an.sample_terrain_profile(src, path, opts)
plot(r.distances_m, r.elevations_m)      # NaN where there is no data -> a gap, not a refusal
r.min_m, r.max_m, r.gain_m, r.no_data_count
```

A hole is not a failure and a path with no coverage at all is still a successful profile with
every point `has_data == False`. Pass `opts.step_m` instead of a count and every turning point
stays a sample.

**The viewshed** is the one call in pyfvw long enough to need a progress bar, and it is bound for
that: the GIL is released for the computation, so a UI thread keeps painting, and the result is a
zero-copy buffer rather than a million boxed floats.

```python
req = an.ViewshedRequest()
req.observer, req.observer_height_m, req.range_m = observer, 30.0, 25000.0
req.step_deg = an.viewshed_step_from_post_spacing(src, observer)

r = an.compute_viewshed(src, req, lambda pct: not cancelled)   # False cancels
grid = np.asarray(r)          # zero-copy (span, span) float32; row 0 north, col 0 west
```

`0.0` means visible from the observer, a positive value is the height something would need to
reach there to be seen, and `NaN` means no answer. A callback that **raises** surfaces its own
exception (a `KeyboardInterrupt` stays a `KeyboardInterrupt`); one that returns `False` raises
`FvError(INTERRUPTED)`. Over `max_posts` the **step widens and the range is still delivered** —
there is no "reduce the range" dialog here.

**The measurements** are the labels, spelled exactly as FalconView spells them, padding spaces
included:

```python
m = an.Measurement(an.MeasurementKind.MULTI_POINT, path)
m.summary_label      # '64.35 NM'
m.labels             # a CUMULATIVE distance at each turning point
m.leg_label(0)       # ' 045.0°T / 12.34 NM '
```

`m.style` is the property sheet — units, degrees or mils, the bearing format, true or magnetic.
The decimal ladder is on the **value** (under 100 gets two places), so one leg reads `12.34 NM`
and `74977 ft`. Magnetic bearings go through `geo_tool`'s model; note that there is no `wmm.dat`
in the tree, so the declination is currently the built-in **WMM-95** table.

---

## 7. Where to find more

The bindings are documented in the module itself; nothing here is the only copy.

- **`help(pyfvw)`, `help(pyfvw.vector.VectorRenderer)`, `dir(pyfvw)`** — every
  bound class and method carries its docstring, and pybind11 prints real
  signatures with argument names and defaults. This is the fastest and most
  current reference; start here.
- **[`ICD-MAPPING.md`](ICD-MAPPING.md)** (this directory) — one row per bound
  surface mapping the legacy COM ICD (`PFPS400-MapServer-ICD.doc`) to pyfvw,
  with the **semantic deltas** called out: feet → meters, HRESULT → `FvError`,
  cursor enumeration → whole lists, and so on. Read this if you are porting
  code written against the Windows interfaces.
- **[`test/test_pyfvw.py`](test/test_pyfvw.py)** and
  **[`test/test_pyfvw_app.py`](test/test_pyfvw_app.py)** — the executable spec.
  Every bound surface has a test with pinned values, and the tests are written
  to be read as examples; the second file is section 5's whole surface with a
  scripted shell, and
  **[`test/test_pyfvw_analysis.py`](test/test_pyfvw_analysis.py)** is section
  6's. Run them with `ctest --test-dir build -R pyfvw_pytest`.
- **[`../../apps/PythonView.py`](../../apps/PythonView.py)** — a complete
  desktop viewer over this API: catalog management, every data family,
  pan/zoom, overlays, click-to-identify, headless `--shot` rendering, and (A6)
  an `AppShell` implementation in tk. The best source of realistic usage.
  **[`../../apps/route.py`](../../apps/route.py)** is the smaller read, and as
  of 2026-08-27 it is small indeed: a tool palette and a type descriptor, which
  is all a shell owns once the overlay behind it is `pyfvw.route`.
- **`pyfvw.analysis`** — the Analysis tools (section 6): `GeoPath`,
  `sample_terrain_profile`, `compute_viewshed` and the four `Measurement`
  kinds, plus the unit and formatting table. Headless — nothing in it knows
  what a chart looks like, which is why the profile of a route is the profile
  call with a route's path.
- **`pyfvw.route`** — RouteKit: the `.fvrte` document, `RoutePlanner` over the
  road graph, `RouteOverlay`, and `RouteEditSession` (`overlay.edit`) — select,
  drag, add, delete, undo, and a snap on every position it places. Its own
  submodule with its own `register_route_overlay_type`, because it links the
  router and `pyfvw.app.register_builtin_types()` may not; a shell calls both.
- **[`pyfvw_module.cpp`](pyfvw_module.cpp)** — where the docstrings live. If a
  method's behavior is unclear, its binding is a few lines long and says which
  C++ call it forwards to.
- **`port/include/fvkit/*.h`** — the C++ headers behind the bindings
  (`geo.h`, `raster.h`, `proj.h`, `engine.h`, `canvas/`, `catalog/`, `overlay/`,
  `vector/`, `store/`). Binding names are the snake_case of these, so a header
  comment answers most questions the docstring doesn't.
- **`port/fvkit-contracts.md`** — the binding conventions themselves (D3 error
  handling, D5 naming), i.e. *why* the API looks like this.
- **`port/PORTING.md`** — the port ledger. Status **`P`** on a row means that
  module is exposed in Python; the row says what landed and what did not.
- **CLI equivalents** — `build/port/fvkit/fvrender` (render a map to PNG from
  the command line) and `fvpack` (build a GeoPackage tile pyramid) are the same
  paths without Python, useful for checking whether an issue is in your script.
- **`pyfvw.Settings`** — the port's registry replacement, a hand-edited INI
  (`port/peregrine.ini.sample`); `pyfvw.default_settings_paths()` says where it
  is looked for.
