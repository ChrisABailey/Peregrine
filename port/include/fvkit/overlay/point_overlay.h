// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/overlay/point_overlay.h — the first C++ FILE overlay (A6).
//
// A named set of geographic points read from a SQLite document, drawn as plain
// geometric shapes. It exists to be the C++ half of A6's acceptance test: the
// grid overlay (A1) is static, draws no document and answers no pick, so until
// now nothing in C++ exercised `Persistence`, `HitTest` or `ContextMenu` outside
// their own unit tests. This does all three, and it is deliberately the SMALLEST
// thing that can: no symbology, no style engine, no vector seam.
//
// WHY SQLITE AND NOT A TEXT FILE. The point of a file overlay is that the
// document is a real artefact with a schema somebody else can write — and the
// port already links SQLite for the catalog (L2) and the tile store (L5), so
// this adds no dependency and gets a schema, a query language and atomic writes
// for free. `.fvpoints` IS a SQLite database; `sqlite3 x.fvpoints "select ..."`
// is a legitimate way to author one, which is what makes evolving the dataset
// (Chris's stated next step) a matter of INSERTs rather than a new parser.
//
// WHAT IS PICK-TESTABLE HERE, and it is the reason the schema carries more than
// a position: `category` and `elevation_ft` ride into `HitItem::hint`, so a
// hover and a right-click can be checked to be reporting the RIGHT point rather
// than merely a point. The sample document (`WriteSampleFile`) plants two pairs
// that overlap within the default 8 px tolerance, because a pick layer that
// aggregates (A5) is only interesting where more than one thing answers.
//
// SCHEMA 2 — THE ARTWORK LIVES IN THE DOCUMENT. A point may carry a
// `symbol_id` into a second table whose rows hold a PNG as a BLOB, so a
// `.fvpoints` file is SELF-CONTAINED: it opens on a machine that has never
// heard of the icon set it was authored against, which a path into somebody's
// symbol directory would not. The table is separate for the reason Chris asked
// for it — MANY POINTS, ONE SYMBOL. Three forts share one `castle` row and pay
// for one copy of it, one decode and one cached tile.
//
// It is also a PALETTE rather than a projection of the points: a symbol nothing
// references is kept, saved and handed back, which is what lets an editor offer
// the set the author assembled before any point wears it.
//
// A SYMBOL POINT IS A BADGE. Icon sets are authored as black artwork on
// transparency (maki, the set the sample uses, is exactly that), so a bare tile
// is invisible over a dark chart and throws away the `color` column. The stamp
// is therefore the point's own SHAPE filled in its own colour, with the tile
// centred on top — the badge every web map draws a POI as. Selection stays what
// it always was (the edge one size up), `color` still identifies the point, and
// a document that wants the bare tile says so by making the colour transparent
// (alpha 0), which suppresses the badge and its edge.
//
// A schema-1 document still opens, columns and all — see `ReadFile`.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/app/capabilities.h"
#include "fvkit/canvas/canvas.h"
#include "fvkit/canvas/geo_draw.h"
#include "fvkit/symbol/builtin.h"
#include "fvkit/geo.h"
#include "fvkit/overlay/overlay.h"
#include "fvkit/proj.h"

namespace fv {

// The document's own artwork, as a symbol library. Private to the
// implementation — an overlay's palette is not a library anyone else composes
// with yet — so it is a pointer here and a class in point_overlay.cpp.
class EmbeddedSymbolLibrary;

// How a point is drawn.
//
// It used to say here that a shape is three lines of canvas calls and needs no
// library: that was A6, when there WAS no library. G2 authored these same six
// into BuiltinSymbolLibrary (for this overlay, by name) and G3 gave an overlay
// a way to stamp one, so a shape is now a symbol id and the switch statement
// that drew them is gone. The DPI decision is still open and still deferred —
// GeoDraw::symbol_dpi_scale is where it will land, and this overlay leaves it
// at 1.0 because it does not know the device.
enum class PointShape {
  kCircle,
  kSquare,
  kTriangle,
  kDiamond,
  kCross,
  kStar,
};

// The document's spelling of a shape ("circle", "square", ...). Unknown names
// read back as kCircle: a document written by a newer version must still open.
const char* ToString(PointShape shape);
PointShape PointShapeFromString(const std::string& name);

// "#rrggbb" or "#rrggbbaa". Anything unparseable is the fallback, which is what
// keeps one bad colour cell from failing an otherwise good document.
FvColor PointColorFromString(const std::string& text, FvColor fallback);
std::string PointColorToString(const FvColor& color);

// One row of the `symbols` table: a raster symbol, artwork and all, carried
// INSIDE the document.
//
// The image is a PNG rather than decoded pixels because that is what an author
// has on disk and what `sqlite3 ... readfile()` can insert without a tool of
// ours; it is decoded lazily, once, on the first draw that needs it. A row that
// will not decode is not an error — the points that reference it fall back to
// their `shape`, which is the same "one bad cell must not fail a good document"
// rule the colour parser follows.
struct PointSymbol {
  int64_t id = 0;    // the row's SQLite id; 0 = not yet written
  std::string name;  // the author's own id for it ("harbor"), UNIQUE in the file
  std::vector<unsigned char> image;  // the PNG bytes
  // Tile pixels per nominal pixel, exactly SymbolPixmap::pixel_ratio: artwork
  // drawn at 2x states 2 here and comes out the same SIZE as its 1x twin with
  // more detail in it.
  double pixel_ratio = 1.0;
  // Where the point's position lands in the tile, in TILE pixels with y down.
  // Unset (the usual case, and what a marker wants) = the tile's centre.
  bool has_pivot = false;
  double pivot_x = 0.0;
  double pivot_y = 0.0;
};

struct MapPoint {
  int64_t id = 0;  // the row's SQLite id; 0 = not yet written
  std::string name;
  GeoPoint position;
  PointShape shape = PointShape::kCircle;
  double size_px = 9.0;  // the shape's full width on screen
  FvColor color{200, 40, 40, 255};
  // The `symbols` row this point wears, or 0 for none. A symbol that is not in
  // the table — an id from a document whose palette was edited out from under
  // it — draws as the `shape` alone, so `shape` is never dead weight: it is
  // the badge under the icon, and the whole marker when there is no icon.
  int64_t symbol_id = 0;
  // The two attributes the pick tests read back. They are ordinary columns —
  // nothing in the overlay treats them specially — but they are what makes a
  // hit ATTRIBUTABLE to one point rather than to "something round".
  std::string category;
  double elevation_ft = 0.0;
  std::string remarks;
};

class PointOverlay : public Overlay,
                     public app::Persistence,
                     public app::HitTest,
                     public app::ContextMenu,
                     public app::EditTarget {
 public:
  explicit PointOverlay(std::string name = "Points");
  ~PointOverlay() override;

  // The registered type id, and the document extension it opens.
  static const char kTypeId[];      // "fv.points"
  static const char kExtension[];   // "fvpoints"

  // Capability accessors (R2 — never dynamic_cast).
  app::Persistence* AsPersistence() override { return this; }
  app::HitTest* AsHitTest() override { return this; }
  app::ContextMenu* AsContextMenu() override { return this; }
  app::EditTarget* AsEditTarget() override { return this; }

  // --- drawing ------------------------------------------------------------

  Status OnDraw(const MapProjection& proj, ICanvas& canvas) override;

  // Labels are off by default: a point set is a pick target first, and a screen
  // full of names is the label-collision gap the ledger already carries.
  void SetShowLabels(bool on) { show_labels_ = on; }
  bool show_labels() const { return show_labels_; }

  // --- the document -------------------------------------------------------

  const std::vector<MapPoint>& points() const { return points_; }
  // Replaces the whole set and marks the document dirty.
  void SetPoints(std::vector<MapPoint> points);
  // Appends. An id of 0 gets the next free id, so a point added in the UI is
  // addressable (selection, pick, delete) before it has ever been saved.
  int64_t AddPoint(MapPoint point);
  bool RemovePoint(int64_t id);
  const MapPoint* Find(int64_t id) const;

  // --- the symbol palette -------------------------------------------------

  const std::vector<PointSymbol>& symbols() const { return symbols_; }
  // Replaces the whole palette and marks the document dirty. Points keep their
  // `symbol_id`, so a palette swapped for one with the same ids re-skins the
  // set — which is the cheap version of the style engine this overlay does not
  // have.
  void SetSymbols(std::vector<PointSymbol> symbols);

  // Adds one, or returns the id of the row that already has that NAME — the
  // dedup is by name because that is what makes "many points, one symbol"
  // happen by default rather than by the caller remembering. An id of 0 gets
  // the next free one. Returns 0 only for a symbol with no name.
  int64_t AddSymbol(PointSymbol symbol);

  // The same from a PNG on disk, which is how a palette is assembled from an
  // icon set. `name` empty takes the file's stem, so a directory of maki icons
  // names itself. The bytes are read and embedded here and now: the file is
  // never referenced again, by this overlay or by the document.
  Status AddSymbolFromPngFile(const std::string& path,
                              const std::string& name = std::string(),
                              int64_t* out_id = nullptr);

  bool RemoveSymbol(int64_t id);
  const PointSymbol* FindSymbol(int64_t id) const;
  const PointSymbol* FindSymbolByName(const std::string& name) const;

  // The selected point, drawn highlighted. 0 = nothing selected. Selection is
  // NOT a document change: selecting does not dirty the overlay.
  int64_t selected() const { return selected_; }
  void SetSelected(int64_t id) { selected_ = id; }

  // --- Persistence --------------------------------------------------------

  Status FileNew() override;
  Status FileOpen(const std::string& spec) override;
  Status FileSaveAs(const std::string& spec, int format_index) override;
  bool SupportsRevert() const override { return true; }
  Status Revert(const std::string& spec) override;

  // --- HitTest / ContextMenu ---------------------------------------------

  // `HitItem::feature` is the point's id — a document-scoped handle that
  // survives a redraw, unlike a vector overlay's minted one (A5), because this
  // overlay's features have identity in the file.
  void HitTestPoint(const MapProjection& proj, PixelPoint p,
                    double tolerance_px, std::vector<app::HitItem>& out) override;

  void AppendMenuItems(const MapProjection& proj, PixelPoint p,
                       app::MenuNode& menu) override;

  // --- the sample document ------------------------------------------------

  // Writes an arbitrary starter set (Kiawah Island and Charleston harbour,
  // because that is where the test data is) to `spec`, overwriting any existing
  // rows. Two PAIRS of points are within a few pixels of each other at harbour
  // scale, so the ambiguous-pick policies have something to be ambiguous about.
  //
  // `symbol_dir` is a directory of loose `<name>.png` icons — the port's own
  // sample set is `testdata/GeoSymbol/makiPng` — and each sample point names
  // the one it wears. Empty (or a directory missing an icon) embeds nothing
  // and the points draw as their shapes, which is the schema-1 picture exactly:
  // the artwork is TEST DATA and is not in the repository, so a caller that has
  // none must still get a usable document.
  static Status WriteSampleFile(const std::string& spec,
                                const std::string& symbol_dir = std::string());

  // The rows the sample file contains, without touching a disk. Exposed
  // because the pick tests want the truth to compare against and reading it
  // back out of the file they just wrote would be testing sqlite.
  static std::vector<MapPoint> SamplePoints();

  // The palette those points reference, read from a directory of loose PNGs.
  // Ids are fixed by the sample's own table and NOT by position here, so an
  // icon the directory does not have is simply absent from the result and the
  // points that wanted it keep their shapes rather than wearing somebody
  // else's artwork.
  static std::vector<PointSymbol> SampleSymbols(const std::string& symbol_dir);

 private:
  Status ReadFile(const std::string& spec, std::vector<MapPoint>* out,
                  std::vector<PointSymbol>* out_symbols,
                  std::string* doc_name) const;

  // Which builtin draws a shape, and a per-colour library to draw it from.
  static const char* SymbolIdFor(PointShape shape);
  BuiltinSymbolLibrary* LibraryFor(const FvColor& c);

  // The embedded palette as something GeoDraw can stamp from — an
  // ISymbolLibrary over the BLOBs, decoding each one at most once. Rebuilt
  // lazily after any change to `symbols_`, because the pixmap pointers it
  // hands out must not outlive the bytes behind them.
  EmbeddedSymbolLibrary* SymbolLibrary();
  void InvalidateSymbolLibrary();
  // The tile for a point's symbol, or null when it has none / it will not
  // decode. Also what tells OnDraw the tile's NOMINAL size, which is what
  // `size_px` has to be scaled against.
  const SymbolPixmap* PixmapFor(int64_t symbol_id);

  // Keyed by packed RGBA. Built on demand and kept for the life of the
  // overlay: a document with three colours in it wants three libraries, and a
  // builtin library is thirteen literals with no files behind it.
  std::map<uint32_t, std::unique_ptr<BuiltinSymbolLibrary>> libraries_;
  std::unique_ptr<EmbeddedSymbolLibrary> symbol_library_;

  std::vector<MapPoint> points_;
  std::vector<PointSymbol> symbols_;
  int64_t selected_ = 0;
  int64_t next_id_ = 1;
  int64_t next_symbol_id_ = 1;
  bool show_labels_ = false;
};

}  // namespace fv
