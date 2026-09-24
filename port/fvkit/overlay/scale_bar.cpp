// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * @file fvkit/overlay/scale_bar.cpp
 * The map scale bar -- see fvkit/overlay/scale_bar.h. Transcribed from
 * Applications/FalconView/scalebar/Scalebar.cpp; the draw order, offsets and
 * arithmetic order follow CScaleBarIcon::draw.
 */

#include "fvkit/overlay/scale_bar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "fvkit/canvas/label_placer.h"
#include "geo_tool.h"  // GEO_geo_to_distance, GEO_distance_to_geo

namespace fv {
namespace {

using app::PropertySpec;
using app::PropertyType;
using app::PropertyValue;

enum Prop {
  kOrientation = 0,
  kUnits,
  kFontSize,
  kColor,
  kBackColor,
  kLabelFont,
  kPropCount
};

enum FontSize { kSmall = 0, kLarge = 1 };

constexpr double kMetersPerNm = 1852.0;
// NOTE: FalconView's feet per nautical mile. The international figure is
// 6076.115; this one makes a "foot" 0.30474 m. Kept for fidelity.
constexpr double kFeetPerNm = 6077.28;

// The hint FalconView showed for the icon, as tooltip and status text alike.
constexpr char kHint[] = "Map Scale Bar";

PropertySpec Spec(const char* key, const char* label, const char* group,
                  PropertyValue def, const char* help = "") {
  PropertySpec s;
  s.key = key;
  s.label = label;
  s.group = group;
  s.type = def.type;
  s.default_value = def;
  s.help = help;
  return s;
}

PropertySpec ChoiceSpec(const char* key, const char* label, long long def,
                        std::vector<std::string> choices) {
  PropertySpec s = Spec(key, label, "Scale bar", PropertyValue::Choice(def));
  s.choices = std::move(choices);
  return s;
}

const std::vector<PropertySpec>& Specs() {
  static const std::vector<PropertySpec> specs = [] {
    std::vector<PropertySpec> v;
    v.reserve(kPropCount);
    v.push_back(ChoiceSpec("orientation", "Orientation", 0,
                           {"vertical", "horizontal", "both"}));
    v.push_back(ChoiceSpec("units", "Units", 0,
                           {"nm_yd", "nm_ft", "km_m"}));
    v.push_back(ChoiceSpec("font_size", "Font size", kLarge,
                           {"small", "large"}));
    v.push_back(Spec("color", "Colour", "Scale bar",
                     PropertyValue::Color(FvColor{0, 0, 0, 255}),
                     "Ruler, ticks, label text and label outline."));
    v.push_back(Spec("back_color", "Background colour", "Scale bar",
                     PropertyValue::Color(FvColor{192, 192, 192, 255}),
                     "The casing under the ruler and the label boxes. "
                     "FalconView's light gray, RGB(192, 192, 192)."));
    v.push_back(Spec("label_font", "Label font", "Scale bar",
                     PropertyValue::String(std::string()),
                     "Path to a TTF/TTC file; empty = the canvas's own."));
    return v;
  }();
  return specs;
}

int IndexOf(const std::string& key) {
  const std::vector<PropertySpec>& specs = Specs();
  for (size_t i = 0; i < specs.size(); ++i)
    if (specs[i].key == key) return static_cast<int>(i);
  return -1;
}

/// The two loops of get_scale_params, run from a starting increment. Returns
/// the division count before FalconView's trailing `num++`, or -1 if the
/// increment underflows (a distance too small to divide).
int FitIncrement(double dist, double start, int min_div, int max_div,
                 double* inc) {
  *inc = start;
  int num = 0;
  while (num < min_div) {
    *inc /= 10;
    // Unreachable for any real view; stops the loop Windows would spin in.
    if (*inc < 1e-12) return -1;
    num = static_cast<int>(dist / *inc);
  }
  while (num > max_div) {
    *inc *= 2;
    num = static_cast<int>(dist / *inc);
  }
  return num;
}

/// Pixel quantity scaled for the device, never below one pixel.
int Scaled(int px, double dpi) {
  return std::max(1, static_cast<int>(std::lround(px * dpi)));
}

PixelSize Extent(ICanvas& canvas, const std::string& text,
                 const TextStyle& style) {
  PixelSize s;
  if (!canvas.GetTextExtent(text, style, &s).ok()) return PixelSize{};
  return s;
}

int RoundPx(double v) { return static_cast<int>(std::lround(v)); }

}  // namespace

ScaleBarDivisions ChooseScaleBarDivisions(double distance_m, ScaleBarUnits units,
                                          int min_divisions, int max_divisions) {
  ScaleBarDivisions d;
  if (!(distance_m > 0.0) || !std::isfinite(distance_m)) return d;

  double cnvnum = kMetersPerNm;
  double dist = distance_m;
  double small_per_large = 0.0;  // small units in one large unit
  const char* large_name = " NM";
  const char* small_name = " Yd";
  switch (units) {
    case ScaleBarUnits::kNmYards:
      dist /= kMetersPerNm;
      small_per_large = kFeetPerNm / 3.0;
      break;
    case ScaleBarUnits::kNmFeet:
      dist /= kMetersPerNm;
      small_per_large = kFeetPerNm;
      small_name = " Ft";
      break;
    case ScaleBarUnits::kKmMeters:
      cnvnum = 1000.0;
      dist /= 1000.0;
      small_per_large = 1000.0;
      large_name = " Km";
      small_name = " m";
      break;
  }

  double inc = 0.0;
  int num = FitIncrement(dist, 1000000, min_divisions, max_divisions, &inc);
  if (num < 0) return d;
  std::string unit_name = large_name;
  if (inc < 1) {
    unit_name = small_name;
    dist *= small_per_large;
    cnvnum /= small_per_large;
    num = FitIncrement(dist, 10000000, min_divisions, max_divisions, &inc);
    if (num < 0) return d;
  }
  // NOTE: the small unit has no further fallback, so its increment can drop
  // below one and "%.0f" then prints repeated labels. Windows does the same.
  d.count = num + 1;
  d.increment = inc;
  d.meters_per_unit = cnvnum;
  d.unit_name = unit_name;
  return d;
}

const char ScaleBarOverlay::kTypeId[] = "fv.scalebar";

ScaleBarOverlay::ScaleBarOverlay() : Overlay("scalebar") {
  values_.reserve(Specs().size());
  for (const PropertySpec& s : Specs()) values_.push_back(s.default_value);
}

const std::vector<app::PropertySpec>& ScaleBarOverlay::Describe() const {
  return Specs();
}

Status ScaleBarOverlay::GetProperty(const std::string& key,
                                    app::PropertyValue* out) const {
  const int i = IndexOf(key);
  if (i < 0)
    return Status::Error(kNotFound, "no scalebar property '" + key + "'");
  if (out != nullptr) *out = values_[static_cast<size_t>(i)];
  return Status::Ok();
}

Status ScaleBarOverlay::SetProperty(const std::string& key,
                                    const app::PropertyValue& value) {
  const int i = IndexOf(key);
  if (i < 0)
    return Status::Error(kNotFound, "no scalebar property '" + key + "'");
  const PropertySpec& spec = Specs()[static_cast<size_t>(i)];
  if (value.type != spec.type)
    return Status::Error(kInvalidArg,
                         "wrong type for scalebar property '" + key + "'");
  if (spec.type == PropertyType::kChoice &&
      (value.i < 0 || value.i >= static_cast<long long>(spec.choices.size())))
    return Status::Error(kInvalidArg,
                         "scalebar property '" + key + "' has no such choice");
  values_[static_cast<size_t>(i)] = value;
  return Status::Ok();
}

Status ScaleBarOverlay::OnDraw(const MapProjection& proj, ICanvas& canvas) {
  stats_ = DrawStats{};
  label_boxes_.clear();

  const PixelSize surface = proj.SurfaceSize();
  const int screen_width = surface.width;
  const int screen_height = surface.height;
  if (screen_width <= 0 || screen_height <= 0) return Status::Ok();

  const auto orientation =
      static_cast<ScaleBarOrientation>(GetInt("orientation", 0));
  const auto units = static_cast<ScaleBarUnits>(GetInt("units", 0));
  const bool both = orientation == ScaleBarOrientation::kBoth;
  const bool horz = orientation != ScaleBarOrientation::kVertical;
  const bool vert = orientation != ScaleBarOrientation::kHorizontal;
  const FvColor color = GetColor("color", FvColor{0, 0, 0, 255});
  const FvColor back_color = GetColor("back_color", FvColor{192, 192, 192, 255});

  const int frgd_width = Scaled(2, dpi_scale_);
  const int bkgd_width = Scaled(4, dpi_scale_);
  const int tick_length = Scaled(11, dpi_scale_);
  const int font_size_0 = GetInt("font_size", kLarge) == kSmall ? 12 : 16;
  const int font_size = Scaled(font_size_0, dpi_scale_);

  TextStyle text_style;
  text_style.font_path = GetString("label_font");
  text_style.size = font_size;
  text_style.color = color;
  const PixelSize ext0 = Extent(canvas, "0", text_style);

  const int vert_min_divisions = 5;
  const int vert_max_divisions = (20 * font_size_0) / font_size;
  const int horz_min_divisions = 3;
  const int horz_max_divisions = (10 * font_size_0) / font_size;

  Pen back_pen;
  back_pen.color = back_color;
  back_pen.width = bkgd_width;
  Pen fore_pen;
  fore_pen.color = color;
  fore_pen.width = frgd_width;

  auto line = [&canvas](int x1, int y1, int x2, int y2, const Pen& pen) {
    canvas.DrawLines({PixelPoint{x1, y1}, PixelPoint{x2, y2}}, pen);
  };

  // FalconView's UTIL_BG_RECT label: the text padded by a space each side, on
  // a box of the padded extent filled with the background colour and outlined
  // one pixel wide in the text colour.
  auto label = [&](int k, const ScaleBarDivisions& d, int x, int y,
                   LabelHAlign halign, LabelVAlign valign) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f", k * d.increment);
    std::string text = buf;
    if (k == d.count - 1) text += d.unit_name;
    stats_.labels.push_back(text);
    const std::string padded = " " + text + " ";
    LabelStyle ls;
    ls.style = text_style;
    ls.halign = halign;
    ls.valign = valign;
    const LabelInk ink = MeasureLabelInk(canvas, x, y, padded, text_style, ls);
    if (ink.measured) {
      Brush fill;
      fill.color = back_color;
      Pen outline;
      outline.color = color;
      outline.width = 1;
      canvas.DrawRectangle(ink.box, &fill, &outline);
      label_boxes_.push_back(ink.box);
    }
    canvas.DrawTextString(padded, ink.origin.x, ink.origin.y, text_style);
  };

  ScaleBarDivisions horz_div;

  if (horz) {
    int leftx = 10;
    if (both) leftx = 38 + tick_length + ext0.width;
    const PixelSize extn = Extent(canvas, "2000 NM", text_style);
    const int rightx = screen_width - (extn.width / 2);
    const int boty = screen_height - 15;
    const int centery = screen_height / 2;

    // Ten legs along the centre row, summed, so a scale that varies across
    // the row is averaged rather than read at one point.
    const int tx = (rightx - leftx) / 10;
    GeoPoint left;
    bool ok = proj.SurfaceToGeo(leftx, centery, &left).ok();
    double tdist = 0.0, tang = 0.0;
    GeoPoint t = left;
    int x2 = leftx + tx;
    for (int k = 0; ok && k < 10; ++k) {
      GeoPoint right;
      ok = proj.SurfaceToGeo(x2, centery, &right).ok();
      if (!ok) break;
      double dist = 0.0, ang = 0.0;
      GEO_geo_to_distance(t.lat, t.lon, right.lat, right.lon, &dist, &ang);
      if (k == 0) tang = ang;
      tdist += dist;
      t = right;
      x2 += tx;
    }

    if (ok) {
      horz_div = ChooseScaleBarDivisions(tdist, units, horz_min_divisions,
                                         horz_max_divisions);
    }
    double sx = 0.0, sy = 0.0;
    GeoPoint end;
    if (horz_div.count > 0 &&
        GEO_distance_to_geo(left.lat, left.lon,
                            horz_div.increment * horz_div.meters_per_unit,
                            tang, &end.lat, &end.lon) == SUCCESS &&
        proj.GeoToSurface(end, &sx, &sy).ok()) {
      const int xinc = RoundPx(sx) - leftx;
      std::vector<int> scale_x(static_cast<size_t>(horz_div.count));
      for (int k = 0; k < horz_div.count; ++k) scale_x[k] = leftx + k * xinc;
      const int last = scale_x.back();

      line(leftx, boty, last, boty, back_pen);
      for (int x : scale_x) line(x, boty, x, boty - tick_length, back_pen);

      line(leftx, boty, last, boty, fore_pen);
      for (int k = 0; k < horz_div.count; ++k) {
        line(scale_x[k], boty, scale_x[k], boty - tick_length, fore_pen);
        label(k, horz_div, scale_x[k], boty - tick_length - 2,
              LabelHAlign::kCenter, LabelVAlign::kBottom);
      }
      stats_.horizontal = horz_div;
      stats_.horizontal_x = std::move(scale_x);
    }
  }

  if (vert) {
    const int topx = 5;
    const int topy = ext0.height / 2;
    const int botx = 5;
    int boty = screen_height - 15;
    if (both) boty -= 37 + tick_length + ext0.height;
    const int centerx = screen_width / 2;

    GeoPoint top, bot;
    if (proj.SurfaceToGeo(centerx, topy, &top).ok() &&
        proj.SurfaceToGeo(centerx, boty, &bot).ok()) {
      double dist = 0.0, ang = 0.0;
      GEO_geo_to_distance(bot.lat, bot.lon, top.lat, top.lon, &dist, &ang);

      // NOTE: in "Both" the vertical ruler takes the horizontal ruler's
      // increment and unit rather than choosing its own, so its tick count
      // follows the view's aspect ratio. Windows' 20-entry tick array could
      // overflow on a tall view; the vector here cannot.
      ScaleBarDivisions div =
          both ? horz_div
               : ChooseScaleBarDivisions(dist, units, vert_min_divisions,
                                         vert_max_divisions);

      std::vector<int> scale_y;
      if (div.count > 0) {
        const double step_m = div.increment * div.meters_per_unit;
        for (int k = 0; static_cast<double>(k) * div.increment *
                                div.meters_per_unit <
                            dist;
             ++k) {
          GeoPoint p;
          double sx = 0.0, sy = 0.0;
          if (GEO_distance_to_geo(bot.lat, bot.lon, k * step_m, ang, &p.lat,
                                  &p.lon) != SUCCESS ||
              !proj.GeoToSurface(p, &sx, &sy).ok()) {
            break;
          }
          scale_y.push_back(RoundPx(sy));
          // Bounds a pathological count; a real view has tens of ticks.
          if (scale_y.size() >= 10000) break;
        }
      }
      // The recount above replaces the chooser's count, as in Windows.
      div.count = static_cast<int>(scale_y.size());

      if (div.count > 0) {
        const int top_tick = scale_y.back();
        line(topx, top_tick, botx, boty, back_pen);
        for (int y : scale_y) line(botx, y, botx + tick_length, y, back_pen);

        line(topx, top_tick + 1, botx, boty - 1, fore_pen);
        for (int k = 0; k < div.count; ++k) {
          line(botx, scale_y[k], botx + tick_length, scale_y[k], fore_pen);
          label(k, div, botx + tick_length + 2, scale_y[k],
                LabelHAlign::kLeft, LabelVAlign::kCenter);
        }
        stats_.vertical = div;
        stats_.vertical_y = std::move(scale_y);
      }
    }
  }

  return Status::Ok();
}

void ScaleBarOverlay::HitTestPoint(const MapProjection& proj, PixelPoint p,
                                   double tolerance_px,
                                   std::vector<app::HitItem>& out) {
  (void)proj;
  double best = -1.0;
  for (const PixelRect& r : label_boxes_) {
    const double dx =
        std::max({0.0, static_cast<double>(r.x - p.x),
                  static_cast<double>(p.x - (r.x + r.width))});
    const double dy =
        std::max({0.0, static_cast<double>(r.y - p.y),
                  static_cast<double>(p.y - (r.y + r.height))});
    const double d = std::hypot(dx, dy);
    if (d <= tolerance_px && (best < 0.0 || d < best)) best = d;
  }
  if (best < 0.0) return;
  app::HitItem item;
  item.overlay = this;
  item.distance_px = best;
  item.hint.tool_tip = kHint;
  item.hint.status = kHint;
  out.push_back(item);
}

}  // namespace fv
