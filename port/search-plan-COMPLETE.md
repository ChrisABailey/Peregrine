# Global search — plan (S1–S4)

Status: **S1–S4 ALL BUILT (2026-08-27 / 08-28 / 08-28 / 08-28).** Designed
2026-08-25 (Chris + session discussion), sequenced in the ledger's §2a AFTER remaining Pippin work.
The plan is finished; read it for the decisions, not for open work.

**What S4 changed about the sketch below.** Three things, and the first is where the road provider
went. **It is `RoadGraphOverlay::AsSearch()`, not a class of its own** — the debug view built the
day before already holds the `.fvroad`, is already in the stack and already packs an arc index into
a `uint64` for its own picks, so a second overlay over the same graph would have been a second
answer to "which graph is this application routing on". The consequence a shell has to know is
that the graph must BE in the stack: PythonView now creates that overlay on the first search and
leaves it NOT VISIBLE, which is decision 5 used exactly as it was written. The provider's own two
notes: **the graph's name table is what makes a global text search affordable** — it holds each
distinct name once, so the text rule runs over thousands of strings and the arc pass that follows
is an integer lookup, and a spatial query (which has no such shortcut) goes through the grid's
`NodesInRect` instead; and **`RoadClass`'s declaration order IS the relevance function**, the
graph's own answer to what `min_zoom` gives tier 2, so a capped "main" is the primary road and not
a service alley. Merging is S3's `MergeFeatureRows`, which matters more here than in the tiles: an
OSM way is split at every junction AND every edge is stored twice, once from each end.

**Second, `kNearest` is exposed by BINDING the enum, which is all it ever needed** (decision 6 said
so). `pyfvw.app` now carries `SearchOrder`, `SearchQuery`, `SearchResult`, `SearchSession`,
`text_match_quality`, `search_distance_meters` and a `CancelFlag` — a `std::atomic<bool>` with a
Python face, because the seam's one async behaviour needs a flag Python can raise. The session
releases the GIL for the walk. **`Overlay.search` joined the trampoline's capability list**, so
"a capability is a method you defined" now covers all seven.

**Third, a shell can only search what is in the stack**, and PythonView had two searchable things
that were not overlays: the chart and the graph. Both are now wrapped and held invisible — which
is why `VectorMapOverlay` is bound as `pyfvw.overlay.VectorMapOverlay`. The other shell-level
finding: **a search box wants the VIEW as its default scope**, because a pack without S3's index
answers a global text query with nothing at all, and "nothing found" is a bad way to learn that.
The box says so in its status line when it happens.

**What S3 changed about the sketch below**, so S4 reads the built thing rather than the design.
The tier-2 path is a NEW SEAM METHOD, not OSM knowledge inside the overlay:
`IVectorSource::HasNameIndex()` + `SearchNames(VectorNameQuery, vector<VectorNameHit>*)`,
kUnsupported by default, so ENC and DNC can grow gazetteers later without the overlay changing.
`VectorMapOverlay` asks it when the query carries TEXT and falls back to the tile scan otherwise
(or when the index fails); a spatial-only query still reads tiles, because "what is here" is a
question about the tiles. **The index is a CANDIDATE GENERATOR and never the judge**: every row it
returns is re-tested with `app::TextMatchQuality`, since FTS5's word boundaries and folding are not
the port's. Three things the sketch did not have. **(1) The builder does not read tiles itself** —
it asks `VectorMapOverlay::ScanRows` (tier 1, with the index switched off) window by window, so
the label chain, "unnamed geometry is never a row" and the merge are decided ONCE for both tiers;
the merge moved to `fvkit/vector/feature_rows.h` for exactly that reason, with a lat-sweep so a
whole-pyramid merge is not quadratic. **(2) `min_zoom` is the tier-2 relevance function** — every
level is scanned, shallowest first, and a name keeps the shallowest it survived to; `ORDER BY
min_zoom` before the cap is what makes "kiawah" limited to one row answer *Kiawah Island* rather
than an arbitrary street. **(3) FTS5 is an optimisation, not a requirement**: `search_names` is a
plain table and a reader without the module (or with a dropped mirror) scans it and applies the
port's rule in C++ — same answers, more work. Two things learned from the data: a `FeatureRef` is
NOT durable (`tile` is a per-open index), so the index stores z/x/y + layer name and the source
interns them back on the way out; and **a pack being READ cannot be written to** — a stepped
SQLite statement holds a read transaction and the write fails with `SQLITE_IOERR`, not `BUSY`
(`MbtilesFile::ReadTile` now resets, and the builder closes the pack before writing).

**What S2 changed about the sketch below**, so S3 reads the built thing rather than the design.
`VectorMapOverlay` (`fvkit/overlay/vector_map_overlay.h`) is generic over `IVectorSource`, not
over OSM: the label field is a KNOB (`SetLabelTags`, default `{name, name:latin, name:en}` —
the delivered Kiawah cut carries only `name:latin`), so ENC's `OBJNAM` and DNC's `nam` are the
same class with a different list rather than three providers. Tier 1 passes NO SCALE by default
(`SetSearchScaleDenominator(0)`), which is how the source's own tile budget stays the single
number that owns how much pyramid a search reads. Two things the sketch did not have and tier 1
does not work without: **unnamed geometry is never a result**, spatial-only queries included
(most of a tile has no name and a blank row is not a choice a user can make), and **pieces of one
named thing are merged into one row** (`SetMergeGapMeters`, default 100 m) — a road is cut at
every tile seam and at every junction where a tag changed, so the un-merged answer to "Ruddy
Turnstone" is a dozen identical rows. A merged row's bounds is the union, its anchor is the
biggest piece's own label point (never the union's centre, which can be off the road), and its
feature id is the first piece met.

**What S1 changed about the sketch below**, so S2 reads the built thing rather than the design:
the `SearchQuery` sketch grew `SearchOrder order = kAuto` (the session resolves it — kBestMatch
with text, kNearest without) and `max_results = 0` means no cap; `TextMatchQuality` /
`SearchTextAccepts` / `SearchAreaAccepts` are SHARED helpers in the header, so a provider decides
which string it matches and never what matching means; `SearchDistanceMeters` is road_snap.h's
metre; and `SearchSession` takes a `const OverlayManager&` and no shell at all.

## The ask

Any data source — a map type or an overlay type — can expose search through one common
architecture. Two query surfaces, independently optional and combinable:

* **spatial** — a bounding region (geographic axis-aligned box, or point + radius): "what is in
  this area", the geo-space sibling of pick/snap;
* **text** — match on the source's PRIMARY LABEL without the caller knowing the field name.
  Searching "Ruddy Turnstone" finds the road in the OSM tiles AND the `.fvpoints` point of that
  name, and the aggregator never learns that one source spells its label `name`, another
  `label`, ENC `OBJNAM`. Whoever implements a source decides where its text search looks.

Later (not v1): ordering variants ("order by distance"), attribute filters.

## Decisions already made — do not re-litigate

1. **Search is a NEW capability, not a flavour of HitTest.** Pick is pixel-space (needs a
   projection and a px tolerance), per-frame cheap, and visible-only. Search is geo-space,
   on-demand, and must find things off-screen and in hidden overlays. Same *pattern* as pick
   (opt-in capability + aggregating session + caller-chosen ordering), different contract.

2. **The vector map becomes an OVERLAY (`VectorMapOverlay`), so there is ONE discovery path.**
   Chris's call (2026-08-25): rather than a second registry of `SearchProvider`s for
   non-overlay map sources, wrap `IVectorSource` in an overlay. This is the ledger's
   "draw OSM over DTED hillshade" layering feature arriving early, and it collapses the
   provenance question (below). The wrapper initially implements ONLY `AsSearch()` — draw
   support (owning a `VectorScene`, rebuilding on `CanServe` failure) can land later without
   touching the search seam. Desktop later gets HitTest/ContextMenu/SnapTo on the base map
   through the same aggregation for free.

3. **`SearchResult` provenance is flat: `Overlay* + uint64_t feature` — `HitItem`'s shape.**
   No `FeatureRef` on the result, no variant, nothing for pyfvw to regret. The 128-bit
   `FeatureRef` → 64-bit feature mapping is PRIVATE to `VectorMapOverlay`, and is already a
   solved problem: `fvkit/vector/pick.h`'s `PickIndex` mints uint64 ids for vector primitives
   and `HitItem::feature` was sized for them. Mint via a kept table of returned refs, or pack
   layer/tile/feature (fits for a single-source overlay); implementation's choice.

4. **Field-name knowledge lives entirely in each provider.** `PointOverlay` matches `name`
   falling back to `category` (the same chain `SnapToPoint` uses for its description);
   `RouteOverlay` matches waypoint names and the route name; the OSM provider knows
   OpenMapTiles puts labels in the `name`/`name:latin` tags of `place`, `poi`, `water_name`,
   `transportation_name`. The session sees only `title`.

5. **Search ignores visibility by default** (`bool visible_only = false` on the query).
   "Where is X" is a legitimate question about a hidden layer, and the answer ("in Points,
   currently hidden") is useful. This is the opposite of pick's rule, deliberately — and it is
   also what lets Pippin hold a not-visible `VectorMapOverlay` purely as the search/identify
   provider while `PPMap` keeps drawing the base its own way (P18's two-layer economy is
   untouched; the "which band does a base-map overlay render in" question belongs to the
   layering feature, not to search).

6. **Ordering belongs to the SESSION, never the providers** — only the aggregator sees all
   streams, and N implementations must not each compute distance their own way. Two orderings:
   `kBestMatch` (match quality, then distance from `near`, then provider order as the stable
   tie-break — `PickSession::Gather`'s trick) default when text is set; `kNearest` (session
   computes distance from `result.position`) default for spatial-only. "Order by" later is one
   more enum value, NOT a query grammar. The combinable filters ARE the optional fields on
   `SearchQuery`.

7. **Synchronous but cancellable.** Providers take an `std::atomic<bool>& cancel`, poll it
   between units of work, may return partial results. Threading is the shell's problem
   (Pippin's render-queue discipline; a desktop worker thread). No async streaming in the core
   API — that is the "too complicated to implement on OSM" trap. Cancellation-on-keystroke is
   the one async behaviour an incremental search box needs.

8. **SpatiaLite: take the lessons, not the library.** Its R-tree the tile pyramid already
   beats; its geometry predicates are overkill for bbox/radius; it is a real dependency with a
   real iOS build cost. Plain SQLite + FTS5 (already compiled in) covers the actual need,
   which is text lookup. The geodatabase idea that DOES pay off is the gazetteer-table schema
   of S3's sidecar index — 40 lines of SQL, not a library.

## The seam (sketch — S1 refines)

```cpp
// fvkit/app/search.h
struct SearchQuery {
  std::optional<GeoRect> area;      // spatial filter
  std::optional<GeoPoint> near;     // distance origin; with radius_m, a cut
  double radius_m = 0.0;            // 0 with `near` = no cut, ordering only
  std::string text;                 // empty = spatial-only. Contract: case-
                                    // insensitive token-prefix ("rud tur"
                                    // finds Ruddy Turnstone)
  bool visible_only = false;
  size_t max_results = 50;          // per provider; session merges
};

struct SearchResult {
  std::string title;                // the primary label, provider's choice
  std::string detail;               // "road · residential", "point · restaurant"
  GeoPoint position;                // representative point (label anchor)
  GeoRect bounds;                   // what "go there" frames; degenerate for a point
  int match_quality = 0;            // 0 exact, 1 prefix, 2 token/substring
  Overlay* overlay = nullptr;       // provenance, HitItem-style
  uint64_t feature = 0;
};

class SearchProvider {
 public:
  virtual ~SearchProvider() = default;
  virtual void Search(const SearchQuery& q, const std::atomic<bool>& cancel,
                      std::vector<SearchResult>& out) = 0;  // appends
};
// Overlay grows: virtual SearchProvider* AsSearch() { return nullptr; }  (R2)
// SearchSession (fvkit/app/) walks OverlayManager — the ONLY discovery path.
```

Radius handling: the session converts point+radius to a bbox once for the coarse filter and
keeps the pair for the exact cut and ordering; providers only ever see `area` (+ `near` for
their own relevance use if they want it).

## The OSM provider — two tiers

**Tier 1 — area-constrained, no index.** Query has an area: pick a zoom, decode the tile
range, walk features, match name tags. Reuses the tile-cap discipline
(`SetMaxTilesPerQuery`-style: step the zoom coarser until the range fits). The pyramid's
pre-generalization is a relevance function FOR FREE: searching a whole state at the zoom that
fits the budget means the tiles only contain cities and major roads — you find "Charleston",
not every Charleston Ct; searching a harbour at z14 finds the POIs. (Nominatim buys importance
ranking with enormous effort; the pyramid gives a crude, honest version by construction.)

**Tier 2 — global text search, index built at STAGING time.** MBTiles is just SQLite and extra
tables are legal and invisible to every other reader, so the index lives INSIDE the .mbtiles:
a `search_names` table (FTS5): `name, layer, class, lon, lat, minzoom` (+ bbox for
lines/areas). Populate once by walking the pyramid — z14 for POIs/roads, lower zooms for
places — dedup by (name, class, ~position). Build it in `stage_data.py` post-processing or a
small `port/tools/` C++ tool over `MbtilesFile` + `fv_mvt` (staying independent of the cutter;
the tilemaker tree exists if emitting at cut time is ever wanted). Runtime = one
`SELECT ... MATCH ?`. A file WITHOUT the table degrades to tier 1 (area-required search only).

## The cheap providers

* **PointOverlay / RouteOverlay** — linear scan of the in-memory document, ten lines each.
  `.fvpoints` is SQLite, so a huge file could gain a `COLLATE NOCASE` index later; nothing
  today needs it.
* **Road network (`.fvroad`)** — names + spatial snap index already there (P11 proves the
  path). Answers "Ruddy Turnstone (road)" instantly where a road pack exists, and it is the
  ROUTABLE answer — complementary to the tile hit, not redundant. **Built as
  `RoadGraphOverlay::AsSearch()`** (S4): the debug view already owns the graph.

## Explicitly deferred

* Attribute/category filtering ("all restaurants") — `detail` gives a client-side filter;
  a real cross-product taxonomy is a separate design.
* Fuzzy matching / transliteration — FTS5 prefix first; spellfix never.
* Cross-provider dedup (the road in the tiles vs. the same road in `.fvroad`) — show both
  with distinct `detail` lines; dedup is a heuristic swamp and both answers are true.

## Sessions

| # | Session | What it builds | Done when |
|---|---------|----------------|-----------|
| S1 ✅ | The seam (**BUILT 2026-08-27**, 34 gtests) | `fvkit/app/search.h` (`SearchQuery`/`SearchResult`/`SearchProvider`), `Overlay::AsSearch()`, `SearchSession` over `OverlayManager`, ordering rules; `PointOverlay` + `RouteOverlay` providers | gtests: text/area/radius/combined queries across a two-overlay stack, ordering + tie-break pinned, hidden-overlay + `visible_only` behaviour pinned |
| S2 ✅ | The vector map searches (**BUILT 2026-08-28**, 26 gtests) | `VectorMapOverlay` wrapping an `IVectorSource`, `AsSearch()` only; tier-1 area scan under the source's own tile budget; the ref↔feature mint (kept table, ids from 1) + `DescribeFeature` | done: 21 fvkit gtests over a fake source, 5 in `port/Osm/test/osm_search_test.cpp` over the real Kiawah pyramid — the road and the `.fvpoints` point through one `SearchSession`, and the budget stepping z14→z13 (a fifth of the features, three quarters of the names) |
| S3 ✅ | Global text (**BUILT 2026-08-28**, 29 gtests) | `IVectorSource::HasNameIndex`/`SearchNames`; `search_names` + `search_names_fts` + `search_names_meta` inside the .mbtiles (`port/Osm/fv_osm_name_index.*`, built by `osm::BuildNameIndex` and the `fvnames` CLI); tier-2 path in `VectorMapOverlay`; the merge shared as `fvkit/vector/feature_rows.h` | done: "ruddy turnstone" over the whole Kiawah pack with NO area and no tile read; the two tiers agree over a viewport, and over the whole island the index holds the z14 POIs a budget-capped scan cannot afford; a capped "kiawah" answers *Kiawah Island*; a pack with the FTS mirror dropped still answers; a pack with no index still degrades to area-only |
| S4 ✅ | Wire-up + extras (**BUILT 2026-08-28**, 16 gtests + 7 pytest) | `RoadGraphOverlay::AsSearch()` over `.fvroad`; `pyfvw.app` search bindings incl. `SearchOrder.NEAREST` and `CancelFlag`; `Overlay.search` in the Python trampoline; `pyfvw.overlay.VectorMapOverlay`; PythonView's **Find…** box (Ctrl-F) with the chart and the graph held in the stack, invisible, purely to be asked | done: the selftest opens the box over the Kiawah OSM series, gets 200 rows for the view and 4 for "ruddy" — the road from the chart AND from the graph, both true — and frames the first |

Independent value per session; stop anywhere.
