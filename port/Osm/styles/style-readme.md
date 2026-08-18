<!--
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) 2026 Chris Bailey
Part of Peregrine, a cross-platform port of FalconView(tm).
See LICENSE and NOTICE.md for the full licensing picture.
-->

# Style syntax

`OsmStyleEngine` reads **MapLibre / Mapbox GL style JSON, spec version 8** — the same file a
browser map is styled with. It reads a **declared subset** of it, and this document is that
declaration.

The reference style in this directory, `peregrine-osm.json`, is written entirely within the
subset and is the worked example for everything below.

## The one rule that shapes everything else

**Anything outside the subset FAILS THE LOAD, naming the layer and the property.** It is never
ignored and never guessed at.

```
style layer "roads-casing": line-width is a function of a data property
```

This is the same philosophy the ENC reader is built on: a style is authored by a person, and
half of one drawn silently is worse than a message. `LoadText`/`LoadFile` are all-or-nothing —
on failure the engine keeps whatever it had before, and a fresh engine stays closed.

Exactly four things are exempt, and each is counted rather than remembered. They are listed
under [Ignored, not rejected](#ignored-not-rejected).

## Layer types

| `type` | Supported | Becomes |
|--------|-----------|---------|
| `background` | yes | not a feature — the colour the application clears the canvas with, via `background()` |
| `fill` | yes | a brush, an optional outline pen, and/or a repeating sprite pattern |
| `line` | yes | a pen, with dashes |
| `symbol` | yes | an icon, a label, or both |
| `circle` | yes | a generated filled-disc symbol |
| `raster`, `heatmap`, `hillshade`, `fill-extrusion` | **no** | rejected |

**Draw order is style-layer order, not feature order.** Each layer's index in the `layers` array
becomes the draw priority, and the renderer sorts by it *across* features. This matters more
than it sounds: OpenMapTiles road rendering is a wide dark casing under a narrow bright fill,
both from the *same* feature. Per-feature drawing would put a road's casing over its neighbour's
fill and shred the network at every junction.

## Properties

Everything not listed here is rejected if present.

### Every layer

`id`, `type`, `source-layer`, `minzoom`, `maxzoom`, `filter`, `layout.visibility`.

`minzoom`/`maxzoom` become a scale band through one zoom↔scale relation shared with the tile
source. Set the engine's reference latitude and display pitch to the source's own, or the
style's zoom windows will not agree with the tiles that were read.

### `background`

`background-color`, `background-opacity`.
`background-pattern` is **rejected**: a background here is a canvas clear, so there is no
geometry to repeat a pattern over.

### `fill`

`fill-color`, `fill-opacity`, `fill-outline-color`, `fill-pattern`.

### `line`

`line-color`, `line-opacity`, `line-width`, `line-dasharray`.
`line-pattern` is **rejected**, and this one is a real gap rather than a category error: GL
*stretches* a tile along the line, while this port repeats a symbol at a fixed step. Those are
different pictures, and guessing which the style meant is what the declared subset exists to
prevent.

`line-dasharray` is a constant array only, in `line-width` units, as GL defines it.

### `symbol`

Icon half: `icon-image`, `icon-size`, `icon-rotate`.
Text half: `text-field`, `text-size`, `text-color`, `text-opacity`, `text-halo-color`,
`text-halo-width`, `symbol-placement`, `symbol-spacing`, `text-max-angle`, `text-offset`.

The two halves are **independent**. A layer may carry an icon and no text (a shield), text and
no icon (a place name), or both (a POI). The application's label switch turns off the text and
leaves the icon drawing.

`symbol-placement` accepts `point`, `line` and `line-center`. `line-center` is `line` with the
spacing forced to zero, which is what "one run per feature" means to the placer. A `line`
placement on a point feature is harmless — it falls back to point placement.

`text-offset` is `[x, y]` in ems, and **only the perpendicular component `y` survives** a line
placement: the placer offsets *across* the path, and an along-axis offset has no meaning once
the run is centred on the geometry. GL's `+y` is down; the placer's positive offset is left of
travel, so the sign is flipped on the way in.

A line-placed label is **centred on its line** — the cap-height box straddles the geometry, as
GL draws it — and `text-offset` is then measured from there.

### `circle`

`circle-color`, `circle-opacity`, `circle-radius`.

## Values: constants and zoom functions

A property is either a **constant** or a **zoom function**:

```json
{"base": 1.2, "stops": [[12, 0.5], [16, 4]]}
```

`base` is optional and defaults to 1 (linear).

**Rejected:** expressions (`["interpolate", …]`, `["step", …]`, `["case", …]`, `["match", …]`,
`["get", …]`, `["coalesce", …]`) and **data-driven functions** — a `"property"` member inside a
function object. The message says which.

> **Colour stops STEP; numeric stops INTERPOLATE.** GL interpolates colour ramps too. Taking the
> last stop at or below the zoom is a difference you can only see side by side, and it keeps
> "interpolate" from quietly meaning two things.

## Filters

**Legacy filter syntax only:**

```json
["all", ["==", "$type", "LineString"], ["in", "class", "motorway", "trunk"]]
```

Operators: `==` `!=` `<` `<=` `>` `>=` `in` `!in` `has` `!has` `all` `any` `none`, over a tag key
or `$type` (`Point` / `LineString` / `Polygon`).

Expression-syntax filters are **rejected**. GL still accepts legacy filters, so a style can be
made loadable here by writing its filters the old way — usually a mechanical edit.

## Token templates

`text-field`, `icon-image` and `fill-pattern` are **token templates**, the `{tag}` form:

```json
"text-field": "{name:latin} {ref}"
"icon-image": "{class}"
```

Each `{…}` is replaced by the feature's value for that tag. A token that resolves to nothing
drops out; a template whose tokens *all* fail to resolve produces nothing at all, which is how a
blank label is skipped rather than drawn empty. A template of pure literal text is always kept.

The expression form of `text-field` (`["get", "name"]`) is rejected.

## Sprites

`sprite` names a sheet **on the local filesystem** — the pair `<base>.json` + `<base>.png`, in
the layout every sprite set ships in:

```json
{"cafe": {"x": 0, "y": 0, "width": 16, "height": 16, "pixelRatio": 1}}
```

Resolution order:

1. `SetSpriteBase("/path/to/sprite")` (no extension) if the caller set one — and if that does
   not open, **the load fails**: a base the caller named and got wrong is worth a message.
2. Otherwise the style's own `sprite` value, resolved **relative to the style file** when
   `LoadFile` was used. A style shipped beside its sheet therefore needs no configuration.
3. An `http(s)://` or `mapbox://` sprite resolves to nothing — **nothing here fetches over the
   network**. This is *not* a load failure, because that would reject every style published on
   the web; the icons simply go undrawn and are counted.

`pixelRatio` is honoured: a `@2x` sprite draws at the same *size* as its 1x twin and merely
carries more detail. `SetPreferHighDpiSprites(true)` takes the 2x artwork where a sheet offers
both; set it before loading, since the load binds names to tiles.

Every icon or pattern name that no sheet answered is counted in `ignored_icons()`, so "why are
there no icons" is a question the diagnostics answer rather than the picture.

### `fill-pattern`

The sprite tiles the area. Spacing is the tile's **own size** in nominal pixels, which is what
makes a stamped lattice read as GL's seamless texture fill instead of a field of stamps with
gaps between them.

Two honest differences from GL:

- The lattice is **anchored to the ground**, not to the viewport, so a pattern stays welded to
  its polygon under pan instead of swimming across it.
- It is **stamped, not texture-mapped**. A tile whose artwork runs to its own edge tiles
  seamlessly; one that expects sub-pixel texture wrapping will not match GL exactly.

`fill-color` may be given alongside `fill-pattern`: the colour is drawn first and shows through
wherever the artwork is transparent.

## Ignored, not rejected

Four things, each counted so it is visible in the diagnostics rather than silent:

| What | Why | Counted by |
|------|-----|-----------|
| `glyphs` | A glyph atlas is network plumbing; text goes through the canvas font, as S-52's and GeoSym's does | — |
| `text-halo-blur` | The halo is a *stamped* dilation — the string redrawn 4–8 times a pixel or two off — so there is no coverage for a blur radius to soften | `ignored_halo_blur()` |
| unresolved `icon-image` / `fill-pattern` | No sheet, or the sheet has no such sprite | `ignored_icons()` |
| a `text-field` whose tokens all failed | The feature did not carry the tag; an empty label is skipped rather than drawn blank | `empty_labels()` |

(An `icon-image` or `fill-pattern` *template* that resolves to nothing draws nothing and is not
counted anywhere — there is no name to count it under. Only a resolved name that no sheet
answered reaches `ignored_icons()`.)

`text-halo-blur` is the interesting one: it is ignored rather than rejected because every
OpenMapTiles-derived style carries a blur beside a width that *is* honoured, and failing the
whole sheet over the one property whose absence is least visible would be the declared-subset
rule turned against itself.

## Units

GL paint values are CSS pixels at a nominal 96 dpi. They are converted to device pixels using
`StyleContext::device_dpi`, because a style engine owns its own unit conversion — the same
contract that makes S-52's 0.32 mm pen and GeoSym's HIMETRIC work.

## Schema

The engine is **schema-neutral**: it only ever asks a feature for the tags a filter names. The
bundled reference style is written against **OpenMapTiles** — `source-layer` names (`water`,
`waterway`, `landuse`, `transportation`, `building`, `place`, …) and the `class` dispatch tag are
that schema's — because the delivered pyramid declares itself as such in its own metadata.

`layers_that_drew()` against the number of layers answers "is this style aimed at this tileset?"
without looking at a picture.

## Making a web style load

In rough order of how often it is what stops a style:

1. Rewrite expression filters as legacy filters.
2. Replace `["interpolate", …]` with `{"base": …, "stops": […]}`.
3. Replace an expression `text-field` with the `{tag}` token form.
4. Remove data-driven (`"property"`) functions, or split the layer by filter.
5. Drop `raster`/`hillshade`/`fill-extrusion` layers.
6. Point `sprite` at a local sheet, or set one with `SetSpriteBase`.

The failure message names the layer and the property every time, so this is a loop: load, read
the message, fix that one thing.
