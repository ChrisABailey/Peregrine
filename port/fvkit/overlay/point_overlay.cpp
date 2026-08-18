// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// PointOverlay — see fvkit/overlay/point_overlay.h.

#include "fvkit/overlay/point_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include "fvkit/detail/sqlite.h"
#include "fvkit/tools/png_io.h"

namespace fv {

namespace {

// One file, one schema. `meta` carries the document's own name so an overlay
// can rename itself to what the author called the set rather than to a path.
//
// SCHEMA 2 adds `symbols` and `points.symbol_id`. The version is REPLACEd
// rather than IGNOREd on write because a save rewrites the whole document —
// a schema-1 file saved by this build IS a schema-2 file, and leaving the row
// saying 1 would make the number a lie the next reader believes.
const char kSchema[] =
    "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT);"
    "INSERT OR REPLACE INTO meta VALUES('schema_version','2');"
    "CREATE TABLE IF NOT EXISTS points("
    "  id INTEGER PRIMARY KEY,"
    "  name TEXT NOT NULL DEFAULT '',"
    "  lat REAL NOT NULL, lon REAL NOT NULL,"
    "  shape TEXT NOT NULL DEFAULT 'circle',"
    "  size_px REAL NOT NULL DEFAULT 9,"
    "  color TEXT NOT NULL DEFAULT '#c82828',"
    "  category TEXT NOT NULL DEFAULT '',"
    "  elevation_ft REAL NOT NULL DEFAULT 0,"
    "  remarks TEXT NOT NULL DEFAULT '',"
    "  symbol_id INTEGER NOT NULL DEFAULT 0);"
    // The artwork, once per symbol however many points wear it. `name` is
    // UNIQUE so a palette assembled twice from the same icon set converges
    // rather than doubling, and it is the handle a human writes INSERTs
    // against; `image` is the file's own bytes, undecoded.
    "CREATE TABLE IF NOT EXISTS symbols("
    "  id INTEGER PRIMARY KEY,"
    "  name TEXT NOT NULL UNIQUE,"
    "  format TEXT NOT NULL DEFAULT 'png',"
    "  pixel_ratio REAL NOT NULL DEFAULT 1,"
    "  pivot_x REAL, pivot_y REAL,"  // NULL = the tile's centre
    "  image BLOB NOT NULL);";

// Brings a schema-1 `points` table up to 2. `CREATE TABLE IF NOT EXISTS` does
// nothing to a table that is already there, so an old document reopened for
// saving would otherwise still have no `symbol_id` and the INSERT below would
// fail on a column that the schema string claims exists.
Status AddSymbolIdColumnIfMissing(detail::SqliteDb& db) {
  detail::SqliteStmt q;
  Status s = q.Prepare(db, "SELECT symbol_id FROM points LIMIT 1");
  if (s.ok()) return Status::Ok();
  return db.Exec("ALTER TABLE points ADD COLUMN symbol_id INTEGER NOT NULL"
                 " DEFAULT 0;");
}

// How much of the badge the icon is allowed to cover. A square tile inscribed
// in a circle of diameter d has a side of d/sqrt(2) = 0.707d; the icon sets
// this is aimed at carry a margin of their own inside the tile, and the badge
// may be a diamond (whose inscribed square is smaller still), so it is pulled
// in to 0.62 — enough that a diamond badge still reads as a diamond.
constexpr double kIconFractionOfBadge = 0.62;

int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// A ring for the shape, centred on (cx, cy), `size` wide overall.
std::vector<PixelPoint> ShapeRing(PointShape shape, int cx, int cy, double size) {
  const double r = size / 2.0;
  auto at = [&](double dx, double dy) {
    return PixelPoint{cx + (int)std::lround(dx), cy + (int)std::lround(dy)};
  };
  switch (shape) {
    case PointShape::kSquare:
      return {at(-r, -r), at(r, -r), at(r, r), at(-r, r)};
    case PointShape::kTriangle:
      // Point up, on the same circumscribed circle as the others so a triangle
      // and a diamond of one `size_px` read as the same size.
      return {at(0, -r), at(r * 0.866, r * 0.5), at(-r * 0.866, r * 0.5)};
    case PointShape::kDiamond:
      return {at(0, -r), at(r, 0), at(0, r), at(-r, 0)};
    case PointShape::kStar: {
      std::vector<PixelPoint> ring;
      for (int i = 0; i < 10; ++i) {
        // Start at the top and alternate outer/inner vertices.
        const double a = -M_PI / 2.0 + i * M_PI / 5.0;
        const double rr = (i % 2 == 0) ? r : r * 0.42;
        ring.push_back(at(rr * std::cos(a), rr * std::sin(a)));
      }
      return ring;
    }
    default:
      return {};  // circle and cross are not rings
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// EmbeddedSymbolLibrary — the document's own artwork, as a symbol library
// ---------------------------------------------------------------------------
//
// PngSymbolLibrary does this for a directory and a sprite sheet; this is the
// third form, a BLOB already in memory. It is not a fourth Open* on that class
// because the two have nothing in common but the decoder: there is no
// cataloguing pass, no @2x pairing and no file to fail on — the bytes are
// simply here, and every one of them is somebody's row.
//
// LAZY, and for the reason the ledger keeps repeating: a palette of two
// hundred icons in a document where six points are on screen must cost six
// decodes. Ids are the ROW's, spelled decimal, not the row's name: a name is
// the human's handle and is free to change, an id is what a point references.
class EmbeddedSymbolLibrary : public ISymbolLibrary {
 public:
  // The symbol vector is BORROWED and must outlive the library, which is what
  // PointOverlay::InvalidateSymbolLibrary exists to guarantee.
  explicit EmbeddedSymbolLibrary(const std::vector<PointSymbol>* symbols)
      : symbols_(symbols) {}

  static std::string IdFor(int64_t symbol_id) {
    return std::to_string(symbol_id);
  }

  const VectorSymbol* Symbol(const std::string& /*symbol_id*/) override {
    return nullptr;  // artwork only; there are no display lists in a PNG
  }

  const SymbolPixmap* Pixmap(const std::string& symbol_id) override {
    auto hit = cache_.find(symbol_id);
    if (hit != cache_.end())
      return hit->second.tile.Empty() ? nullptr : &hit->second;

    const PointSymbol* row = nullptr;
    for (const PointSymbol& s : *symbols_)
      if (IdFor(s.id) == symbol_id) {
        row = &s;
        break;
      }
    if (row == nullptr) return nullptr;

    SymbolPixmap sym;
    // A row that will not decode is cached AS AN EMPTY TILE rather than not
    // cached at all — the same call PngSymbolLibrary makes, for the same
    // reason: otherwise every frame re-runs libpng over the same bad blob.
    if (!row->image.empty() &&
        DecodePng(row->image.data(), row->image.size(), &sym.tile).ok()) {
      sym.pixel_ratio = row->pixel_ratio > 0.0 ? row->pixel_ratio : 1.0;
      if (row->has_pivot) {
        sym.pivot_x = row->pivot_x;
        sym.pivot_y = row->pivot_y;
      } else {
        sym.pivot_x = sym.tile.Width() / 2.0;
        sym.pivot_y = sym.tile.Height() / 2.0;
      }
    }
    auto ins = cache_.emplace(symbol_id, std::move(sym));
    return ins.first->second.tile.Empty() ? nullptr : &ins.first->second;
  }

 private:
  const std::vector<PointSymbol>* symbols_;
  std::map<std::string, SymbolPixmap> cache_;
};

const char* ToString(PointShape shape) {
  switch (shape) {
    case PointShape::kSquare: return "square";
    case PointShape::kTriangle: return "triangle";
    case PointShape::kDiamond: return "diamond";
    case PointShape::kCross: return "cross";
    case PointShape::kStar: return "star";
    case PointShape::kCircle: break;
  }
  return "circle";
}

PointShape PointShapeFromString(const std::string& name) {
  if (name == "square") return PointShape::kSquare;
  if (name == "triangle") return PointShape::kTriangle;
  if (name == "diamond") return PointShape::kDiamond;
  if (name == "cross") return PointShape::kCross;
  if (name == "star") return PointShape::kStar;
  return PointShape::kCircle;
}

FvColor PointColorFromString(const std::string& text, FvColor fallback) {
  if (text.size() != 7 && text.size() != 9) return fallback;
  if (text[0] != '#') return fallback;
  int v[8];
  for (size_t i = 1; i < text.size(); ++i) {
    v[i - 1] = HexDigit(text[i]);
    if (v[i - 1] < 0) return fallback;
  }
  FvColor c;
  c.r = (unsigned char)(v[0] * 16 + v[1]);
  c.g = (unsigned char)(v[2] * 16 + v[3]);
  c.b = (unsigned char)(v[4] * 16 + v[5]);
  c.a = text.size() == 9 ? (unsigned char)(v[6] * 16 + v[7]) : 255;
  return c;
}

std::string PointColorToString(const FvColor& color) {
  char buf[16];
  if (color.a == 255)
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", color.r, color.g, color.b);
  else
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", color.r, color.g,
                  color.b, color.a);
  return buf;
}

const char PointOverlay::kTypeId[] = "fv.points";
const char PointOverlay::kExtension[] = "fvpoints";

PointOverlay::PointOverlay(std::string name) : Overlay(std::move(name)) {}
PointOverlay::~PointOverlay() = default;

// ---------------------------------------------------------------------------
// The document
// ---------------------------------------------------------------------------

void PointOverlay::SetPoints(std::vector<MapPoint> points) {
  points_ = std::move(points);
  next_id_ = 1;
  for (const MapPoint& p : points_) next_id_ = std::max(next_id_, p.id + 1);
  selected_ = 0;
  set_dirty(true);
}

int64_t PointOverlay::AddPoint(MapPoint point) {
  if (point.id <= 0) point.id = next_id_;
  next_id_ = std::max(next_id_, point.id + 1);
  points_.push_back(std::move(point));
  set_dirty(true);
  return points_.back().id;
}

bool PointOverlay::RemovePoint(int64_t id) {
  auto it = std::find_if(points_.begin(), points_.end(),
                         [id](const MapPoint& p) { return p.id == id; });
  if (it == points_.end()) return false;
  points_.erase(it);
  if (selected_ == id) selected_ = 0;
  set_dirty(true);
  return true;
}

const MapPoint* PointOverlay::Find(int64_t id) const {
  for (const MapPoint& p : points_)
    if (p.id == id) return &p;
  return nullptr;
}

// ---------------------------------------------------------------------------
// The symbol palette
// ---------------------------------------------------------------------------

void PointOverlay::SetSymbols(std::vector<PointSymbol> symbols) {
  symbols_ = std::move(symbols);
  next_symbol_id_ = 1;
  for (const PointSymbol& s : symbols_)
    next_symbol_id_ = std::max(next_symbol_id_, s.id + 1);
  InvalidateSymbolLibrary();
  set_dirty(true);
}

int64_t PointOverlay::AddSymbol(PointSymbol symbol) {
  if (symbol.name.empty()) return 0;
  // BY NAME, deliberately: "many points, one symbol" is the whole reason the
  // table is separate, and a caller adding `harbor` for the fifth point that
  // wants it should get the row it already has rather than a fifth copy of
  // the same 800 bytes.
  if (const PointSymbol* existing = FindSymbolByName(symbol.name))
    return existing->id;
  if (symbol.id <= 0) symbol.id = next_symbol_id_;
  next_symbol_id_ = std::max(next_symbol_id_, symbol.id + 1);
  symbols_.push_back(std::move(symbol));
  InvalidateSymbolLibrary();
  set_dirty(true);
  return symbols_.back().id;
}

Status PointOverlay::AddSymbolFromPngFile(const std::string& path,
                                          const std::string& name,
                                          int64_t* out_id) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::Error(kNotFound, "cannot read symbol " + path);
  PointSymbol sym;
  sym.image.assign(std::istreambuf_iterator<char>(in),
                   std::istreambuf_iterator<char>());
  if (in.bad()) return Status::Error(kIoError, "read error on " + path);
  if (sym.image.empty())
    return Status::Error(kIoError, "empty symbol file " + path);
  sym.name = name.empty() ? std::filesystem::path(path).stem().string() : name;
  // `<name>@2x.png` states its own ratio, the same spelling PngSymbolLibrary
  // reads a loose directory with — and the `@2x` comes OFF the name, so the
  // 1x and 2x forms of one icon are two rows a document can choose between
  // rather than one name that means two things.
  const std::string kRetina = "@2x";
  if (sym.name.size() > kRetina.size() &&
      sym.name.compare(sym.name.size() - kRetina.size(), kRetina.size(),
                       kRetina) == 0) {
    sym.name.resize(sym.name.size() - kRetina.size());
    sym.pixel_ratio = 2.0;
  }
  const int64_t id = AddSymbol(std::move(sym));
  if (out_id != nullptr) *out_id = id;
  return Status::Ok();
}

bool PointOverlay::RemoveSymbol(int64_t id) {
  auto it = std::find_if(symbols_.begin(), symbols_.end(),
                         [id](const PointSymbol& s) { return s.id == id; });
  if (it == symbols_.end()) return false;
  symbols_.erase(it);
  // The points that referenced it are NOT rewritten: they fall back to their
  // shape, and putting the row back (an undo, a re-import) makes them wear it
  // again. Clearing the ids would make removing a symbol destroy information
  // the palette does not own.
  InvalidateSymbolLibrary();
  set_dirty(true);
  return true;
}

const PointSymbol* PointOverlay::FindSymbol(int64_t id) const {
  if (id == 0) return nullptr;
  for (const PointSymbol& s : symbols_)
    if (s.id == id) return &s;
  return nullptr;
}

const PointSymbol* PointOverlay::FindSymbolByName(const std::string& name) const {
  for (const PointSymbol& s : symbols_)
    if (s.name == name) return &s;
  return nullptr;
}

EmbeddedSymbolLibrary* PointOverlay::SymbolLibrary() {
  if (!symbol_library_)
    symbol_library_.reset(new EmbeddedSymbolLibrary(&symbols_));
  return symbol_library_.get();
}

void PointOverlay::InvalidateSymbolLibrary() { symbol_library_.reset(); }

const SymbolPixmap* PointOverlay::PixmapFor(int64_t symbol_id) {
  if (symbol_id == 0) return nullptr;
  return SymbolLibrary()->Pixmap(EmbeddedSymbolLibrary::IdFor(symbol_id));
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// The id of the builtin that draws each shape. The shapes were authored into
// BuiltinSymbolLibrary FOR this overlay (G2), so the mapping is one to one and
// the switch statement that used to draw them is gone.
const char* PointOverlay::SymbolIdFor(PointShape shape) {
  switch (shape) {
    case PointShape::kSquare: return builtin_symbol::kSquare;
    case PointShape::kTriangle: return builtin_symbol::kTriangle;
    case PointShape::kDiamond: return builtin_symbol::kDiamond;
    case PointShape::kCross: return builtin_symbol::kCross;
    case PointShape::kStar: return builtin_symbol::kStar;
    case PointShape::kCircle: break;
  }
  return builtin_symbol::kCircle;
}

BuiltinSymbolLibrary* PointOverlay::LibraryFor(const FvColor& c) {
  // COLOUR IS A LIBRARY SETTING, not a per-draw one — a display list carries
  // its own colours and there is no tint at this seam — so a document with
  // three colours in it wants three libraries. They cost nothing (no files,
  // thirteen literals) and they are cached for the life of the overlay, which
  // is what keeps a pan from re-baking them per frame.
  const uint32_t key = (uint32_t)c.r << 24 | (uint32_t)c.g << 16 |
                       (uint32_t)c.b << 8 | (uint32_t)c.a;
  auto it = libraries_.find(key);
  if (it != libraries_.end()) return it->second.get();
  std::unique_ptr<BuiltinSymbolLibrary> lib(new BuiltinSymbolLibrary);
  lib->SetColor(c);
  BuiltinSymbolLibrary* raw = lib.get();
  libraries_.emplace(key, std::move(lib));
  return raw;
}

Status PointOverlay::OnDraw(const MapProjection& proj, ICanvas& canvas) {
  if (!proj.Ready()) return Status::Error(kInvalidArg, "projection not ready");
  const PixelSize surf = proj.SurfaceSize();

  // G3. Every marker and every name below goes through GeoDraw, which is what
  // this overlay was A6's placeholder for: the six shapes are BuiltinSymbol-
  // Library display lists (they were authored there for exactly this), so they
  // scale and rotate, and the labels get T2's halo and E8's alignment because
  // they are the seam's own LabelStyle.
  //
  // symbol_dpi_scale is left at 1.0: the overlay does not know the device, and
  // the shell that does has no way to tell it yet (the ledger's symbol-DPI
  // item). Setting it here would be guessing.
  GeoDraw draw(proj, &canvas);

  for (const MapPoint& p : points_) {
    double sx = 0, sy = 0;
    if (!proj.GeoToSurface(p.position, &sx, &sy).ok()) continue;
    const double r = p.size_px / 2.0;
    // Cull generously: a shape whose centre is off-screen may still have ink
    // on it, and the canvas clips anyway — this is only to skip the work.
    if (sx < -p.size_px || sy < -p.size_px || sx > surf.width + p.size_px ||
        sy > surf.height + p.size_px)
      continue;

    const bool sel = (p.id != 0 && p.id == selected_);
    const char* id = SymbolIdFor(p.shape);
    // A builtin is authored 9 nominal pixels across, so `size_px` is a scale.
    const double scale = p.size_px / kBuiltinSymbolNominalPx;

    // The document's own artwork, if this point wears any and it decodes.
    const SymbolPixmap* icon = PixmapFor(p.symbol_id);

    // A fully transparent colour means "no badge": the icon stands alone, and
    // so does the edge, which is a ring around nothing without it. There is
    // still an edge when SELECTED, because the highlight below needs a
    // silhouette to be a halo OF, and a bare icon may be nearly all
    // transparent.
    const bool badge = p.color.a != 0;
    const bool edge = badge || sel;

    // G4. SELECTION IS A RENDER STATE, not a colour. The edge is now always
    // black — it is the badge's outline and nothing to do with selection — and
    // the selected marker instead wears GeoDraw's highlight: its own
    // silhouette stamped around it in the selection colour. That keeps the
    // point's own colour, which is what a user identifies it by and what the
    // pre-G4 swap threw away.
    //
    // THE HIGHLIGHT GOES ON THE OUTERMOST STAMP AND ON THAT ONE ONLY. A marker
    // is up to three stamps deep (edge, badge, icon); highlighting all three
    // would draw the badge's glow over the edge and the icon's over the badge,
    // and the marker would read as a set of rings rather than as one selected
    // thing.
    bool highlight_pending = sel;
    auto next_state = [&]() {
      const RenderState st =
          highlight_pending ? RenderState::kHighlighted : RenderState::kNormal;
      highlight_pending = false;
      return st;
    };

    Status s = Status::Ok();
    if (edge) {
      const double edge_px = p.size_px + 2.0;
      draw.SetState(next_state());
      draw.SetSymbols(LibraryFor(FvColor{0, 0, 0, 255}));
      s = draw.DrawSymbolAtPixel(
          sx, sy, id,
          PointSymbolStyle{true, id, 0.0, edge_px / kBuiltinSymbolNominalPx});
      if (!s.ok()) return s;
    }

    if (badge) {
      draw.SetState(next_state());
      draw.SetSymbols(LibraryFor(p.color));
      s = draw.DrawSymbolAtPixel(sx, sy, id,
                                 PointSymbolStyle{true, id, 0.0, scale});
      if (!s.ok()) return s;
    }

    if (icon != nullptr) {
      // The tile goes ON the badge, sized as a fraction of it, so `size_px`
      // keeps meaning one thing — the marker's full width on screen — whether
      // or not there is artwork inside it.
      //
      // The style's `scale` is in NOMINAL pixels: DrawResolvedSymbol divides
      // by the tile's pixel_ratio on the way through, so a 32 px tile that
      // states a ratio of 2 is 16 nominal pixels wide and asks for half the
      // scale a 1x tile of the same file would. Hence the ratio here rather
      // than the raw tile width.
      const double ratio = icon->pixel_ratio > 0.0 ? icon->pixel_ratio : 1.0;
      const double nominal_w = icon->tile.Width() / ratio;
      if (nominal_w > 0.0) {
        const double want_px =
            badge ? p.size_px * kIconFractionOfBadge : p.size_px;
        const std::string icon_id = EmbeddedSymbolLibrary::IdFor(p.symbol_id);
        draw.SetState(next_state());
        draw.SetSymbols(SymbolLibrary());
        s = draw.DrawSymbolAtPixel(
            sx, sy, icon_id,
            PointSymbolStyle{true, icon_id, 0.0, want_px / nominal_w});
        if (!s.ok()) return s;
      }
    }

    if (show_labels_ && !p.name.empty()) {
      // The NAME is not highlighted: the selection belongs to the marker, and
      // a yellow-outlined name over a chart is less legible than the white
      // halo it already has, not more.
      draw.SetState(RenderState::kNormal);
      LabelStyle ls;
      ls.valid = true;
      ls.style.size = 12.0;
      ls.style.color = FvColor{0, 0, 0, 255};
      ls.dx = (int)std::lround(r) + 3;
      ls.dy = -(int)std::lround(r);
      // A white halo, which the hand-rolled version could not have had: a name
      // over a chart is unreadable without one. Labels are still OFF by
      // default — the halo makes a name legible, not un-overlapping, and label
      // collision is still the ledger's open item.
      ls.halo_width = 1.0;
      ls.halo_color = FvColor{255, 255, 255, 255};
      s = draw.DrawLabelAtPixel(sx, sy, p.name, ls);
      if (!s.ok()) return s;
    }
  }
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

Status PointOverlay::FileNew() {
  points_.clear();
  symbols_.clear();
  InvalidateSymbolLibrary();
  selected_ = 0;
  next_id_ = 1;
  next_symbol_id_ = 1;
  // Not dirty: an empty new document has nothing in it to lose. The session
  // still sends it through Save As on the first Save, because it has never
  // been saved (plan §3g).
  set_dirty(false);
  return Status::Ok();
}

Status PointOverlay::ReadFile(const std::string& spec,
                              std::vector<MapPoint>* out,
                              std::vector<PointSymbol>* out_symbols,
                              std::string* doc_name) const {
  detail::SqliteDb db;
  Status s = db.OpenReadOnly(spec);
  if (!s.ok()) return s;

  // Schema 2 first, then schema 1. Two prepares rather than a
  // `PRAGMA table_info` walk: sqlite has already parsed the table to answer
  // the first one, and a failed prepare is the cheapest possible probe.
  // A schema-1 document must keep opening — the port has written them, and a
  // reader that rejected one would make the version number a wall instead of
  // a record.
  bool has_symbols = true;
  detail::SqliteStmt q;
  s = q.Prepare(db,
                "SELECT id, name, lat, lon, shape, size_px, color, category,"
                "       elevation_ft, remarks, symbol_id FROM points"
                " ORDER BY id");
  if (!s.ok()) {
    has_symbols = false;
    s = q.Prepare(db,
                  "SELECT id, name, lat, lon, shape, size_px, color, category,"
                  "       elevation_ft, remarks FROM points ORDER BY id");
  }
  if (!s.ok())
    return Status::Error(kIoError, "not a point document (" + spec + "): " +
                                       s.message);
  Status step;
  while (q.Step(&step)) {
    MapPoint p;
    p.id = q.ColInt64(0);
    p.name = q.ColText(1);
    p.position = GeoPoint{q.ColDouble(2), q.ColDouble(3)};
    p.shape = PointShapeFromString(q.ColText(4));
    p.size_px = q.ColDouble(5);
    if (p.size_px <= 0) p.size_px = 9.0;
    p.color = PointColorFromString(q.ColText(6), FvColor{200, 40, 40, 255});
    p.category = q.ColText(7);
    p.elevation_ft = q.ColDouble(8);
    p.remarks = q.ColText(9);
    if (has_symbols) p.symbol_id = q.ColInt64(10);
    out->push_back(std::move(p));
  }
  if (!step.ok()) return step;
  q.Finalize();

  // The palette. Absent (schema 1) or empty is normal, and so is a row NO
  // point references — a document may carry the icons its author intends to
  // use next.
  detail::SqliteStmt sq;
  if (has_symbols &&
      sq.Prepare(db, "SELECT id, name, pixel_ratio, pivot_x, pivot_y, image"
                     " FROM symbols ORDER BY id")
          .ok()) {
    Status sym_step;
    while (sq.Step(&sym_step)) {
      PointSymbol sym;
      sym.id = sq.ColInt64(0);
      sym.name = sq.ColText(1);
      sym.pixel_ratio = sq.ColDouble(2);
      if (!(sym.pixel_ratio > 0.0)) sym.pixel_ratio = 1.0;
      // A pivot is per-AXIS optional in principle but not in practice: sqlite
      // hands back 0.0 for a NULL, so both are taken only when both are
      // stated. Neither = the tile's centre, which the library supplies.
      if (sq.ColType(3) != SQLITE_NULL && sq.ColType(4) != SQLITE_NULL) {
        sym.has_pivot = true;
        sym.pivot_x = sq.ColDouble(3);
        sym.pivot_y = sq.ColDouble(4);
      }
      const void* blob = sq.ColBlob(5);
      const int n = sq.ColBytes(5);
      if (blob != nullptr && n > 0) {
        const unsigned char* b = static_cast<const unsigned char*>(blob);
        sym.image.assign(b, b + n);
      }
      out_symbols->push_back(std::move(sym));
    }
    if (!sym_step.ok()) return sym_step;
    sq.Finalize();
  }

  detail::SqliteStmt meta;
  if (meta.Prepare(db, "SELECT value FROM meta WHERE key='name'").ok()) {
    if (meta.Step()) *doc_name = meta.ColText(0);
  }
  return Status::Ok();
}

Status PointOverlay::FileOpen(const std::string& spec) {
  std::vector<MapPoint> loaded;
  std::vector<PointSymbol> palette;
  std::string doc_name;
  Status s = ReadFile(spec, &loaded, &palette, &doc_name);
  if (!s.ok()) return s;

  points_ = std::move(loaded);
  symbols_ = std::move(palette);
  InvalidateSymbolLibrary();
  selected_ = 0;
  next_id_ = 1;
  next_symbol_id_ = 1;
  for (const MapPoint& p : points_) next_id_ = std::max(next_id_, p.id + 1);
  for (const PointSymbol& sym : symbols_)
    next_symbol_id_ = std::max(next_symbol_id_, sym.id + 1);
  if (!doc_name.empty()) SetName(doc_name);
  // The session owns file_spec/has_been_saved/dirty after a successful open.
  return Status::Ok();
}

Status PointOverlay::FileSaveAs(const std::string& spec, int format_index) {
  if (format_index != 0)
    return Status::Error(kUnsupported,
                         "point documents have one format (index 0), asked for " +
                             std::to_string(format_index));
  detail::SqliteDb db;
  Status s = db.Open(spec);
  if (!s.ok()) return s;
  s = db.Exec(kSchema);
  if (!s.ok()) return s;
  // Saving OVER a schema-1 document: the CREATEs above left its `points`
  // table alone, so the column the INSERT below binds has to be added.
  s = AddSymbolIdColumnIfMissing(db);
  if (!s.ok()) return s;
  // One transaction, so a failed write leaves the previous document intact
  // rather than half of it.
  s = db.Exec("BEGIN IMMEDIATE; DELETE FROM points; DELETE FROM symbols;");
  if (!s.ok()) return s;

  // The palette goes in FIRST so that a document is never, even mid-
  // transaction, a set of points referencing rows that are not there yet.
  detail::SqliteStmt sym_ins;
  s = sym_ins.Prepare(db,
                      "INSERT INTO symbols(id, name, format, pixel_ratio,"
                      " pivot_x, pivot_y, image) VALUES(?,?,'png',?,?,?,?)");
  if (!s.ok()) {
    db.Exec("ROLLBACK;");
    return s;
  }
  for (const PointSymbol& sym : symbols_) {
    sym_ins.Reset();
    sym_ins.BindInt64(1, sym.id);
    sym_ins.BindText(2, sym.name);
    sym_ins.BindDouble(3, sym.pixel_ratio);
    if (sym.has_pivot) {
      sym_ins.BindDouble(4, sym.pivot_x);
      sym_ins.BindDouble(5, sym.pivot_y);
    }  // else left NULL by Reset(), which is "the tile's centre"
    sym_ins.BindBlob(6, sym.image.data(), sym.image.size());
    Status step;
    sym_ins.Step(&step);
    if (!step.ok()) {
      db.Exec("ROLLBACK;");
      return step;
    }
  }
  sym_ins.Finalize();

  detail::SqliteStmt ins;
  s = ins.Prepare(db,
                  "INSERT INTO points(id, name, lat, lon, shape, size_px,"
                  " color, category, elevation_ft, remarks, symbol_id)"
                  " VALUES(?,?,?,?,?,?,?,?,?,?,?)");
  if (!s.ok()) {
    db.Exec("ROLLBACK;");
    return s;
  }
  for (const MapPoint& p : points_) {
    ins.Reset();
    ins.BindInt64(1, p.id);
    ins.BindText(2, p.name);
    ins.BindDouble(3, p.position.lat);
    ins.BindDouble(4, p.position.lon);
    ins.BindText(5, ToString(p.shape));
    ins.BindDouble(6, p.size_px);
    ins.BindText(7, PointColorToString(p.color));
    ins.BindText(8, p.category);
    ins.BindDouble(9, p.elevation_ft);
    ins.BindText(10, p.remarks);
    ins.BindInt64(11, p.symbol_id);
    Status step;
    ins.Step(&step);
    if (!step.ok()) {
      db.Exec("ROLLBACK;");
      return step;
    }
  }
  ins.Finalize();

  detail::SqliteStmt meta;
  s = meta.Prepare(db, "INSERT OR REPLACE INTO meta VALUES('name', ?)");
  if (s.ok()) {
    meta.BindText(1, Name());
    meta.Step();
    meta.Finalize();
  }
  return db.Exec("COMMIT;");
}

Status PointOverlay::Revert(const std::string& spec) {
  Status s = FileOpen(spec);
  if (!s.ok()) return s;
  set_dirty(false);
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Pick
// ---------------------------------------------------------------------------

void PointOverlay::HitTestPoint(const MapProjection& proj, PixelPoint p,
                                double tolerance_px,
                                std::vector<app::HitItem>& out) {
  if (!proj.Ready()) return;
  for (const MapPoint& mp : points_) {
    double sx = 0, sy = 0;
    if (!proj.GeoToSurface(mp.position, &sx, &sy).ok()) continue;
    const double dx = sx - p.x;
    const double dy = sy - p.y;
    const double d = std::sqrt(dx * dx + dy * dy);
    // The shape's own half-width counts as ink: a click INSIDE a big symbol is
    // on it however far that is from its centre, which is what the user sees.
    if (d > tolerance_px + mp.size_px / 2.0) continue;

    app::HitItem item;
    item.overlay = this;
    item.feature = (uint64_t)mp.id;
    item.distance_px = d;
    item.hint.tool_tip = mp.name;
    // The status line is where the pick tests read the ATTRIBUTES back from,
    // and where a user reads them: name, category, elevation, remarks.
    char elev[32];
    std::snprintf(elev, sizeof(elev), "%.0f ft", mp.elevation_ft);
    std::string status = mp.name;
    if (!mp.category.empty()) status += " (" + mp.category + ")";
    status += " - ";
    status += elev;
    if (!mp.remarks.empty()) status += " - " + mp.remarks;
    item.hint.status = std::move(status);
    item.cursor = app::CursorId::kHand;
    out.push_back(std::move(item));
  }
}

void PointOverlay::AppendMenuItems(const MapProjection& proj, PixelPoint p,
                                   app::MenuNode& menu) {
  std::vector<app::HitItem> hits;
  HitTestPoint(proj, p, 8.0, hits);
  if (hits.empty()) return;
  std::sort(hits.begin(), hits.end(),
            [](const app::HitItem& a, const app::HitItem& b) {
              return a.distance_px < b.distance_px;
            });
  for (const app::HitItem& h : hits) {
    const int64_t id = (int64_t)h.feature;
    app::MenuNode item;
    item.label = h.hint.status.empty() ? h.hint.tool_tip : h.hint.status;
    item.checked = (id == selected_);
    // The action captures `this` and an ID, never a MapPoint* — the vector can
    // reallocate between building the menu and the user picking a row.
    item.action = [this, id] { SetSelected(id); };
    menu.children.push_back(std::move(item));
  }
}

// ---------------------------------------------------------------------------
// The sample document
// ---------------------------------------------------------------------------

namespace {

// Arbitrary, and meant to be replaced: Kiawah Island and Charleston harbour
// are simply where the port's test data already is, so these draw over a real
// chart and a real road graph rather than over nothing. The ELEVATIONS are
// made up, the categories are not a taxonomy, and the whole set exists so
// that a click has something to land on.
//
// ONE TABLE, TWO PRODUCTS. SamplePoints() and SampleSymbols() are both derived
// from this, which is what keeps a point's `symbol_id` and the palette's ids
// from drifting apart: a symbol's id is the position of its FIRST appearance
// in the `icon` column, so an icon named by three rows is one row of the
// palette and three references to it — the sharing Chris asked the separate
// table for, demonstrated by the data rather than only permitted by the
// schema. The three forts are the case to look at.
struct SampleRow {
  int64_t id;
  const char* name;
  double lat, lon;
  PointShape shape;
  double size_px;
  FvColor color;
  const char* icon;  // a maki icon's file stem, "" for a bare shape
  const char* category;
  double elevation_ft;
  const char* remarks;
};

constexpr FvColor kRed{200, 40, 40, 255};
constexpr FvColor kBlue{40, 90, 210, 255};
constexpr FvColor kGreen{30, 140, 70, 255};
constexpr FvColor kAmber{225, 160, 30, 255};
constexpr FvColor kPlum{130, 70, 160, 255};
constexpr FvColor kSlate{70, 90, 110, 255};

const SampleRow kSampleRows[] = {
    // --- Kiawah Island: the demo route's ends and what is around them ------
    {1, "Ruddy Turnstone", 32.6044007, -80.1083007, PointShape::kDiamond, 22,
     kRed, "bicycle", "junction", 12, "west end of the demo route"},
    {2, "Beach Access 12", 32.5957369, -80.1097501, PointShape::kDiamond, 22,
     kRed, "beach", "junction", 8, "east end of the demo route"},
    {3, "Kiawah Island Golf", 32.6084, -80.0876, PointShape::kCircle, 22,
     kGreen, "golf", "landmark", 20, ""},
    {4, "Captain Sams Inlet", 32.6162, -80.1509, PointShape::kTriangle, 22,
     kAmber, "danger", "hazard", 0, "shoaling reported"},
    {5, "Bass Creek Landing", 32.6238, -80.0977, PointShape::kSquare, 22,
     kGreen, "slipway", "landing", 4, ""},
    // --- Charleston harbour -----------------------------------------------
    {6, "Fort Sumter", 32.7522, -79.8747, PointShape::kCircle, 24, kSlate,
     "castle", "landmark", 15, "national monument"},
    {7, "Castle Pinckney", 32.7783, -79.9114, PointShape::kCircle, 22, kSlate,
     "castle", "landmark", 10, ""},
    {8, "Charleston Light", 32.6917, -79.8828, PointShape::kCircle, 22, kAmber,
     "lighthouse", "light", 163, "Sullivans Island"},
    {9, "Fort Moultrie", 32.7594, -79.8577, PointShape::kCircle, 22, kSlate,
     "castle", "landmark", 12, ""},
    {10, "Buoy R2", 32.7128, -79.8917, PointShape::kCircle, 16, kRed, "marker",
     "buoy", 0, "red nun"},
    // The AMBIGUITY the pick policies need. Each of these two sits a few
    // metres from the point above it -- about three pixels at harbour scale
    // -- so a single tap answers with two hits and kAskWhenAmbiguous has a
    // question to ask. Anything further apart and the ambiguous path is
    // untestable through the real pick session.
    {11, "Buoy G1", 32.71275, -79.89162, PointShape::kCircle, 16, kGreen,
     "marker", "buoy", 0, "green can, paired with R2"},
    {12, "Fort Sumter Dock", 32.75213, -79.87462, PointShape::kSquare, 22,
     kBlue, "ferry", "dock", 3, "ferry landing, paired with Fort Sumter"},
    // --- the peninsula, added with the palette (schema 2) ------------------
    //
    // These exist to make the icon set worth having: a dozen points can be
    // told apart by colour alone, thirty cannot, which is the argument for
    // symbology in one screenshot.
    {13, "Charleston City Marina", 32.7815, -79.9497, PointShape::kCircle, 22,
     kBlue, "harbor", "marina", 2, "Ashley River"},
    {14, "White Point Garden", 32.7699, -79.9313, PointShape::kCircle, 22,
     kGreen, "park", "park", 6, "the Battery"},
    {15, "Charleston Museum", 32.7887, -79.9385, PointShape::kCircle, 22,
     kPlum, "museum", "civic", 14, ""},
    {16, "St Philips Church", 32.7810, -79.9298, PointShape::kCircle, 22,
     kPlum, "place-of-worship", "landmark", 18, "steeple, a charted landmark"},
    {17, "Magnolia Cemetery", 32.8188, -79.9425, PointShape::kCircle, 22,
     kSlate, "cemetery", "civic", 9, ""},
    {18, "Charleston Executive", 32.7009, -80.0028, PointShape::kCircle, 24,
     kSlate, "airport", "airfield", 18, "JZI"},
    {19, "MUSC Medical Center", 32.7845, -79.9470, PointShape::kCircle, 22,
     kRed, "hospital", "emergency", 16, ""},
    {20, "Visitor Center", 32.7876, -79.9403, PointShape::kCircle, 22, kBlue,
     "information", "civic", 15, ""},
    {21, "Waterfront Park Pier", 32.7794, -79.9250, PointShape::kCircle, 22,
     kGreen, "viewpoint", "park", 5, "the pineapple fountain pier"},
    {22, "Shem Creek", 32.7935, -79.8850, PointShape::kCircle, 22, kAmber,
     "restaurant-seafood", "waterfront", 3, "shrimp fleet"},
    {23, "Patriots Point", 32.7911, -79.9082, PointShape::kCircle, 24, kSlate,
     "monument", "landmark", 8, "USS Yorktown"},
    {24, "Sullivans Island Beach", 32.7620, -79.8380, PointShape::kCircle, 22,
     kAmber, "swimming", "beach", 4, ""},
    {25, "Beachwalker Park", 32.6089, -80.1580, PointShape::kCircle, 22,
     kGreen, "picnic-site", "park", 5, "Kiawah's west end"},
    {26, "Freshfields Village", 32.6323, -80.1102, PointShape::kCircle, 22,
     kPlum, "cafe", "services", 10, ""},
};

// The distinct icons, in first-appearance order. A symbol's id is its
// position here plus one, which is why this is computed the same way by both
// callers rather than written out twice.
const std::vector<std::string>& SampleIconNames() {
  static const std::vector<std::string>* names = [] {
    auto* v = new std::vector<std::string>;
    for (const SampleRow& r : kSampleRows) {
      if (r.icon[0] == '\0') continue;
      if (std::find(v->begin(), v->end(), r.icon) == v->end())
        v->push_back(r.icon);
    }
    return v;
  }();
  return *names;
}

int64_t SampleIconId(const char* icon) {
  if (icon[0] == '\0') return 0;
  const std::vector<std::string>& names = SampleIconNames();
  auto it = std::find(names.begin(), names.end(), icon);
  if (it == names.end()) return 0;
  return (int64_t)(it - names.begin()) + 1;
}

}  // namespace

std::vector<MapPoint> PointOverlay::SamplePoints() {
  std::vector<MapPoint> pts;
  pts.reserve(sizeof(kSampleRows) / sizeof(kSampleRows[0]));
  for (const SampleRow& r : kSampleRows) {
    MapPoint p;
    p.id = r.id;
    p.name = r.name;
    p.position = GeoPoint{r.lat, r.lon};
    p.shape = r.shape;
    p.size_px = r.size_px;
    p.color = r.color;
    // The id stands whether or not the artwork was available to embed: a
    // document written with no symbol directory draws as shapes and starts
    // wearing icons the moment a palette with these ids is put in it.
    p.symbol_id = SampleIconId(r.icon);
    p.category = r.category;
    p.elevation_ft = r.elevation_ft;
    p.remarks = r.remarks;
    pts.push_back(std::move(p));
  }
  return pts;
}

std::vector<PointSymbol> PointOverlay::SampleSymbols(
    const std::string& symbol_dir) {
  std::vector<PointSymbol> out;
  if (symbol_dir.empty()) return out;
  const std::vector<std::string>& names = SampleIconNames();
  for (size_t i = 0; i < names.size(); ++i) {
    const std::string path =
        (std::filesystem::path(symbol_dir) / (names[i] + ".png")).string();
    // Read directly rather than through AddSymbolFromPngFile: the id is the
    // TABLE's, not the next free one, so an icon the directory is missing
    // leaves a hole instead of shifting every icon after it onto the wrong
    // points.
    std::ifstream in(path, std::ios::binary);
    if (!in) continue;
    PointSymbol sym;
    sym.id = (int64_t)i + 1;
    sym.name = names[i];
    sym.image.assign(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
    if (sym.image.empty()) continue;
    out.push_back(std::move(sym));
  }
  return out;
}

Status PointOverlay::WriteSampleFile(const std::string& spec,
                                     const std::string& symbol_dir) {
  PointOverlay tmp("Sample Points");
  tmp.SetPoints(SamplePoints());
  tmp.SetSymbols(SampleSymbols(symbol_dir));
  return tmp.FileSaveAs(spec, 0);
}

}  // namespace fv
