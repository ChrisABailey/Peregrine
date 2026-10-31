// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for GeoTiffDem / GeoTiffDemSource / ElevationStack. Fixtures are
/// written here with libtiff; UTM positions are checked against GEOTRANS.

#include <gtest/gtest.h>
#include <tiffio.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "GeodeticCoordinates.h"
#include "UTM.h"
#include "UTMCoordinates.h"
#include "fvkit/formats/dted.h"
#include "fvkit/formats/elevation_stack.h"
#include "fvkit/formats/geotiff.h"
#include "fvkit/formats/geotiff_dem.h"
#include "fvkit/formats/lidar_shaded.h"
#include "fvkit/formats/registry.h"
#include "fvkit/geo/terrain_contour.h"
#include "dem_fixture.h"

namespace fs = std::filesystem;

namespace {

using fv::test_dem::DemSpec;
using fv::test_dem::WriteDem;

constexpr double kDeg = M_PI / 180.0;

class GeoTiffDemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = fs::temp_directory_path() /
           ("fv_geotiff_dem_" + std::to_string(::getpid()) + "_" +
            ::testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(dir_);
    fs::create_directories(dir_);
  }
  void TearDown() override { fs::remove_all(dir_); }

  std::string Write(const std::string& name, const DemSpec& s) {
    const std::string path = (dir_ / name).string();
    WriteDem(path, s);
    return path;
  }
  std::unique_ptr<fv::GeoTiffDem> Open(const std::string& path) {
    std::unique_ptr<fv::GeoTiffDem> dem;
    fv::Status st = fv::GeoTiffDem::Open(path, &dem);
    EXPECT_TRUE(st.ok()) << st.message;
    return dem;
  }

  fs::path dir_;
};

/// Geographic post (c, r) centre for the default spec.
fv::GeoPoint GeoPost(double c, double r) {
  return {33.0 - (r + 0.5) * 0.001, -80.0 + (c + 0.5) * 0.001};
}

TEST_F(GeoTiffDemTest, GeographicTiledLzwIsBilinearBetweenPostCentres) {
  DemSpec s;
  s.predictor = PREDICTOR_FLOATINGPOINT;
  auto dem = Open(Write("geo.tif", s));
  ASSERT_TRUE(dem);
  EXPECT_EQ(dem->Width(), 40);
  EXPECT_EQ(dem->Height(), 30);
  EXPECT_NEAR(dem->Bounds().ll.lat, 32.97, 1e-12);
  EXPECT_NEAR(dem->Bounds().ll.lon, -80.0, 1e-12);
  EXPECT_NEAR(dem->Bounds().ur.lat, 33.0, 1e-12);
  EXPECT_NEAR(dem->Bounds().ur.lon, -79.96, 1e-12);

  float v = 0;
  ASSERT_TRUE(dem->GetElevation(GeoPost(10.3, 7.6), &v).ok());
  EXPECT_NEAR(v, 5 + 0.5 * 10.3 + 0.25 * 7.6, 1e-4);
  // Elevation rises to the east and to the south, as written.
  float east = 0, south = 0;
  ASSERT_TRUE(dem->GetElevation(GeoPost(11.3, 7.6), &east).ok());
  ASSERT_TRUE(dem->GetElevation(GeoPost(10.3, 8.6), &south).ok());
  EXPECT_NEAR(east - v, 0.5, 1e-4);
  EXPECT_NEAR(south - v, 0.25, 1e-4);
  // Inside the outer half pixel the edge post answers.
  ASSERT_TRUE(dem->GetElevation(GeoPost(-0.3, 0), &v).ok());
  EXPECT_NEAR(v, 5.0, 1e-4);
  EXPECT_EQ(dem->GetElevation({33.01, -79.98}, &v).code, fv::kOutOfCoverage);

  double dlat = 0, dlon = 0;
  ASSERT_TRUE(dem->PostSpacing(GeoPost(5, 5), &dlat, &dlon));
  EXPECT_NEAR(dlat, 0.001, 1e-12);
  EXPECT_NEAR(dlon, 0.001, 1e-12);
  EXPECT_NEAR(dem->PostSizeMeters(), 0.001 * 111320.0 * std::cos(32.985 * kDeg), 1e-6);
}

TEST_F(GeoTiffDemTest, PixelIsPointPutsPostsOnTheTiepoint) {
  DemSpec s;
  s.keys = {{1024, 2}, {1025, 2}, {2048, 4326}};
  auto dem = Open(Write("point.tif", s));
  ASSERT_TRUE(dem);
  float v = 0;
  ASSERT_TRUE(dem->GetElevation({33.0 - 3 * 0.001, -80.0 + 4 * 0.001}, &v).ok());
  EXPECT_NEAR(v, 5 + 0.5 * 4 + 0.25 * 3, 1e-4);
}

TEST_F(GeoTiffDemTest, Int16StripsWithNodataAndFeet) {
  DemSpec s;
  s.tiled = false;
  s.block = 4;
  s.format = SAMPLEFORMAT_INT;
  s.bits = 16;
  s.compression = COMPRESSION_ADOBE_DEFLATE;
  s.predictor = PREDICTOR_HORIZONTAL;
  s.keys.push_back({4099, 9002});  // vertical units: feet
  s.nodata = "-9999";
  s.value = [](int c, int r) { return (c == 5 && r == 5) ? -9999.0 : 100.0; };
  auto dem = Open(Write("ft.tif", s));
  ASSERT_TRUE(dem);
  float v = 0;
  ASSERT_TRUE(dem->GetElevation(GeoPost(2, 2), &v).ok());
  EXPECT_NEAR(v, 30.48, 1e-4);
  ASSERT_TRUE(dem->GetElevation(GeoPost(5, 5), &v).ok());
  EXPECT_TRUE(std::isnan(v));
  // A cell touching the void answers its nearest post.
  ASSERT_TRUE(dem->GetElevation(GeoPost(4.2, 5.0), &v).ok());
  EXPECT_NEAR(v, 30.48, 1e-4);
}

/// GEOTRANS's UTM inverse, as the independent reference.
fv::GeoPoint GeotransUtmToGeo(long zone, char hemi, double e, double n) {
  MSP::CCS::UTM utm(6378137.0, 1 / 298.257223563);
  MSP::CCS::UTMCoordinates in(MSP::CCS::CoordinateType::universalTransverseMercator, zone, hemi, e,
                              n);
  std::unique_ptr<MSP::CCS::GeodeticCoordinates> g(utm.convertToGeodetic(&in));
  return {g->latitude() / kDeg, g->longitude() / kDeg};
}

DemSpec UtmSpec() {
  DemSpec s;
  s.width = s.height = 50;
  s.scale = {10.0, 10.0, 0.0};
  s.tiepoint = {0, 0, 0, 740000.0, 3740000.0, 0};
  s.keys = {{1024, 1}, {1025, 1}, {3072, 26916}};  // NAD83 / UTM zone 16N
  // Plane in easting/northing of the post centre.
  s.value = [](int c, int r) {
    const double e = 740000.0 + (c + 0.5) * 10.0, n = 3740000.0 - (r + 0.5) * 10.0;
    return (e - 740000.0) * 0.01 + (n - 3739000.0) * 0.001;
  };
  return s;
}

double UtmPlane(double e, double n) { return (e - 740000.0) * 0.01 + (n - 3739000.0) * 0.001; }

TEST_F(GeoTiffDemTest, UtmPositionsAgreeWithGeotrans) {
  auto dem = Open(Write("utm.tif", UtmSpec()));
  ASSERT_TRUE(dem);
  for (const auto& [e, n] : std::vector<std::pair<double, double>>{
           {740123.4, 3739800.0}, {740011.0, 3739511.0}, {740480.0, 3739990.0}}) {
    const fv::GeoPoint p = GeotransUtmToGeo(16, 'N', e, n);
    float v = 0;
    ASSERT_TRUE(dem->GetElevation(p, &v).ok());
    // 0.01 m of elevation per metre east: 1 mm of position is 1e-5 m.
    EXPECT_NEAR(v, UtmPlane(e, n), 2e-4) << e << "," << n;
  }
  const fv::GeoPoint nw = GeotransUtmToGeo(16, 'N', 740000.0, 3740000.0);
  EXPECT_NEAR(dem->Bounds().ur.lat, nw.lat, 3e-4);  // grid convergence tilts the north edge
  EXPECT_LT(dem->Bounds().ll.lon, nw.lon + 1e-9);
  EXPECT_NEAR(dem->PostSizeMeters(), 10.0, 1e-9);
  double dlat = 0, dlon = 0;
  ASSERT_TRUE(dem->PostSpacing(GeotransUtmToGeo(16, 'N', 740250.0, 3739750.0), &dlat, &dlon));
  EXPECT_NEAR(dlat, 10.0 / 110900.0, 2e-6);
  EXPECT_GT(dlon, dlat);  // a metre of longitude is more degrees at 33 N
}

TEST_F(GeoTiffDemTest, UtmZoneFromCitationAndSouthernHemisphere) {
  DemSpec cited = UtmSpec();
  cited.keys = {{1024, 1}, {1025, 1}, {3072, 32767}, {2048, 6318}};
  cited.pcs_citation = "NAD83(2011) / UTM zone 16N";
  auto dem = Open(Write("cited.tif", cited));
  ASSERT_TRUE(dem);
  float v = 0;
  ASSERT_TRUE(dem->GetElevation(GeotransUtmToGeo(16, 'N', 740123.4, 3739800.0), &v).ok());
  EXPECT_NEAR(v, UtmPlane(740123.4, 3739800.0), 2e-4);

  DemSpec south = UtmSpec();
  south.keys = {{1024, 1}, {1025, 1}, {3072, 32755}};  // WGS 84 / UTM zone 55S
  south.tiepoint = {0, 0, 0, 520000.0, 5250000.0, 0};
  south.value = [](int c, int r) { return 1000.0 + (r + 0.5) * 10.0; };  // falls to the north
  auto sdem = Open(Write("south.tif", south));
  ASSERT_TRUE(sdem);
  ASSERT_TRUE(sdem->GetElevation(GeotransUtmToGeo(55, 'S', 520255.0, 5249745.0), &v).ok());
  EXPECT_NEAR(v, 1255.0, 1e-3);
  EXPECT_LT(sdem->Bounds().ur.lat, 0.0);
}

TEST_F(GeoTiffDemTest, OverviewsAnswerCoarseGrids) {
  DemSpec s;
  s.width = s.height = 64;
  s.scale = {0.0001, 0.0001, 0};
  s.value = [](int, int) { return 100.0; };
  s.overviews = {{2, 200.0}, {4, 300.0}};
  const std::string path = Write("cog.tif", s);
  auto dem = Open(path);
  ASSERT_TRUE(dem);
  const fv::GeoPoint mid{33.0 - 0.0032, -80.0 + 0.0032};
  float v = 0;
  ASSERT_TRUE(dem->GetElevation(mid, &v).ok());
  EXPECT_FLOAT_EQ(v, 100.0f);
  ASSERT_TRUE(dem->GetElevationAtResolution(mid, 25.0, &v).ok());
  EXPECT_FLOAT_EQ(v, 200.0f);  // posts are ~9.3 m, ~18.7 m, ~37 m
  ASSERT_TRUE(dem->GetElevationAtResolution(mid, 1000.0, &v).ok());
  EXPECT_FLOAT_EQ(v, 300.0f);

  fv::GeoTiffDemSource src(path);
  const fv::GeoRect inner{{33.0 - 0.006, -80.0 + 0.0005}, {33.0 - 0.0005, -80.0 + 0.006}};
  fv::ElevationGrid coarse, fine;
  ASSERT_TRUE(fv::SampleElevationGrid(src, inner, 4, 4, &coarse).ok());
  EXPECT_FLOAT_EQ(coarse.At(1, 1), 300.0f);
  ASSERT_TRUE(fv::SampleElevationGrid(src, inner, 120, 120, &fine).ok());
  EXPECT_FLOAT_EQ(fine.At(60, 60), 100.0f);
}

TEST_F(GeoTiffDemTest, AFileWithoutOverviewsGetsBoxMeanLevels) {
  DemSpec s;  // > 1024 posts wide and no overviews
  s.width = 1100;
  s.height = 128;
  s.scale = {0.0001, 0.0001, 0};
  s.block = 64;
  // Alternating columns 0/10 m on a 0.01 m-per-post eastward ramp.
  s.value = [](int c, int) { return (c % 2) * 10.0 + 0.01 * c; };
  auto dem = Open(Write("flat_cog.tif", s));
  ASSERT_TRUE(dem);
  auto at = [](double c, double r) {
    return fv::GeoPoint{33.0 - (r + 0.5) * 0.0001, -80.0 + (c + 0.5) * 0.0001};
  };
  float v = 0;
  ASSERT_TRUE(dem->GetElevation(at(401, 60), &v).ok());
  EXPECT_NEAR(v, 10.0 + 4.01, 1e-4);  // full resolution sees the stripes
  // A 4x4 box mean: half the stripes, and the ramp at the box centre.
  ASSERT_TRUE(dem->GetElevationAtResolution(at(401.5, 61.5), 4 * dem->PostSizeMeters(), &v).ok());
  EXPECT_NEAR(v, 5.0 + 4.015, 1e-3);
  ASSERT_TRUE(dem->GetElevationAtResolution(at(407.5, 55.5), 16 * dem->PostSizeMeters(), &v).ok());
  EXPECT_NEAR(v, 5.0 + 4.075, 1e-3);
  EXPECT_GT(dem->CachedBytes(), 0u);
}

TEST_F(GeoTiffDemTest, ImageryAndForeignDatumsAreRejected) {
  {
    TIFF* tif = TIFFOpen((dir_ / "rgb.tif").c_str(), "w");
    ASSERT_NE(tif, nullptr);
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, 4);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 4);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    unsigned char row[12] = {};
    for (int y = 0; y < 4; ++y) TIFFWriteScanline(tif, row, y, 0);
    TIFFClose(tif);
  }
  DemSpec nad27;
  nad27.keys = {{1024, 2}, {1025, 1}, {2048, 4267}};
  Write("nad27.tif", nad27);
  Write("good.tif", DemSpec{});

  std::unique_ptr<fv::GeoTiffDem> dem;
  EXPECT_EQ(fv::GeoTiffDem::Open((dir_ / "rgb.tif").string(), &dem).code, fv::kUnsupported);
  EXPECT_EQ(fv::GeoTiffDem::Open((dir_ / "nad27.tif").string(), &dem).code, fv::kUnsupported);

  fv::GeoTiffDemEnumerator en;
  ASSERT_TRUE(en.Begin(dir_.string()).ok());
  fv::FrameInfo f;
  ASSERT_TRUE(en.Next(&f));
  EXPECT_EQ(fs::path(f.path).filename(), "good.tif");
  EXPECT_EQ(f.series_key, "Lidar");
  EXPECT_EQ(f.scale, 93.0);  // 0.001 deg of longitude at 33 N
  EXPECT_EQ(f.scale_units, 4);
  EXPECT_FALSE(en.Next(&f));

  fv::GeoTiffDemSource src(dir_.string());
  EXPECT_EQ(src.FileCount(), 1u);
}

TEST_F(GeoTiffDemTest, FinerFileWinsAndVoidsFallThrough) {
  DemSpec coarse;  // 0.001 deg, 10 m everywhere
  coarse.value = [](int, int) { return 10.0; };
  DemSpec fine;  // 0.0005 deg over the NW corner, 20 m with one void
  fine.width = fine.height = 20;
  fine.scale = {0.0005, 0.0005, 0};
  fine.nodata = "-1";
  fine.value = [](int c, int r) { return (c == 3 && r == 3) ? -1.0 : 20.0; };
  fv::GeoTiffDemSource src(std::vector<std::string>{Write("coarse.tif", coarse),
                                                    Write("fine.tif", fine)});
  ASSERT_EQ(src.FileCount(), 2u);
  float v = 0;
  ASSERT_TRUE(src.GetElevation({33.0 - 0.0025, -80.0 + 0.0025}, &v).ok());
  EXPECT_FLOAT_EQ(v, 20.0f);
  ASSERT_TRUE(src.GetElevation({33.0 - 3.5 * 0.0005, -80.0 + 3.5 * 0.0005}, &v).ok());
  EXPECT_FLOAT_EQ(v, 10.0f);  // the fine file's void is filled from the coarse one
  ASSERT_TRUE(src.GetElevation({33.0 - 0.02, -80.0 + 0.03}, &v).ok());
  EXPECT_FLOAT_EQ(v, 10.0f);
  double dlat = 0;
  ASSERT_TRUE(src.PostSpacing({33.0 - 0.0025, -80.0 + 0.0025}, &dlat, nullptr));
  EXPECT_NEAR(dlat, 0.0005, 1e-12);
}

/// A constant-height source with no SampleGrid, standing in for DTED.
class Flat : public fv::IElevationSource {
 public:
  explicit Flat(float h) : h_(h) {}
  fv::GeoRect Bounds() const override { return {{32.0, -81.0}, {34.0, -79.0}}; }
  fv::Status GetElevation(const fv::GeoPoint& p, float* m) override {
    if (!Bounds().Contains(p)) return fv::Status::Error(fv::kOutOfCoverage, "out");
    ++calls;
    *m = h_;
    return fv::Status::Ok();
  }
  int calls = 0;

 private:
  float h_;
};

TEST_F(GeoTiffDemTest, StackPutsLidarOverTheLayerBelow) {
  DemSpec s;
  s.nodata = "-1";
  s.value = [](int c, int) { return c == 0 ? -1.0 : 50.0; };
  auto lidar = std::make_shared<fv::GeoTiffDemSource>(Write("l.tif", s));
  auto flat = std::make_shared<Flat>(7.0f);
  fv::ElevationStack stack({lidar, nullptr, flat});
  EXPECT_EQ(stack.LayerCount(), 2u);
  EXPECT_DOUBLE_EQ(stack.Bounds().ll.lat, 32.0);

  float v = 0;
  ASSERT_TRUE(stack.GetElevation(GeoPost(10, 10), &v).ok());
  EXPECT_FLOAT_EQ(v, 50.0f);
  ASSERT_TRUE(stack.GetElevation(GeoPost(0, 10), &v).ok());
  EXPECT_FLOAT_EQ(v, 7.0f);
  ASSERT_TRUE(stack.GetElevation({32.5, -80.5}, &v).ok());
  EXPECT_FLOAT_EQ(v, 7.0f);
  EXPECT_EQ(stack.GetElevation({40.0, -80.5}, &v).code, fv::kOutOfCoverage);

  // The lower layer is asked only for posts the lidar left empty.
  flat->calls = 0;
  const fv::GeoRect box{{32.975, -79.995}, {32.995, -79.965}};  // inside the lidar
  fv::ElevationGrid g;
  std::vector<unsigned char> cov;
  ASSERT_TRUE(fv::SampleElevationGrid(stack, box, 10, 10, &g, &cov).ok());
  EXPECT_EQ(flat->calls, 0);
  EXPECT_FLOAT_EQ(g.At(5, 5), 50.0f);
  const fv::GeoRect wide{{32.95, -80.02}, {32.99, -79.98}};  // half outside
  ASSERT_TRUE(fv::SampleElevationGrid(stack, wide, 9, 9, &g, &cov).ok());
  EXPECT_GT(flat->calls, 0);
  EXPECT_FLOAT_EQ(g.At(8, 0), 7.0f);   // north-west: off the lidar
  EXPECT_FLOAT_EQ(g.At(8, 8), 50.0f);  // north-east: on it
  EXPECT_EQ(cov[0], 1);
}

TEST_F(GeoTiffDemTest, ContoursRunNorthSouthOnAnEastwardSlope) {
  DemSpec s;
  s.value = [](int c, int) { return 1.0 * c; };  // 1 m per post, rising east
  fv::GeoTiffDemSource src(Write("slope.tif", s));
  const fv::GeoRect box{{32.975, -79.995}, {32.995, -79.965}};
  fv::ElevationGrid g;
  ASSERT_TRUE(fv::SampleElevationGrid(src, box, 31, 21, &g).ok());
  const std::vector<fv::ContourLine> lines = fv::TraceElevationContours(g, 10.0);
  ASSERT_FALSE(lines.empty());
  double last_lon = -180.0;
  for (const fv::ContourLine& line : lines) {
    ASSERT_GE(line.points.size(), 2u);
    // Level L sits at post c = L, i.e. lon = -80 + (L + 0.5) * 0.001.
    const double want = -80.0 + (line.level_m + 0.5) * 0.001;
    for (const fv::GeoPoint& p : line.points) EXPECT_NEAR(p.lon, want, 1e-6);
    EXPECT_GT(line.points.front().lon, last_lon);  // higher levels lie further east
    last_lon = line.points.front().lon;
    EXPECT_GT(std::fabs(line.points.front().lat - line.points.back().lat), 0.015);
  }
}

TEST_F(GeoTiffDemTest, RegistryMakesALidarElevationSource) {
  Write("r.tif", DemSpec{});
  fv::RegisterBuiltinFormats();
  const fv::FormatFactories* f = fv::FindFormat("lidar");
  ASSERT_NE(f, nullptr);
  ASSERT_TRUE(f->make_enumerator);
  ASSERT_TRUE(f->make_elevation_source);
  auto src = f->make_elevation_source(dir_.string());
  float v = 0;
  ASSERT_TRUE(src->GetElevation(GeoPost(2, 2), &v).ok());
  EXPECT_NEAR(v, 5 + 1 + 0.5, 1e-4);
}

TEST_F(GeoTiffDemTest, LidarShadedImageIsThePostGrid) {
  fv::RegisterBuiltinFormats();
  const fv::FormatFactories* f = fv::FindFormat("lidar-shaded");
  ASSERT_NE(f, nullptr);
  ASSERT_TRUE(f->make_raster_source);
  auto src = f->make_raster_source();
  ASSERT_TRUE(src->Open(Write("s.tif", DemSpec{})).ok());
  fv::ImageInfo info;
  ASSERT_TRUE(src->Info(&info).ok());
  EXPECT_EQ(info.size.width, 40);
  EXPECT_EQ(info.size.height, 30);
  fv::GeoPoint g;
  ASSERT_TRUE(src->PixelToGeo(0, 0, &g).ok());
  EXPECT_NEAR(g.lon, -79.9995, 1e-9);
  EXPECT_NEAR(g.lat, 32.9995, 1e-9);
  double px = 0, py = 0;
  ASSERT_TRUE(src->GeoToPixel(GeoPost(7, 3), &px, &py).ok());
  EXPECT_NEAR(px, 7, 1e-6);
  EXPECT_NEAR(py, 3, 1e-6);
}

TEST_F(GeoTiffDemTest, LidarShadedBlocksAgreeWithTheWholeImage) {
  DemSpec s;
  s.value = [](int c, int r) { return 200 + 60 * std::sin(c * 0.4) + 40 * std::cos(r * 0.3); };
  fv::LidarShadedRasterSource src;
  ASSERT_TRUE(src.Open(Write("b.tif", s)).ok());
  fv::PixelBuffer whole, part;
  ASSERT_TRUE(src.ReadBlock({0, 0, 40, 30}, &whole).ok());
  ASSERT_TRUE(src.ReadBlock({11, 7, 13, 9}, &part).ok());
  for (int y = 0; y < part.Height(); ++y)
    EXPECT_EQ(0, std::memcmp(part.Row(y), whole.Row(7 + y) + 4 * 11, 4 * part.Width()))
        << "row " << y;
  // Relief, not a flat fill: the sun lights some slopes more than others.
  int lo = 255, hi = 0;
  for (int y = 0; y < whole.Height(); ++y)
    for (int x = 0; x < whole.Width(); ++x) {
      EXPECT_EQ(whole.Row(y)[4 * x + 3], 255);
      lo = std::min<int>(lo, whole.Row(y)[4 * x + 1]);
      hi = std::max<int>(hi, whole.Row(y)[4 * x + 1]);
    }
  EXPECT_GT(hi - lo, 10);
  EXPECT_FALSE(src.ReadBlock({30, 0, 11, 1}, &part).ok());
}

TEST_F(GeoTiffDemTest, LidarShadedVoidsAreTransparent) {
  DemSpec s;
  s.nodata = "-9999";
  s.value = [](int c, int r) { return (c < 10 && r < 10) ? -9999.0 : 150.0 + c; };
  fv::LidarShadedRasterSource src;
  ASSERT_TRUE(src.Open(Write("v.tif", s)).ok());
  fv::PixelBuffer img;
  ASSERT_TRUE(src.ReadBlock({0, 0, 40, 30}, &img).ok());
  EXPECT_EQ(img.Row(2)[4 * 2 + 3], 0);
  EXPECT_EQ(img.Row(20)[4 * 20 + 3], 255);
}

}  // namespace

namespace {

TEST_F(GeoTiffDemTest, ImageryEnumeratorLeavesDemsAlone) {
  DemSpec tiled;
  Write("tiled.tif", tiled);
  DemSpec strips;
  strips.tiled = false;
  strips.compression = COMPRESSION_NONE;
  Write("strips.tif", strips);
  fv::GeoTiffFrameEnumerator en;
  ASSERT_TRUE(en.Begin(dir_.string()).ok());
  fv::FrameInfo f;
  EXPECT_FALSE(en.Next(&f)) << f.path << " catalogued as imagery";
}

}  // namespace

namespace {

/// Real DEMs from FVW_TESTDATA_DIR/lidar, enumerated; skipped without them.
TEST(GeoTiffDemData, EveryLidarFileReadsLikeTerrain) {
  const char* root = std::getenv("FVW_TESTDATA_DIR");
  const std::string dir = root ? std::string(root) + "/lidar" : std::string();
  if (dir.empty() || !fs::is_directory(dir)) GTEST_SKIP() << "no testdata/lidar";
  fv::GeoTiffDemEnumerator en;
  ASSERT_TRUE(en.Begin(dir).ok());
  fv::DtedElevationSource dted(std::string(root) + "/dted");
  int files = 0;
  for (fv::FrameInfo f; en.Next(&f);) {
    ++files;
    SCOPED_TRACE(f.path);
    EXPECT_EQ(f.series_key, "Lidar");
    EXPECT_GT(f.scale, 0.0);
    const fv::GeoRect b = f.bounds;
    ASSERT_LT(b.ll.lat, b.ur.lat);
    ASSERT_LT(b.ll.lon, b.ur.lon);

    fv::GeoTiffDemSource src(f.path);
    ASSERT_EQ(src.FileCount(), 1u);
    // An inset box, so edge voids of a clipped file do not dominate.
    const double dlat = b.ur.lat - b.ll.lat, dlon = b.ur.lon - b.ll.lon;
    const fv::GeoRect inner{{b.ll.lat + 0.1 * dlat, b.ll.lon + 0.1 * dlon},
                            {b.ur.lat - 0.1 * dlat, b.ur.lon - 0.1 * dlon}};
    fv::ElevationGrid coarse, fine;
    ASSERT_TRUE(fv::SampleElevationGrid(src, inner, 64, 64, &coarse).ok());
    ASSERT_TRUE(fv::SampleElevationGrid(src, inner, 1024, 1024, &fine).ok());
    int finite = 0, compared = 0, agree = 0, dted_close = 0, dted_seen = 0;
    double lo = 1e9, hi = -1e9;
    for (int r = 0; r < 64; ++r)
      for (int c = 0; c < 64; ++c) {
        const float v = coarse.At(r, c);
        if (std::isnan(v)) continue;
        ++finite;
        lo = std::min<double>(lo, v);
        hi = std::max<double>(hi, v);
        // Same post on the full-resolution grid (64 -> 1024 posts: 1023/63 apart).
        const float w = fine.At(r * 1023 / 63, c * 1023 / 63);
        if (!std::isnan(w)) {
          ++compared;
          // A box mean and a point differ by local relief, not by tens of metres.
          if (std::fabs(v - w) < 25.0) ++agree;
        }
        float d = 0;
        const fv::GeoPoint p{inner.ll.lat + r * coarse.LatStep(),
                             inner.ll.lon + c * coarse.LonStep()};
        if (dted.GetElevation(p, &d).ok() && !std::isnan(d)) {
          ++dted_seen;
          // Metres on both; DTED carries buildings and canopy on top.
          if (std::fabs(v - d) < 100.0) ++dted_close;
        }
      }
    EXPECT_GT(finite, 64 * 64 / 2);
    EXPECT_GE(lo, -500.0);
    EXPECT_LE(hi, 9000.0);
    EXPECT_LT(lo, hi);  // terrain, not a constant fill
    ASSERT_GT(compared, 0);
    EXPECT_GT(agree, compared * 9 / 10);
    if (dted_seen > 0) {
      EXPECT_GT(dted_close, dted_seen * 9 / 10);
    }
    EXPECT_FALSE(fv::TraceElevationContours(coarse, 10.0).empty());
  }
  EXPECT_GT(files, 0);
}

}  // namespace
