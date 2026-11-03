// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The python package kind end to end: the rangerings-py example is installed
// beside rangerings-c, both are drawn, hit-tested, edited and saved, and the
// two must agree pixel for pixel. Without the bridge, python packages are
// unavailable and the C package still loads.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "fv_desk_base_map.h"
#include "fv_desk_fake.h"
#include "fv_desk_packages.h"
#include "fv_desk_python_plugin.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/properties.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "scratch.h"

namespace {

namespace fs = std::filesystem;
using fv::desk::PackageState;
using fv::desk::test::Scratch;

constexpr char kPyPackage[] = "com.example.pyrings";
constexpr char kPyType[] = "com.example.pyrings.rings";
constexpr char kCType[] = "com.example.rangerings.rings";
constexpr double kMetresPerNm = 1852.0;

std::string ReadFile(const fs::path& path) {
  std::ostringstream ss;
  ss << std::ifstream(path, std::ios::binary).rdbuf();
  return ss.str();
}

/// Pixels that differ between two buffers of the same size.
int DifferentPixels(const fv::PixelBuffer& a, const fv::PixelBuffer& b) {
  int n = 0;
  for (int y = 0; y < a.Height(); ++y)
    for (int x = 0; x < a.Width(); ++x)
      if (std::memcmp(a.Row(y) + x * 4, b.Row(y) + x * 4, 4) != 0) ++n;
  return n;
}

/// Pixels with any ink.
int InkedPixels(const fv::PixelBuffer& buf) {
  int n = 0;
  for (int y = 0; y < buf.Height(); ++y)
    for (int x = 0; x < buf.Width(); ++x)
      if (buf.Row(y)[x * 4 + 3] > 0) ++n;
  return n;
}

// MARK: Without Python

// First in the file: once a bridge loads it stays loaded for the process.
TEST(PythonUnavailable, PythonPackagesAreUnavailableWithoutTheBridge) {
  Scratch scratch;
  fv::desk::FakeDesk fake;
  fv::desk::SetPythonBridgeLocation(scratch.Path("absent/libperegrine_python.dylib"));
  fake.desk().LoadPackages(fv::desk::PackageDirs{std::string(), scratch.Path("user")});
  fv::desk::InstallResult r;
  ASSERT_TRUE(fake.desk().InstallPackage(FV_PYRINGS_PGPKG, &r).ok());
  ASSERT_TRUE(fake.desk().InstallPackage(FV_RANGERINGS_PGPKG, &r).ok());
  EXPECT_TRUE(r.live);

  const fv::desk::PackageInfo* py = fake.desk().packages()->Find(kPyPackage);
  ASSERT_NE(py, nullptr);
  EXPECT_EQ(py->state, PackageState::kUnavailable);
  EXPECT_NE(py->reason.find("Python support is not installed"), std::string::npos) << py->reason;
  EXPECT_EQ(fake.desk().types().Find(kPyType), nullptr);
  EXPECT_NE(fake.desk().types().Find(kCType), nullptr);
}

// MARK: With Python

class PythonPluginTest : public ::testing::Test {
 protected:
  void SetUp() override {
#ifndef FV_PYTHON_BRIDGE
    GTEST_SKIP() << "built without Python";
#else
    fv::desk::SetPythonBridgeLocation(FV_PYTHON_BRIDGE);
    desk().LoadPackages(fv::desk::PackageDirs{std::string(), scratch_.Path("user")});
    for (const char* pgpkg : {FV_PYRINGS_PGPKG, FV_RANGERINGS_PGPKG}) {
      fv::desk::InstallResult r;
      const fv::Status s = desk().InstallPackage(pgpkg, &r);
      ASSERT_TRUE(s.ok()) << s.message;
      const fv::desk::PackageInfo* p = desk().packages()->Find(r.id);
      ASSERT_TRUE(r.live) << r.id << ": " << (p != nullptr ? p->reason : "");
    }
    py_ = desk().types().Find(kPyType)->factory();
    c_ = desk().types().Find(kCType)->factory();
    ASSERT_NE(py_, nullptr);
    ASSERT_NE(c_, nullptr);

    ASSERT_TRUE(proj_.SetSurfaceSize(512, 512).ok());
    ASSERT_TRUE(proj_.SetCenter(fv::GeoPoint{32.7765, -79.9311}).ok());
    ASSERT_TRUE(proj_.SetScale(100000).ok());
#endif
  }

  fv::desk::Desk& desk() { return fake_.desk(); }

  /// Draws `o` on a fresh transparent canvas, with the host's label font so
  /// that the ring labels are compared too.
  fv::Status Draw(fv::Overlay& o, fv::CpuCanvas* canvas) {
    canvas->Clear(fv::FvColor{0, 0, 0, 0});
    if (!fv::desk::HostLabelFont().empty()) canvas->SetDefaultFont(fv::desk::HostLabelFont());
    return o.OnDraw(proj_, *canvas);
  }

  /// Draws both overlays and answers how many pixels differ.
  int DrawBothAndCompare() {
    fv::CpuCanvas a(512, 512), b(512, 512);
    const fv::Status sa = Draw(*py_, &a);
    const fv::Status sb = Draw(*c_, &b);
    EXPECT_TRUE(sa.ok()) << sa.message;
    EXPECT_TRUE(sb.ok()) << sb.message;
    EXPECT_GT(InkedPixels(b.Buffer()), 500);
    return DifferentPixels(a.Buffer(), b.Buffer());
  }

  /// Surface position of `metres` north of the default centre.
  fv::PixelPoint NorthOfCentre(double metres) const {
    double x = 0, y = 0;
    proj_.GeoToSurface(fv::GeoPoint{32.7765 + metres / 6371008.8 * 180.0 / M_PI, -79.9311}, &x, &y);
    return fv::PixelPoint{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))};
  }

  static std::vector<fv::app::HitItem> HitsAt(fv::Overlay& o, const fv::MapProjection& proj,
                                              fv::PixelPoint p) {
    std::vector<fv::app::HitItem> hits;
    o.AsHitTest()->HitTestPoint(proj, p, 3.0, hits);
    return hits;
  }

  Scratch scratch_;
  fv::desk::FakeDesk fake_;
  std::shared_ptr<fv::Overlay> py_, c_;
  fv::MapProjection proj_;
};

TEST_F(PythonPluginTest, TheExampleLoadsWithItsCapabilitiesAndTool) {
  EXPECT_EQ(desk().packages()->Find(kPyPackage)->state, PackageState::kLoaded);
  EXPECT_NE(desk().tools().Find("com.example.pyrings.open_sample"), nullptr);
  EXPECT_EQ(py_->Name(), "Range Rings (Python)");
  EXPECT_NE(py_->AsProperties(), nullptr);
  EXPECT_NE(py_->AsPersistence(), nullptr);
  EXPECT_NE(py_->AsHitTest(), nullptr);
}

TEST_F(PythonPluginTest, DrawsTheSamePixelsAsTheCExample) {
  EXPECT_EQ(DrawBothAndCompare(), 0);
}

TEST_F(PythonPluginTest, DrawsOnAnotherThread) {
  fv::CpuCanvas canvas(512, 512);
  fv::Status s;
  std::thread worker([&] { s = Draw(*py_, &canvas); });
  worker.join();
  EXPECT_TRUE(s.ok()) << s.message;
  EXPECT_GT(InkedPixels(canvas.Buffer()), 500);
}

TEST_F(PythonPluginTest, HitTestAgreesWithTheCExample) {
  for (double nm : {0.0, 1.0, 2.0, 3.0, 1.5}) {
    const fv::PixelPoint p = NorthOfCentre(nm * kMetresPerNm);
    const std::vector<fv::app::HitItem> py = HitsAt(*py_, proj_, p);
    const std::vector<fv::app::HitItem> c = HitsAt(*c_, proj_, p);
    ASSERT_EQ(py.size(), c.size()) << nm << " nm";
    if (c.empty()) continue;
    EXPECT_EQ(py[0].overlay, py_.get());
    EXPECT_EQ(py[0].feature, c[0].feature) << nm << " nm";
    EXPECT_EQ(py[0].hint.tool_tip, c[0].hint.tool_tip) << nm << " nm";
    EXPECT_NEAR(py[0].distance_px, c[0].distance_px, 1e-9) << nm << " nm";
  }
  EXPECT_EQ(HitsAt(*py_, proj_, NorthOfCentre(2 * kMetresPerNm))[0].hint.tool_tip, "Ring 2: 2 nm");
}

TEST_F(PythonPluginTest, PropertiesRoundTripAndStillDrawLikeC) {
  fv::app::Properties* props = py_->AsProperties();
  ASSERT_NE(props, nullptr);
  const std::vector<fv::app::PropertySpec>& specs = props->Describe();
  const std::vector<fv::app::PropertySpec>& c_specs = c_->AsProperties()->Describe();
  ASSERT_EQ(specs.size(), c_specs.size());
  for (size_t i = 0; i < specs.size(); ++i) {
    EXPECT_EQ(specs[i].key, c_specs[i].key);
    EXPECT_EQ(specs[i].type, c_specs[i].type) << specs[i].key;
    EXPECT_EQ(specs[i].default_value.ToString(), c_specs[i].default_value.ToString()) << specs[i].key;
  }

  const fv::FvColor blue{0, 0, 255, 255};
  for (fv::Overlay* o : {py_.get(), c_.get()}) {
    fv::app::Properties* p = o->AsProperties();
    ASSERT_TRUE(p->SetProperty("ring_count", fv::app::PropertyValue::Int(5)).ok());
    ASSERT_TRUE(p->SetProperty("spacing_nm", fv::app::PropertyValue::Double(0.5)).ok());
    ASSERT_TRUE(p->SetProperty("line_color", fv::app::PropertyValue::Color(blue)).ok());
    ASSERT_TRUE(p->SetProperty("labels", fv::app::PropertyValue::Bool(false)).ok());
  }
  EXPECT_EQ(props->GetInt("ring_count", 0), 5);
  EXPECT_DOUBLE_EQ(props->GetDouble("spacing_nm", 0), 0.5);
  EXPECT_EQ(props->GetColor("line_color", fv::FvColor{}).b, 255);
  EXPECT_FALSE(props->GetBool("labels", true));
  EXPECT_EQ(DrawBothAndCompare(), 0);

  // The C plugin clamps; the declared range refuses before Python is called.
  EXPECT_EQ(props->SetProperty("ring_count", fv::app::PropertyValue::Int(99)).code, fv::kInvalidArg);
  EXPECT_EQ(props->GetInt("ring_count", 0), 5);
  EXPECT_EQ(props->SetProperty("spacing_nm", fv::app::PropertyValue::String("2")).code,
            fv::kInvalidArg);
  EXPECT_EQ(props->SetProperty("nope", fv::app::PropertyValue::Int(1)).code, fv::kNotFound);
}

TEST_F(PythonPluginTest, OpensAndSavesItsDocument) {
  fv::app::Persistence* doc = py_->AsPersistence();
  ASSERT_NE(doc, nullptr);
  const fs::path sample = fs::path(scratch_.Path("user")) / kPyPackage / "resources" / "charleston.prng";
  ASSERT_TRUE(doc->FileOpen(sample.string()).ok());  // the session flow records file_spec
  EXPECT_EQ(py_->AsProperties()->GetInt("ring_count", 0), 4);
  EXPECT_DOUBLE_EQ(py_->AsProperties()->GetDouble("spacing_nm", 0), 2.0);

  const std::string out = scratch_.Path("saved.prng");
  ASSERT_TRUE(doc->FileSaveAs(out, 0).ok());
  EXPECT_EQ(ReadFile(out), "center 32.77650000 -79.93110000\nrings 4\nspacing_nm 2\n");

  EXPECT_EQ(doc->FileOpen(scratch_.Path("absent.prng")).code, fv::kIoError);
}

TEST_F(PythonPluginTest, AClickMovesTheCentreAfterAFrame) {
  fv::MouseEvent e;
  e.x = 100;
  e.y = 120;
  EXPECT_FALSE(py_->OnMouseDown(e));  // no frame drawn yet

  fv::CpuCanvas canvas(512, 512);
  ASSERT_TRUE(Draw(*py_, &canvas).ok());
  EXPECT_TRUE(py_->OnMouseDown(e));
  EXPECT_TRUE(py_->AsPersistence()->is_dirty());
  const std::vector<fv::app::HitItem> hits = HitsAt(*py_, proj_, fv::PixelPoint{100, 120});
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].feature, 0u);
}

TEST_F(PythonPluginTest, TheToolOpensTheSampleAsAnOverlay) {
  ASSERT_TRUE(desk().Execute("tools.run.com.example.pyrings.open_sample").ok());
  EXPECT_TRUE(fake_.shell().errors.empty());
  const std::vector<std::shared_ptr<fv::Overlay>> open = desk().overlays().OfType(kPyType);
  ASSERT_EQ(open.size(), 1u);
  EXPECT_EQ(open[0]->AsProperties()->GetInt("ring_count", 0), 4);
}

// MARK: Refusals

class PythonRefusalTest : public PythonPluginTest {
 protected:
  /// Links a one-overlay package whose module `pyrefused` has `source`.
  const fv::desk::PackageInfo* LinkPackage(const std::string& requires_python,
                                           const std::string& source) {
    const fs::path dir = fs::path(scratch_.Path("linked")) / "com.example.pyrefused";
    fs::create_directories(dir / "python");
    std::ofstream(dir / "python" / "pyrefused.py") << source;
    std::ofstream(dir / "peregrine-package.json") << R"({
  "format": 1, "id": "com.example.pyrefused", "name": "Refused", "version": "1.0",
  "requires": { "python": ")" + requires_python + R"(" }, "python_path": "python",
  "overlays": [ { "id": "com.example.pyrefused.o", "display_name": "Refused",
                  "implementation": { "python": "pyrefused:Thing" } } ]
})";
    fv::desk::InstallResult r;
    desk().LinkPackage(dir.string(), &r);
    return desk().packages()->Find("com.example.pyrefused");
  }
};

TEST_F(PythonRefusalTest, PythonOlderThan310IsUnavailable) {
  const fv::desk::PackageInfo* p = LinkPackage("3.9", "");
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->state, PackageState::kUnavailable);
  EXPECT_NE(p->reason.find("3.10"), std::string::npos) << p->reason;
}

TEST_F(PythonRefusalTest, AModuleThatRaisesFailsWithThePythonError) {
  const fv::desk::PackageInfo* p = LinkPackage("3.10", "1 / 0\n");
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->state, PackageState::kFailed);
  EXPECT_NE(p->reason.find("ZeroDivisionError"), std::string::npos) << p->reason;
}

TEST_F(PythonRefusalTest, AClassThatIsNotAnOverlayFails) {
  const fv::desk::PackageInfo* p = LinkPackage("3.10", "class Thing:\n    pass\n");
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->state, PackageState::kFailed);
  EXPECT_NE(p->reason.find("not a pyfvw.overlay.Overlay"), std::string::npos) << p->reason;
}

}  // namespace
