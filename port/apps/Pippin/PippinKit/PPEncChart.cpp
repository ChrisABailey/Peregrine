// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "PPEncChart.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "fv_enc_format.h"
#include "fv_enc_index.h"
#include "fv_enc_names.h"
#include "fv_enc_vector_source.h"
#include "fv_s52_style.h"
#include "fv_s57.h"
#include "fvkit/app/search.h"
#include "fvkit/vector/renderer.h"

namespace pippin {

namespace {

/// Neighbouring cells each carry their own copy of a name (a river, a sea
/// area); copies closer than this, in authored pixels, are drawn once.
constexpr double kLabelRepeatPoints = 120.0;

constexpr double kFeetPerMetre = 1.0 / 0.3048;

/** The raw value of attribute `code`, or nullptr when absent or empty. */
const std::string* Raw(const fv::FeatureDescription& d, const char* code) {
  for (const fv::FeatureAttribute& a : d.attributes)
    if (a.code == code && !a.raw.empty()) return &a.raw;
  return nullptr;
}

/** `v` with at most one decimal and no trailing ".0". */
std::string Number(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f", v);
  std::string t = buf;
  if (t.size() > 2 && t.compare(t.size() - 2, 2, ".0") == 0) t.resize(t.size() - 2);
  return t == "-0" ? "0" : t;
}

std::string Depth(const std::string& metres, EncDepthUnits units) {
  const double m = std::atof(metres.c_str());
  return units == EncDepthUnits::kFeet ? Number(m * kFeetPerMetre) + " ft"
                                       : Number(m) + " m";
}

/** Chart No. 1 abbreviation for an S-57 LITCHR code; nullptr for the
 *  composite rhythms, which keep the catalogue's wording. */
const char* LightRhythm(int litchr) {
  switch (litchr) {
    case 1: return "F";
    case 2: return "Fl";
    case 3: return "LFl";
    case 4: return "Q";
    case 5: return "VQ";
    case 6: return "UQ";
    case 7: return "Iso";
    case 8: return "Oc";
    case 9: return "IQ";
    case 10: return "IVQ";
    case 11: return "IUQ";
    case 12: return "Mo";
    case 17: return "Al.Oc";
    case 18: return "Al.LFl";
    case 19: return "Al.Fl";
    case 28: return "Al";
    default: return nullptr;
  }
}

/** Chart No. 1 letter for an S-57 COLOUR code; nullptr when it has none. */
const char* LightColour(int colour) {
  switch (colour) {
    case 1: return "W";
    case 3: return "R";
    case 4: return "G";
    case 5: return "Bu";
    case 6: return "Y";
    case 9: return "Am";
    case 10: return "Vi";
    case 11: return "Or";
    default: return nullptr;
  }
}

/** "Fl(2) G 6s 12m 5M" from a LIGHTS feature's attributes. */
std::string LightSummary(const fv::FeatureDescription& d) {
  std::string out;
  if (const std::string* c = Raw(d, "LITCHR")) {
    const char* r = LightRhythm(std::atoi(c->c_str()));
    if (r != nullptr) {
      out = r;
    } else {
      for (const fv::FeatureAttribute& a : d.attributes)
        if (a.code == "LITCHR") out = a.display;
    }
  }
  // A single flash's "(1)" is left off on charts.
  if (const std::string* g = Raw(d, "SIGGRP"))
    if (*g != "(1)" && *g != "()") out += *g;
  if (const std::string* c = Raw(d, "COLOUR")) {
    std::string letters;
    size_t start = 0;
    while (start <= c->size()) {
      const size_t comma = c->find(',', start);
      const std::string one = c->substr(start, comma == std::string::npos
                                                   ? std::string::npos
                                                   : comma - start);
      if (const char* l = LightColour(std::atoi(one.c_str()))) letters += l;
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
    if (!letters.empty()) out += (out.empty() ? "" : " ") + letters;
  }
  auto add = [&](const char* code, const char* suffix) {
    if (const std::string* v = Raw(d, code))
      out += (out.empty() ? "" : " ") + Number(std::atof(v->c_str())) + suffix;
  };
  add("SIGPER", "s");
  // Light heights stay in metres in the chart notation regardless of units.
  add("HEIGHT", "m");
  add("VALNMR", "M");
  return out;
}

}  // namespace

bool EncChartSettings::operator==(const EncChartSettings& o) const {
  return safety_contour == o.safety_contour && safety_depth == o.safety_depth &&
         shallow_contour == o.shallow_contour && deep_contour == o.deep_contour &&
         category == o.category && palette == o.palette &&
         navaid_names == o.navaid_names &&
         symbolized_boundaries == o.symbolized_boundaries;
}

std::string EncFeatureSummary(const fv::FeatureDescription& d, EncDepthUnits units) {
  const std::string& cls = d.class_name;
  if (cls == "LIGHTS") return LightSummary(d);
  if (cls == "DEPARE" || cls == "DRGARE") {
    const std::string* lo = Raw(d, "DRVAL1");
    const std::string* hi = Raw(d, "DRVAL2");
    if (lo != nullptr && hi != nullptr) {
      const std::string a = Depth(*lo, units), b = Depth(*hi, units);
      const size_t sp = a.find(' ');
      return "Depth " + a.substr(0, sp) + "\u2013" + b;
    }
    if (lo != nullptr) return "Depth " + Depth(*lo, units) + " or more";
    return {};
  }
  if (cls == "DEPCNT") {
    if (const std::string* v = Raw(d, "VALDCO")) return "Contour " + Depth(*v, units);
    return {};
  }
  if (cls == "WRECKS" || cls == "OBSTRN" || cls == "UWTROC") {
    if (const std::string* v = Raw(d, "VALSOU")) return "Depth " + Depth(*v, units);
    return {};
  }
  return {};
}

struct EncChart::Band {
  std::shared_ptr<fv::EncVectorSource> source;
  std::unique_ptr<fv::VectorRenderer> base;
  std::unique_ptr<fv::VectorRenderer> underlay;
};

int EncBandForScale(const std::vector<int>& bands, double scale_denominator) {
  int best = 0;
  double best_d = 0.0;
  const double target = std::log(std::max(scale_denominator, 1.0));
  for (int b : bands) {
    const double nominal = fv::EncBandNominalScale(b);
    if (nominal <= 0.0) continue;
    const double d = std::fabs(std::log(nominal) - target);
    if (best == 0 || d < best_d || (d == best_d && b > best)) {
      best = b;
      best_d = d;
    }
  }
  return best;
}

/** Search over a chart: names from its table, areas from its last drawn band.
 *  Detached when the chart goes. */
class EncDrawnBandSource : public fv::IVectorSource {
 public:
  explicit EncDrawnBandSource(EncChart* chart) : chart_(chart) {}
  void Detach() { chart_ = nullptr; }

  fv::Status Open(const std::string&) override {
    return fv::Status::Error(fv::kUnsupported, "opened by its EncChart");
  }
  bool IsOpen() const override { return chart_ != nullptr; }
  fv::GeoRect Bounds() const override {
    return chart_ != nullptr ? chart_->coverage() : fv::GeoRect{};
  }
  std::vector<std::string> Layers() const override {
    return band() != nullptr ? band()->Layers() : std::vector<std::string>{};
  }
  fv::Status Query(const fv::VectorQuery& q, std::vector<fv::VectorFeature>* out) override {
    if (band() == nullptr) return fv::Status::Error(fv::kInvalidArg, "no chart drawn yet");
    return band()->Query(q, out);
  }
  fv::Status Describe(const fv::FeatureRef& ref, fv::FeatureDescription* out) override {
    if (ref.source == kNameRow)
      return fv::Status::Error(fv::kUnsupported, "a name-table row has no feature");
    if (band() == nullptr) return fv::Status::Error(fv::kInvalidArg, "no chart drawn yet");
    return band()->Describe(ref, out);
  }

  bool HasNameIndex() const override { return chart_ != nullptr; }

  fv::Status SearchNames(const fv::VectorNameQuery& q,
                         std::vector<fv::VectorNameHit>* out) override {
    out->clear();
    if (chart_ == nullptr) return fv::Status::Error(fv::kUnsupported, "chart closed");
    const std::vector<fv::EncNameEntry>& names = chart_->names();
    const fv::GeoPoint centre = chart_->last_center();
    struct Ranked {
      int quality;
      double distance;
      size_t row;
    };
    std::vector<Ranked> ranked;
    for (size_t i = 0; i < names.size(); ++i) {
      const fv::EncNameEntry& e = names[i];
      const int quality = fv::app::TextMatchQuality(q.text, e.name);
      if (quality < 0) continue;
      if (q.area && !q.area->Intersects(e.bounds)) continue;
      ranked.push_back({quality, fv::app::SearchDistanceMeters(centre, e.position), i});
    }
    // Best match, then nearest the chart on screen, so a capped answer keeps
    // the buoys in view rather than the first ones in the file.
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
      return a.quality != b.quality ? a.quality < b.quality : a.distance < b.distance;
    });
    if (q.max_results > 0 && ranked.size() > q.max_results) ranked.resize(q.max_results);
    for (const Ranked& r : ranked) {
      const fv::EncNameEntry& e = names[r.row];
      fv::VectorNameHit h;
      h.name = e.name;
      h.layer = e.object_class;
      h.style_key = e.object_class;
      h.position = e.position;
      h.bounds = e.bounds;
      h.ref.source = kNameRow;
      h.ref.layer = 0;
      h.ref.feature = static_cast<int32_t>(r.row);
      out->push_back(std::move(h));
    }
    return fv::Status::Ok();
  }

 private:
  fv::EncVectorSource* band() const {
    return chart_ != nullptr ? chart_->last_source() : nullptr;
  }
  /** FeatureRef::source marking a hit that came from the name table. */
  static constexpr int32_t kNameRow = -2;
  EncChart* chart_;
};

EncChart::EncChart() = default;

EncChart::~EncChart() {
  if (search_source_) search_source_->Detach();
}

const std::vector<fv::EncNameEntry>& EncChart::names() {
  if (names_loaded_) return names_;
  names_loaded_ = true;
  const std::string path = cell_root_ + "/" + fv::kEncNamesFileName;
  if (fv::ReadEncNames(path, &names_).ok()) return names_;
  std::vector<std::string> cells;
  for (const auto& kv : cells_)
    for (const fv::EncCellEntry& c : kv.second) cells.push_back(c.path);
  // Cached only beside an installed set's index: those cells never change,
  // while a pack or fixture directory can be edited under a stale table.
  if (fv::BuildEncNames(cells, s52_dir_, &names_).ok() && indexed_)
    fv::WriteEncNames(path, names_);
  return names_;
}

std::shared_ptr<fv::IVectorSource> EncChart::search_source() {
  if (!search_source_) search_source_ = std::make_shared<EncDrawnBandSource>(this);
  return search_source_;
}

size_t EncChart::cell_count() const {
  size_t n = 0;
  for (const auto& kv : cells_) n += kv.second.size();
  return n;
}

fv::GeoRect EncChart::coverage() const {
  constexpr int kFirstDetailedBand = 3;
  const bool has_detail =
      !bands_.empty() && bands_.back() >= kFirstDetailedBand;
  fv::GeoRect r{{90.0, 180.0}, {-90.0, -180.0}};
  for (const auto& kv : cells_) {
    if (has_detail && kv.first < kFirstDetailedBand) continue;
    for (const fv::EncCellEntry& c : kv.second) {
      r.ll.lat = std::min(r.ll.lat, c.bounds.ll.lat);
      r.ll.lon = std::min(r.ll.lon, c.bounds.ll.lon);
      r.ur.lat = std::max(r.ur.lat, c.bounds.ur.lat);
      r.ur.lon = std::max(r.ur.lon, c.bounds.ur.lon);
    }
  }
  return r;
}

fv::Status EncChart::Open(const std::string& cell_root, const std::string& s52_dir) {
  cells_.clear();
  open_.clear();
  drawn_.clear();
  last_bands_.clear();
  last_band_ = 0;
  last_renderer_ = nullptr;
  last_source_ = nullptr;
  outdated_cells_ = 0;
  fv::Status s;
  const std::string index_path = cell_root + "/" + fv::kEncIndexFileName;
  std::error_code ec;
  indexed_ = std::filesystem::is_regular_file(index_path, ec);
  if (indexed_) {
    std::vector<fv::EncIndexEntry> indexed;
    s = fv::ReadEncIndex(index_path, &indexed);
    if (s.code == fv::kUnsupported) {
      // An index from an older build: rebuilt once, in place.
      s = fv::BuildEncIndex(cell_root, &indexed);
      if (s.ok()) fv::WriteEncIndex(index_path, indexed);
    }
    if (!s.ok()) return s;
    for (const fv::EncIndexEntry& e : indexed) {
      if (e.cancelled) continue;
      if (e.pending_updates() > 0) ++outdated_cells_;
      if (fv::EncBandNominalScale(e.usage_band) > 0.0)
        cells_[e.usage_band].push_back(
            fv::EncCellEntry{cell_root + "/" + e.path, e.bounds});
    }
  } else {
    // Bounds come from CATALOG.031; a cell missing from it is parsed once here.
    fv::EncFrameEnumerator index;
    s = index.Begin(cell_root);
    if (!s.ok() && s.code != fv::kUnsupported) return s;
    fv::FrameInfo f;
    while (s.ok() && index.Next(&f)) {
      const fv::S57CellName name = fv::ParseCellName(f.path);
      if (name.valid && fv::EncBandNominalScale(name.usage_band) > 0.0)
        cells_[name.usage_band].push_back(fv::EncCellEntry{f.path, f.bounds});
    }
  }
  if (cells_.empty())
    return fv::Status::Error(fv::kNotFound, "no ENC cells under " + cell_root);
  bands_.clear();
  for (const auto& kv : cells_) bands_.push_back(kv.first);

  auto style = std::make_shared<fv::S52StyleEngine>();
  s = style->Open(s52_dir);
  if (!s.ok()) return s;
  style_ = std::move(style);
  s52_dir_ = s52_dir;
  cell_root_ = cell_root;
  names_.clear();
  names_loaded_ = false;
  ApplySettings();
  return fv::Status::Ok();
}

void EncChart::SetDrawLabels(bool on) {
  if (style_) style_->SetDrawLabels(on);
}

void EncChart::SetSettings(const EncChartSettings& settings) {
  EncChartSettings s = settings;
  s.shallow_contour = std::min(s.shallow_contour, s.safety_contour);
  s.deep_contour = std::max(s.deep_contour, s.safety_contour);
  if (s == settings_) return;
  settings_ = s;
  ApplySettings();
}

void EncChart::ApplySettings() {
  if (!style_) return;
  fv::MarinerSettings m = style_->mariner();
  m.safety_contour = settings_.safety_contour;
  m.safety_depth = settings_.safety_depth;
  m.shallow_contour = settings_.shallow_contour;
  m.deep_contour = settings_.deep_contour;
  if (m != style_->mariner()) style_->SetMariner(m);

  int max_category = fv::kDisplayOther;
  if (settings_.category == EncDisplayCategory::kBase) max_category = fv::kDisplayBase;
  if (settings_.category == EncDisplayCategory::kStandard)
    max_category = fv::kDisplayStandard;
  style_->viewing_groups().SetMaxCategory(max_category);

  fv::S52ColorScheme scheme = fv::S52ColorScheme::kDay;
  if (settings_.palette == EncPalette::kDusk) scheme = fv::S52ColorScheme::kDusk;
  if (settings_.palette == EncPalette::kNight) scheme = fv::S52ColorScheme::kNight;
  if (scheme != style_->color_scheme()) style_->SetColorScheme(scheme);

  // The identify panel still gives each buoy's and beacon's name on a tap.
  style_->SetShowBuoyNames(settings_.navaid_names);
  style_->SetShowBeaconNames(settings_.navaid_names);
  style_->SetAreaStyle(settings_.symbolized_boundaries
                           ? fv::S52AreaStyle::kSymbolizedBoundaries
                           : fv::S52AreaStyle::kPlainBoundaries);
}

fv::FvColor EncChart::ground_color() const {
  // Day keeps the paper-chart white; dusk and night use the table's no-data
  // colour so the screen does not glare around the charted area.
  fv::FvColor c{255, 255, 255, 255};
  if (style_ && settings_.palette != EncPalette::kDay &&
      style_->library().Color("NODTA", &c)) {
    c.a = 255;
  }
  return c;
}

std::vector<EncFeatureInfo> EncChart::FeaturesAt(double x, double y, double tolerance,
                                                 EncDepthUnits units, size_t max_items) {
  std::vector<EncFeatureInfo> out;
  // The finest drawn band with anything under the point answers; a coarser
  // band answers only where the finer cells have no data.
  std::vector<fv::PickHit> hits;
  fv::EncVectorSource* source = nullptr;
  for (Band* b : drawn_) {
    hits = b->base->pick_index().HitTest((int)std::lround(x), (int)std::lround(y),
                                         tolerance);
    source = b->source.get();
    if (!hits.empty()) break;
  }
  for (const fv::PickHit& hit : hits) {
    if (out.size() >= max_items) break;
    fv::FeatureDescription d;
    if (!source->Describe(hit.ref, &d).ok()) continue;
    EncFeatureInfo info;
    info.kind = d.title;
    info.acronym = d.class_name;
    const std::string* name = Raw(d, "OBJNAM");
    info.title = name != nullptr ? *name : d.title;
    info.summary = EncFeatureSummary(d, units);
    for (const fv::FeatureAttribute& a : d.attributes)
      if (!a.display.empty()) info.attributes.emplace_back(a.name, a.display);
    info.source = d.source_note;
    out.push_back(std::move(info));
  }
  return out;
}

fv::Status EncChart::OpenBand(int band, Band** out) {
  auto it = open_.find(band);
  if (it == open_.end()) {
    auto b = std::make_unique<Band>();
    b->source = std::make_shared<fv::EncVectorSource>();
    b->source->SetMaxResidentCells(max_resident_cells_);
    const fv::Status s = b->source->OpenIndexed(cells_[band], s52_dir_);
    if (!s.ok()) return s;
    b->base = std::make_unique<fv::VectorRenderer>(b->source, style_);
    b->base->SetSceneMargin(scene_margin_);
    b->base->SetSimplifyPixels(simplify_px_);
    // An underlay is redrawn at a new camera, never re-panned.
    b->underlay = std::make_unique<fv::VectorRenderer>(b->source, style_);
    b->underlay->SetSimplifyPixels(simplify_px_);
    it = open_.emplace(band, std::move(b)).first;
  }
  *out = it->second.get();
  return fv::Status::Ok();
}

void EncChart::ReleaseOtherBands(const std::vector<int>& keep) {
  for (auto& kv : open_) {
    if (std::find(keep.begin(), keep.end(), kv.first) != keep.end()) continue;
    kv.second->source->ReleaseCells();
    kv.second->base->InvalidateScene();
    kv.second->underlay->InvalidateScene();
  }
}

size_t EncChart::resident_cell_count() const {
  size_t n = 0;
  for (const auto& kv : open_) n += kv.second->source->resident_cell_count();
  return n;
}

std::vector<int> EncChart::BandsForView(const fv::MapProjection& proj) const {
  const int wanted = EncBandForScale(bands_, proj.Scale());
  const fv::GeoRect view = proj.VmapBounds();
  std::vector<int> out;
  for (auto it = bands_.rbegin(); it != bands_.rend(); ++it) {
    if (*it > wanted) continue;
    bool meets = false, covers = false;
    for (const fv::EncCellEntry& c : cells_.at(*it)) {
      if (!c.bounds.Intersects(view)) continue;
      meets = true;
      if (c.bounds.Contains(view.ll) && c.bounds.Contains(view.ur) &&
          c.bounds.Contains({view.ll.lat, view.ur.lon}) &&
          c.bounds.Contains({view.ur.lat, view.ll.lon})) {
        covers = true;
        break;
      }
    }
    if (meets) out.push_back(*it);
    if (covers) break;
  }
  // Nothing charted in view: draw the scale's band as before (an empty ground).
  if (out.empty() && wanted != 0) out.push_back(wanted);
  std::reverse(out.begin(), out.end());
  return out;
}

fv::Status EncChart::Render(const fv::MapProjection& proj, double device_dpi,
                            double symbol_scale, EncChartPass pass,
                            fv::ICanvas* canvas) {
  if (!style_) return fv::Status::Error(fv::kInvalidArg, "ENC chart is not open");
  canvas->Clear(ground_color());
  const std::vector<int> bands = BandsForView(proj);
  if (pass == EncChartPass::kBase && bands != last_bands_) ReleaseOtherBands(bands);
  if (pass == EncChartPass::kBase) drawn_.clear();
  // Coarsest first: a finer band's opaque area fills cover the coarser chart
  // wherever the finer cells have data, and the coarser chart shows elsewhere.
  for (int band : bands) {
    Band* b = nullptr;
    const fv::Status s = OpenBand(band, &b);
    if (!s.ok()) return s;
    fv::VectorRenderer& r = pass == EncChartPass::kBase ? *b->base : *b->underlay;
    r.SetDeviceDpi(device_dpi);
    r.SetSymbolScale(symbol_scale);
    r.SetLabelRepeatPixels(kLabelRepeatPoints * symbol_scale);
    const fv::Status rs = r.Render(proj, canvas);
    if (!rs.ok()) return rs;
    if (pass == EncChartPass::kBase) drawn_.insert(drawn_.begin(), b);
  }
  if (pass == EncChartPass::kBase && !bands.empty()) {
    Band* top = drawn_.front();
    last_bands_ = bands;
    last_band_ = bands.back();
    last_renderer_ = top->base.get();
    last_source_ = top->source.get();
    last_center_ = proj.Center();
  }
  return fv::Status::Ok();
}

}  // namespace pippin
