// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fv_cgm_symbol.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "stdafx.h"  // POSIX branch: fv_compat + CString + MFC containers

#include "CGMFile.h"
#include "fv_win32_path.h"

namespace fv {
namespace {

CgmPoint ToPoint(const POINT& p) {
  CgmPoint out;
  out.x = p.x;
  out.y = p.y;
  return out;
}

// Style fields every element carries. CCGMDrawingObject exposes these through
// public getters; the members themselves stay protected.
void CopyStyle(CCGMDrawingObject* obj, CgmElement* out) {
  out->line_color = static_cast<uint32_t>(obj->GetLineColor());
  out->fill_color = static_cast<uint32_t>(obj->GetFillColor());
  out->edge_color = static_cast<uint32_t>(obj->GetEdgeColor());
  out->line_type = static_cast<int>(obj->GetLineType());
  out->fill_style = static_cast<int>(obj->GetFillStyle());
  out->line_width = obj->GetLineWidth();
  out->edge_width = obj->GetEdgeWidth();
  out->edge_visible = obj->GetEdgeVisible() != FALSE;
}

// m_vertices holds the element as parsed; m_disp_vertices is the rotated copy
// the GDI path draws. A display list must carry the UNROTATED geometry — the
// renderer applies its own rotation — so this reads m_vertices.
void CopyVertices(CCGMPolyLine* poly, CgmElement* out) {
  const int n = poly->m_vertices.GetSize();
  out->vertices.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) out->vertices.push_back(ToPoint(poly->m_vertices[i]));
}

void CopyEllipseGeometry(CCGMEllipticalObject* eo, CgmElement* out) {
  out->center = ToPoint(eo->m_ptCenter);
  out->radius1 = ToPoint(eo->m_ptCanonicalRadius1);
  out->radius2 = ToPoint(eo->m_ptCanonicalRadius2);
  out->major_radius = eo->m_lMajorRadius;
  out->minor_radius = eo->m_lMinorRadius;
  out->bounding_rotation = eo->m_dBoundingRotation;
  out->clockwise = eo->m_bCWDrawing != FALSE;
}

bool Flatten(CCGMDrawingObject* obj, CgmElement* out) {
  switch (obj->GetElementType()) {
    case CCGMDrawingObject::ELEMTYPE_POLYLINE: {
      out->type = CgmElementType::kPolyline;
      CopyStyle(obj, out);
      CopyVertices(static_cast<CCGMPolyLine*>(obj), out);
      return true;
    }
    case CCGMDrawingObject::ELEMTYPE_POLYGON: {
      out->type = CgmElementType::kPolygon;
      CopyStyle(obj, out);
      CopyVertices(static_cast<CCGMPolyLine*>(obj), out);
      return true;
    }
    case CCGMDrawingObject::ELEMTYPE_POLYGON_SET: {
      out->type = CgmElementType::kPolygonSet;
      CopyStyle(obj, out);
      CCGMPolygonSet* set = static_cast<CCGMPolygonSet*>(obj);
      CopyVertices(set, out);
      const CArray<long, long>& types = set->GetVertexTypes();
      const int n = types.GetSize();
      out->vertex_flags.reserve(static_cast<size_t>(n));
      for (int i = 0; i < n; ++i) out->vertex_flags.push_back(types[i]);
      return true;
    }
    case CCGMDrawingObject::ELEMTYPE_CIRCLE:
    case CCGMDrawingObject::ELEMTYPE_ELLIPSE: {
      out->type = CgmElementType::kEllipse;
      CopyStyle(obj, out);
      CopyEllipseGeometry(static_cast<CCGMEllipticalObject*>(obj), out);
      return true;
    }
    case CCGMDrawingObject::ELEMTYPE_CIRCULAR_ARC:
    case CCGMDrawingObject::ELEMTYPE_CIRCULAR_ARC_CLOSE:
    case CCGMDrawingObject::ELEMTYPE_ELLIPTICAL_ARC:
    case CCGMDrawingObject::ELEMTYPE_ELLIPTICAL_ARC_CLOSE: {
      out->type = CgmElementType::kEllipticalArc;
      CopyStyle(obj, out);
      CCGMEllipticalArc* arc = static_cast<CCGMEllipticalArc*>(obj);
      CopyEllipseGeometry(arc, out);
      out->ray[0] = ToPoint(arc->m_ptRays[0]);
      out->ray[1] = ToPoint(arc->m_ptRays[1]);
      out->arc_close = static_cast<CgmArcClose>(arc->m_eCloseType);
      return true;
    }
    case CCGMDrawingObject::ELEMTYPE_TEXT: {
      out->type = CgmElementType::kText;
      CopyStyle(obj, out);
      CCGMText* txt = static_cast<CCGMText*>(obj);
      const CString& cs = txt->GetText();
      out->text.assign(static_cast<const char*>(cs), cs.GetLength());
      out->position = ToPoint(txt->GetPosition());
      out->char_height = txt->GetCharHeight();
      out->font_index = txt->GetFontIndex();
      // Text colour rides in line_color: CCGMText::UsesColor treats the text
      // colour AS the line colour, and GetLineColor() returns it.
      out->line_color = static_cast<uint32_t>(txt->GetTextColor());
      const CHAR_ORIENTATION& co = txt->GetCharOrientation();
      out->orientation.up_x = co.lUpX;
      out->orientation.up_y = co.lUpY;
      out->orientation.base_x = co.lBaseX;
      out->orientation.base_y = co.lBaseY;
      return true;
    }
    default:
      // ELEMTYPE_UNKNOWN / ELEMTYPE_ELLIPTICAL_OBJECT are never instantiated
      // as leaves by the parser; skip rather than emit a junk element.
      return false;
  }
}

// The APS/SAMI line style: how a GeoSym LINE symbol says to stroke a path.
// CCGMSymbol::DrawSAMILine reads exactly these fields, falling back to the
// picture-level width/colour when there are no components.
void CopyLineStyle(CCGMPicture* pic, CgmLineStyle* out) {
  out->line_width = pic->m_line_width;
  out->line_color = static_cast<uint32_t>(pic->m_line_color);
  out->round_cap = pic->m_eLineEndCap != CGM_LINE_CAP_BUTT;

  POSITION pos = pic->m_sami_line.m_components.GetHeadPosition();
  while (pos != nullptr) {
    CApsLineComponent* comp = pic->m_sami_line.m_components.GetNext(pos);
    if (comp == nullptr) continue;
    CgmLineComponent c;
    c.line_width = comp->m_line_width;
    c.line_color = static_cast<uint32_t>(comp->m_line_color);
    c.start_anchor = comp->m_start_anchor;
    c.iteration_type = comp->m_iteration_type;
    c.start_phase = comp->m_start_phase;

    POSITION epos = comp->m_elements.GetHeadPosition();
    while (epos != nullptr) {
      CApsLineElement* el = comp->m_elements.GetNext(epos);
      if (el == nullptr) continue;
      CgmLineElement e;
      e.type = static_cast<CgmLineElementType>(el->m_type);
      e.length = el->m_length;
      e.vertical_displacement = el->m_vertical_displacement;
      e.symbol_definition.assign(static_cast<const char*>(el->m_symbol_definition),
                                 el->m_symbol_definition.GetLength());
      e.symbol_scale = el->m_symbol_scale;
      e.symbol_orientation = el->m_symbol_orientation;
      c.elements.push_back(std::move(e));
    }
    out->components.push_back(std::move(c));
  }
}

// The picture's brush: what CCGMSymbol::DrawArea fills a face region with.
// It reads m_picture->m_fill_color and m_pattern[nPattern-1]; a pattern whose
// bits are all 0xFF is solid, anything else is blitted as a stipple.
void CopyAreaStyle(CCGMPicture* pic, CgmAreaStyle* out) {
  out->fill_color = static_cast<uint32_t>(pic->m_fill_color);
  out->fill_style = static_cast<int>(pic->m_eFillStyle);
  for (int i = 0; i < MAX_PATTERNS; ++i) {
    CCGMPattern& p = pic->m_pattern[i];
    if (p.m_bits == nullptr || p.m_num_bytes <= 0) continue;
    CgmPattern out_pat;
    out_pat.index = p.m_index;
    out_pat.width = static_cast<int>(p.m_pattern_size.cx);
    out_pat.height = static_cast<int>(p.m_pattern_size.cy);
    out_pat.solid = p.IsSolid();
    out_pat.bits.assign(p.m_bits, p.m_bits + p.m_num_bytes);
    out->patterns.push_back(std::move(out_pat));
  }
}

}  // namespace

CgmSymbol::CgmSymbol() = default;
CgmSymbol::~CgmSymbol() = default;

Status CgmSymbol::LoadBuffer(const void* data, size_t size) {
  elements_.clear();
  bounds_ = CgmRect();
  line_style_ = CgmLineStyle();
  area_style_ = CgmAreaStyle();
  vdc_width_ = vdc_height_ = 0;
  dir_x_ = 1;
  dir_y_ = -1;
  name_.clear();

  if (data == nullptr || size == 0)
    return Status::Error(kInvalidArg, "empty CGM buffer");

  CCGMFile file;
  // bVDCOrientationEnable=TRUE matches how GeoSymServer loads symbols: the
  // VDC EXTENT's direction multipliers are honoured, so parsed coordinates
  // are already in the metafile's intended orientation.
  const CGM_ERROR err = file.LoadCGM(
      reinterpret_cast<PCHAR>(const_cast<void*>(data)),
      static_cast<UINT>(size), TRUE, FALSE);
  if (err != E_CGM_SUCCESS) {
    char msg[96];
    snprintf(msg, sizeof(msg), "CGM parse failed (CGM_ERROR %d)",
             static_cast<int>(err));
    return Status::Error(kIoError, msg);
  }

  RECT rc;
  file.GetBoundingRectangle(rc);
  bounds_.left = rc.left;
  bounds_.top = rc.top;
  bounds_.right = rc.right;
  bounds_.bottom = rc.bottom;
  name_.assign(static_cast<const char*>(file.m_meta_filename),
               file.m_meta_filename.GetLength());

  // Walk every picture in file order (GeoSym symbols have exactly one, but
  // the parser supports more and CCGMFile keeps them all).
  for (int p = 0; p < file.m_apPictures.GetSize(); ++p) {
    CCGMPicture* pic = file.m_apPictures[p];
    if (pic == nullptr) continue;
    if (p == 0) {
      CopyLineStyle(pic, &line_style_);
      CopyAreaStyle(pic, &area_style_);
      dir_x_ = static_cast<int>(pic->m_iDirX);
      dir_y_ = static_cast<int>(pic->m_iDirY);
      // Symbol sizing uses the VDC EXTENT, not the drawn extent — see
      // CCGMSymbol::DrawSymbol, which maps a nMaxDim-square window onto the
      // device box.
      if (file.m_vdctype == CGM_VDCTYPE_INTEGER) {
        vdc_width_ = std::labs(static_cast<long>(pic->m_vdc_extent.int_extent.llx) -
                               static_cast<long>(pic->m_vdc_extent.int_extent.urx));
        vdc_height_ = std::labs(static_cast<long>(pic->m_vdc_extent.int_extent.lly) -
                                static_cast<long>(pic->m_vdc_extent.int_extent.ury));
      } else {
        vdc_width_ = static_cast<long>(std::fabs(pic->m_vdc_extent.real_extent.llx -
                                                 pic->m_vdc_extent.real_extent.urx));
        vdc_height_ = static_cast<long>(std::fabs(pic->m_vdc_extent.real_extent.lly -
                                                  pic->m_vdc_extent.real_extent.ury));
      }
    }
    POSITION pos = pic->m_drawing_objects.GetHeadPosition();
    while (pos != nullptr) {
      CCGMDrawingObject* obj = pic->m_drawing_objects.GetNext(pos);
      if (obj == nullptr) continue;
      CgmElement elem;
      if (Flatten(obj, &elem)) elements_.push_back(std::move(elem));
    }
  }
  return Status::Ok();
}

Status CgmSymbol::LoadFile(const std::string& path) {
  // Reset up front: a load that fails before reaching LoadBuffer (missing
  // file, short read) must not leave the PREVIOUS symbol's elements visible.
  elements_.clear();
  bounds_ = CgmRect();
  line_style_ = CgmLineStyle();
  area_style_ = CgmAreaStyle();
  vdc_width_ = vdc_height_ = 0;
  dir_x_ = 1;
  dir_y_ = -1;
  name_.clear();

  std::string resolved = FvResolveWin32Path(path.c_str());
  FILE* f = fopen(resolved.c_str(), "rb");
  if (f == nullptr)
    return Status::Error(kNotFound, "cannot open CGM: " + path);

  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  if (len <= 0) {
    fclose(f);
    return Status::Error(kIoError, "empty CGM file: " + path);
  }
  fseek(f, 0, SEEK_SET);
  std::vector<char> buf(static_cast<size_t>(len));
  const size_t got = fread(buf.data(), 1, buf.size(), f);
  fclose(f);
  if (got != buf.size())
    return Status::Error(kIoError, "short read on CGM: " + path);

  Status s = LoadBuffer(buf.data(), buf.size());
  if (!s.ok()) s.message += " [" + path + "]";
  return s;
}

}  // namespace fv
