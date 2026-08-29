// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_s52_preslib.h"

#include <expat.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

#include "fvkit/tools/png_io.h"

namespace fv {
namespace {

// S-52's nominal line-width unit is 0.32 mm; VectorSymbol is HIMETRIC (0.01
// mm), so `SWn` is n * 32. See the header's UNITS note.
constexpr double kHimetricPerPenUnit = 32.0;

std::string Trim(const std::string& s) {
  const size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  const size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// Strips only newlines/tabs/CR, keeping spaces — for the one element whose
// value can legitimately BE a space (see S52LookupAttribute).
std::string TrimLineNoise(const std::string& s) {
  const size_t b = s.find_first_not_of("\t\r\n");
  if (b == std::string::npos) return {};
  const size_t e = s.find_last_not_of("\t\r\n");
  return s.substr(b, e - b + 1);
}

const char* Attr(const char** atts, const char* name) {
  for (size_t i = 0; atts != nullptr && atts[i] != nullptr; i += 2) {
    if (std::strcmp(atts[i], name) == 0) return atts[i + 1];
  }
  return nullptr;
}

int AttrInt(const char** atts, const char* name, int fallback = 0) {
  const char* v = Attr(atts, name);
  return v == nullptr ? fallback : std::atoi(v);
}

S52ObjectType ToObjectType(const std::string& s) {
  if (s == "Line") return S52ObjectType::kLine;
  if (s == "Area") return S52ObjectType::kArea;
  return S52ObjectType::kPoint;
}

S52LookupTable ToLookupTable(const std::string& s) {
  if (s == "Simplified") return S52LookupTable::kSimplified;
  if (s == "Symbolized") return S52LookupTable::kSymbolizedBoundaries;
  if (s == "Plain") return S52LookupTable::kPlainBoundaries;
  if (s == "Lines") return S52LookupTable::kLines;
  return S52LookupTable::kPaperChart;
}

S52DisplayCategory ToDisplayCategory(const std::string& s) {
  if (s == "Displaybase") return S52DisplayCategory::kDisplayBase;
  if (s == "Other") return S52DisplayCategory::kOther;
  if (s == "Mariners") return S52DisplayCategory::kMariners;
  return S52DisplayCategory::kStandard;
}

// The XML spells the priorities with S-52's own names. An unrecognised one
// lands at 0 (No data), which draws first and under everything — the safe end.
int ToDisplayPriority(const std::string& s) {
  if (s == "Group 1") return kS52PrioGroup1;
  if (s == "Area 1") return kS52PrioArea1;
  if (s == "Area 2") return kS52PrioArea2;
  if (s == "Point Symbol") return kS52PrioPointSymbol;
  if (s == "Line Symbol") return kS52PrioLineSymbol;
  if (s == "Area Symbol") return kS52PrioAreaSymbol;
  if (s == "Routing") return kS52PrioRouting;
  if (s == "Hazards") return kS52PrioHazards;
  if (s == "Mariners") return kS52PrioMariners;
  return kS52PrioNoData;
}

// "CATACH8" -> {"CATACH", "8"}. S-57 acronyms are exactly 6 characters, so the
// split is positional; anything shorter is kept whole with an empty value
// ("attribute present, any value").
S52LookupAttribute SplitAttributeCode(const std::string& token) {
  S52LookupAttribute a;
  if (token.size() <= 6) {
    a.acronym = Trim(token);
  } else {
    a.acronym = token.substr(0, 6);
    a.value = token.substr(6);
  }
  return a;
}

}  // namespace

// ---------------------------------------------------------------------------
// Symbology instructions
// ---------------------------------------------------------------------------

std::vector<S52Instruction> ParseS52Instructions(const std::string& text) {
  std::vector<S52Instruction> out;
  size_t i = 0;
  while (i < text.size()) {
    // One instruction runs to the next ';' outside quotes.
    std::string chunk;
    bool quoted = false;
    for (; i < text.size(); ++i) {
      const char c = text[i];
      if (c == '\'') quoted = !quoted;
      if (c == ';' && !quoted) {
        ++i;
        break;
      }
      chunk.push_back(c);
    }
    chunk = Trim(chunk);
    if (chunk.empty()) continue;

    S52Instruction ins;
    const size_t open = chunk.find('(');
    if (open == std::string::npos) {
      ins.op = chunk;  // an op with no parameter list (none in the current file)
      out.push_back(std::move(ins));
      continue;
    }
    ins.op = Trim(chunk.substr(0, open));
    size_t close = chunk.rfind(')');
    if (close == std::string::npos || close < open) close = chunk.size();
    const std::string body = chunk.substr(open + 1, close - open - 1);

    // Parameters split on commas OUTSIDE single quotes: TX/TE format strings
    // carry commas of their own.
    std::string cur;
    quoted = false;
    for (char c : body) {
      if (c == '\'') {
        quoted = !quoted;
        continue;  // quotes are delimiters, not content
      }
      if (c == ',' && !quoted) {
        ins.params.push_back(Trim(cur));
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
    ins.params.push_back(Trim(cur));
    out.push_back(std::move(ins));
  }
  return out;
}

// ---------------------------------------------------------------------------
// HPGL -> VectorSymbol
// ---------------------------------------------------------------------------
//
// The eight opcodes the delivered library actually uses (verified by sweeping
// all 459 <HPGL> blocks: PD PU SP SW PM CI FP ST, nothing else):
//
//   SP<c>   select pen: `c` indexes the definition's colour-reference string
//   SW<n>   select width, in S-52 pen units (0.32 mm)
//   PU x,y  pen up   — move, ending any run being drawn
//   PD x,y[,x,y...]  pen down — draw through the points
//   PM0     begin polygon mode; PM2 ends it; FP then fills what was collected
//   CI r    circle of radius r about the current point
//   ST n    fill transparency 0..3 = 0/25/50/75 %, applied to FP
//
// Polygon blocks in this file are always PM0 (PD... | CI) PM2 FP, so a polygon
// is either a ring of PD vertices or a single circle. Both shapes are handled;
// an unexpected mixture would emit the ring and drop the circle rather than
// misdraw, which is why the sweep is recorded here.

namespace {

struct HpglPen {
  FvColor color{0, 0, 0, 255};
  double width = kHimetricPerPenUnit;
};

// `pen_table` is 6-character groups: pen character + 5-character colour token.
FvColor ResolvePen(const std::string& pen_table, char pen,
                   const S52ColorTable& palette) {
  for (size_t i = 0; i + 5 < pen_table.size(); i += 6) {
    if (pen_table[i] != pen) continue;
    const std::string token = pen_table.substr(i + 1, 5);
    auto it = palette.colors.find(token);
    if (it != palette.colors.end()) return it->second;
    break;
  }
  return FvColor{0, 0, 0, 255};  // see the header: wrong, never invisible
}

unsigned char TransparencyToAlpha(int st) {
  // S-52 ST is the transparency STEP: 0 opaque, 1 25 %, 2 50 %, 3 75 %.
  const int clamped = std::max(0, std::min(3, st));
  return static_cast<unsigned char>(255 - clamped * 64);
}

}  // namespace

Status ParseS52Hpgl(const std::string& hpgl, const std::string& pen_table,
                    const S52ColorTable& palette, int pivot_x, int pivot_y,
                    VectorSymbol* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "null out");
  out->primitives.clear();

  // Authoring space is y DOWN with the hot spot at the pivot; VectorSymbol is
  // y UP about its own origin.
  const auto map_point = [pivot_x, pivot_y](double x, double y) {
    SymbolPoint p;
    p.x = x - pivot_x;
    p.y = pivot_y - y;
    return p;
  };

  HpglPen pen;
  int transparency = 0;
  SymbolPoint cur{0.0, 0.0};
  std::vector<SymbolPoint> run;      // open polyline
  std::vector<SymbolPoint> ring;     // polygon-mode ring
  bool in_polygon = false;
  bool ring_is_circle = false;
  double circle_radius = 0.0;
  SymbolPoint circle_center{0.0, 0.0};

  const auto flush_run = [&]() {
    if (run.size() >= 2) {
      SymbolPrimitive p;
      p.type = SymbolPrimitiveType::kPolyline;
      p.points = run;
      p.has_stroke = true;
      p.stroke_color = pen.color;
      p.stroke_width = pen.width;
      out->primitives.push_back(std::move(p));
    }
    run.clear();
  };

  // Reads "x,y,x,y,..." into transformed points.
  const auto read_points = [&](const std::string& args,
                               std::vector<SymbolPoint>* pts) {
    std::vector<double> nums;
    size_t i = 0;
    while (i < args.size()) {
      while (i < args.size() && (args[i] == ',' || args[i] == ' ')) ++i;
      const size_t start = i;
      while (i < args.size() && args[i] != ',' && args[i] != ' ') ++i;
      if (i > start) nums.push_back(std::atof(args.substr(start, i - start).c_str()));
    }
    for (size_t k = 0; k + 1 < nums.size(); k += 2) {
      pts->push_back(map_point(nums[k], nums[k + 1]));
    }
  };

  size_t pos = 0;
  while (pos < hpgl.size()) {
    const size_t end = hpgl.find(';', pos);
    const std::string cmd =
        Trim(hpgl.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
    pos = (end == std::string::npos) ? hpgl.size() : end + 1;
    if (cmd.size() < 2) continue;

    const std::string op = cmd.substr(0, 2);
    const std::string args = cmd.substr(2);

    if (op == "SP") {
      flush_run();
      if (!args.empty()) pen.color = ResolvePen(pen_table, args[0], palette);
    } else if (op == "SW") {
      flush_run();
      const int units = std::atoi(args.c_str());
      pen.width = (units > 0 ? units : 1) * kHimetricPerPenUnit;
    } else if (op == "ST") {
      transparency = std::atoi(args.c_str());
    } else if (op == "PU") {
      flush_run();
      std::vector<SymbolPoint> pts;
      read_points(args, &pts);
      if (!pts.empty()) cur = pts.back();
    } else if (op == "PD") {
      std::vector<SymbolPoint> pts;
      read_points(args, &pts);
      if (pts.empty()) {
        // `PD;` with no coordinates is a DOT at the current point, and the
        // library really uses it: OBSTRN02 and SPRING02 are whole symbols
        // built from `PUx,y;PD;` stipples. Emitted as a zero-length run, which
        // the canvas stamps as one square nib of the pen width — the same
        // thing HPGL's pen-down-in-place does. Dropping it would erase the
        // symbol entirely (which is how this was found).
        flush_run();
        SymbolPrimitive p;
        p.type = SymbolPrimitiveType::kPolyline;
        p.points = {cur, cur};
        p.has_stroke = true;
        p.stroke_color = pen.color;
        p.stroke_width = pen.width;
        out->primitives.push_back(std::move(p));
        continue;
      }
      if (in_polygon) {
        if (ring.empty()) ring.push_back(cur);
        ring.insert(ring.end(), pts.begin(), pts.end());
      } else {
        if (run.empty()) run.push_back(cur);
        run.insert(run.end(), pts.begin(), pts.end());
      }
      cur = pts.back();
    } else if (op == "PM") {
      const int mode = std::atoi(args.c_str());
      if (mode == 0) {
        flush_run();
        in_polygon = true;
        ring.clear();
        ring_is_circle = false;
      } else if (mode == 2) {
        in_polygon = false;
      }
      // PM1 (close subpolygon) does not occur in the delivered library; if it
      // ever does, it would close `ring` and start another — deliberately not
      // guessed at here.
    } else if (op == "CI") {
      const double r = std::atof(args.c_str());
      if (in_polygon) {
        ring_is_circle = true;
        circle_radius = r;
        circle_center = cur;
      } else {
        SymbolPrimitive p;
        p.type = SymbolPrimitiveType::kEllipse;
        p.center = cur;
        p.radius1 = SymbolPoint{r, 0.0};
        p.radius2 = SymbolPoint{0.0, r};
        p.has_stroke = true;
        p.stroke_color = pen.color;
        p.stroke_width = pen.width;
        out->primitives.push_back(std::move(p));
      }
    } else if (op == "FP") {
      FvColor fill = pen.color;
      fill.a = TransparencyToAlpha(transparency);
      if (ring_is_circle) {
        SymbolPrimitive p;
        p.type = SymbolPrimitiveType::kEllipse;
        p.center = circle_center;
        p.radius1 = SymbolPoint{circle_radius, 0.0};
        p.radius2 = SymbolPoint{0.0, circle_radius};
        p.has_fill = true;
        p.fill_color = fill;
        out->primitives.push_back(std::move(p));
      } else if (ring.size() >= 3) {
        SymbolPrimitive p;
        p.type = SymbolPrimitiveType::kPolygon;
        p.points = ring;
        p.has_fill = true;
        p.fill_color = fill;
        out->primitives.push_back(std::move(p));
      }
      ring.clear();
      ring_is_circle = false;
    }
    // Any other opcode is ignored: unlike the CGM parser (which asserts), an
    // unknown HPGL opcode here costs one primitive, and the library is trusted
    // local data. Add it above when a future PresLib uses one.
  }
  flush_run();

  // Extent from the emitted geometry rather than the declared <vector> box —
  // the declared one is the authoring bounding box and does not account for
  // pen width or for the pivot shift.
  bool first = true;
  for (const SymbolPrimitive& p : out->primitives) {
    const auto grow = [&](double x, double y) {
      if (first) {
        out->min_x = out->max_x = x;
        out->min_y = out->max_y = y;
        first = false;
        return;
      }
      out->min_x = std::min(out->min_x, x);
      out->max_x = std::max(out->max_x, x);
      out->min_y = std::min(out->min_y, y);
      out->max_y = std::max(out->max_y, y);
    };
    for (const SymbolPoint& v : p.points) grow(v.x, v.y);
    if (p.type == SymbolPrimitiveType::kEllipse) {
      const double rx = std::max(std::abs(p.radius1.x), std::abs(p.radius2.x));
      const double ry = std::max(std::abs(p.radius1.y), std::abs(p.radius2.y));
      grow(p.center.x - rx, p.center.y - ry);
      grow(p.center.x + rx, p.center.y + ry);
    }
  }
  if (first) {
    out->min_x = out->max_x = out->min_y = out->max_y = 0.0;
  }
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// The library
// ---------------------------------------------------------------------------

namespace {

// A symbol's PIVOT is the point that lands on the feature's position. For most
// of the library it sits on or just off the glyph — a light's flare rises from
// its pivot, a beacon stands on it — and that offset is meaningful, so the
// pivot is honoured as authored.
//
// 61 of the 367 vector symbols in the delivered chartsymbols.xml carry a pivot
// that lies OUTSIDE their own ink by more than a quarter of the symbol's size,
// most of them 1.5-2.0 symbol-widths clear of it (CTNARE51, CTYARE51,
// ENTRES51/61/71, INFARE51, RSRDEF51, TSSCRS51, RETRFL01/02 ...). Drawn as
// authored, those symbols land 40-70 px away from the feature they annotate:
// a caution mark floating in open water next to the area it belongs to.
//
// This is the DELIVERED DATA, not a parse error, and the file says so twice:
// for 47 of the 61 the independent BITMAP pivot carries the same out-of-range
// fraction of its own tile (CTNARE51's is 1.79 box-widths right in both), and
// a raster pivot outside a 29x29 tile cannot be a deliberate anchor. It is a
// systematic artefact of whatever converted the S-52 PLIB into this XML.
//
// DEVIATION, deliberate and narrow: when the pivot is that far off the ink,
// the symbol is re-anchored on the centre of its own geometry. The bound is
// generous (a quarter of the symbol's size beyond its box) precisely so the
// legitimate hangers-off keep the pivot they were authored with — every light,
// beacon and topmark tested lands inside it. POINT SYMBOLS ONLY: a line-style
// or an area pattern is positioned by the placer, where an authored offset is
// part of how the pattern tiles, so those are left exactly as delivered.
bool ReanchorIfPivotIsOffTheGlyph(VectorSymbol* sym) {
  double x0 = 1e18, x1 = -1e18, y0 = 1e18, y1 = -1e18;
  auto add = [&](double x, double y) {
    x0 = std::min(x0, x); x1 = std::max(x1, x);
    y0 = std::min(y0, y); y1 = std::max(y1, y);
  };
  for (const SymbolPrimitive& p : sym->primitives) {
    for (const SymbolPoint& q : p.points) add(q.x, q.y);
    if (p.type == SymbolPrimitiveType::kEllipse) {
      const double rx = std::hypot(static_cast<double>(p.radius1.x),
                                   static_cast<double>(p.radius1.y));
      const double ry = std::hypot(static_cast<double>(p.radius2.x),
                                   static_cast<double>(p.radius2.y));
      add(p.center.x - rx, p.center.y - ry);
      add(p.center.x + rx, p.center.y + ry);
    } else if (p.points.empty()) {
      add(p.center.x, p.center.y);
    }
  }
  if (x0 > x1) return false;  // no geometry to measure

  const double w = x1 - x0, h = y1 - y0;
  const double mx = 0.25 * std::max(w, 1.0), my = 0.25 * std::max(h, 1.0);
  // The anchor is the origin: ParseS52Hpgl already translated by the pivot.
  if (0.0 >= x0 - mx && 0.0 <= x1 + mx && 0.0 >= y0 - my && 0.0 <= y1 + my)
    return false;

  const int dx = static_cast<int>(std::lround((x0 + x1) / 2.0));
  const int dy = static_cast<int>(std::lround((y0 + y1) / 2.0));
  for (SymbolPrimitive& p : sym->primitives) {
    for (SymbolPoint& q : p.points) { q.x -= dx; q.y -= dy; }
    p.center.x -= dx;
    p.center.y -= dy;
  }
  return true;
}

}  // namespace

struct S52PresentationLibrary::Impl {
  std::string path;
  bool open = false;

  std::vector<S52ColorTable> color_tables;
  std::string active_table;

  std::vector<S52Lookup> lookups;
  // (table, acronym) -> row indices in file order.
  std::unordered_map<std::string, std::vector<size_t>> lookup_index;

  std::unordered_map<std::string, S52SymbolDef> symbols;
  std::unordered_map<std::string, S52SymbolDef> line_styles;
  std::unordered_map<std::string, S52SymbolDef> patterns;

  // Display lists, keyed "<colour table>\x1f<name>" because colours are baked
  // into the primitives.
  std::unordered_map<std::string, VectorSymbol> cache;

  // --- SAX state ---
  enum class Where { kNone, kColorTable, kLookup, kSymbol, kLineStyle, kPattern };
  Where where = Where::kNone;
  bool in_bitmap = false;
  std::string text;
  S52Lookup lookup;
  S52SymbolDef def;
  bool parse_failed = false;

  static std::string IndexKey(S52LookupTable t, const std::string& acronym) {
    return std::to_string(static_cast<int>(t)) + "\x1f" + acronym;
  }

  const S52ColorTable* Table(const std::string& name) const {
    for (const S52ColorTable& t : color_tables) {
      if (t.name == name) return &t;
    }
    return nullptr;
  }

  // `kind` keeps the caches apart: a name can be BOTH a symbol and a
  // line-style (ACHARE51 is), with different geometry.
  size_t repivoted = 0;  // symbols re-anchored by the rule above

  const VectorSymbol* Build(const std::unordered_map<std::string, S52SymbolDef>& defs,
                            char kind, const std::string& name) {
    const std::string key = active_table + "\x1f" + kind + "\x1f" + name;
    auto cached = cache.find(key);
    if (cached != cache.end()) return &cached->second;

    auto it = defs.find(name);
    if (it == defs.end() || it->second.hpgl.empty()) return nullptr;
    const S52ColorTable* palette = Table(active_table);
    if (palette == nullptr) return nullptr;

    VectorSymbol sym;
    const S52SymbolDef& d = it->second;
    const Status st = ParseS52Hpgl(d.hpgl, d.color_ref, *palette, d.pivot_x,
                                   d.pivot_y, &sym);
    if (!st.ok()) return nullptr;
    if (kind == 'S' && ReanchorIfPivotIsOffTheGlyph(&sym)) ++repivoted;
    return &(cache[key] = std::move(sym));
  }

  // --- raster symbol sheet -------------------------------------------------
  //
  // One decoded sheet per colour table (the three PNGs ARE the day/dusk/night
  // palettes), loaded on the first tile asked of it. Cached tiles are keyed
  // like the display lists, so a colour-scheme switch re-cuts rather than
  // recolouring — which is right, because the ink differs per sheet.
  std::unordered_map<std::string, PixelBuffer> sheets;
  std::unordered_map<std::string, SymbolPixmap> bitmap_cache;
  std::unordered_map<std::string, std::string> sheet_errors;  // table -> why

  // The directory chartsymbols.xml came from; `<graphics-file>` is relative
  // to it.
  std::string DataDir() const {
    const size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? std::string(".") : path.substr(0, cut);
  }

  // nullptr + a recorded reason when the active table has no sheet. The
  // failure is remembered so a chart full of raster symbols does not retry a
  // missing file once per feature.
  const PixelBuffer* Sheet() {
    auto have = sheets.find(active_table);
    if (have != sheets.end()) return &have->second;
    if (sheet_errors.count(active_table) != 0) return nullptr;

    const S52ColorTable* t = Table(active_table);
    if (t == nullptr || t->graphics_file.empty()) {
      sheet_errors[active_table] =
          active_table + ": colour table names no graphics-file";
      return nullptr;
    }
    const std::string file = DataDir() + "/" + t->graphics_file;
    PixelBuffer sheet;
    const Status st = ReadPngFile(file, &sheet);
    if (!st.ok()) {
      sheet_errors[active_table] = st.message;
      return nullptr;
    }
    return &(sheets[active_table] = std::move(sheet));
  }

  const SymbolPixmap* Bitmap(const std::string& name) {
    const std::string key = active_table + "\x1f" + name;
    auto cached = bitmap_cache.find(key);
    if (cached != bitmap_cache.end())
      return cached->second.tile.Empty() ? nullptr : &cached->second;

    auto it = symbols.find(name);
    if (it == symbols.end() || !it->second.has_bitmap) return nullptr;
    const S52SymbolDef& d = it->second;
    const int w = d.bitmap_width, h = d.bitmap_height;
    if (w <= 0 || h <= 0) return nullptr;

    const PixelBuffer* sheet = Sheet();
    if (sheet == nullptr) return nullptr;
    const int sx = d.bitmap_location_x, sy = d.bitmap_location_y;
    if (sx < 0 || sy < 0 || sx + w > sheet->Width() ||
        sy + h > sheet->Height()) {
      // A remembered failure: an empty tile in the cache, so a tile whose
      // <graphics-location> does not fit this sheet is measured once.
      bitmap_cache[key] = SymbolPixmap();
      return nullptr;
    }

    SymbolPixmap out;
    out.tile = PixelBuffer(w, h);
    for (int y = 0; y < h; ++y)
      std::memcpy(out.tile.Row(y), sheet->Row(sy + y) + sx * 4,
                  static_cast<size_t>(w) * 4);

    // The bitmap pivot is in tile pixels, y down; the origin is the tile's own
    // corner (0,0 throughout the delivered file, subtracted anyway because
    // that is what the pair means).
    out.pivot_x = d.bitmap_pivot_x - d.bitmap_origin_x;
    out.pivot_y = d.bitmap_pivot_y - d.bitmap_origin_y;

    // The same delivered-data defect E5 found in the VECTOR pivots, in the
    // half of the file that carries it more plainly: 292 of the 1083 bitmap
    // pivots lie more than a quarter of the tile outside the tile itself, and
    // 28 of them are INT_MIN — ARPONE01's is written `-2147483648`, which is
    // not an anchor, it is a missing value. A pivot that far out is discarded
    // for the tile's centre. The bound matches the vector rule deliberately:
    // the two halves of this file are wrong in the same way, and a symbol that
    // legitimately hangs off its pivot (a daymark standing on its post, pivot
    // y=30 of a 33 px tile) stays inside it and keeps what it was authored
    // with.
    //
    // EXCEPT the SOUNDING DIGITS, where a far-off pivot is the whole design.
    // `SOUNDS`/`SOUNDG` + position + digit is a 6x10 glyph placed in one of
    // five slots on a 7 px grid around the sounding's position, and the slot IS
    // the pivot: 19, 12, 5, -2, -9 for positions 3, 2, 1, 0, 4, with position 5
    // repeating 0's column half a line lower to make the decimetre a
    // subscript. Every slot but two is outside a quarter of a 6 px tile, so the
    // rule above would re-centre them and stack a two-digit sounding on top of
    // itself — which is exactly what it did. The rule cannot tell these apart
    // by geometry (a 19 px offset on a 6 px tile looks precisely like the
    // INT_MIN garbage it was written for), so the family is named.
    const bool laid_out_by_pivot = name.compare(0, 5, "SOUND") == 0;
    const double mx = 0.25 * w, my = 0.25 * h;
    if (!laid_out_by_pivot &&
        (out.pivot_x < -mx || out.pivot_x > w + mx || out.pivot_y < -my ||
         out.pivot_y > h + my)) {
      out.pivot_x = w / 2.0;
      out.pivot_y = h / 2.0;
    }
    return &(bitmap_cache[key] = std::move(out));
  }

  void StartElement(const char* name, const char** atts);
  void EndElement(const char* name);

  // expat trampolines. Members, not free functions: Impl is private.
  static void XMLCALL OnStart(void* user, const XML_Char* name,
                              const XML_Char** atts) {
    static_cast<Impl*>(user)->StartElement(name, atts);
  }
  static void XMLCALL OnEnd(void* user, const XML_Char* name) {
    static_cast<Impl*>(user)->EndElement(name);
  }
  static void XMLCALL OnText(void* user, const XML_Char* s, int len) {
    static_cast<Impl*>(user)->text.append(s, len);
  }
};

void S52PresentationLibrary::Impl::StartElement(const char* name,
                                                const char** atts) {
  text.clear();
  const std::string el(name);

  if (el == "color-table") {
    where = Where::kColorTable;
    S52ColorTable t;
    const char* n = Attr(atts, "name");
    t.name = n == nullptr ? "" : n;
    color_tables.push_back(std::move(t));
    return;
  }
  if (el == "lookup") {
    where = Where::kLookup;
    lookup = S52Lookup();
    lookup.id = AttrInt(atts, "id");
    lookup.rcid = AttrInt(atts, "RCID");
    const char* n = Attr(atts, "name");
    lookup.acronym = n == nullptr ? "" : Trim(n);
    lookup.order = static_cast<int>(lookups.size());
    return;
  }
  if (el == "symbol" || el == "line-style" || el == "pattern") {
    where = el == "symbol" ? Where::kSymbol
                           : (el == "line-style" ? Where::kLineStyle : Where::kPattern);
    def = S52SymbolDef();
    def.rcid = AttrInt(atts, "RCID");
    return;
  }

  switch (where) {
    case Where::kColorTable:
      if (el == "graphics-file") {
        const char* n = Attr(atts, "name");
        if (n != nullptr) color_tables.back().graphics_file = n;
      } else if (el == "color") {
        const char* n = Attr(atts, "name");
        if (n != nullptr) {
          FvColor c;
          c.r = static_cast<unsigned char>(AttrInt(atts, "r"));
          c.g = static_cast<unsigned char>(AttrInt(atts, "g"));
          c.b = static_cast<unsigned char>(AttrInt(atts, "b"));
          c.a = 255;
          color_tables.back().colors[n] = c;
        }
      }
      return;
    case Where::kSymbol:
    case Where::kLineStyle:
    case Where::kPattern:
      if (el == "bitmap") {
        in_bitmap = true;
        def.has_bitmap = true;
        def.bitmap_width = AttrInt(atts, "width");
        def.bitmap_height = AttrInt(atts, "height");
      } else if (el == "vector") {
        def.vector_width = AttrInt(atts, "width");
        def.vector_height = AttrInt(atts, "height");
      } else if (el == "pivot") {
        if (in_bitmap) {
          def.bitmap_pivot_x = AttrInt(atts, "x");
          def.bitmap_pivot_y = AttrInt(atts, "y");
        } else {
          def.pivot_x = AttrInt(atts, "x");
          def.pivot_y = AttrInt(atts, "y");
        }
      } else if (el == "origin") {
        if (in_bitmap) {
          def.bitmap_origin_x = AttrInt(atts, "x");
          def.bitmap_origin_y = AttrInt(atts, "y");
        } else {
          def.origin_x = AttrInt(atts, "x");
          def.origin_y = AttrInt(atts, "y");
        }
      } else if (el == "graphics-location") {
        def.bitmap_location_x = AttrInt(atts, "x");
        def.bitmap_location_y = AttrInt(atts, "y");
      } else if (el == "distance" && !in_bitmap) {
        def.distance_min = AttrInt(atts, "min");
        def.distance_max = AttrInt(atts, "max");
      }
      return;
    default:
      return;
  }
}

void S52PresentationLibrary::Impl::EndElement(const char* name) {
  const std::string el(name);
  const std::string raw_text = text;
  const std::string value = Trim(text);
  text.clear();

  if (el == "bitmap") {
    in_bitmap = false;
    return;
  }

  switch (where) {
    case Where::kLookup:
      if (el == "type") {
        lookup.type = ToObjectType(value);
      } else if (el == "disp-prio") {
        lookup.display_priority = ToDisplayPriority(value);
      } else if (el == "radar-prio") {
        lookup.radar_on_top = (value == "On Top");
      } else if (el == "table-name") {
        lookup.table = ToLookupTable(value);
      } else if (el == "attrib-code") {
        // NOT `value`: a lone trailing space is a real S-52 value here (see
        // the header), so only line noise is stripped.
        const std::string raw = TrimLineNoise(raw_text);
        if (!Trim(raw).empty()) lookup.attributes.push_back(SplitAttributeCode(raw));
      } else if (el == "instruction") {
        lookup.instruction = value;
        lookup.instructions = ParseS52Instructions(value);
      } else if (el == "display-cat") {
        lookup.display_category = ToDisplayCategory(value);
      } else if (el == "comment") {
        lookup.comment = value;
      } else if (el == "lookup") {
        lookup_index[IndexKey(lookup.table, lookup.acronym)].push_back(lookups.size());
        lookups.push_back(std::move(lookup));
        where = Where::kNone;
      }
      return;

    case Where::kSymbol:
    case Where::kLineStyle:
    case Where::kPattern: {
      if (el == "name") {
        def.name = value;
      } else if (el == "description") {
        def.description = value;
      } else if (el == "definition") {
        def.vector_defined = (value == "V");
      } else if (el == "prefer-bitmap") {
        def.prefer_bitmap = (value != "no");
      } else if (el == "color-ref") {
        def.color_ref = value;
      } else if (el == "HPGL") {
        def.hpgl = value;
      } else if (el == "filltype") {
        def.fill_type = value.empty() ? '\0' : value[0];
      } else if (el == "spacing") {
        def.spacing = value.empty() ? '\0' : value[0];
      } else if (el == "symbol" || el == "line-style" || el == "pattern") {
        if (!def.name.empty()) {
          // 73 symbol names are defined TWICE in the delivered library, once
          // as a vector definition and once as raster-only (<definition>R,
          // no HPGL) with a different RCID — ACHARE51 and BCNGEN01 among them.
          // A VECTOR definition always wins, whichever came first, because it
          // is the one that can be drawn; between two of the same kind the
          // first wins. Order in the file is NOT a safe tie-break here (that
          // is how this was found: keying on order alone silently lost the
          // geometry of every symbol whose raster row came first).
          auto& into = (where == Where::kSymbol)
                           ? symbols
                           : (where == Where::kLineStyle ? line_styles : patterns);
          auto it = into.find(def.name);
          if (it == into.end()) {
            into.emplace(def.name, def);
          } else if (it->second.hpgl.empty() && !def.hpgl.empty()) {
            it->second = def;
          }
        }
        where = Where::kNone;
      }
      return;
    }

    case Where::kColorTable:
      if (el == "color-table") where = Where::kNone;
      return;

    default:
      return;
  }
}

S52PresentationLibrary::S52PresentationLibrary() : impl_(new Impl) {}
S52PresentationLibrary::~S52PresentationLibrary() = default;

Status S52PresentationLibrary::Open(const std::string& path) {
  std::string file = path;
  const bool is_xml = file.size() > 4 && file.compare(file.size() - 4, 4, ".xml") == 0;
  if (!is_xml) {
    if (!file.empty() && (file.back() == '/' || file.back() == '\\')) file.pop_back();
    file += "/chartsymbols.xml";
  }

  std::ifstream in(file, std::ios::binary);
  if (!in) return Status::Error(kIoError, "cannot open " + file);

  *impl_ = Impl();
  impl_->path = file;

  XML_Parser parser = XML_ParserCreate(nullptr);
  if (parser == nullptr) return Status::Error(kInternal, "XML_ParserCreate failed");
  XML_SetUserData(parser, impl_.get());
  XML_SetElementHandler(parser, &Impl::OnStart, &Impl::OnEnd);
  XML_SetCharacterDataHandler(parser, &Impl::OnText);

  std::vector<char> buf(1 << 16);
  Status st = Status::Ok();
  for (;;) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    const std::streamsize got = in.gcount();
    const bool last = !in;
    if (XML_Parse(parser, buf.data(), static_cast<int>(got), last ? 1 : 0) ==
        XML_STATUS_ERROR) {
      st = Status::Error(
          kIoError, file + ": XML error at line " +
                        std::to_string(XML_GetCurrentLineNumber(parser)) + ": " +
                        XML_ErrorString(XML_GetErrorCode(parser)));
      break;
    }
    if (last) break;
  }
  XML_ParserFree(parser);
  if (!st.ok()) return st;

  if (impl_->color_tables.empty()) {
    return Status::Error(kIoError, file + ": no colour tables");
  }
  if (impl_->lookups.empty()) {
    return Status::Error(kIoError, file + ": no lookup rows");
  }
  // DAY_BRIGHT is S-52's day palette and the first table in the file; fall back
  // to whatever came first rather than refusing to open.
  impl_->active_table =
      impl_->Table("DAY_BRIGHT") != nullptr ? "DAY_BRIGHT" : impl_->color_tables.front().name;
  impl_->open = true;
  return Status::Ok();
}

bool S52PresentationLibrary::is_open() const { return impl_->open; }
const std::string& S52PresentationLibrary::file_path() const { return impl_->path; }

const std::vector<S52ColorTable>& S52PresentationLibrary::color_tables() const {
  return impl_->color_tables;
}

const S52ColorTable* S52PresentationLibrary::ColorTable(const std::string& name) const {
  return impl_->Table(name);
}

Status S52PresentationLibrary::SetActiveColorTable(const std::string& name) {
  if (impl_->Table(name) == nullptr) {
    return Status::Error(kNotFound, "no colour table named " + name);
  }
  impl_->active_table = name;
  return Status::Ok();
}

const std::string& S52PresentationLibrary::active_color_table() const {
  return impl_->active_table;
}

bool S52PresentationLibrary::Color(const std::string& token, FvColor* out) const {
  const S52ColorTable* t = impl_->Table(impl_->active_table);
  if (t == nullptr) return false;
  auto it = t->colors.find(token);
  if (it == t->colors.end()) return false;
  if (out != nullptr) *out = it->second;
  return true;
}

const std::vector<S52Lookup>& S52PresentationLibrary::lookups() const {
  return impl_->lookups;
}

std::vector<const S52Lookup*> S52PresentationLibrary::Lookups(
    S52LookupTable table, const std::string& acronym) const {
  std::vector<const S52Lookup*> out;
  auto it = impl_->lookup_index.find(Impl::IndexKey(table, acronym));
  if (it == impl_->lookup_index.end()) return out;
  out.reserve(it->second.size());
  for (size_t i : it->second) out.push_back(&impl_->lookups[i]);
  return out;
}

size_t S52PresentationLibrary::lookup_count(S52LookupTable table) const {
  size_t n = 0;
  for (const S52Lookup& l : impl_->lookups) {
    if (l.table == table) ++n;
  }
  return n;
}

const S52SymbolDef* S52PresentationLibrary::SymbolDef(const std::string& name) const {
  auto it = impl_->symbols.find(name);
  return it == impl_->symbols.end() ? nullptr : &it->second;
}

const S52SymbolDef* S52PresentationLibrary::LineStyleDef(const std::string& name) const {
  auto it = impl_->line_styles.find(name);
  return it == impl_->line_styles.end() ? nullptr : &it->second;
}

const S52SymbolDef* S52PresentationLibrary::PatternDef(const std::string& name) const {
  auto it = impl_->patterns.find(name);
  return it == impl_->patterns.end() ? nullptr : &it->second;
}

size_t S52PresentationLibrary::symbol_count() const { return impl_->symbols.size(); }
size_t S52PresentationLibrary::line_style_count() const {
  return impl_->line_styles.size();
}
size_t S52PresentationLibrary::pattern_count() const { return impl_->patterns.size(); }

std::vector<std::string> S52PresentationLibrary::UnresolvedSymbolReferences() const {
  std::vector<std::string> out;
  for (const S52Lookup& l : impl_->lookups) {
    for (const S52Instruction& ins : l.instructions) {
      if (ins.params.empty() || ins.params[0].empty()) continue;
      const std::string& n = ins.params[0];
      bool known = true;
      if (ins.op == "SY") {
        known = impl_->symbols.count(n) != 0;
      } else if (ins.op == "LC") {
        known = impl_->line_styles.count(n) != 0;
      } else if (ins.op == "AP") {
        known = impl_->patterns.count(n) != 0;
      }
      if (!known) out.push_back(ins.op + "(" + n + ")");
    }
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

const VectorSymbol* S52PresentationLibrary::Symbol(const std::string& name) {
  return impl_->Build(impl_->symbols, 'S', name);
}
const VectorSymbol* S52PresentationLibrary::LineStyle(const std::string& name) {
  return impl_->Build(impl_->line_styles, 'L', name);
}
const VectorSymbol* S52PresentationLibrary::Pattern(const std::string& name) {
  return impl_->Build(impl_->patterns, 'P', name);
}

const SymbolPixmap* S52PresentationLibrary::SymbolBitmap(
    const std::string& name) {
  return impl_->Bitmap(name);
}

const std::string& S52PresentationLibrary::raster_sheet_error() const {
  static const std::string kNone;
  auto it = impl_->sheet_errors.find(impl_->active_table);
  return it == impl_->sheet_errors.end() ? kNone : it->second;
}

}  // namespace fv
