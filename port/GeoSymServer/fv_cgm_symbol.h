// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::CgmSymbol — resolution-independent CGM display list (vpf-geosym plan
// phase V4).
//
// FalconView's CCGMFile does two jobs at once: it decodes the binary CGM
// element stream into a CCGMDrawingObject hierarchy, then draws that
// hierarchy straight onto an HDC. The decode half is portable and is
// compiled in place (see port/GeoSymServer/CMakeLists.txt); the Draw/pen/
// brush/font half is severed with #ifdef _WIN32.
//
// This header is the seam. CgmSymbol runs the in-place parser and flattens
// its MFC object graph into plain C++17 structs, so consumers never see
// CString/CPoint/CObject and never need MFC to be present. Phase V5's
// VpfRenderer walks these elements onto ICanvas.
//
// COORDINATES: everything is in the symbol's own VDC space, exactly as
// encoded — no scaling, no rotation, no device mapping. That is what makes
// the list resolution-independent: a caller picks a target size, derives one
// transform from Bounds(), and applies it. Y is **screen-DOWN**: LoadCGM runs
// with bVDCOrientationEnable=TRUE, so CCGMFile::ReadVDCScaledY has already
// multiplied every y by the picture's m_iDirY (-1 for the standard lly<ury
// extent that GeoSym symbols use). That is what the GDI original drew into a
// DC unchanged; `bounds().top` is accordingly the SMALLER value. Consumers
// targeting a y-up space (fvkit's VectorSymbol) must negate — see
// fv_geosym_style.cpp's FlipY.

#ifndef FV_CGM_SYMBOL_H_
#define FV_CGM_SYMBOL_H_

#include <cstdint>
#include <string>
#include <vector>

#include "fvkit/geo.h"  // fv::Status

namespace fv {

// A point in symbol-local VDC coordinates.
struct CgmPoint {
  long x = 0;
  long y = 0;
};

// Integer VDC bounding box (CCGMFile's accumulated extent).
struct CgmRect {
  long left = 0;
  long top = 0;
  long right = 0;
  long bottom = 0;

  long Width() const { return right - left; }
  long Height() const { return bottom - top; }
  bool Empty() const { return left >= right || top >= bottom; }
};

// Mirrors CCGMDrawingObject::eElementType. Values are ours, not an ABI.
enum class CgmElementType {
  kUnknown = 0,
  kPolyline,
  kPolygon,
  kPolygonSet,
  kEllipse,
  kEllipticalArc,
  kText,
};

// CGM line types (eLineType in CGMFile.h) and fill styles (eFillStyle) are
// carried through as ints so this header never has to include CGMFile.h.
// 1=solid 2=dash 3=dot 4=dashdot 5=dashdotdot; fill 0=hollow 1=solid
// 2=pattern 3=hatch 4=empty 5=geometric 6=interpolated.

// How an arc is closed (eArcClose): -1 open, 0 pie, 1 chord.
enum class CgmArcClose { kOpen = -1, kPie = 0, kChord = 1 };

// Text baseline/up vectors as encoded by CHARACTER ORIENTATION.
struct CgmCharOrientation {
  long up_x = 0;
  long up_y = 1;
  long base_x = 1;
  long base_y = 0;
};

// One drawable element. A tagged struct rather than a variant: the field set
// is small, the consumer switches on `type` anyway, and keeping it flat makes
// the Python binding (a later slice) trivial.
struct CgmElement {
  CgmElementType type = CgmElementType::kUnknown;

  // --- style (all elements) -------------------------------------------
  uint32_t line_color = 0;  // COLORREF: 0x00BBGGRR
  uint32_t fill_color = 0;
  uint32_t edge_color = 0;
  int line_type = 1;
  int fill_style = 1;
  long line_width = 0;
  long edge_width = 0;
  bool edge_visible = false;

  // --- kPolyline / kPolygon / kPolygonSet ------------------------------
  std::vector<CgmPoint> vertices;
  // kPolygonSet only: per-vertex CGM edge-out flag, parallel to `vertices`.
  // Empty for plain polygons.
  std::vector<long> vertex_flags;

  // --- kEllipse / kEllipticalArc ---------------------------------------
  // CGM encodes an ellipse as a center plus two CONJUGATE DIAMETER endpoints,
  // which the parser has already reduced to center-relative radius vectors.
  // An arc adds two ray directions delimiting the swept sector.
  CgmPoint center;
  CgmPoint radius1;
  CgmPoint radius2;
  long major_radius = 0;
  long minor_radius = 0;
  double bounding_rotation = 0.0;  // radians, +X toward +Y
  bool clockwise = false;
  CgmPoint ray[2];
  CgmArcClose arc_close = CgmArcClose::kOpen;

  // --- kText ------------------------------------------------------------
  std::string text;
  CgmPoint position;
  long char_height = 0;
  // Index into the CGM font list; resolving it to a real typeface is the
  // renderer's job (the GDI mapping table is Windows-only).
  long font_index = 0;
  CgmCharOrientation orientation;
};

// --- SAMI (APS) line style -------------------------------------------------
//
// GeoSym LINE symbols are not stamped glyphs: the CGM carries an APS
// application-structure extension describing how to stroke a path (width,
// colour, and a dash pattern whose runs can themselves be point symbols).
// FalconView reads it as CCGMPicture::m_sami_line and CCGMSymbol::DrawSAMILine
// walks it; this is the portable mirror of those structures.

// Element kinds, from CGMDefines.h's SAMI_ELEMENT_TYPE_*.
enum class CgmLineElementType { kGap = 0, kDash = 1, kPointSymbol = 2 };

struct CgmLineElement {
  CgmLineElementType type = CgmLineElementType::kGap;
  double length = 0.0;  // HIMETRIC run length; 0 on a dash = "to the end"
  double vertical_displacement = 0.0;
  std::string symbol_definition;  // kPointSymbol: the symbol number to stamp
  double symbol_scale = 0.0;
  long symbol_orientation = 0;
};

struct CgmLineComponent {
  double line_width = 0.0;  // HIMETRIC
  uint32_t line_color = 0;  // COLORREF 0x00BBGGRR
  long start_anchor = 0;    // SAMI_ANCHOR_BEGINNING/MIDDLE/END
  long iteration_type = 0;
  double start_phase = 0.0;
  std::vector<CgmLineElement> elements;
};

// Picture-level line defaults plus the SAMI components. DrawSAMILine falls
// back to the picture values when a symbol has no components, so both travel
// together.
struct CgmLineStyle {
  long line_width = 0;      // HIMETRIC
  uint32_t line_color = 0;  // COLORREF
  bool round_cap = true;    // eLineEndCap != CGM_LINE_CAP_BUTT
  std::vector<CgmLineComponent> components;
};

// --- area fill -------------------------------------------------------------
//
// GeoSym AREA symbols are not display lists to stamp: CCGMSymbol::DrawArea
// reads the PICTURE's own fill colour and one of its bitmap patterns, then
// fills the face region with it. So an area symbol is a brush description,
// the same way a line symbol is a stroke description.
//
// A pattern whose bits are all 1 (CCGMPattern::IsSolid) is a plain solid
// fill; anything else is a monochrome stipple that DrawArea blits. The bits
// are carried verbatim, row-major, one bit per pixel, MSB first — a canvas
// that cannot stipple can still honour `fill_color` and check `solid`.
struct CgmPattern {
  long index = 0;
  int width = 0;
  int height = 0;
  bool solid = true;
  std::vector<unsigned char> bits;
};

struct CgmAreaStyle {
  uint32_t fill_color = 0;  // COLORREF 0x00BBGGRR — CCGMPicture::m_fill_color
  int fill_style = 1;       // eFillStyle: 0 hollow 1 solid 2 pattern 3 hatch
  std::vector<CgmPattern> patterns;  // m_pattern[], those actually populated
};

// A parsed CGM symbol: an ordered display list plus its VDC extent.
//
// NOTE: FalconView's GeoSym symbols are single-picture CGMs. If a file holds
// more than one picture, the elements of ALL pictures are appended in file
// order, matching what CCGMFile itself accumulates.
class CgmSymbol {
 public:
  CgmSymbol();
  ~CgmSymbol();

  CgmSymbol(const CgmSymbol&) = delete;
  CgmSymbol& operator=(const CgmSymbol&) = delete;

  // Parse a .cgm file. Backslash-separated and case-odd paths resolve like
  // the rest of the port (fv_win32_path).
  Status LoadFile(const std::string& path);

  // Parse an in-memory CGM byte stream (what the GeoSym cache holds).
  Status LoadBuffer(const void* data, size_t size);

  const std::vector<CgmElement>& elements() const { return elements_; }
  const CgmRect& bounds() const { return bounds_; }

  // Stroke description for LINE symbols (empty components on a point symbol).
  const CgmLineStyle& line_style() const { return line_style_; }

  // Brush description for AREA symbols (see CgmAreaStyle).
  const CgmAreaStyle& area_style() const { return area_style_; }

  // VDC extent of the picture, |urx-llx| by |ury-lly|. This — not bounds() —
  // is what CCGMSymbol::DrawSymbol scales a point symbol by, so it is what
  // the renderer needs to size one.
  long vdc_width() const { return vdc_width_; }
  long vdc_height() const { return vdc_height_; }

  // The picture's VDC direction multipliers (+1/-1 each; -1 on y for the
  // standard lly<ury extent every GeoSym symbol uses). The parser applied
  // these ONCE while reading coordinates, and Windows applies them a SECOND
  // time in CCGMDrawingObject::RotateVDC when it builds the display vertices
  // it actually draws ("VDC adjustments for reflection even if zero rotation
  // angle"). We keep the once-applied vertices, so a consumer that wants what
  // GDI drew must multiply by these — see fv_geosym_style.cpp's ApplyVdcDir.
  int dir_x() const { return dir_x_; }
  int dir_y() const { return dir_y_; }

  // Name the metafile encodes for itself (may be empty).
  const std::string& name() const { return name_; }

 private:
  std::vector<CgmElement> elements_;
  CgmRect bounds_;
  CgmLineStyle line_style_;
  CgmAreaStyle area_style_;
  int dir_x_ = 1;
  int dir_y_ = -1;
  long vdc_width_ = 0;
  long vdc_height_ = 0;
  std::string name_;
};

}  // namespace fv

#endif  // FV_CGM_SYMBOL_H_
