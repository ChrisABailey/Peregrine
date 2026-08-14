// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_s52_preslib.h — the IHO S-52 Presentation Library as data (ENC phase E2,
// plan sections 5.1 and 7). GeoSym's `fullsym.txt` + `COLOR.TXT` + `Graphics/
// *.cgm` under IHO names, and deliberately the same shape: a lookup table
// keyed on a dispatch string, a palette, and a vector symbol library that
// reduces to `fv::VectorSymbol`.
//
// THIS HEADER IS A LOADER, NOT A STYLE ENGINE. It parses and exposes; deciding
// which lookup a feature matches, running the CS procedures and emitting
// `StyleResult`s is `S52StyleEngine` (E3/Q11), which is also where the shared
// `LookupTableStyleEngine` core gets extracted from GeoSym — plan section 5.1
// says to cut that abstraction once a SECOND real table exists, and this file
// is that second table.
//
// SOURCE: OpenCPN's `chartsymbols.xml` (decided 2026-07-25, plan section 5.6),
// git-ignored under `TestData/enc/` and supplied at runtime by data-dir
// argument exactly like the GeoSym assets. GPL-3.0, compatible with Peregrine
// AS DATA; OpenCPN's C++ stays reference-for-behaviour only and none of it is
// copied here. The file is a derived encoding of IHO's `.dai` records, and a
// conveniently complete one: 5 colour tables, 3057 lookups across the five
// S-52 lookup tables, 1093 symbols, 59 line-styles, 30 patterns.
//
// UNITS. S-52 authors vector symbols in 0.01 mm, which IS the HIMETRIC unit
// `fvkit/vector/style.h` declares for `VectorSymbol` — so symbol geometry
// passes through unscaled. Two conversions do happen on the way in:
//   * y is flipped. The PresLib's vector space has y DOWN (an anchor symbol's
//     ring is at the smaller y); VectorSymbol is y UP.
//   * the origin moves to the symbol's PIVOT, because that is the hot spot and
//     VectorSymbol anchors at its own (0,0).
// Pen width `SWn` is n * 0.32 mm = n * 32 HIMETRIC (S-52's nominal line-width
// unit), so a symbol keeps its authored weight independent of device DPI.

#ifndef FV_S52_PRESLIB_H_
#define FV_S52_PRESLIB_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/canvas/canvas.h"       // FvColor
#include "fvkit/geo.h"                 // Status
#include "fvkit/vector/style.h"        // VectorSymbol

namespace fv {

// ---------------------------------------------------------------------------
// Lookup tables
// ---------------------------------------------------------------------------

// The five S-52 lookup tables. Points come in two mutually exclusive flavours
// (paper-chart vs simplified) and areas in two (plain vs symbolized
// boundaries); which pair is in force is a mariner display setting, which is
// why they are all loaded and the choice is made at style time.
enum class S52LookupTable {
  kPaperChart = 0,       // "Paper"      — point, full paper-chart symbology
  kSimplified,           // "Simplified" — point, simplified symbology
  kSymbolizedBoundaries, // "Symbolized" — area, symbolized boundary
  kPlainBoundaries,      // "Plain"      — area, plain boundary
  kLines,                // "Lines"      — line
  kLookupTableCount,
};

enum class S52ObjectType { kPoint = 0, kLine, kArea };

// IMO display category. Same axis as GeoSym's `dispcat` column and as
// fvkit/vector/rules.h's display-category threshold — see plan section 5.1.
enum class S52DisplayCategory {
  kDisplayBase = 0,  // never switchable off
  kStandard,
  kOther,
  kMariners,         // mariner's own additions (AIS, own ship, ...)
};

// S-52 display priority, 0..9. The names in the XML are the spec's own; the
// numbers are the drawing order, low first, which is exactly what
// StyleResult::priority wants.
enum S52DisplayPriority {
  kS52PrioNoData = 0,
  kS52PrioGroup1 = 1,       // skin of the earth
  kS52PrioArea1 = 2,
  kS52PrioArea2 = 3,
  kS52PrioPointSymbol = 4,
  kS52PrioLineSymbol = 5,
  kS52PrioAreaSymbol = 6,
  kS52PrioRouting = 7,
  kS52PrioHazards = 8,
  kS52PrioMariners = 9,
};

// Text is lifted OUT of its object's priority band and drawn above all
// geometry, at kS52PrioTextBase + the object's own priority.
//
// S-52 assigns a display priority to the LOOKUP, and the lookup's whole
// instruction chain — TX/TE included — would otherwise inherit it. The
// delivered library puts text-bearing rows at every priority there is,
// including Group 1: LNDARE carries a TX, so a land name drew at priority 1
// and every depth area, built-up area and symbol pass above it painted over
// the name. Measured on the Charleston cells before this changed: a name at
// Group 1 is overdrawn by four later bands.
//
// Adding the object's priority rather than flattening to one number keeps the
// library's own relative order AMONG labels, so a hazard's text still sits
// over a land area's text.
//
// The base leaves a gap above kS52PrioMariners rather than sitting on 10: an
// own-ship or AIS layer is the one thing that should eventually draw over the
// text, and it needs somewhere to go.
constexpr int kS52PrioTextBase = 16;

// One attribute condition of a lookup row. The XML packs them as a single
// token, `<attrib-code index="0">CATACH8</attrib-code>`: six characters of
// acronym followed by the value, so the split is POSITIONAL (S-57 acronyms are
// exactly 6 characters). All four value shapes in the delivered library are
// kept verbatim rather than normalised, because S-52 treats them differently:
//   "8"        a single value          (2908 rows)
//   "3,4,3"    a list attribute's values, commas and all   (464 rows)
//   " " / ""   present, value irrelevant / no value stated (98 / 45 rows;
//              the single space survives the parse — only newlines and tabs
//              are stripped from this element, since here the space IS data)
//   "?"        present with any value  (4 rows)
// String-valued attributes land here too (SYMINS carries "CHCRDEL1").
struct S52LookupAttribute {
  std::string acronym;  // 6 chars, e.g. "CATACH"
  std::string value;    // "8", "", " "
};

// One symbology instruction, e.g. `LS(DASH,2,CHMGF)` -> op "LS", params
// {"DASH","2","CHMGF"}. Quoted parameters (TX/TE format strings) keep their
// commas; the quotes are stripped.
struct S52Instruction {
  std::string op;                    // SY, LS, LC, AC, AP, TX, TE, CS
  std::vector<std::string> params;
};

struct S52Lookup {
  int id = 0;     // the XML's own `id`
  int rcid = 0;   // the PresLib record id
  int order = 0;  // position in the file; the tie-break S-52 needs

  std::string acronym;  // object class, e.g. "DEPARE"
  S52ObjectType type = S52ObjectType::kPoint;
  S52LookupTable table = S52LookupTable::kPaperChart;
  int display_priority = kS52PrioNoData;
  bool radar_on_top = false;
  S52DisplayCategory display_category = S52DisplayCategory::kStandard;

  std::vector<S52LookupAttribute> attributes;
  std::string instruction;                  // verbatim, as authored
  std::vector<S52Instruction> instructions; // parsed
  std::string comment;
};

// Splits a symbology instruction string on ';', honouring single-quoted
// parameters. Exposed for testing and for the style engine's own use.
std::vector<S52Instruction> ParseS52Instructions(const std::string& text);

// ---------------------------------------------------------------------------
// Colour tables
// ---------------------------------------------------------------------------

struct S52ColorTable {
  std::string name;           // DAY_BRIGHT, DAY_BLACKBACK, DAY_WHITEBACK, DUSK, NIGHT
  std::string graphics_file;  // rastersymbols-*.png, the raster symbol sheet
  std::map<std::string, FvColor> colors;  // 5-char token -> RGB
};

// ---------------------------------------------------------------------------
// Symbol library
// ---------------------------------------------------------------------------

// A symbol / line-style / pattern definition, verbatim from the XML. The three
// kinds share a shape (name + description + vector metrics + HPGL + colour
// reference); patterns add fill type and spacing, symbols add a raster tile in
// the symbol sheet.
struct S52SymbolDef {
  std::string name;         // e.g. "ACHARE02"
  std::string description;
  int rcid = 0;

  // <definition>V</definition>; R = raster only. Symbols and patterns carry
  // the element, LINE-STYLES DO NOT — they are always vector, so this stays
  // false for them and `hpgl.empty()` is the test that means "drawable".
  bool vector_defined = false;
  bool prefer_bitmap = false;   // <prefer-bitmap>no</prefer-bitmap> when present

  // <vector> metrics, in the same 0.01 mm units as the HPGL.
  int vector_width = 0, vector_height = 0;
  int pivot_x = 0, pivot_y = 0;
  int origin_x = 0, origin_y = 0;
  int distance_min = 0, distance_max = 0;  // pattern spacing, 0.01 mm

  // <bitmap> tile in the raster symbol sheet named by the colour table.
  bool has_bitmap = false;
  int bitmap_width = 0, bitmap_height = 0;
  int bitmap_pivot_x = 0, bitmap_pivot_y = 0;
  int bitmap_origin_x = 0, bitmap_origin_y = 0;
  int bitmap_location_x = 0, bitmap_location_y = 0;

  // Pen table: 6-character groups of (pen char, 5-char colour token), so
  // `SPA` in the HPGL means "the colour this symbol calls A".
  std::string color_ref;
  std::string hpgl;

  // Patterns only.
  char fill_type = '\0';  // 'S' staggered, 'L' linear
  char spacing = '\0';    // 'C' constant
};

// ---------------------------------------------------------------------------
// The library
// ---------------------------------------------------------------------------

class S52PresentationLibrary {
 public:
  S52PresentationLibrary();
  ~S52PresentationLibrary();

  S52PresentationLibrary(const S52PresentationLibrary&) = delete;
  S52PresentationLibrary& operator=(const S52PresentationLibrary&) = delete;

  // `path` is either the directory holding chartsymbols.xml (TestData/enc in
  // this tree) or the file itself.
  Status Open(const std::string& path);
  bool is_open() const;
  const std::string& file_path() const;

  // --- colour tables ------------------------------------------------------
  const std::vector<S52ColorTable>& color_tables() const;
  // nullptr when unknown. Names are the XML's: "DAY_BRIGHT", "DUSK", "NIGHT".
  const S52ColorTable* ColorTable(const std::string& name) const;
  // The palette used by Symbol()/LineStyle()/Pattern() when no name is given.
  // Defaults to DAY_BRIGHT (the first table, and S-52's day palette).
  Status SetActiveColorTable(const std::string& name);
  const std::string& active_color_table() const;
  // Resolved colour, or false when the token is not in the active table.
  bool Color(const std::string& token, FvColor* out) const;

  // --- lookups ------------------------------------------------------------
  const std::vector<S52Lookup>& lookups() const;
  // Every row of `table` whose object class is `acronym`, in file order. The
  // style engine walks them in order and takes the first whose attribute
  // conditions all match, which is S-52's rule.
  std::vector<const S52Lookup*> Lookups(S52LookupTable table,
                                        const std::string& acronym) const;
  size_t lookup_count(S52LookupTable table) const;

  // --- symbol definitions -------------------------------------------------
  // Counts are of DISTINCT NAMES. 73 symbol names are defined twice in the
  // delivered library — once as a vector definition and once as raster-only —
  // and the vector one wins (see the .cpp), so 1093 <symbol> elements become
  // 1018 symbols.
  const S52SymbolDef* SymbolDef(const std::string& name) const;
  const S52SymbolDef* LineStyleDef(const std::string& name) const;
  const S52SymbolDef* PatternDef(const std::string& name) const;
  size_t symbol_count() const;
  size_t line_style_count() const;
  size_t pattern_count() const;

  // --- display lists ------------------------------------------------------
  //
  // The HPGL of a symbol / line-style / pattern, flattened to the seam's
  // product-neutral `VectorSymbol` (HIMETRIC, y up, origin at the pivot).
  // Owned and cached by the library, keyed by (name, colour table), because
  // colours are baked into the primitives; valid until the library dies.
  // nullptr = unknown name, or a raster-only definition with no HPGL.
  const VectorSymbol* Symbol(const std::string& name);
  const VectorSymbol* LineStyle(const std::string& name);
  const VectorSymbol* Pattern(const std::string& name);

  // --- raster tiles -------------------------------------------------------
  //
  // 679 of the library's 1018 symbols are defined RASTER ONLY — a `<bitmap>`
  // tile in the colour table's symbol sheet and no HPGL at all — and every one
  // of them has that tile. The lateral buoys, beacons and daymarks a harbour
  // is made of are in that set, which is why a vector-only path drew question
  // marks over the navaids.
  //
  // This cuts the named symbol's tile out of the sheet the ACTIVE colour table
  // points at (`<graphics-file>`, resolved beside chartsymbols.xml), and hands
  // it over as the seam's product-neutral SymbolPixmap. Owned and cached by
  // the library, keyed like the display lists by (name, colour table) — the
  // three sheets ARE the day/dusk/night palettes, already coloured.
  //
  // nullptr = unknown name, no `<bitmap>` element, a tile that does not lie
  // inside the sheet, or a sheet that could not be read (see
  // `raster_sheet_error()` — a missing sheet is not an Open() failure, because
  // the vector half of the library is fully usable without it).
  const SymbolPixmap* SymbolBitmap(const std::string& name);

  // Empty when the active colour table's sheet loaded, or has not been asked
  // for yet.
  const std::string& raster_sheet_error() const;

  // --- integrity ----------------------------------------------------------
  //
  // Symbol / line-style / pattern names that lookup instructions REFER to and
  // the library does not define, as "SY(FLTHAZ02)" strings, sorted and unique.
  // The delivered library has 24 of them (BOYLAT52..56, FLTHAZ02, ARCSLN01 and
  // friends, 92 references in all) — a real gap in the source file, not a
  // parse failure, and the reason plan section 7's "never silently drop a
  // feature" rule needs a visible placeholder in the E3 style engine. Computed
  // on demand, not cached.
  std::vector<std::string> UnresolvedSymbolReferences() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Flattens one HPGL string to a display list. Standalone because it is the
// piece worth testing on its own, and because the line-style placer (E3) wants
// it without a library in hand.
//
// `pen_table` is the definition's `color-ref` string; `palette` resolves the
// 5-character tokens. Unresolvable pens draw black rather than dropping the
// primitive — a symbol that loses a colour should look wrong, not vanish.
Status ParseS52Hpgl(const std::string& hpgl, const std::string& pen_table,
                    const S52ColorTable& palette, int pivot_x, int pivot_y,
                    VectorSymbol* out);

}  // namespace fv

#endif  // FV_S52_PRESLIB_H_
