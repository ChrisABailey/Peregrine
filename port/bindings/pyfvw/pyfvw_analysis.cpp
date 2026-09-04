// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// pyfvw.analysis — the Analysis tools, bound (analysis plan AN5).
//
// AN1-AN4 are pure computation over two things already on this side of the
// seam (`geo_tool`'s geodesy and `IElevationSource`), so there is no overlay
// and no shell in this submodule at all: a path, a terrain profile, a
// viewshed and the four measurement objects, and every one of them is a value
// a Python UI can hold. AN6 and AN7 are the UI over exactly this.
//
// THREE THINGS ARE LOAD-BEARING HERE, and they are all about the viewshed,
// because it is the first pyfvw call long enough to need any of them.
//
//   1. THE GIL IS RELEASED FOR THE COMPUTATION AND RE-TAKEN FOR THE CALLBACK.
//      A million posts is ~105 ms of pure arithmetic (AN3 measured it), and a
//      UI thread that cannot repaint for that long is the reason FalconView
//      grew a progress dialog. So `compute_viewshed` drops the GIL round
//      ComputeViewshed and the progress adapter re-acquires it for the few
//      dozen calls the integer-percent rule actually makes.
//
//   2. A RAISING CALLBACK CANCELS; IT DOES NOT UNWIND THROUGH C++. Letting a
//      py::error_already_set propagate out of OnProgress would tear through
//      ComputeViewshed's ring walk with the GIL in an unclear state. The
//      adapter catches it, stores it, returns false — which is the cancel the
//      C++ interface already has a meaning for — and the binding re-raises
//      the ORIGINAL Python exception once the stack is back. A callback that
//      raises KeyboardInterrupt therefore surfaces as KeyboardInterrupt, not
//      as FvError(INTERRUPTED).
//
//   3. THE RESULT IS A BUFFER, NOT A LIST. A million posts as a Python list
//      of floats is ~32 MB of boxed doubles and a second of allocation, for a
//      grid whose only consumer is a raster. `ViewshedResult` exposes the
//      buffer protocol, so `np.asarray(result)` is a zero-copy (span, span)
//      float32 view, laid out exactly as AN3 documents it: row 0 north,
//      column 0 west.
//
// The profile is the mirror image and needs none of that: its default cap is
// 20 000 samples, so `points` is an ordinary list, and `distances_m` /
// `elevations_m` are the two parallel arrays AN6's chart actually wants.
//
// One naming note (contracts D3): `GeoPath.points` is a property returning a
// COPY, because the C++ accessor hands out a const reference into the path
// and a Python list that aliased it would go stale the moment anyone called
// `set_points`.

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "pyfvw_common.h"

#include "fvkit/analysis/measure.h"
#include "fvkit/analysis/path.h"
#include "fvkit/analysis/profile.h"
#include "fvkit/analysis/viewshed.h"
#include "fvkit/formats/source.h"
#include "fvkit/geo.h"

namespace pyfvw {

namespace py = pybind11;
using namespace pybind11::literals;

namespace {

namespace an = fv::analysis;

// The progress adapter. See note 2 in the header comment: a Python exception
// becomes a cancel plus a saved error, never an unwind through the ring walk.
class PyViewshedProgress : public an::IViewshedProgress {
 public:
  explicit PyViewshedProgress(py::object fn) : fn_(std::move(fn)) {}

  bool OnProgress(int percent) override {
    py::gil_scoped_acquire gil;
    try {
      py::object r = fn_(percent);
      // A callback that returns None has not asked to cancel. Requiring an
      // explicit `return True` from a one-line progress bar would be a trap.
      if (r.is_none()) return true;
      return py::cast<bool>(r);
    } catch (py::error_already_set& e) {
      error_ = std::make_unique<py::error_already_set>(e);
      return false;
    } catch (const py::cast_error&) {
      // A callback returning something that is not a bool is a bug in the
      // callback; say so in the callback's own language.
      error_ = nullptr;
      cast_error_ = true;
      return false;
    }
  }

  // Re-raises whatever the callback raised, once the C++ stack is unwound.
  void Rethrow() {
    if (error_) {
      py::error_already_set e = *error_;
      error_.reset();
      throw e;
    }
    if (cast_error_) {
      cast_error_ = false;
      throw py::type_error(
          "viewshed progress callback must return a bool or None");
    }
  }

  bool cancelled_by_python() const { return error_ != nullptr || cast_error_; }

 private:
  py::object fn_;
  std::unique_ptr<py::error_already_set> error_;
  bool cast_error_ = false;
};

}  // namespace

void BindAnalysis(py::module_& m) {
  py::module_ an_mod = m.def_submodule(
      "analysis",
      "The Analysis tools (analysis plan AN1-AN5): a geographic path, the "
      "terrain profile under it, the XDraw viewshed, and the four measurement "
      "objects of FalconView's Range & Bearing overlay. All headless -- "
      "nothing here knows what a chart looks like, which is why the same "
      "TerrainProfile call serves a two-point measurement and a route.");

  // ---- AN1: the path ------------------------------------------------------

  py::enum_<an::LineType>(
      an_mod, "LineType",
      "Which line a leg is measured and interpolated along. It is a property "
      "of the PATH, not a global: the midpoint of a great-circle leg is on "
      "the great circle, not on the rhumb line between its ends.")
      .value("GREAT_CIRCLE", an::LineType::kGreatCircle)
      .value("RHUMB", an::LineType::kRhumb);

  py::class_<an::PathLeg>(
      an_mod, "PathLeg",
      "One leg, vertex i to vertex i+1. `ok` is false when the geodesy "
      "refused the pair (coincident points, a pole on a rhumb line); such a "
      "leg is ZERO-LENGTH AND FLAGGED, never dropped, so every index into the "
      "path still lines up with its vertices.")
      .def_readonly("range_m", &an::PathLeg::range_m)
      .def_readonly("bearing_deg", &an::PathLeg::bearing_deg,
                    "True, at the START of the leg, in [0, 360).")
      .def_readonly("ok", &an::PathLeg::ok)
      .def("__repr__", [](const an::PathLeg& l) {
        return "PathLeg(range_m=" + std::to_string(l.range_m) +
               ", bearing_deg=" + std::to_string(l.bearing_deg) +
               (l.ok ? ", ok=True)" : ", ok=False)");
      });

  py::class_<an::GeoPath>(
      an_mod, "GeoPath",
      "An ordered list of points plus a line type, and the arithmetic every "
      "analysis tool does over one. Two points is a range-and-bearing line, "
      "many is a polyline or a route -- the same object either way, which is "
      "what makes 'the elevation profile of a route' a one-line call.")
      .def(py::init<>())
      .def(py::init([](std::vector<fv::GeoPoint> points, an::LineType type) {
             return an::GeoPath(std::move(points), type);
           }),
           "points"_a, "line_type"_a = an::LineType::kGreatCircle)
      .def_property(
          "points",
          // A COPY: the C++ accessor returns a reference into the path.
          [](const an::GeoPath& p) { return p.points(); },
          [](an::GeoPath& p, std::vector<fv::GeoPoint> pts) {
            p.SetPoints(std::move(pts));
          })
      .def_property("line_type", &an::GeoPath::line_type,
                    &an::GeoPath::SetLineType)
      .def_property_readonly("point_count", &an::GeoPath::PointCount)
      .def_property_readonly("leg_count", &an::GeoPath::LegCount)
      .def_property_readonly(
          "total_length_m", &an::GeoPath::TotalLength,
          "Distance from the first vertex to the last, in metres.")
      .def_property_readonly(
          "measured", &an::GeoPath::Measured,
          "True when every leg measured. A single-vertex path is trivially "
          "measured; an empty one is not a path.")
      .def("leg", &an::GeoPath::Leg, "i"_a,
           py::return_value_policy::copy)
      .def("cumulative_at", &an::GeoPath::CumulativeAt, "i"_a,
           "Distance from vertex 0 to vertex i. Defined for every vertex, "
           "including 0.")
      .def(
          "point_at_distance",
          [](const an::GeoPath& p, double distance_m) {
            fv::GeoPoint out;
            if (!p.PointAtDistance(distance_m, &out))
              throw FvErrorCpp{fv::Status::Error(
                  fv::kInvalidArg,
                  "no point at that distance (empty path or a refused leg)")};
            return out;
          },
          "distance_m"_a,
          "The point that far along the path, interpolated along the "
          "containing leg's OWN line type. Distances outside the path clamp "
          "to its ends -- a caller stepping by a fixed interval overshoots by "
          "less than one step and that is not an error.")
      .def("densify", &an::GeoPath::Densify, "step_m"_a, "max_points"_a = 0,
           "Every vertex, plus enough interpolated points that no gap exceeds "
           "step_m. VERTICES ARE ALWAYS PRESENT AND EXACT. max_points widens "
           "the step; the path is never truncated.")
      .def("resample", &an::GeoPath::Resample, "count"_a,
           "`count` points evenly spaced BY DISTANCE ALONG THE PATH, first "
           "and last exactly the path's own ends. On a polyline that is not "
           "evenly spaced in a straight line: two samples either side of a "
           "turning point are closer as the crow flies than the step.")
      .def_property_readonly(
          "area_square_meters", &an::GeoPath::AreaSquareMeters,
          "Polygon area, treating the vertices as a closed ring. FalconView's "
          "local-tangent-plane shoelace about vertex 0, transcribed -- "
          "accurate for the sizes a user drags out, not for a country. Fewer "
          "than three vertices is zero, not an error.")
      .def("__len__", &an::GeoPath::PointCount)
      .def("__repr__", [](const an::GeoPath& p) {
        return "GeoPath(" + std::to_string(p.PointCount()) + " points, " +
               (p.line_type() == an::LineType::kGreatCircle ? "GREAT_CIRCLE"
                                                            : "RHUMB") +
               ", " + std::to_string(p.TotalLength()) + " m)";
      });

  // ---- AN2: the terrain profile -------------------------------------------

  py::class_<an::ProfilePoint>(
      an_mod, "ProfilePoint",
      "One sample. `elevation_m` is NaN when has_data is false -- a void post "
      "or ground the source does not reach. `is_vertex` marks a turning point "
      "of the path rather than an interior sample.")
      .def_readonly("at", &an::ProfilePoint::at)
      .def_readonly("distance_m", &an::ProfilePoint::distance_m)
      .def_readonly("elevation_m", &an::ProfilePoint::elevation_m)
      .def_readonly("has_data", &an::ProfilePoint::has_data)
      .def_readonly("is_vertex", &an::ProfilePoint::is_vertex);

  py::class_<an::ProfileOptions>(
      an_mod, "ProfileOptions",
      "How to sample. Exactly one of sample_count (the Windows 'Segments' "
      "box) and step_m drives it; a count of 0 means 'use step_m'.")
      .def(py::init<>())
      .def_readwrite("sample_count", &an::ProfileOptions::sample_count)
      .def_readwrite("step_m", &an::ProfileOptions::step_m)
      .def_readwrite("include_vertices", &an::ProfileOptions::include_vertices,
                     "With a step: keep every turning point as a sample. On "
                     "by default -- a polyline profile that does not show its "
                     "vertices is a lie about where the legs are.")
      .def_readwrite("clamp_to_post_spacing",
                     &an::ProfileOptions::clamp_to_post_spacing,
                     "Widen the step to the source's post spacing when it is "
                     "coarser. A profile sampled at 10 m over 3-arcsecond "
                     "DTED draws interpolation, not terrain. STEP SAMPLING "
                     "ONLY: a sample count is a chart's x-axis and the user "
                     "picked it.")
      .def_readwrite("max_samples", &an::ProfileOptions::max_samples,
                     "Hard cap either way; the step widens to fit and the "
                     "path is never truncated. 0 means no cap.");

  py::class_<an::ProfileResult>(
      an_mod, "ProfileResult",
      "The ground under the path. A HOLE IS NOT A FAILURE: a void sample is "
      "NaN with has_data false and counted in no_data_count, and the profile "
      "still returns -- Windows popped 'No data available.' and refused to "
      "draw a 300 nm route because of one gap.")
      .def_readonly("points", &an::ProfileResult::points)
      .def_readonly("min_m", &an::ProfileResult::min_m)
      .def_readonly("max_m", &an::ProfileResult::max_m)
      .def_readonly("gain_m", &an::ProfileResult::gain_m,
                    "Sum of the rises between consecutive KNOWN samples.")
      .def_readonly("loss_m", &an::ProfileResult::loss_m,
                    "Sum of the falls, as a positive number.")
      .def_readonly("total_distance_m", &an::ProfileResult::total_distance_m)
      .def_readonly("no_data_count", &an::ProfileResult::no_data_count)
      .def_readonly("step_m", &an::ProfileResult::step_m,
                    "The spacing actually used, after any clamp.")
      .def_readonly("step_was_clamped", &an::ProfileResult::step_was_clamped)
      .def_property_readonly("empty", &an::ProfileResult::Empty)
      .def_property_readonly(
          "distances_m",
          [](const an::ProfileResult& r) {
            std::vector<double> out;
            out.reserve(r.points.size());
            for (const auto& p : r.points) out.push_back(p.distance_m);
            return out;
          },
          "The x axis, as a plain list -- one half of what a chart wants.")
      .def_property_readonly(
          "elevations_m",
          [](const an::ProfileResult& r) {
            std::vector<double> out;
            out.reserve(r.points.size());
            for (const auto& p : r.points)
              out.push_back(p.has_data ? static_cast<double>(p.elevation_m)
                                       : std::nan(""));
            return out;
          },
          "The y axis, NaN where there is no data. matplotlib and tk both "
          "leave a NaN as a gap, which is the drawing a hole should produce.")
      .def("__len__",
           [](const an::ProfileResult& r) { return r.points.size(); });

  an_mod.def(
      "sample_terrain_profile",
      [](std::shared_ptr<fv::IElevationSource> src, const an::GeoPath& path,
         const an::ProfileOptions& opts) {
        if (!src)
          throw FvErrorCpp{
              fv::Status::Error(fv::kInvalidArg, "elevation source is None")};
        an::ProfileResult out;
        fv::Status st;
        {
          py::gil_scoped_release release;
          st = an::SampleTerrainProfile(*src, path, opts, &out);
        }
        ThrowIfError(st);
        return out;
      },
      "source"_a, "path"_a, "options"_a = an::ProfileOptions(),
      "Walks the path, one elevation read per sample. N reads for N samples, "
      "correct at every bearing, and identical for a two-point line, a "
      "polyline and a route. A path the source has no coverage for is a "
      "SUCCESSFUL profile with every point has_data false -- that is a real "
      "answer and a chart can draw it.");

  // ---- AN3: the viewshed ---------------------------------------------------

  py::enum_<an::HeightMethod>(
      an_mod, "HeightMethod",
      "Which of the three visible-height answers the result carries. All "
      "three are computed whatever this says; the three are genuinely "
      "bracketed (min <= interpolated <= max).")
      .value("MIN", an::HeightMethod::kMin,
             "Err toward smaller heights -- more of the ground reads visible.")
      .value("MAX", an::HeightMethod::kMax, "Err toward larger heights.")
      .value("INTERPOLATED", an::HeightMethod::kInterpolated,
             "Between the two; what FalconView always asks for.");

  py::class_<an::ViewshedSector>(
      an_mod, "ViewshedSector",
      "A wedge to crop to: angle_deg wide, centred on bearing_deg true. 270 "
      "degrees or more is no crop at all, as upstream.")
      .def(py::init<>())
      .def(py::init([](double bearing_deg, double angle_deg) {
             return an::ViewshedSector{bearing_deg, angle_deg};
           }),
           "bearing_deg"_a, "angle_deg"_a)
      .def_readwrite("bearing_deg", &an::ViewshedSector::bearing_deg)
      .def_readwrite("angle_deg", &an::ViewshedSector::angle_deg);

  py::class_<an::ViewshedRequest>(
      an_mod, "ViewshedRequest",
      "What to compute. `step_deg` is the post spacing in degrees of "
      "latitude; viewshed_step_from_post_spacing gives the answer a terrain "
      "source justifies.")
      .def(py::init<>())
      .def_readwrite("observer", &an::ViewshedRequest::observer)
      .def_readwrite("observer_height_m",
                     &an::ViewshedRequest::observer_height_m,
                     "Above the ground AT the observer, not above sea level.")
      .def_readwrite("range_m", &an::ViewshedRequest::range_m)
      .def_readwrite("step_deg", &an::ViewshedRequest::step_deg)
      .def_readwrite("method", &an::ViewshedRequest::method)
      .def_readwrite(
          "max_posts", &an::ViewshedRequest::max_posts,
          "Cap on span*span. OVER THE CAP THE SPAN SHRINKS AND THE STEP "
          "WIDENS -- the range asked for is always the range delivered, more "
          "coarsely. This is what stands where FalconView's 'Out of Memory. "
          "Try reducing the range' stood. Default a million posts, ~80 MB.")
      .def_readwrite("has_sector", &an::ViewshedRequest::has_sector)
      .def_readwrite("sector", &an::ViewshedRequest::sector);

  py::class_<an::ViewshedResult>(
      an_mod, "ViewshedResult", py::buffer_protocol(),
      "span*span visible heights. np.asarray(result) is a ZERO-COPY (span, "
      "span) float32 view: row 0 is the NORTHERNMOST and column 0 the "
      "WESTERNMOST, so index [0][0] is the north-west corner and `bounds` "
      "names the same two corners. 0 means visible from the observer; a "
      "positive value is the height in metres something would have to reach "
      "there to be seen; NaN means no answer (unknown elevation, or cropped "
      "out of the sector).")
      .def_readonly("span", &an::ViewshedResult::span)
      .def_readonly("bounds", &an::ViewshedResult::bounds)
      .def_readonly("step_deg", &an::ViewshedResult::step_deg,
                    "After any widening for max_posts.")
      .def_readonly("step_was_widened", &an::ViewshedResult::step_was_widened)
      .def_readonly("no_data_count", &an::ViewshedResult::no_data_count)
      .def_property_readonly("valid", &an::ViewshedResult::Valid)
      .def("at", &an::ViewshedResult::At, "row"_a, "col"_a,
           "One post, row 0 north and column 0 west. For the whole grid use "
           "np.asarray(result); this is for a caller with no numpy.")
      .def_buffer([](an::ViewshedResult& r) -> py::buffer_info {
        const py::ssize_t n = r.span;
        return py::buffer_info(
            r.visible_height_m.data(), sizeof(float),
            py::format_descriptor<float>::format(), 2, {n, n},
            {static_cast<py::ssize_t>(sizeof(float)) * n,
             static_cast<py::ssize_t>(sizeof(float))});
      });

  an_mod.def(
      "viewshed_step_from_post_spacing",
      [](std::shared_ptr<fv::IElevationSource> src, const fv::GeoPoint& at) {
        if (!src)
          throw FvErrorCpp{
              fv::Status::Error(fv::kInvalidArg, "elevation source is None")};
        return an::ViewshedStepFromPostSpacing(*src, at);
      },
      "source"_a, "at"_a,
      "The step the source's own posts justify, in degrees of latitude -- the "
      "COARSER of its two axes, because a lattice finer than the data is "
      "inventing terrain to occlude with. Returns 0.0 when the source does "
      "not know its spacing, which is a legal answer: choose a step some "
      "other way.");

  an_mod.def(
      "compute_viewshed",
      [](std::shared_ptr<fv::IElevationSource> src,
         const an::ViewshedRequest& req, py::object progress) {
        if (!src)
          throw FvErrorCpp{
              fv::Status::Error(fv::kInvalidArg, "elevation source is None")};

        std::unique_ptr<PyViewshedProgress> cb;
        if (!progress.is_none())
          cb = std::make_unique<PyViewshedProgress>(progress);

        an::ViewshedResult out;
        fv::Status st;
        {
          // The GIL goes here and comes back for each progress call. See note
          // 1 in this file's header comment.
          py::gil_scoped_release release;
          st = an::ComputeViewshed(*src, req, &out, cb.get());
        }
        // A callback that raised re-raises ITS OWN exception, not FvError:
        // the cancel was our doing, not the user's.
        if (cb) cb->Rethrow();
        ThrowIfError(st);
        return out;
      },
      "source"_a, "request"_a, "progress"_a = py::none(),
      "XDraw intervisibility (Franklin 1994). `progress` is called as "
      "progress(percent) ONLY WHEN THE INTEGER PERCENT CHANGES, and returning "
      "False cancels (FvError INTERRUPTED, with no partial result); returning "
      "None does not. The GIL is released for the computation, so a UI thread "
      "keeps painting. Raises FvError OUT_OF_COVERAGE when the OBSERVER'S OWN "
      "post has no elevation -- there is nothing to stand on.");

  // ---- AN4: the measurements ----------------------------------------------

  py::enum_<an::RangeUnit>(an_mod, "RangeUnit",
                           "FalconView's rb::units_t, in its own order.")
      .value("NAUTICAL_MILES", an::RangeUnit::kNauticalMiles)
      .value("MILES", an::RangeUnit::kMiles)
      .value("KILOMETERS", an::RangeUnit::kKilometers)
      .value("METERS", an::RangeUnit::kMeters)
      .value("YARDS", an::RangeUnit::kYards)
      .value("FEET", an::RangeUnit::kFeet);

  py::enum_<an::AngleUnit>(an_mod, "AngleUnit",
                           "Mils BEAT the bearing format: asking for mils "
                           "renders mils whatever the format says.")
      .value("DEGREES", an::AngleUnit::kDegrees)
      .value("MILS", an::AngleUnit::kMils);

  py::enum_<an::BearingFormat>(
      an_mod, "BearingFormat",
      "How a bearing is spelled. The two sexagesimal forms TRUNCATE at every "
      "field, so 44.99999 degrees is 044 59' 59\" and never 045.")
      .value("DEGREES", an::BearingFormat::kDegrees)
      .value("DEGREES_MINUTES", an::BearingFormat::kDegreesMinutes)
      .value("DEGREES_MINUTES_SECONDS",
             an::BearingFormat::kDegreesMinutesSeconds);

  py::enum_<an::BearingReference>(an_mod, "BearingReference",
                                  "True or magnetic; the label's T/M suffix.")
      .value("TRUE", an::BearingReference::kTrue)
      .value("MAGNETIC", an::BearingReference::kMagnetic);

  py::class_<an::MagneticEpoch>(
      an_mod, "MagneticEpoch",
      "Which world magnetic model epoch to ask for. Year 0 means 'now' (UTC), "
      "which is what FalconView does with GetSystemTime -- an argument here "
      "so a label is testable and a saved measurement does not drift.\n\n"
      "NOTE: geo_tool looks for wmm.dat under $FVW_GEODATA_DIR and there is "
      "no such file in this tree, so the model behind every magnetic bearing "
      "is currently the built-in WMM-95 table. Right arithmetic, 1995 "
      "declination.")
      .def(py::init<>())
      .def(py::init([](int year, int month, int altitude_m) {
             an::MagneticEpoch e;
             e.year = year;
             e.month = month;
             e.altitude_m = altitude_m;
             return e;
           }),
           "year"_a, "month"_a, "altitude_m"_a = 0)
      .def_readwrite("year", &an::MagneticEpoch::year)
      .def_readwrite("month", &an::MagneticEpoch::month)
      .def_readwrite("altitude_m", &an::MagneticEpoch::altitude_m)
      .def_property_readonly("is_now", &an::MagneticEpoch::is_now);

  py::class_<an::MeasureStyle>(
      an_mod, "MeasureStyle",
      "Everything a measurement needs that is not geometry -- FalconView's "
      "property sheet.")
      .def(py::init<>())
      .def_readwrite("units", &an::MeasureStyle::units)
      .def_readwrite("angle_units", &an::MeasureStyle::angle_units)
      .def_readwrite("bearing_format", &an::MeasureStyle::bearing_format)
      .def_readwrite("bearing_reference", &an::MeasureStyle::bearing_reference)
      .def_readwrite("epoch", &an::MeasureStyle::epoch,
                     "Read only when bearing_reference is MAGNETIC.");

  py::enum_<an::MeasurementKind>(
      an_mod, "MeasurementKind",
      "The four objects of the Range & Bearing overlay. They differ only in "
      "what they label, which is why they are one class here and five copies "
      "of the same geodesy over there.")
      .value("RANGE_BEARING", an::MeasurementKind::kRangeBearing,
             "One leg: bearing and range.")
      .value("MULTI_POINT", an::MeasurementKind::kMultiPoint,
             "A CUMULATIVE distance label at each turning point.")
      .value("TOTAL_DISTANCE", an::MeasurementKind::kTotalDistance,
             "One 'Total Distance:' label.")
      .value("AREA", an::MeasurementKind::kArea, "The shoelace, in sq units.");

  py::class_<an::LegMeasurement>(
      an_mod, "LegMeasurement",
      "A leg as a measurement reports it: metres, the bearing in the style's "
      "own reference, and the running total to the leg's END vertex.")
      .def_readonly("range_m", &an::LegMeasurement::range_m)
      .def_readonly("bearing_deg", &an::LegMeasurement::bearing_deg,
                    "True or magnetic, per the style.")
      .def_readonly("cumulative_m", &an::LegMeasurement::cumulative_m)
      .def_readonly("ok", &an::LegMeasurement::ok);

  py::class_<an::Measurement>(
      an_mod, "Measurement",
      "A path, a style, and the labels FalconView would draw for them.")
      .def(py::init<>())
      .def(py::init([](an::MeasurementKind kind, an::GeoPath path,
                       an::MeasureStyle style) {
             return an::Measurement(kind, std::move(path), style);
           }),
           "kind"_a, "path"_a, "style"_a = an::MeasureStyle())
      .def_property("kind", &an::Measurement::kind, &an::Measurement::SetKind)
      .def_property(
          "path", [](const an::Measurement& m) { return m.path(); },
          [](an::Measurement& m, an::GeoPath p) { m.SetPath(std::move(p)); })
      .def_property(
          "style", [](const an::Measurement& m) { return m.style(); },
          &an::Measurement::SetStyle)
      .def_property_readonly("leg_count", &an::Measurement::LegCount)
      .def("leg", &an::Measurement::Leg, "i"_a)
      .def_property_readonly("total_range_m",
                             &an::Measurement::TotalRangeMeters)
      .def_property_readonly("area_square_meters",
                             &an::Measurement::AreaSquareMeters)
      .def("leg_label", &an::Measurement::LegLabel, "i"_a,
           "' 045.0<deg>T / 12.34 NM ' -- padding spaces included, because "
           "they are FalconView's own padding round the text it hands its "
           "symbol layer. Empty for a leg the geodesy refused.")
      .def("leg_bearing_label", &an::Measurement::LegBearingLabel, "i"_a,
           "' 045.0<deg>T '.")
      .def_property_readonly("summary_label", &an::Measurement::SummaryLabel,
                             "The one label that names the whole object.")
      .def_property_readonly(
          "labels", &an::Measurement::Labels,
          "Everything the overlay would draw for this object, in order. For "
          "MULTI_POINT that is the CUMULATIVE distance at every turning point "
          "after the first -- not a per-leg range, and a leg the geodesy "
          "refused contributes nothing at all.");

  // The unit and formatting table on its own, for a UI that has a number and
  // needs it spelled the way the rest of the app spells it.
  an_mod.def("meters_per_unit", &an::MetersPerUnit, "unit"_a,
             "1852 to the nautical mile, 1609.344 to the statute mile, 0.9144 "
             "to the yard, 0.3048 to the foot -- FalconView's CUnits.");
  an_mod.def("convert_range", &an::ConvertRange, "meters"_a, "unit"_a);
  an_mod.def("convert_area", &an::ConvertArea, "square_meters"_a, "unit"_a,
             "The square of the linear conversion. FalconView converts every "
             "leg BEFORE its shoelace, which is the same number.");
  an_mod.def("range_unit_name", &an::RangeUnitName, "unit"_a,
             "'NM', 'miles', 'km', 'm', 'yards', 'ft'.");
  an_mod.def("area_unit_name", &an::AreaUnitName, "unit"_a,
             "The same with 'sq ' in front.");
  an_mod.def("degrees_to_mils", &an::DegreesToMils, "degrees"_a,
             "6400 to the circle, written out as FalconView writes it.");
  an_mod.def("mils_to_degrees", &an::MilsToDegrees, "mils"_a);
  an_mod.def("range_decimals", &an::RangeDecimals, "value_in_units"_a,
             "The magnitude ladder: under 100 gets two places, under 1000 "
             "gets one, 1000 and over gets none. It is on the VALUE, not the "
             "unit, so one leg reads '12.34 NM' and '74977 ft'.");
  an_mod.def("true_to_magnetic", &an::TrueToMagnetic, "true_bearing_deg"_a,
             "at"_a, "epoch"_a = an::MagneticEpoch(),
             "Subtract the variation (east positive) and wrap. A model "
             "failure is a variation of zero, i.e. the true bearing comes "
             "back unchanged -- FalconView does the same and says nothing.");
  an_mod.def("format_bearing", &an::FormatBearing, "degrees"_a, "style"_a);
  an_mod.def("format_range", &an::FormatRange, "meters"_a, "style"_a,
             "decimals"_a = -1,
             "decimals < 0 uses the magnitude ladder (the range/bearing "
             "label); decimals >= 0 fixes the places (the total-distance and "
             "multi-point labels pass 2).");
  an_mod.def("format_area", &an::FormatArea, "square_meters"_a, "style"_a);
  an_mod.attr("DEGREE_SIGN") = py::str(an::kDegreeSign);
}

}  // namespace pyfvw
