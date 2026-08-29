// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_geosym_style.h"

#include <climits>

// NOTE: GeoSymServer's StdAfx POSIX block defines the Win32 min/max MACROS
// (scoped to this tree, see the 2026-07-21 ledger entry), so every std::min /
// std::max call below is parenthesized to stop macro expansion.
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

#include "stdafx.h"  // POSIX branch: fv_compat + CString + MFC containers

#include "AttributeExpressions.h"
#include "DelimitedParser.h"
#include "SymColors.h"
#include "fv_cgm_symbol.h"
// G2: FromColorRef and ToVectorSymbol used to be defined below, file-local.
#include "fv_cgm_to_symbol.h"

namespace fv {
namespace {

// HIMETRIC (0.01 mm) per inch.
constexpr double kHimetricPerInch = 2540.0;

// DASH LENGTHS ARE NOT HIMETRIC. CGMFile.cpp scales LineWidth and
// VerticalDisplacement by 100 into 100ths of a mm when it reads them, but
// leaves ElementLength alone (only applying the `fabs(len * 2)` end-cap
// kludge); CCGMSymbol::DrawLine then walks the pattern with
// `m_length * dblMultiplier` measured against DEVICE PIXEL distances. So a
// dash run length is "whatever the CGM encoded, times the zoom, as pixels".
// Preserved verbatim (bit-faithful rule).

// The hardcoded fallback entries from CSymProduct::OpenProduct. Not table
// data: the original appends them to every product it loads.
constexpr const char* kFallbackUnknownPoint = "icon";   // symbol 5000
constexpr const char* kFallbackUnknownLine = "line";    // symbol 5001
constexpr const char* kFallbackNoMatchPoint = "defpt";  // 0051 black dot
constexpr const char* kFallbackNoMatchLine = "defln";   // 3113 black line

// A SAMI point-symbol element names its symbol as a FILE ("5010.cgm"), while
// every other symbol reference in GeoSym is a bare number. Symbol() keys on
// the number, so the extension comes off here.
std::string StripCgmExtension(const std::string& s) {
  if (s.size() > 4) {
    const std::string tail = s.substr(s.size() - 4);
    if (tail == ".cgm" || tail == ".CGM") return s.substr(0, s.size() - 4);
  }
  return s;
}


std::string ToStd(const CString& s) {
  return std::string(static_cast<const char*>(s), s.GetLength());
}

int DelinFor(VectorGeometryType t) {
  switch (t) {
    case VectorGeometryType::kPoint: return 1;
    case VectorGeometryType::kLine: return 2;
    default: return 3;
  }
}

// One fullsym.txt row, for the configured product only.
struct SymRow {
  long id = 0;
  int delin = 0;
  std::string point_sym;
  std::string line_sym;
  std::string area_sym;
  int priority = 0;
  std::string label_att;
  long text_row = -1;
  // The IHO/IMO visibility columns (R2). Zero means the row left them blank,
  // which the rule layer reads as "ungrouped" and "no category" — always on.
  int viewing_group = 0;
  int text_group = 0;
  int display_category = kDisplayCategoryNone;
};

// One TEXT.TXT row.
struct TextRow {
  long id = 0;
  double size_pt = 10.0;
  long color_index = 1;
  double dist_mm = 0.0;
  double dir_deg = 0.0;
};

}  // namespace

// ---------------------------------------------------------------------------

struct GeoSymStyleEngine::Impl {
  std::string data_dir;
  int product_id = kGeoSymDnc;
  bool open = false;
  bool draw_labels = false;

  // FACC -> rows, in table order (the original keeps per-FACC lists too).
  std::unordered_map<std::string, std::vector<SymRow>> rows;
  size_t row_count = 0;
  std::map<long, TextRow> text_rows;

  CAttributeExpressions att_exp;
  CSymColors colors;
  CSymColorAdjuster adjuster;
  // Sentinels, NOT 0: the first SetColorAdjust must reach the adjuster even
  // when it asks for (0,0), because a default-constructed CSymColorAdjuster
  // never ran Setup at all (see the UB note in the ledger). Only a REPEATED
  // set with the same numbers is skipped.
  int brightness = INT_MIN;
  int contrast = INT_MIN;
  // The mariner's depth numbers, in the form ATTEXP evaluates: every ATTEXP
  // row that names ssdc/msdc/mssc/idsm/isdm resolves through this object
  // (CAEAttributeList holds a reference to it), so keeping it in step with the
  // shared MarinerSettings is the whole of DNC's mariner wiring.
  CECDISValues ecdis;
  // The mariner epoch this `ecdis` was derived from. A mutable-reference
  // caller can move a contour at any time, so the derivation is checked per
  // Style() call — an integer compare — rather than at a setter that may
  // never be called.
  uint64_t ecdis_epoch = 0;

  // The R2 rule layer and the VectorSymbol display-list cache live in the
  // shared LookupTableStyleEngine core (E3c). What stays here is GeoSym's own
  // DERIVED caches, which are not display lists: a LINE symbol number resolves
  // to a stroke description and an AREA symbol number to a brush.
  struct LineStroke {
    bool valid = false;
    std::vector<CgmLineComponent> components;
    long picture_width = 0;
    uint32_t picture_color = 0;
  };
  std::unordered_map<std::string, LineStroke> line_strokes;
  // …and the derived brush for an AREA symbol number.
  struct AreaFill {
    bool valid = false;
    uint32_t color = 0;
    // Stipple coverage in [0,1]; 1.0 for a solid pattern. See AreaFillFor.
    double coverage = 1.0;
  };
  std::unordered_map<std::string, AreaFill> area_fills;

  std::string SymAssign(const char* file) const {
    return data_dir + "\\GeoSymbol\\SymAssign\\" + file;
  }
  std::string GraphicsFile(const std::string& number) const {
    return data_dir + "\\GeoSymbol\\Graphics\\" + number + ".cgm";
  }

  bool LoadAssignments();
  bool LoadTextTable();
  const LineStroke& LineStrokeFor(const std::string& number);
  const AreaFill& AreaFillFor(const std::string& number);
};

// Reads fullsym.txt exactly as CSymProduct::OpenProduct does: skip the field
// header block up to the ';' line, then one row per line, keeping only rows
// whose product id matches.
bool GeoSymStyleEngine::Impl::LoadAssignments() {
  CDelimitedParser parser;
  if (!parser.Open(SymAssign("fullsym.txt").c_str())) return false;

  bool semi_found = false;
  while (!semi_found && parser.ReadLine()) {
    if (parser.CurrentText()[0] == ';') semi_found = true;
  }
  if (!semi_found) return false;

  bool line_good = true;
  while (line_good && parser.ReadLine()) {
    line_good = false;
    long id = 0, pid = 0, delin = 0, priority = 0, text_row = 0;
    LPCTSTR facc = nullptr;
    LPCTSTR point_sym = nullptr;
    LPCTSTR line_sym = nullptr;
    LPCTSTR area_sym = nullptr;
    LPCTSTR label_att = nullptr;
    if (parser.ParseLong(id)                 // Row id
        && parser.ParseLong(pid)             // Product id
        && parser.ParseString(facc)          // FACC
        && parser.ParseLong(delin)           // Delineation
        && parser.SkipField()                // Coverage (empty for DNC)
        && parser.ParseString(point_sym)     // Point symbol
        && parser.ParseString(line_sym)      // Line symbol
        && parser.ParseString(area_sym)      // Area symbol
        && parser.ParseLong(priority)        // Display priority
        && parser.SkipField()                // Orientation (unused, as in SanSymbol)
        && parser.ParseString(label_att)     // Label attribute
        && parser.ParseLong(text_row, true)) {
      line_good = true;

      // The remaining columns (vgroup | txtgroup | radar | dispcat | feadesc)
      // are read SEPARATELY and non-fatally: before R2 the parse chain simply
      // stopped at txrowid, and folding these into the required chain would
      // turn a row that ends early into "the whole table failed". A field
      // that does not parse stays 0 = ungrouped/no category = always visible,
      // which is the same behaviour as before this was read at all.
      long vgroup = 0, txtgroup = 0, radar = 0, dispcat = 0;
      parser.ParseLong(vgroup, true);
      parser.ParseLong(txtgroup, true);
      parser.ParseLong(radar, true);  // IHO radar category: parsed, unused
      parser.ParseLong(dispcat, true);
      (void)radar;

      if (pid != product_id) continue;
      SymRow r;
      r.viewing_group = static_cast<int>(vgroup);
      r.text_group = static_cast<int>(txtgroup);
      r.display_category = static_cast<int>(dispcat);
      r.id = id;
      r.delin = static_cast<int>(delin);
      r.point_sym = point_sym;
      r.line_sym = line_sym;
      r.area_sym = area_sym;
      r.priority = static_cast<int>(priority);
      r.label_att = label_att;
      r.text_row = text_row;
      rows[facc].push_back(std::move(r));
      ++row_count;
    }
  }
  parser.Close();
  if (!line_good) return false;

  // The four hardcoded fallbacks (CSymProduct::OpenProduct).
  auto add_fallback = [this](const char* facc, int delin, const char* point,
                             const char* line, int priority) {
    SymRow r;
    r.id = -1;
    r.delin = delin;
    r.point_sym = point;
    r.line_sym = line;
    r.priority = priority;
    r.text_row = -1;
    rows[facc].push_back(std::move(r));
  };
  add_fallback(kFallbackUnknownPoint, 1, "5000", "", 9);
  add_fallback(kFallbackUnknownLine, 2, "", "5001", 9);
  add_fallback(kFallbackNoMatchPoint, 1, "0051", "", 9);
  add_fallback(kFallbackNoMatchLine, 2, "", "3113", 9);
  return true;
}

bool GeoSymStyleEngine::Impl::LoadTextTable() {
  CDelimitedParser parser;
  if (!parser.Open(SymAssign("TEXT.TXT").c_str())) return false;
  bool semi_found = false;
  while (!semi_found && parser.ReadLine()) {
    if (parser.CurrentText()[0] == ';') semi_found = true;
  }
  if (!semi_found) return false;

  while (parser.ReadLine()) {
    long id = 0, font = 0, style = 0, size = 0, just = 0, color = 0;
    double dist = 0.0, dir = 0.0;
    if (parser.ParseLong(id) && parser.ParseLong(font) &&
        parser.ParseLong(style) && parser.ParseLong(size) &&
        parser.ParseLong(just) && parser.ParseLong(color) &&
        parser.ParseDouble(dist, true) && parser.ParseDouble(dir, true)) {
      TextRow t;
      t.id = id;
      t.size_pt = static_cast<double>(size);
      t.color_index = color;
      t.dist_mm = dist;
      t.dir_deg = dir;
      text_rows[id] = t;
    }
  }
  parser.Close();
  return true;
}

const GeoSymStyleEngine::Impl::LineStroke&
GeoSymStyleEngine::Impl::LineStrokeFor(const std::string& number) {
  auto it = line_strokes.find(number);
  if (it != line_strokes.end()) return it->second;

  LineStroke stroke;
  CgmSymbol sym;
  if (sym.LoadFile(GraphicsFile(number)).ok()) {
    stroke.valid = true;
    stroke.components = sym.line_style().components;
    stroke.picture_width = sym.line_style().line_width;
    stroke.picture_color = sym.line_style().line_color;
  }
  return line_strokes.emplace(number, std::move(stroke)).first->second;
}

// An AREA symbol is a brush, not a display list: CCGMSymbol::DrawArea fills the
// face region with the PICTURE's m_fill_color, through pattern
// m_pattern[nPattern-1] — solid when the pattern's bits are all set, otherwise
// a monochrome stipple it blits.
//
// DEVIATION (documented): ICanvas has only a solid Brush, so a non-solid
// pattern is approximated by its INK COVERAGE carried in the fill alpha — a
// 25%-set stipple becomes the same colour at alpha 64. Over a chart that is
// what a stipple reads as from any distance, and it keeps the underlying
// features visible the way the stipple was meant to. A real pattern brush is
// an ICanvas addition; when it lands, this is the one place to change.
const GeoSymStyleEngine::Impl::AreaFill&
GeoSymStyleEngine::Impl::AreaFillFor(const std::string& number) {
  auto it = area_fills.find(number);
  if (it != area_fills.end()) return it->second;

  AreaFill fill;
  CgmSymbol sym;
  if (sym.LoadFile(GraphicsFile(number)).ok()) {
    const CgmAreaStyle& area = sym.area_style();
    // eFillStyle 0 = hollow, 4 = empty: the symbol says "do not fill".
    // DrawArea is only reached for the fill styles that paint.
    if (area.fill_style != 0 && area.fill_style != 4) {
      fill.valid = true;
      fill.color = area.fill_color;
      // DrawArea uses m_pattern[nPattern-1] and the VPF caller always passes
      // pattern 1, so the first populated pattern is the one that paints.
      //
      // BITS ARE INVERTED. CCGMPattern::AddMonochromeBit sets a bit when the
      // cell is OFF ("add new bit. Invert pattern."), and DrawArea hands the
      // buffer to a GDI monochrome pattern brush under
      // SetTextColor(fill)/SetBkColor(white) — which paints ZERO bits in the
      // foreground colour. So ink coverage is the fraction of CLEAR bits.
      // Getting this backwards turns DNC's ~5%-ink shallow-water stipple into
      // a 95% grey slab that hides the depth shading underneath it.
      for (const CgmPattern& p : area.patterns) {
        if (p.solid) break;
        size_t ink = 0, total = 0;
        for (unsigned char byte : p.bits) {
          for (int bit = 0; bit < 8; ++bit) {
            if ((byte & (1u << bit)) == 0) ++ink;
            ++total;
          }
        }
        if (total > 0)
          fill.coverage = static_cast<double>(ink) / static_cast<double>(total);
        break;
      }
    }
  }
  return area_fills.emplace(number, std::move(fill)).first->second;
}

// ---------------------------------------------------------------------------

GeoSymStyleEngine::GeoSymStyleEngine() : impl_(new Impl) {
  // GeoSym's OWN defaults, which are not the struct's (those are S-52's): the
  // numbers CECDISValues has default-constructed since V3, and therefore the
  // ones every DNC golden was pinned over. ISDM defaults to 1, so DNC ships
  // with the shallow-water pattern ON — see fvkit/vector/mariner.h.
  MarinerSettings m;
  m.safety_contour = 10.0;   // ssdc
  m.safety_depth = 10.0;     // DNC has no second number: ssdc is both
  m.deep_contour = 30.0;     // msdc
  m.shallow_contour = 2.0;   // mssc
  m.two_shades = false;      // idsm = 0, four shades
  m.shallow_pattern = true;  // isdm = 1
  SetMariner(m);
  ApplyMariner();
}

// MarinerSettings -> the ATTEXP pseudo-attributes. The mapping is read off the
// delivered tables, not the spec — mariner.h names the rows.
void GeoSymStyleEngine::ApplyMariner() {
  const MarinerSettings& m = current_mariner();
  impl_->ecdis.SSDC(m.safety_contour);
  impl_->ecdis.MSDC(m.deep_contour);
  impl_->ecdis.MSSC(m.shallow_contour);
  impl_->ecdis.IDSM(m.two_shades ? 1 : 0);
  impl_->ecdis.ISDM(m.shallow_pattern ? 1 : 0);
  impl_->ecdis_epoch = mariner_epoch();
}
GeoSymStyleEngine::~GeoSymStyleEngine() = default;

Status GeoSymStyleEngine::Open(const std::string& data_dir, int product_id) {
  set_open(false);
  impl_->rows.clear();
  impl_->row_count = 0;
  impl_->text_rows.clear();
  ClearSymbolCache();
  ResetUnresolvedSymbols();
  impl_->line_strokes.clear();
  impl_->area_fills.clear();
  impl_->data_dir = data_dir;
  impl_->product_id = product_id;

  if (!impl_->colors.Open(impl_->SymAssign("COLOR.TXT").c_str()))
    return Status::Error(kNotFound, "cannot read GeoSym COLOR.TXT under " + data_dir);
  if (!impl_->att_exp.Open(impl_->SymAssign("ATTEXP.TXT").c_str()))
    return Status::Error(kNotFound, "cannot read GeoSym ATTEXP.TXT under " + data_dir);
  if (!impl_->LoadAssignments())
    return Status::Error(kIoError, "cannot parse GeoSym fullsym.txt under " + data_dir);
  // TEXT.TXT only drives labels; a missing one is not fatal.
  impl_->LoadTextTable();

  set_open(true);
  return Status::Ok();
}

void GeoSymStyleEngine::SetColorAdjust(int brightness, int contrast) {
  // A no-op set must stay a no-op: an interactive caller pushes its current
  // brightness/contrast down on EVERY frame, and bumping unconditionally would
  // throw the retained scene away once per redraw (R3a).
  if (brightness == impl_->brightness && contrast == impl_->contrast) return;
  impl_->brightness = brightness;
  impl_->contrast = contrast;
  impl_->adjuster.Setup(brightness, contrast);
  // Cached display lists baked the old adjustment in — and so did any retained
  // VectorScene, which is what the epoch bump tells it.
  ClearSymbolCache();
  BumpStyleEpoch();
}

size_t GeoSymStyleEngine::row_count() const { return impl_->row_count; }
size_t GeoSymStyleEngine::symbols_loaded() const { return symbols_cached(); }
const std::string& GeoSymStyleEngine::data_dir() const { return impl_->data_dir; }

bool GeoSymStyleEngine::LoadSymbol(const std::string& symbol_id,
                                   VectorSymbol* out) {
  CgmSymbol cgm;
  if (!cgm.LoadFile(impl_->GraphicsFile(symbol_id)).ok()) return false;
  *out = ToVectorSymbol(cgm, impl_->adjuster);
  return true;
}

// Every drawing entry point in SanSymbol/SymText computes
// `dblScaleZoomFactor = scale * zoom / 100` and bails below 0.20 ("make sure it
// is something viewable"). Same cutoff, same place in the flow — which is why
// the core evaluates AcceptContext before the rule plan.
bool GeoSymStyleEngine::AcceptContext(const StyleContext& ctx) const {
  return !(ctx.symbol_scale > 0.0 && ctx.symbol_scale < 0.20);
}

namespace {

// Builds the comma-separated "att=value" string CAEAttributeList parses in
// its no-format-handler branch (the "old way" the V3 tests pin).
//
// NOTE that branch is a plain substring search for "<name>=", so a lookup of
// "dp=" would also hit inside "hdp=". That is the original's behaviour and it
// is why the search string keeps the '='; VPF attribute names are 3-4
// characters and DNC has no such colliding pair, but a new product could.
std::string AttributeString(const VectorFeature& f) {
  std::string s;
  for (const auto& kv : f.attributes) {
    if (!s.empty()) s.push_back(',');
    s += kv.first;
    s.push_back('=');
    s += kv.second;
  }
  return s;
}

}  // namespace

Status GeoSymStyleEngine::StyleFeature(const VectorFeature& f,
                                      const StyleContext& ctx,
                                      const StylePass& rule_pass,
                                      std::vector<StyleResult>* out) {
  // A rule may scale this feature's symbology on top of the engine-wide zoom;
  // the original's "make sure it is something viewable" cutoff then applies
  // to the product, not just the caller's half (AcceptContext checked that).
  const double symbol_scale = rule_pass.symbol_scale;
  if (symbol_scale > 0.0 && symbol_scale < 0.20) return Status::Ok();
  const bool draw_labels = rule_pass.draw_labels;
  const ViewingGroupSet& groups = viewing_groups();

  // A caller may have moved a contour through the mutable mariner() reference
  // since the last feature; re-deriving costs one integer compare per feature
  // and five assignments per actual change.
  if (impl_->ecdis_epoch != mariner_epoch()) ApplyMariner();

  const int delin = DelinFor(f.type);
  const std::string attrs = AttributeString(f);
  CAEAttributeList atts(impl_->ecdis, attrs.c_str(), nullptr, ',');

  // 1st chance: rows for this FACC. Unknown FACC -> "icon"/"line".
  auto it = impl_->rows.find(f.style_key);
  if (it == impl_->rows.end()) {
    it = impl_->rows.find(delin == 1 ? kFallbackUnknownPoint
                                     : kFallbackUnknownLine);
    if (it == impl_->rows.end()) return Status::Ok();
  }

  // The 2nd chance is driven by whether any row's CONDITION matched, not by
  // whether a draw came out of it - that is CSymFACCEntry::DrawSymbol's
  // RetVal, which it sets on a true expression even when the row only carries
  // a label. Getting this wrong made the fallback depend on SetDrawLabels:
  // DNC soundings are label-only rows, so turning labels off resurrected 2564
  // default dots the Windows renderer never draws.
  bool any_matched = false;
  for (int pass = 0; pass < 2; ++pass) {
    for (const SymRow& r : it->second) {
      if (r.delin != delin) continue;
      // Evaluate() passes an id with no rules (including the -1 the
      // fallbacks carry), which is what makes them unconditional.
      if (!impl_->att_exp.Evaluate(r.id, atts)) continue;
      any_matched = true;

      // Viewing-group / display-category filtering (R2) sits AFTER
      // any_matched, deliberately. A group the operator switched off means
      // "do not draw this", not "the FACC had no matching row" — running it
      // before would fire the 2nd-chance fallback and paint a default black
      // dot in place of every hidden feature, the same failure the label
      // comment above records.
      if (!groups.Enabled(r.viewing_group)) continue;
      if (!groups.CategoryEnabled(r.display_category)) continue;

      StyleResult sr;
      sr.priority = rule_pass.PriorityOr(r.priority);

      // AREA rows carry a brush in the `areasym` column — this is what makes
      // DNC land buff and water blue, and it is also the whole depth-shading
      // mechanism: BE010's rows name a different area symbol per depth band
      // (deep 0805 -> medium 0820 -> shallow 0821 -> very shallow 0810), and
      // the ATTEXP condition on each row already picks the right one from the
      // mariner's ssdc/msdc/mssc settings.
      if (delin == 3 && !r.area_sym.empty()) {
        const Impl::AreaFill& fill = impl_->AreaFillFor(r.area_sym);
        if (fill.valid) {
          sr.fill.valid = true;
          sr.fill.brush.color = FromColorRef(impl_->adjuster.Adjust(fill.color));
          sr.fill.brush.color.a = static_cast<unsigned char>(
              std::lround(255.0 * fill.coverage));
        }
      }

      if (delin == 2 || delin == 3) {
        const std::string& line_num = r.line_sym;
        if (!line_num.empty()) {
          const Impl::LineStroke& stroke = impl_->LineStrokeFor(line_num);
          if (stroke.valid) {
            // One StyleResult per SAMI component: DrawSAMILine strokes the
            // path once per component, which is how compound lines (a wide
            // casing under a narrow core) are encoded.
            const size_t n = stroke.components.empty()
                                 ? 1
                                 : stroke.components.size();
            for (size_t ci = 0; ci < n; ++ci) {
              StyleResult one = sr;
              double width_himetric = static_cast<double>(stroke.picture_width);
              COLORREF color = stroke.picture_color;
              const CgmLineComponent* comp =
                  stroke.components.empty() ? nullptr : &stroke.components[ci];
              if (comp != nullptr) {
                width_himetric = comp->line_width;
                color = comp->line_color;
              }
              one.stroke.valid = true;
              one.stroke.pen.color =
                  FromColorRef(impl_->adjuster.Adjust(color));
              const double w_px = width_himetric * symbol_scale *
                                  ctx.device_dpi / kHimetricPerInch;
              one.stroke.pen.width =
                  (std::max)(1, static_cast<int>(std::lround(w_px)));
              if (comp != nullptr) {
                bool stamps = false;
                for (const CgmLineElement& el : comp->elements)
                  if (el.type == CgmLineElementType::kPointSymbol) stamps = true;

                if (stamps) {
                  // SAMI's real primitive: a cycle that stamps symbols as well
                  // as strokes, which no pen can express. Goes to the shared
                  // along-path placer (E3b) — the same one S-52's LC uses.
                  // 89 of the 757 delivered CGM line symbols need this; before
                  // E3b their symbol runs degraded to gaps.
                  one.line_pattern.valid = true;
                  one.line_pattern.pen = one.stroke.pen;
                  one.stroke.valid = false;  // the placer draws the dashes
                  for (const CgmLineElement& el : comp->elements) {
                    PathRun run;
                    run.length = el.length * symbol_scale;
                    switch (el.type) {
                      case CgmLineElementType::kDash:
                        run.type = PathRunType::kDash;
                        // length 0 = "to the end of the line", carried through
                        // as the placer's own zero-length dash convention.
                        break;
                      case CgmLineElementType::kPointSymbol: {
                        run.type = PathRunType::kSymbol;
                        // The element names a FILE ("5010.cgm"); Symbol() keys
                        // on the bare number and appends the extension itself.
                        run.symbol_id = StripCgmExtension(el.symbol_definition);
                        run.symbol_scale =
                            el.symbol_scale > 0.0 ? el.symbol_scale : 1.0;
                        run.rotation_deg =
                            static_cast<double>(el.symbol_orientation);
                        // Vertical displacement IS scaled to HIMETRIC by the
                        // reader (unlike ElementLength — see the note at the
                        // top of this file), so it converts like a line width.
                        run.offset = el.vertical_displacement * symbol_scale *
                                     ctx.device_dpi / kHimetricPerInch;
                        break;
                      }
                      default:
                        run.type = PathRunType::kGap;
                        break;
                    }
                    one.line_pattern.runs.push_back(std::move(run));
                  }
                } else {
                  // Dash runs, in the original's units (see the note above).
                  for (const CgmLineElement& el : comp->elements) {
                    if (el.type == CgmLineElementType::kDash &&
                        el.length == 0.0) {
                      // "to the end of the line" — a solid run.
                      one.stroke.pen.dash.clear();
                      break;
                    }
                    const int run = (std::max)(
                        1, static_cast<int>(std::lround(el.length *
                                                        symbol_scale)));
                    one.stroke.pen.dash.push_back(run);
                  }
                  // Pen::dash is on/off pairs; an odd count would flip the
                  // phase every cycle, so drop a trailing lone run.
                  if (one.stroke.pen.dash.size() % 2 == 1)
                    one.stroke.pen.dash.pop_back();
                }
              }
              if (ci + 1 == n) {
                // Symbol/label ride on the last component only, so they are
                // emitted once per row.
                sr = one;
              } else {
                out->push_back(std::move(one));
              }
            }
          }
        }
      }

      if (!r.point_sym.empty()) {
        sr.symbol.valid = true;
        sr.symbol.symbol_id = r.point_sym;
        sr.symbol.scale = 1.0;
        // Direction-of-flow rotates the symbol, exactly as
        // CCGMSymbol::DrawSymbol does (the `orient` column is unused there
        // too — it calls SkipField on it).
        const CAEValue* dof = atts.GetValue("dof");
        if (dof != nullptr)
          sr.symbol.rotation_deg = static_cast<double>(dof->LongValue());
      }

      // The row's IHO TEXT group gates the label alone: turning text group
      // 23 off drops DNC's name labels while every symbol stays put.
      if (draw_labels && !r.label_att.empty() &&
          groups.Enabled(r.text_group)) {
        // CCGMSymbol::DrawLabel: a comma-separated label attribute
        // concatenates each attribute's value, comma-joined; a label whose
        // attribute is ABSENT is not drawn at all. Values are NOT filtered
        // (an "UNK" nam really does get painted on the Windows chart).
        std::string text;
        size_t pos = 0;
        bool missing = false;
        while (pos <= r.label_att.size() && !missing) {
          const size_t comma = r.label_att.find(',', pos);
          const std::string name = r.label_att.substr(
              pos, comma == std::string::npos ? std::string::npos : comma - pos);
          const CAEValue* v = atts.GetValue(name.c_str());
          if (v == nullptr) {
            // Single-attribute labels bail outright; the multi-attribute
            // branch skips the missing one and keeps going.
            if (comma == std::string::npos && pos == 0) missing = true;
          } else {
            if (!text.empty()) text.push_back(',');
            text += ToStd(v->StringValue());
          }
          if (comma == std::string::npos) break;
          pos = comma + 1;
        }

        // No TEXT.TXT row means no label: CCGMSymbol::DrawLabel returns when
        // GetTextByID misses, so there is no default styling to fall back on.
        const auto tr = impl_->text_rows.find(r.text_row);
        if (!missing && !text.empty() && tr != impl_->text_rows.end()) {
          sr.label.valid = true;
          sr.label.text = std::move(text);
          // QUIRK: TEXT.TXT documents tsize as POINTS, but CSymFont builds
          // the font with `nHeight = -m_nSize` after scaling by the zoom -
          // a negative CreateFont height is character height in DEVICE
          // units. So the column behaves as pixels, not points, and that is
          // preserved here rather than converted at 72 dpi.
          sr.label.style.size = tr->second.size_pt * symbol_scale;
          sr.label.style.color = FromColorRef(
              impl_->adjuster.Adjust(impl_->colors.GetColor(tr->second.color_index)));
          // tdist is mm from the feature, tdir an azimuth in degrees
          // (CSymText::DrawText: x = sin, y = -cos, mm -> HIMETRIC -> device).
          const double px = tr->second.dist_mm * symbol_scale *
                            ctx.device_dpi / 25.4;
          const double rad = tr->second.dir_deg * 3.14159265358979323846 / 180.0;
          sr.label.dx = static_cast<int>(std::lround(px * std::sin(rad)));
          sr.label.dy = static_cast<int>(std::lround(-px * std::cos(rad)));
        }
      }

      if (sr.stroke.valid || sr.fill.valid || sr.symbol.valid ||
          sr.label.valid || sr.line_pattern.valid)
        out->push_back(std::move(sr));
    }

    // 2nd chance: the FACC was known but nothing evaluated true.
    if (any_matched || pass == 1) break;
    auto fb = impl_->rows.find(delin == 1 ? kFallbackNoMatchPoint
                                          : kFallbackNoMatchLine);
    if (fb == impl_->rows.end()) break;
    it = fb;
  }
  return Status::Ok();
}

}  // namespace fv
