// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::OsmStyleEngine — a MapLibre/Mapbox GL **style JSON** loader over
// fv::LookupTableStyleEngine (OSM phase O2, vpf-geosym plan §5.1/§5.2).
//
// THIS IS A LOADER, NOT A FOURTH ENGINE. GeoSym reads fullsym.txt, S-52 reads
// chartsymbols.xml, and this reads style.json; all three end up as table rows
// consulted by the same core, behind the same rule layer, feeding the same
// renderer. The standing rule in the ledger ("style engines are loaders over
// LookupTableStyleEngine — do not write a fourth engine") is the whole design
// constraint here, and the three things §5.2 promised would line up do:
//
//   MapLibre `filter`         -> the §5.2 predicate AST (Predicate)
//   MapLibre minzoom/maxzoom  -> ScaleBand, via ONE zoom<->scale relation
//                                (webmerc::ZoomForScaleExact / ScaleForZoomExact)
//   MapLibre layer order      -> StyleResult::priority
//
// SCHEMA: OpenMapTiles. The delivered us-south.mbtiles says so in its own
// metadata ("Tilemaker to OpenMapTiles schema"), so `source-layer` names
// (water, waterway, landuse, transportation, building, place, …) and the
// `class` dispatch tag are that schema's. The engine itself is schema-neutral
// — it only ever asks the feature for the tags a filter names — but the
// bundled reference style is written against OpenMapTiles and nothing else.
//
// DRAW ORDER IS STYLE-LAYER ORDER, NOT FEATURE ORDER. This is not a detail:
// OpenMapTiles road rendering is built out of casing layers, a wide dark line
// under a narrow bright one, and both come from the SAME feature. Drawing
// per-feature would put a road's own casing on top of its neighbour's fill and
// the network would look shredded at every junction. Each style layer's index
// becomes the StyleResult priority, and VectorScene stable-sorts by priority
// ACROSS features, so the result is exactly the painter's algorithm the style
// was authored for.
//
// THE SUPPORTED SUBSET IS DECLARED AND ENFORCED (the ENC-reader philosophy —
// never guess at data you do not understand). Load*() FAILS, naming the layer
// and the property, on anything outside it:
//
//   layer types    background, fill, line, symbol, circle
//                  (raster / heatmap / hillshade / fill-extrusion: rejected)
//   filters        LEGACY syntax only — ==, !=, <, <=, >, >=, in, !in, has,
//                  !has, all, any, none, over a tag key or `$type`.
//                  An expression (["get",…], ["match",…], ["case",…],
//                  ["step",…], ["coalesce",…], …) is REJECTED, not ignored.
//   paint/layout   a constant, or a {"base":b,"stops":[[z,v],…]} zoom
//                  function. `interpolate`/`step` expressions: rejected.
//   text-field     the `{tag}` token form ("{name:latin}", "{name} {ref}").
//                  An expression form: rejected.
//
// THREE DELIBERATE DEVIATIONS, all counted rather than remembered:
//
//   * `glyphs` URLs are IGNORED by design (the ledger's O2 row said `sprite`
//     was too; O6 closed that half). A glyph atlas is network plumbing and
//     text goes through the canvas font like S-52's and GeoSym's does.
//     `sprite` IS honoured now, but only from the LOCAL FILESYSTEM: the pair
//     `<base>.json` + `<base>.png` read through PngSymbolLibrary's sheet form,
//     resolved next to the style file (LoadFile) or wherever SetSpriteBase
//     says. An `http(s)` sprite with no local override is not a load failure —
//     that would reject every style published on the web — it just leaves the
//     icons unresolved, which ignored_icons() counts.
//   * `text-halo-blur` is IGNORED, while `text-halo-color` and
//     `text-halo-width` are honoured (T2). The halo is a stamped dilation —
//     the string redrawn 4-8 times a pixel or two off in the halo colour, the
//     way Windows FalconView drew it — so there is no coverage for a blur
//     radius to soften. Ignored rather than rejected because every
//     OpenMapTiles-derived style carries a blur beside a width, and failing
//     the whole sheet over the one property whose absence is least visible
//     would be the declared-subset rule turned against itself.
//     ignored_halo_blur() counts it.
//   * COLOUR STOPS STEP, numeric stops INTERPOLATE. MapLibre interpolates
//     colour ramps too; stepping at the last stop at or below the zoom is a
//     visible-only-side-by-side difference, and it keeps the one place where
//     "interpolate" could quietly mean two things down to numbers.
//
// UNITS: MapLibre paint values are CSS pixels at a nominal 96 dpi. They are
// converted to device pixels here with StyleContext::device_dpi, because a
// style engine owns its own unit conversion per fvkit/vector/style.h — the
// same contract that makes S-52's 0.32 mm pen and GeoSym's HIMETRIC work.

#ifndef FV_OSM_STYLE_H_
#define FV_OSM_STYLE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/vector/lookup_engine.h"
#include "fvkit/vector/rules.h"
#include "fvkit/vector/style.h"

namespace fv {

enum class OsmStyleLayerType {
  kBackground = 0,
  kFill,
  kLine,
  kSymbol,
  kCircle,
};

// What one style layer became, exposed so a test (and an identify panel) can
// assert on the load rather than on the pixels.
struct OsmStyleLayerInfo {
  std::string id;
  OsmStyleLayerType type = OsmStyleLayerType::kFill;
  std::string source_layer;  // empty for `background`
  double minzoom = 0.0;
  double maxzoom = 24.0;
  ScaleBand band;         // the minzoom/maxzoom window, as a scale window
  std::string filter;     // Predicate::ToText(), "" when unfiltered
  bool visible = true;    // layout.visibility != "none"
  int priority = 0;       // = index in the style's layer array
};

class OsmStyleEngine : public LookupTableStyleEngine {
 public:
  OsmStyleEngine();
  ~OsmStyleEngine() override;

  OsmStyleEngine(const OsmStyleEngine&) = delete;
  OsmStyleEngine& operator=(const OsmStyleEngine&) = delete;

  // All-or-nothing, like RuleSet::LoadText and for the same reason: a style
  // sheet is authored, and half of one is worse than none. On failure the
  // engine is left CLOSED and *error (optional) carries the same text as the
  // returned Status.
  Status LoadText(const std::string& json_text, std::string* error = nullptr);
  Status LoadFile(const std::string& path, std::string* error = nullptr);

  // --- the sprite sheet (O6) -----------------------------------------------
  // Where `sprite` resolves to on disk, WITHOUT its extension: the loader
  // reads `<base>.json` and `<base>.png`. Set it before Load*, because the
  // load is what binds icon names to tiles.
  //
  // LoadFile derives this on its own from the style's own `sprite` value when
  // that value is a relative path — the usual shape of a style shipped beside
  // its sprite — so a caller with a style file and a sheet next to it needs
  // none of this. It is here for LoadText (which has no directory to resolve
  // against) and for pointing a web-published style at a local sheet.
  void SetSpriteBase(const std::string& path_without_extension);
  const std::string& sprite_base() const;
  // Take the `@2x` sprites when the sheet defines both. Like
  // PngSymbolLibrary::SetPreferHighDpi it must be set BEFORE the load.
  void SetPreferHighDpiSprites(bool on);

  // What the style's own `sprite` member said, unresolved and as written.
  const std::string& sprite_url() const;
  // Sprite ids the sheet actually offers, sorted. Empty when no sheet loaded.
  std::vector<std::string> sprite_ids() const;

  // --- the zoom<->scale relation ------------------------------------------
  // A StyleContext carries a scale and no geography, so the latitude half of
  // the Web Mercator relation has to be told to the engine. Set it to the
  // viewport's centre latitude; the default (0) is the equator, where a zoom
  // is 1.4x coarser in denominator than it is at 45 deg.
  //
  // `mm_per_pixel` must be the SAME value the OsmVectorSource was given, or
  // the style's minzoom will not agree with the tile the source read.
  void SetReferenceLatitude(double lat);
  double reference_latitude() const;
  void SetDisplayMmPerPixel(double mm_per_pixel);
  double display_mm_per_pixel() const;

  // Pin the zoom regardless of the context's scale (< 0 restores derivation).
  // For a golden test, and for a caller driving the source with its own
  // SetZoomOverride, which must be matched here or the two disagree.
  void SetZoomOverride(double z);
  double zoom_override() const;

  // The zoom this engine will style at for `scale_denominator`, fractional and
  // unclamped, used to evaluate every zoom function in the style.
  double ZoomForScale(double scale_denominator) const;

  // The zoom a zoom FUNCTION is evaluated at when the context carries no
  // scale at all. Default 14 — the maximum zoom Tilemaker cut the delivered
  // OpenMapTiles pyramid at, so a scale-less bulk render gets the widths and
  // sizes the finest available tiles were authored for. It is a separate knob
  // from visibility: per fvkit's convention a scale of 0 matches EVERY
  // ScaleBand, so no layer is thinned away, but a stop still has to be
  // evaluated somewhere and "somewhere" must be stated.
  void SetScalelessZoom(double z);
  double scaleless_zoom() const;

  // --- the loaded style ----------------------------------------------------
  const std::vector<OsmStyleLayerInfo>& layers() const;
  const std::string& style_name() const;
  // A `background` layer is not a feature and cannot be a StyleResult; the
  // application clears the canvas with this before rendering. false = the
  // style has no background layer.
  bool background(double scale_denominator, FvColor* out) const;

  // --- diagnostics ---------------------------------------------------------
  // `icon-image` / `fill-pattern` values the style asked for that NO SHEET
  // ANSWERED — no sprite loaded, or loaded and missing that name. Kept under
  // its original name because its meaning is unchanged: these are the icons
  // that did not get drawn. Before O6 that was all of them. name -> times.
  const std::map<std::string, size_t>& ignored_icons() const;
  // Style layers whose `source-layer` no feature ever carried, and layers that
  // did style something. Together they answer "is this style aimed at this
  // tileset?" without looking at a picture.
  size_t layers_that_drew() const;
  // text-field tokens that named a tag the feature did not have. An empty
  // label is skipped rather than drawn blank.
  size_t empty_labels() const;
  // Style layers that asked for `text-halo-blur`. A load-time count, not a
  // per-render one, so ResetDiagnostics() leaves it alone: it describes the
  // STYLE (see the header's deviation list — the halo is stamped, so a blur
  // radius has nothing to act on), and the answer does not change until the
  // next LoadText.
  size_t ignored_halo_blur() const;
  void ResetDiagnostics();

 protected:
  Status StyleFeature(const VectorFeature& f, const StyleContext& ctx,
                     const StylePass& pass,
                     std::vector<StyleResult>* out) override;

  // `circle` layers become a generated ellipse display list, keyed
  // "circle:<r_himetric>". A sprite-backed symbol has no display list at all
  // and resolves through Pixmap() instead.
  bool LoadSymbol(const std::string& symbol_id, VectorSymbol* out) override;

 public:
  // The raster half of the library: "sprite:<name>" resolves out of the loaded
  // sheet. Public because ISymbolLibrary is, and the renderer calls it through
  // that interface after Symbol() comes back empty.
  const SymbolPixmap* Pixmap(const std::string& symbol_id) override;

  // Sprite ids short-circuit to nullptr here instead of falling through to
  // the base class, whose miss path would count every drawn icon as an
  // unresolved symbol. Everything else is the base class's answer.
  const VectorSymbol* Symbol(const std::string& symbol_id) override;

 private:
  // The loaded sheet, looked up by BARE sprite name. nullptr when no sheet
  // loaded or the sheet has no such sprite.
  const SymbolPixmap* SpriteTile(const std::string& name) const;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fv

#endif  // FV_OSM_STYLE_H_
