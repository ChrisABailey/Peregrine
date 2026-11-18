// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_map_options.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>

#include "fv_desk_enc_import.h"
#include "fv_desk_moving_map.h"
#include "fv_desk_ownship.h"
#include "fv_desk_vector_map.h"
#include "fv_route_overlay.h"
#include "fvkit/overlay/moving_map_overlay.h"
#include "fvkit/formats/dted_shaded.h"

namespace fv {
namespace desk {
namespace {

constexpr size_t kMaxBands = 5;

/// The Elevation page: one string property holding the breakpoint list.
class ElevationOptions : public app::Properties {
 public:
  ElevationOptions() { value_ = Describe().front().default_value; }

  const std::vector<app::PropertySpec>& Describe() const override {
    static const std::vector<app::PropertySpec> specs = [] {
      app::PropertySpec s;
      s.key = "elevation_bands_ft";
      s.label = "Colour breaks (ft)";
      s.group = "Shaded relief";
      s.type = app::PropertyType::kString;
      s.default_value = app::PropertyValue::String(std::string());
      s.help = "Up to five elevations in feet, lowest first, where the shaded relief "
               "changes colour. Empty uses 2500, 5000, 7500, 10000, 12500.";
      return std::vector<app::PropertySpec>{s};
    }();
    return specs;
  }

  Status GetProperty(const std::string& key, app::PropertyValue* out) const override {
    if (key != "elevation_bands_ft") return Status::Error(kNotFound, "no property '" + key + "'");
    *out = value_;
    return Status::Ok();
  }

  Status SetProperty(const std::string& key, const app::PropertyValue& v) override {
    if (key != "elevation_bands_ft") return Status::Error(kNotFound, "no property '" + key + "'");
    if (v.type != app::PropertyType::kString)
      return Status::Error(kInvalidArg, "elevation_bands_ft is a string");
    // Stored canonical, so the dialog shows what will be drawn.
    value_ = app::PropertyValue::String(FormatElevationBands(ParseElevationBands(v.s)));
    return Status::Ok();
  }

 private:
  app::PropertyValue value_;
};

/// A page over a fixed spec list whose values are only type-checked; the
/// meaning is applied when the page is.
class SpecProperties : public app::Properties {
 public:
  explicit SpecProperties(std::vector<app::PropertySpec> specs) : specs_(std::move(specs)) {
    for (const app::PropertySpec& s : specs_) values_[s.key] = s.default_value;
  }

  const std::vector<app::PropertySpec>& Describe() const override { return specs_; }

  Status GetProperty(const std::string& key, app::PropertyValue* out) const override {
    auto it = values_.find(key);
    if (it == values_.end()) return Status::Error(kNotFound, "no property '" + key + "'");
    *out = it->second;
    return Status::Ok();
  }

  Status SetProperty(const std::string& key, const app::PropertyValue& v) override {
    const app::PropertySpec* spec = FindSpec(key);
    if (spec == nullptr) return Status::Error(kNotFound, "no property '" + key + "'");
    if (v.type != spec->type) return Status::Error(kInvalidArg, key + " has the wrong type");
    app::PropertyValue stored = v;
    if (spec->type == app::PropertyType::kInt && spec->min < spec->max)
      stored.i = std::clamp<long long>(v.i, static_cast<long long>(spec->min),
                                       static_cast<long long>(spec->max));
    if (spec->type == app::PropertyType::kString && key.compare(0, 8, "mariner.") == 0) {
      std::optional<double> depth;
      std::optional<bool> flag;
      const bool is_flag = key == "mariner.two_shades" || key == "mariner.shallow_pattern";
      if (is_flag ? !ParseMarinerFlag(v.s, &flag) : !ParseMarinerDepth(v.s, &depth))
        return Status::Error(kInvalidArg, key + ": '" + v.s + "' is not " +
                                              (is_flag ? "on or off" : "a depth in metres"));
    }
    values_[key] = stored;
    return Status::Ok();
  }

 private:
  std::vector<app::PropertySpec> specs_;
  std::map<std::string, app::PropertyValue> values_;
};

app::PropertySpec PathSpec(const std::string& key, const std::string& label,
                           app::PathKind kind, const std::string& help) {
  app::PropertySpec s;
  s.key = key;
  s.label = label;
  s.type = app::PropertyType::kPath;
  s.path_kind = kind;
  s.default_value = app::PropertyValue::Path(std::string());
  s.help = help;
  if (kind == app::PathKind::kFile) s.path_filters = {{"GL Style Sheets (*.json)", "*.json"}};
  return s;
}

/// The [mariner] keys. They also drive DNC, but live on the ENC page alone so
/// that one open dialog cannot hold two different values for them.
void AddMarinerSpecs(std::vector<app::PropertySpec>* specs) {
  const struct {
    const char* key;
    const char* label;
    const char* help;
  } rows[] = {
      {"mariner.safety_contour", "Safety contour (m)",
       "The bold contour, where the depth shading splits. Empty keeps the chart's default."},
      {"mariner.shallow_contour", "Shallow contour (m)", "Inner edge of the shallow shade."},
      {"mariner.deep_contour", "Deep contour (m)", "Outer edge of the deep shade."},
      {"mariner.safety_depth", "Safety depth (m)",
       "ENC soundings at or below this print bold. DNC has no counterpart."},
      {"mariner.two_shades", "Two depth shades", "on or off; empty keeps the chart's default."},
      {"mariner.shallow_pattern", "Shallow pattern", "on or off; empty keeps the chart's default."},
  };
  for (const auto& r : rows) {
    app::PropertySpec s;
    s.key = r.key;
    s.label = r.label;
    s.group = "Mariner (ENC and DNC)";
    s.type = app::PropertyType::kString;
    s.default_value = app::PropertyValue::String(std::string());
    s.help = r.help;
    specs->push_back(std::move(s));
  }
}

MarinerOverrides MarinerFrom(const app::Properties& p) {
  MarinerOverrides m;
  ParseMarinerDepth(p.GetString("mariner.safety_contour"), &m.safety_contour);
  ParseMarinerDepth(p.GetString("mariner.shallow_contour"), &m.shallow_contour);
  ParseMarinerDepth(p.GetString("mariner.deep_contour"), &m.deep_contour);
  ParseMarinerDepth(p.GetString("mariner.safety_depth"), &m.safety_depth);
  ParseMarinerFlag(p.GetString("mariner.two_shades"), &m.two_shades);
  ParseMarinerFlag(p.GetString("mariner.shallow_pattern"), &m.shallow_pattern);
  return m;
}

/// Read-modify-write of the process-wide vector configuration.
template <typename Edit>
void EditVectorConfig(Edit edit) {
  VectorMapConfig c = CurrentVectorMapConfig();
  edit(c);
  SetVectorMapConfig(c);
}

}  // namespace

bool ParseMarinerDepth(const std::string& text, std::optional<double>* out) {
  out->reset();
  const size_t b = text.find_first_not_of(" \t");
  if (b == std::string::npos) return true;
  const std::string t = text.substr(b, text.find_last_not_of(" \t") - b + 1);
  char* stop = nullptr;
  const double d = std::strtod(t.c_str(), &stop);
  if (stop != t.c_str() + t.size() || !std::isfinite(d) || d < 0) return false;
  *out = d;
  return true;
}

bool ParseMarinerFlag(const std::string& text, std::optional<bool>* out) {
  out->reset();
  std::string t;
  for (char c : text)
    if (c != ' ' && c != '\t') t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (t.empty()) return true;
  if (t == "1" || t == "on" || t == "yes" || t == "true") *out = true;
  else if (t == "0" || t == "off" || t == "no" || t == "false") *out = false;
  else return false;
  return true;
}

std::vector<int> ParseElevationBands(const std::string& text) {
  std::vector<int> feet;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find_first_of(",;", start);
    if (end == std::string::npos) end = text.size();
    const std::string part = text.substr(start, end - start);
    const char* begin = part.c_str();
    char* stop = nullptr;
    const double d = std::strtod(begin, &stop);
    // Python's float() takes surrounding whitespace and no hex.
    bool whole = stop != begin && std::isfinite(d) && part.find_first_of("xX") == std::string::npos;
    for (const char* c = stop; whole && *c != '\0'; ++c)
      if (*c != ' ' && *c != '\t') whole = false;
    // nearbyint under the default rounding mode is Python's round().
    if (whole) feet.push_back(static_cast<int>(std::nearbyint(d)));
    start = end + 1;
  }
  std::sort(feet.begin(), feet.end());
  if (feet.size() > kMaxBands) feet.resize(kMaxBands);
  return feet;
}

std::string FormatElevationBands(const std::vector<int>& feet) {
  std::string out;
  for (int f : feet) {
    if (!out.empty()) out += ',';
    out += std::to_string(f);
  }
  return out;
}

MapOptionsSource ElevationMapOptions() {
  MapOptionsSource src;
  src.group_id = "elevation";
  src.prefix = "dted.";
  src.make = [] { return std::make_unique<ElevationOptions>(); };
  src.apply = [](const app::Properties& p) {
    SetDtedShadedElevationBands(ParseElevationBands(p.GetString("elevation_bands_ft")));
  };
  return src;
}

MapOptionsSource DncMapOptions() {
  MapOptionsSource src;
  src.group_id = "dnc";
  src.make = [] {
    std::vector<app::PropertySpec> specs;
    specs.push_back(PathSpec("geosym.data_dir", "GeoSym directory", app::PathKind::kDirectory,
                             "The folder that contains GeoSymbol/, not GeoSymbol/ itself."));
    for (const char* key : {"geosym.brightness", "geosym.contrast"}) {
      app::PropertySpec s;
      s.key = key;
      s.label = key[7] == 'b' ? "Brightness" : "Contrast";
      s.group = "Symbology";
      s.type = app::PropertyType::kInt;
      s.min = -100;
      s.max = 100;
      s.default_value = app::PropertyValue::Int(0);
      specs.push_back(std::move(s));
    }
    return std::make_unique<SpecProperties>(std::move(specs));
  };
  src.apply = [](const app::Properties& p) {
    EditVectorConfig([&p](VectorMapConfig& c) {
      c.geosym_dir = p.GetString("geosym.data_dir");
      c.geosym_brightness = static_cast<int>(p.GetInt("geosym.brightness", 0));
      c.geosym_contrast = static_cast<int>(p.GetInt("geosym.contrast", 0));
    });
  };
  return src;
}

MapOptionsSource EncMapOptions() {
  MapOptionsSource src;
  src.group_id = "enc";
  src.make = [] {
    std::vector<app::PropertySpec> specs;
    specs.push_back(PathSpec("enc.data_dir", "S-52 library directory", app::PathKind::kDirectory,
                             "The directory holding chartsymbols.xml and the S-57 catalogue "
                             "CSVs (s57objectclasses.csv, s57attributes.csv)."));
    app::PropertySpec meta;
    meta.key = "enc.show_meta_objects";
    meta.label = "Show meta objects";
    meta.type = app::PropertyType::kBool;
    meta.default_value = app::PropertyValue::Bool(false);
    meta.help = "Draw M_QUAL, M_COVR, M_NSYS and M_NPUB, which describe the dataset.";
    specs.push_back(std::move(meta));
    specs.push_back(PathSpec(kEncChartsDirKey, "Imported charts directory",
                             app::PathKind::kDirectory,
                             "Where Import adds chart sets, in its enc folder; empty uses " +
                                 DefaultChartsDir() + ". It is added to Map Data Sources."));
    AddMarinerSpecs(&specs);
    return std::make_unique<SpecProperties>(std::move(specs));
  };
  src.apply = [](const app::Properties& p) {
    EditVectorConfig([&p](VectorMapConfig& c) {
      c.enc_dir = p.GetString("enc.data_dir");
      c.enc_show_meta = p.GetBool("enc.show_meta_objects", false);
      c.mariner = MarinerFrom(p);
    });
  };
  src.actions = {
      {"enc.import_zip", "Import Zip…", "Add the charts in NOAA ENC zips to the charts directory."},
      {"enc.import_folder", "Import Folder…",
       "Add the charts in an ENC_ROOT folder to the charts directory."},
  };
  return src;
}

MapOptionsSource OsmMapOptions() {
  MapOptionsSource src;
  src.group_id = "osm";
  src.make = [] {
    return std::make_unique<SpecProperties>(std::vector<app::PropertySpec>{
        PathSpec("osm.style", "Style sheet", app::PathKind::kFile,
                 "The MapLibre GL style sheet OpenStreetMap is drawn with; empty "
                 "uses the Peregrine style that ships with the app.")});
  };
  src.apply = [](const app::Properties& p) {
    std::string style = p.GetString("osm.style");
    if (style.empty()) style = DefaultOsmStyle();
    EditVectorConfig([&style](VectorMapConfig& c) { c.osm_style = style; });
  };
  return src;
}

MapOptionsSource RoutingOptions(std::function<void()> changed) {
  MapOptionsSource src;
  src.group_id = RouteOverlay::kTypeId;
  src.make = [] {
    app::PropertySpec graph = PathSpec("routing.graph", "Road network", app::PathKind::kFile,
                                       "The .fvroad graph Follow Roads routes over "
                                       "(fvgraph build).");
    graph.group = "Routing";
    graph.path_filters = {{"Road Graphs (*.fvroad)", "*.fvroad"}};
    app::PropertySpec rules = PathSpec("routing.rules", "Routing rules", app::PathKind::kFile,
                                       "The rule file that prices each way; empty uses the "
                                       "built-in weights.");
    rules.group = "Routing";
    rules.path_filters = {{"Routing Rules (*.json)", "*.json"}};
    app::PropertySpec profile;
    profile.key = "routing.profile";
    profile.label = "Profile for new routes";
    profile.group = "Routing";
    profile.type = app::PropertyType::kString;
    profile.default_value = app::PropertyValue::String(std::string());
    profile.help = "A profile named in the rule file; an opened route keeps its own.";
    app::PropertySpec hills;
    hills.key = "routing.avoid_hills_penalty";
    hills.label = "Avoid hills penalty";
    hills.group = "Routing";
    hills.type = app::PropertyType::kDouble;
    hills.default_value = app::PropertyValue::Double(kDefaultAvoidHillsPenalty);
    hills.min = 1;
    hills.max = 20;
    hills.help = "How many times the climb cost a route marked Avoid Hills pays; "
                 "needs a road network built with elevation.";
    return std::make_unique<SpecProperties>(
        std::vector<app::PropertySpec>{graph, rules, profile, hills});
  };
  src.apply = [changed](const app::Properties&) {
    if (changed) changed();
  };
  return src;
}

MapOptionsSource MovingMapOptions(std::function<void()> changed) {
  MapOptionsSource src;
  src.group_id = MovingMapOverlay::kTypeId;
  src.make = [] {
    auto spec = [](const char* key, const char* label, const char* group, app::PropertyType type,
                   app::PropertyValue def, const char* help) {
      app::PropertySpec s;
      s.key = key;
      s.label = label;
      s.group = group;
      s.type = type;
      s.default_value = std::move(def);
      s.help = help;
      return s;
    };
    using T = app::PropertyType;
    using V = app::PropertyValue;
    std::vector<app::PropertySpec> specs;
    app::PropertySpec feed = spec("movingmap.feed", "Feed", "Default Feed", T::kChoice, V::Choice(0),
                                  "What a new moving map listens to. demo replays the open "
                                  "route (or circles the map); nmea_tcp connects to a receiver "
                                  "serving NMEA 0183; nmea_udp listens for NMEA broadcasts.");
    feed.choices = MovingMapFeedNames();
    specs.push_back(feed);
    specs.push_back(spec("movingmap.nmea_host", "NMEA host", "Default Feed", T::kString,
                         V::String(std::string()), "The receiver's address for nmea_tcp."));
    app::PropertySpec port = spec("movingmap.nmea_port", "NMEA port", "Default Feed", T::kInt,
                                  V::Int(10110), "The TCP port to connect to, or the UDP port "
                                                 "to listen on.");
    port.min = 1;
    port.max = 65535;
    specs.push_back(port);
    specs.push_back(spec("movingmap.speed_mps", "Demo speed (m/s)", "Default Feed", T::kDouble,
                         V::Double(15.0), "Ground speed of the demo replay."));
    specs.push_back(spec("movingmap.noise_m", "Demo scatter (m)", "Default Feed", T::kDouble,
                         V::Double(0.0),
                         "Scatters the demo fixes by up to this distance, as a receiver under "
                         "cover would; shows snapping at work."));
    specs.push_back(spec("ownship.playback_rate", "Replay speed", "Replay", T::kDouble,
                         V::Double(1.0), "1 plays opened tracks in real time, 10 ten times "
                                         "faster."));
    app::PropertySpec units = spec("ownship.units", "Ownship units", "Replay", T::kChoice,
                                   V::Choice(0), "How the Ownship window reads altitude and "
                                                 "speed: nautical is ft and kt, metric is m and "
                                                 "km/h.");
    units.choices = OwnshipUnitNames();
    specs.push_back(units);
    specs.push_back(spec("movingmap.auto_rotate", "Track up", "Following", T::kBool, V::Bool(false),
                         "Turn the chart so the ship's course points up the screen while "
                         "centring on every fix."));
    specs.push_back(spec("movingmap.continuous", "Centre on every fix", "Following", T::kBool,
                         V::Bool(false),
                         "Keep the ship centred on each fix. Off, the map does not follow "
                         "the ship."));
    specs.push_back(spec("movingmap.slew_seconds", "Slew time (s)", "Following", T::kDouble,
                         V::Double(0.35), "How long the map takes to glide to a new centre; "
                                          "0 jumps."));
    specs.push_back(spec("movingmap.snap_to_road", "Snap to road", "Following", T::kBool,
                         V::Bool(true), "Whether a new moving map puts its ship on the road it "
                                        "is most likely on; needs the Route page's road network."));
    specs.push_back(spec("movingmap.show_apron", "Show the apron", "Following", T::kBool,
                         V::Bool(false),
                         "Draw the box the ship may move in before the map recentres."));
    specs.push_back(spec("movingmap.symbol_px", "Ship size (px)", "Following", T::kDouble,
                         V::Double(9.0), "Size of the ownship chevron."));
    specs.push_back(spec("movingmap.show_trail_dots", "Trail dots", "Trail", T::kBool,
                         V::Bool(true), "A dot at each fix the ship has passed; on a replay "
                                        "the fixes still to come are faint."));
    specs.push_back(spec("movingmap.show_trail_line", "Trail line", "Trail", T::kBool,
                         V::Bool(false), "A line through the trail."));
    app::PropertySpec trail_max = spec("movingmap.trail_max", "Trail length (fixes)", "Trail",
                                       T::kInt, V::Int(5000),
                                       "How many of a live map's newest fixes the trail shows. "
                                       "Saving writes every recorded fix.");
    trail_max.min = 0;
    specs.push_back(trail_max);
    app::PropertySpec record_max = spec("movingmap.record_max", "Recording limit (fixes)",
                                        "Trail", T::kInt, V::Int(500000),
                                        "A live map stops recording at this many fixes and "
                                        "says so in the status bar.");
    record_max.min = 1;
    specs.push_back(record_max);
    return std::make_unique<SpecProperties>(std::move(specs));
  };
  src.apply = [changed](const app::Properties&) {
    if (changed) changed();
  };
  return src;
}

namespace {

std::string& AppDataDirStorage() {
  static std::string dir;
  return dir;
}

/// `<AppDataDir()>/<relative>` when it exists, else "".
std::string AppDataPath(const char* relative) {
  if (AppDataDirStorage().empty()) return {};
  const std::filesystem::path p = std::filesystem::path(AppDataDirStorage()) / relative;
  std::error_code ec;
  return std::filesystem::exists(p, ec) ? p.string() : std::string();
}

}  // namespace

void SetAppDataDir(const std::string& dir) { AppDataDirStorage() = dir; }

std::string AppDataDir() { return AppDataDirStorage(); }

std::string DefaultOsmStyle() { return AppDataPath("osm-styles/peregrine-osm.json"); }

void SetGeoTransDataDir(const std::string& dir) {
  std::string use = dir;
  if (use.empty()) {
    if (std::getenv("MSPCCS_DATA") != nullptr) return;
    use = AppDataPath("geotrans");
    if (use.empty()) return;
  }
#ifdef _WIN32
  _putenv_s("MSPCCS_DATA", use.c_str());
#else
  setenv("MSPCCS_DATA", use.c_str(), 1);
#endif
}

MapOptionsSource DataOptions() {
  MapOptionsSource src;
  src.group_id = "data";
  src.title = "Data";
  src.make = [] {
    return std::make_unique<SpecProperties>(std::vector<app::PropertySpec>{
        PathSpec("geotrans.data_dir", "GeoTrans data", app::PathKind::kDirectory,
                 "The directory holding the GeoTrans datum and ellipsoid tables "
                 "(ellips.dat, 3_param.dat, 7_param.dat, egm96.grd). Needed to enter "
                 "MGRS in Go To / Find; empty uses the tables that ship with the app. "
                 "Takes effect the next time Peregrine starts.")});
  };
  src.apply = [](const app::Properties& p) { SetGeoTransDataDir(p.GetString("geotrans.data_dir")); };
  return src;
}

std::vector<MapOptionsSource> BuiltinMapOptions() {
  return {ElevationMapOptions(), DncMapOptions(), EncMapOptions(), OsmMapOptions(), DataOptions()};
}

}  // namespace desk
}  // namespace fv
