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
#include <map>
#include <optional>
#include <sstream>
#include <thread>

#include "fv_desk_base_map.h"
#include "fv_desk_fake.h"
#include "fv_desk_menu_model.h"
#include "fv_desk_packages.h"
#include "fv_desk_python_plugin.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/properties.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/formats/registry.h"
#include "scratch.h"

namespace {

/// True when the test DTED covers the Charleston cell (w080/n32) the viewshed
/// tests observe from; the sample set in a clean clone covers only Kiawah.
bool HasCharlestonDted() {
  for (const char* name : {"n32.dt1", "n32.dt2"})
    if (std::filesystem::exists(std::filesystem::path(FV_TESTDATA_DTED) / "w080" / name)) return true;
  return false;
}

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

TEST_F(PythonPluginTest, EditHintAndItemInfoAgreeWithTheCExample) {
  ASSERT_NE(py_->AsEditHint(), nullptr);
  ASSERT_NE(c_->AsEditHint(), nullptr);
  EXPECT_EQ(py_->AsEditHint()->EditHintAt(fv::PixelPoint{10, 10}), "Move the centre here");
  EXPECT_EQ(py_->AsEditHint()->EditHintAt(fv::PixelPoint{10, 10}),
            c_->AsEditHint()->EditHintAt(fv::PixelPoint{10, 10}));

  ASSERT_NE(py_->AsItemInfo(), nullptr);
  ASSERT_NE(c_->AsItemInfo(), nullptr);
  for (double nm : {0.0, 2.0, 1.5}) {
    const fv::PixelPoint p = NorthOfCentre(nm * kMetresPerNm);
    const std::optional<fv::app::ItemInfoSheet> py = py_->AsItemInfo()->InfoAt(proj_, p, 3.0);
    const std::optional<fv::app::ItemInfoSheet> c = c_->AsItemInfo()->InfoAt(proj_, p, 3.0);
    ASSERT_EQ(py.has_value(), c.has_value()) << nm << " nm";
    if (!c) continue;
    EXPECT_EQ(py->title, c->title);
    EXPECT_EQ(py->edit_label, c->edit_label);
    ASSERT_EQ(py->rows.size(), c->rows.size()) << nm << " nm";
    for (size_t i = 0; i < c->rows.size(); ++i) {
      EXPECT_EQ(py->rows[i].section, c->rows[i].section) << nm << " nm, row " << i;
      EXPECT_EQ(py->rows[i].key, c->rows[i].key) << nm << " nm, row " << i;
      EXPECT_EQ(py->rows[i].value, c->rows[i].value) << nm << " nm, row " << i;
    }
  }
  const std::optional<fv::app::ItemInfoSheet> ring =
      py_->AsItemInfo()->InfoAt(proj_, NorthOfCentre(2 * kMetresPerNm), 3.0);
  ASSERT_TRUE(ring.has_value());
  EXPECT_EQ(ring->title, "Range Rings");
  EXPECT_EQ(ring->edit_label, "Edit Rings\xE2\x80\xA6");
  ASSERT_EQ(ring->rows.size(), 6u);
  EXPECT_EQ(ring->rows[1].key, "Radius");
  EXPECT_EQ(ring->rows[1].value, "2 nm");
  EXPECT_EQ(ring->rows[2].value, "32.776500");
  EXPECT_FALSE(py_->AsItemInfo()->InfoAt(proj_, NorthOfCentre(1.5 * kMetresPerNm), 3.0));
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

// MARK: The analysis example

/// The analysis-py package installed alone: a file overlay edited through its
/// context menu, with a profile and a viewshed over the test DTED.
class AnalysisPackageTest : public PythonPluginTest {
 protected:
  void SetUp() override {
    PythonPluginTest::SetUp();
    if (IsSkipped() || HasFatalFailure()) return;
    fv::desk::InstallResult r;
    const fv::Status s = desk().InstallPackage(FV_ANALYSIS_PGPKG, &r);
    ASSERT_TRUE(s.ok()) << s.message;
    const fv::desk::PackageInfo* p = desk().packages()->Find(r.id);
    ASSERT_TRUE(r.live) << r.id << ": " << (p != nullptr ? p->reason : "");
  }

  /// Gives the desk a catalog holding testdata/dted, the map data the
  /// analysis overlay takes its terrain from.
  void UseTestDted() {
    fv::ClearFormatRegistryForTest();
    fv::RegisterBuiltinFormats();
    auto cat = std::make_shared<fv::Catalog>();
    ASSERT_TRUE(cat->Open(":memory:").ok());
    int64_t src = 0;
    int added = 0;
    ASSERT_TRUE(cat->AddDataSource(FV_TESTDATA_DTED, "dted", 0, &src).ok());
    ASSERT_TRUE(cat->Scan(src, &added).ok());
    desk().SetCatalog(cat);
    ASSERT_NE(desk().elevation_source(), nullptr);
  }

  /// Waits until the observer at `at` has finished computing; false if there
  /// is no observer there or it is still computing after a minute.
  static bool WaitForViewshed(fv::Overlay& o, const fv::MapProjection& proj, fv::PixelPoint at) {
    for (int i = 0; i < 6000; ++i) {
      const std::vector<fv::app::HitItem> hits = HitsAt(o, proj, at);
      if (hits.empty() || hits[0].hint.status.find("Observer") == std::string::npos) return false;
      if (hits[0].hint.status.find("computing") == std::string::npos) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  }

  /// Runs the context-menu entry labelled `label` for a right-click at `at`.
  static bool RunMenu(fv::Overlay& o, const fv::MapProjection& proj, fv::PixelPoint at,
                      const std::string& label) {
    fv::app::MenuNode menu;
    o.AsContextMenu()->AppendMenuItems(proj, at, menu);
    for (const fv::app::MenuNode& n : menu.children) {
      if (n.label == label && n.action) {
        n.action();
        return true;
      }
    }
    return false;
  }
};

TEST_F(AnalysisPackageTest, LoadsWithItsCapabilitiesAndTool) {
  EXPECT_NE(desk().types().Find("com.example.analysis.measure"), nullptr);
  EXPECT_NE(desk().tools().Find("com.example.analysis.open_sample"), nullptr);
  std::shared_ptr<fv::Overlay> o = desk().types().Find("com.example.analysis.measure")->factory();
  ASSERT_NE(o, nullptr);
  EXPECT_EQ(o->Name(), "Analysis (Python)");
  EXPECT_NE(o->AsPersistence(), nullptr);
  EXPECT_NE(o->AsContextMenu(), nullptr);
  EXPECT_NE(o->AsHitTest(), nullptr);
  ASSERT_NE(o->AsProperties(), nullptr);
  const fv::app::PropertySpec* units = nullptr;
  for (const fv::app::PropertySpec& s : o->AsProperties()->Describe()) {
    EXPECT_NE(s.key, "elevation_dir");  // terrain comes from the catalog
    if (s.key == "units") units = &s;
  }
  ASSERT_NE(units, nullptr);
  ASSERT_TRUE(o->AsProperties()->SetProperty("units", fv::app::PropertyValue::Choice(2)).ok());
  fv::app::PropertyValue v;
  ASSERT_TRUE(o->AsProperties()->GetProperty("units", &v).ok());
  EXPECT_EQ(v.i, 2);
}

TEST_F(AnalysisPackageTest, MeasuresProfilesAndComputesAViewshedFromTheMenu) {
  if (!HasCharlestonDted()) GTEST_SKIP() << "no DTED for w080/n32 under " << FV_TESTDATA_DTED;
  ASSERT_TRUE(desk().Execute("tools.run.com.example.analysis.open_sample").ok());
  EXPECT_TRUE(fake_.shell().errors.empty());
  const std::vector<std::shared_ptr<fv::Overlay>> open =
      desk().overlays().OfType("com.example.analysis.measure");
  ASSERT_EQ(open.size(), 1u);
  fv::Overlay& o = *open[0];
  double sx = 0, sy = 0;
  ASSERT_TRUE(proj_.GeoToSurface(fv::GeoPoint{32.79, -79.90}, &sx, &sy).ok());
  const fv::PixelPoint sample{static_cast<int>(std::lround(sx)), static_cast<int>(std::lround(sy))};
  ASSERT_TRUE(WaitForViewshed(o, proj_, sample));
  EXPECT_NE(HitsAt(o, proj_, sample)[0].hint.status.find("no elevation data"), std::string::npos);

  UseTestDted();  // the catalog change reaches the overlay, which computes what lacked terrain
  ASSERT_TRUE(WaitForViewshed(o, proj_, sample));
  EXPECT_EQ(HitsAt(o, proj_, sample)[0].hint.status.find("("), std::string::npos)
      << HitsAt(o, proj_, sample)[0].hint.status;

  fv::CpuCanvas before(512, 512);
  ASSERT_TRUE(Draw(o, &before).ok());

  ASSERT_TRUE(RunMenu(o, proj_, fv::PixelPoint{300, 200}, "Add Viewshed Here"));
  ASSERT_TRUE(RunMenu(o, proj_, fv::PixelPoint{100, 100}, "Measure Line From Here"));
  ASSERT_TRUE(RunMenu(o, proj_, fv::PixelPoint{400, 300}, "Add Point Here"));
  ASSERT_TRUE(WaitForViewshed(o, proj_, fv::PixelPoint{300, 200}));
  EXPECT_TRUE(desk().FlushOverlayRedraw());  // the finished viewsheds asked for a repaint
  fv::CpuCanvas after(512, 512);
  ASSERT_TRUE(Draw(o, &after).ok());
  EXPECT_GT(DifferentPixels(before.Buffer(), after.Buffer()), 500);
  EXPECT_TRUE(o.AsPersistence()->is_dirty());

  const std::vector<fv::app::HitItem> hits = HitsAt(o, proj_, fv::PixelPoint{400, 300});
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].feature, 2001u);  // point 2 of the third measurement: the sample's stay

  const std::string saved = scratch_.Path("line.pyana");
  ASSERT_TRUE(o.AsPersistence()->FileSaveAs(saved, 0).ok());
  const std::string doc = ReadFile(saved);
  EXPECT_NE(doc.find("\"multi_point\""), std::string::npos) << doc;
  EXPECT_NE(doc.find("\"area\""), std::string::npos) << doc;
  EXPECT_NE(doc.find("\"range_bearing\""), std::string::npos) << doc;
}

// MARK: Editor

/// The editor palette's commands by label.
std::map<std::string, std::string> EditorTools(const fv::desk::Desk& desk) {
  std::map<std::string, std::string> by_label;
  const fv::desk::Menu* editor = desk.menus().Find("editor");
  if (editor == nullptr) return by_label;
  for (const fv::desk::MenuItem& item : editor->items)
    if (!item.is_separator()) by_label[item.label] = item.command_id;
  return by_label;
}

/// True when the toolbar shows `command_id`.
bool OnToolbar(const fv::desk::Desk& desk, const std::string& command_id) {
  for (const fv::desk::MenuItem& item : desk.menus().Toolbar())
    if (item.command_id == command_id) return true;
  return false;
}

/// The analysis package driven through its editor palette and the pointer.
class AnalysisEditorTest : public AnalysisPackageTest {
 protected:
  void SetUp() override {
    AnalysisPackageTest::SetUp();
    if (IsSkipped() || HasFatalFailure()) return;
    auto& mv = desk().map_view();
    mv.SetViewport(mv.View().WithCamera(fv::GeoPoint{32.7765, -79.9311}, 100000, 0));
  }

  void Click(double x, double y) {
    ASSERT_TRUE(desk().OverlayPointerDown(x, y));
    desk().OverlayPointerUp(x, y);
  }

  /// The document as saved now.
  std::string Saved(fv::Overlay& o) {
    const std::string path = scratch_.Path("doc.pyana");
    EXPECT_TRUE(o.AsPersistence()->FileSaveAs(path, 0).ok());
    return ReadFile(path);
  }

  /// Hovers (x, y) until its status no longer says "computing"; returns that status.
  std::string SettledStatusAt(double x, double y) {
    fv::PixelBuffer frame;
    std::string status;
    for (int i = 0; i < 6000; ++i) {
      EXPECT_TRUE(fake_.Render(&frame).ok());
      desk().OverlayHover(x, y);
      status = desk().CurrentStatus().feedback;
      if (status.find("computing") == std::string::npos) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return status;
  }

  const std::string type_ = "com.example.analysis.measure";
  const std::string mode_ = "editor.mode." + type_;
};

TEST_F(AnalysisEditorTest, ThePaletteDrawsDragsUndoesAndComputesViewsheds) {
  if (!HasCharlestonDted()) GTEST_SKIP() << "no DTED for w080/n32 under " << FV_TESTDATA_DTED;
  const fv::app::OverlayTypeDesc* type = desk().types().Find(type_);
  ASSERT_NE(type, nullptr);
  EXPECT_TRUE(type->editor_factory);
  EXPECT_NE(type->icon.find("analysis.svg"), std::string::npos) << type->icon;
  ASSERT_TRUE(fake_.Run({mode_}).ok());
  ASSERT_TRUE(desk().Editing());
  std::shared_ptr<fv::Overlay> o = desk().overlays().FirstOfType(type_);
  ASSERT_NE(o, nullptr);
  std::map<std::string, std::string> tools = EditorTools(desk());
  for (const char* label : {"Line", "Path", "Area", "Viewshed", "Profile"})
    EXPECT_TRUE(OnToolbar(desk(), tools[label])) << label;
  EXPECT_NE(desk().commands().Find(tools["Viewshed"])->icon.find("viewshed.svg"),
            std::string::npos);
  EXPECT_TRUE(desk().commands().IsChecked(tools["Profile"]));
  EXPECT_FALSE(desk().commands().IsEnabled(tools["Clear"]));

  fv::PixelBuffer frame;
  ASSERT_TRUE(fake_.Render(&frame).ok());  // the overlay maps the pointer through its last frame
  EXPECT_FALSE(desk().OverlayPointerDown(300, 300));  // nothing armed, nothing to grab: the map pans
  desk().OverlayPointerUp(300, 300);

  ASSERT_TRUE(desk().Execute(tools["Path"]).ok());
  EXPECT_TRUE(desk().commands().IsChecked(tools["Path"]));
  Click(300, 300);
  Click(400, 250);
  Click(500, 320);
  fv::KeyEvent ret;
  ret.key = fv::Key::kReturn;
  ASSERT_TRUE(desk().OverlayKeyDown(ret));
  EXPECT_FALSE(desk().commands().IsChecked(tools["Path"]));
  const std::string drawn = Saved(*o);
  EXPECT_NE(drawn.find("\"multi_point\""), std::string::npos) << drawn;
  EXPECT_TRUE(desk().commands().IsEnabled(tools["Clear"]));

  ASSERT_TRUE(fake_.Render(&frame).ok());
  ASSERT_TRUE(desk().OverlayPointerDown(400, 250));
  EXPECT_TRUE(desk().commands().IsEnabled(tools["Delete Selected"]));
  desk().OverlayPointerDrag(410, 230);
  desk().OverlayPointerDrag(420, 200);
  desk().OverlayPointerUp(420, 200);
  const std::string dragged = Saved(*o);
  EXPECT_NE(dragged, drawn);
  ASSERT_TRUE(desk().commands().IsEnabled("edit.undo"));
  ASSERT_TRUE(fake_.Run({"edit.undo"}).ok());
  EXPECT_EQ(Saved(*o), drawn);  // the whole drag was one edit
  ASSERT_TRUE(fake_.Run({"edit.redo"}).ok());
  EXPECT_EQ(Saved(*o), dragged);

  // A second measurement keeps the first.
  ASSERT_TRUE(desk().Execute(tools["Line"]).ok());
  Click(200, 100);
  Click(300, 120);
  EXPECT_FALSE(desk().commands().IsChecked(tools["Line"]));  // a line ends at its second point
  const std::string two = Saved(*o);
  EXPECT_NE(two.find("\"multi_point\""), std::string::npos) << two;
  EXPECT_NE(two.find("\"range_bearing\""), std::string::npos) << two;

  UseTestDted();
  ASSERT_TRUE(desk().Execute(tools["Viewshed"]).ok());
  ASSERT_TRUE(fake_.Render(&frame).ok());
  Click(250, 420);
  EXPECT_FALSE(desk().commands().IsChecked(tools["Viewshed"]));  // one observer per press
  const std::string status = SettledStatusAt(250, 420);
  EXPECT_NE(status.find("Observer"), std::string::npos) << status;
  EXPECT_EQ(status.find("("), std::string::npos) << status;  // no error
  EXPECT_TRUE(desk().FlushOverlayRedraw());  // the finished viewshed asked for a repaint

  ASSERT_TRUE(fake_.Run({mode_}).ok());  // leave the mode
  EXPECT_FALSE(desk().OverlayPointerDown(400, 250));
}

class PythonEditorTest : public PythonPluginTest {
 protected:
  void SetUp() override {
    PythonPluginTest::SetUp();
    if (IsSkipped() || HasFatalFailure()) return;
    auto& mv = desk().map_view();
    mv.SetViewport(mv.View().WithCamera(fv::GeoPoint{32.7765, -79.9311}, 100000, 0));
  }

  const std::string mode_ = std::string("editor.mode.") + kPyType;
};

TEST_F(PythonEditorTest, TheModeButtonAndPaletteAreOnTheToolbar) {
  const fv::app::OverlayTypeDesc* type = desk().types().Find(kPyType);
  ASSERT_NE(type, nullptr);
  EXPECT_TRUE(type->editor_factory);
  EXPECT_NE(type->icon.find("rings.svg"), std::string::npos) << type->icon;
  EXPECT_TRUE(OnToolbar(desk(), mode_));

  ASSERT_TRUE(fake_.Run({mode_}).ok());
  ASSERT_TRUE(desk().Editing());
  EXPECT_TRUE(desk().commands().IsChecked(mode_));
  ASSERT_NE(desk().overlays().FirstOfType(kPyType), nullptr);  // the mode made one
  std::map<std::string, std::string> tools = EditorTools(desk());
  ASSERT_EQ(tools.count("Show Labels"), 1u);
  ASSERT_EQ(tools.count("Reset Centre"), 1u);
  EXPECT_TRUE(OnToolbar(desk(), tools["Show Labels"]));
  EXPECT_TRUE(OnToolbar(desk(), tools["Reset Centre"]));
  EXPECT_TRUE(desk().commands().IsEnabled(tools["Show Labels"]));
  EXPECT_TRUE(desk().commands().IsChecked(tools["Show Labels"]));
  EXPECT_FALSE(desk().commands().IsEnabled(tools["Reset Centre"]));  // already at home
}

TEST_F(PythonEditorTest, ClicksMoveTheCentreOnlyWhileEditing) {
  ASSERT_TRUE(fake_.Run({mode_}).ok());
  std::shared_ptr<fv::Overlay> rings = desk().overlays().FirstOfType(kPyType);
  ASSERT_NE(rings, nullptr);
  fv::PixelBuffer before;
  ASSERT_TRUE(fake_.Render(&before).ok());  // the overlay maps clicks through its last frame

  ASSERT_TRUE(desk().OverlayPointerDown(120, 90));
  desk().OverlayPointerUp(120, 90);
  EXPECT_TRUE(rings->AsPersistence()->is_dirty());
  fv::PixelBuffer after;
  ASSERT_TRUE(fake_.Render(&after).ok());
  EXPECT_GT(DifferentPixels(before, after), 500);

  std::map<std::string, std::string> tools = EditorTools(desk());
  ASSERT_TRUE(desk().commands().IsEnabled(tools["Reset Centre"]));
  ASSERT_TRUE(desk().Execute(tools["Reset Centre"]).ok());
  EXPECT_FALSE(desk().commands().IsEnabled(tools["Reset Centre"]));
  EXPECT_TRUE(desk().FlushOverlayRedraw());  // the tool asked through Overlay.host
  fv::PixelBuffer reset;
  ASSERT_TRUE(fake_.Render(&reset).ok());
  EXPECT_EQ(DifferentPixels(before, reset), 0);

  ASSERT_TRUE(desk().Execute(tools["Show Labels"]).ok());
  EXPECT_FALSE(desk().commands().IsChecked(tools["Show Labels"]));

  ASSERT_TRUE(fake_.Run({mode_}).ok());  // leave the mode
  EXPECT_FALSE(desk().Editing());
  EXPECT_FALSE(desk().OverlayPointerDown(120, 90));
}

TEST_F(PythonEditorTest, EditUndoStepsThroughTheCentreHistory) {
  ASSERT_TRUE(fake_.Run({mode_}).ok());
  fv::PixelBuffer home, moved, now;
  ASSERT_TRUE(fake_.Render(&home).ok());
  auto& commands = desk().commands();
  EXPECT_FALSE(commands.IsEnabled("edit.undo"));

  ASSERT_TRUE(desk().OverlayPointerDown(120, 90));
  desk().OverlayPointerUp(120, 90);
  ASSERT_TRUE(fake_.Render(&moved).ok());
  std::map<std::string, std::string> tools = EditorTools(desk());
  ASSERT_TRUE(desk().Execute(tools["Reset Centre"]).ok());
  EXPECT_FALSE(desk().undo().CanUndo());  // the history is the overlay's own

  ASSERT_TRUE(commands.IsEnabled("edit.undo"));
  ASSERT_TRUE(fake_.Run({"edit.undo"}).ok());
  ASSERT_TRUE(fake_.Render(&now).ok());
  EXPECT_EQ(DifferentPixels(moved, now), 0);
  ASSERT_TRUE(fake_.Run({"edit.undo"}).ok());
  ASSERT_TRUE(fake_.Render(&now).ok());
  EXPECT_EQ(DifferentPixels(home, now), 0);
  EXPECT_FALSE(commands.IsEnabled("edit.undo"));

  ASSERT_TRUE(commands.IsEnabled("edit.redo"));
  ASSERT_TRUE(fake_.Run({"edit.redo"}).ok());
  ASSERT_TRUE(fake_.Render(&now).ok());
  EXPECT_EQ(DifferentPixels(moved, now), 0);

  ASSERT_TRUE(fake_.Run({mode_}).ok());  // leave the mode: Edit ▸ Undo no longer reaches it
  EXPECT_FALSE(commands.IsEnabled("edit.undo"));
  EXPECT_FALSE(commands.IsEnabled("edit.redo"));
}

TEST_F(PythonEditorTest, ToolbarIconsAreThePackagesOwnArt) {
  // The mode button and a tool naming "resources/labels.svg" both reach the
  // toolbar as absolute paths into the installed package, for each kind.
  for (const std::string type : {std::string(kPyType), std::string(kCType)}) {
    const std::string mode = "editor.mode." + type;
    ASSERT_TRUE(fake_.Run({mode}).ok()) << type;
    std::string mode_icon, labels_icon;
    for (const fv::desk::MenuItem& item : desk().menus().Toolbar()) {
      if (item.command_id == mode) mode_icon = item.icon;
      if (item.label == "Show Labels") labels_icon = item.icon;
    }
    for (const std::string& icon : {mode_icon, labels_icon}) {
      EXPECT_TRUE(std::filesystem::path(icon).is_absolute()) << type << ": " << icon;
      EXPECT_TRUE(std::filesystem::exists(icon)) << type << ": " << icon;
    }
    EXPECT_EQ(std::filesystem::path(mode_icon).filename(), "rings.svg") << type;
    EXPECT_EQ(std::filesystem::path(labels_icon).filename(), "labels.svg") << type;
    EXPECT_EQ(std::filesystem::path(mode_icon).parent_path(),
              std::filesystem::path(labels_icon).parent_path()) << type;
    // A symbolic name is left for the shell to map.
    const std::map<std::string, std::string> tools = EditorTools(desk());
    ASSERT_EQ(tools.count("Reset Centre"), 1u);
    EXPECT_EQ(desk().commands().Find(tools.at("Reset Centre"))->icon, "recenter") << type;
    ASSERT_TRUE(fake_.Run({mode}).ok()) << type;  // leave the mode
  }
}

TEST_F(PythonEditorTest, HoverShowsTheEditHintOnlyWhileEditing) {
  ASSERT_TRUE(fake_.Run({mode_}).ok());
  desk().OverlayHover(30, 40);
  EXPECT_EQ(desk().CurrentStatus().feedback, "Move the centre here");
  ASSERT_TRUE(fake_.Run({mode_}).ok());  // leave the mode
  desk().OverlayHover(31, 40);
  EXPECT_EQ(desk().CurrentStatus().feedback, "");
}

TEST_F(PythonEditorTest, GetInfoOnTheContextMenuShowsTheRingSheet) {
  ASSERT_TRUE(fake_.Run({mode_}).ok());
  ASSERT_TRUE(fake_.Run({mode_}).ok());  // the rings stay open, not edited
  ASSERT_FALSE(desk().Editing());
  ASSERT_NE(desk().overlays().FirstOfType(kPyType), nullptr);
  fv::PixelBuffer frame;
  ASSERT_TRUE(fake_.Render(&frame).ok());

  double x = 0, y = 0;
  desk().map_view().View().Projection().GeoToSurface(fv::GeoPoint{32.7765, -79.9311}, &x, &y);
  const fv::app::MenuNode menu = desk().MapContextMenu(x, y);
  const fv::app::MenuNode* get = nullptr;
  bool has_edit = false;
  for (const fv::app::MenuNode& item : menu.children) {
    if (item.label.rfind("Get Info", 0) == 0) get = &item;
    if (item.label == "Edit Rings\xE2\x80\xA6") has_edit = true;
  }
  ASSERT_NE(get, nullptr);
  EXPECT_TRUE(has_edit);
  get->action();
  EXPECT_EQ(desk().item_info().title, "Range Rings");
  ASSERT_EQ(desk().item_info().rows.size(), 4u);  // the centre: no ring section
  EXPECT_EQ(desk().item_info().rows[0].section, "Centre");
  EXPECT_EQ(desk().item_info().rows[3].value, "1 nm");
}

// MARK: Host services

constexpr char kHostModule[] = R"(import threading
import pyfvw

class Thing(pyfvw.overlay.Overlay):
    def __init__(self):
        super().__init__("Host")

    def properties_schema(self):
        return [{"key": "worker_redraws", "label": "Redraws", "type": "int", "default": 0},
                {"key": "has_host", "label": "Host", "type": "bool", "default": False},
                {"key": "elevation_none", "label": "None", "type": "bool", "default": False}]

    def read_property(self, key):
        if key == "has_host":
            return self.host is not None
        if key == "elevation_none":
            return self.host.elevation_at(32.7765, -79.9311) is None
        return 0

    def write_property(self, key, value):
        host = self.host
        def work():
            for _ in range(value):
                host.redraw()
        t = threading.Thread(target=work)
        t.start()
        t.join()

    def on_mouse_down(self, e):
        self.host.capture_mouse()
        return True

    def on_mouse_move(self, e):
        return True

    def on_mouse_up(self, e):
        self.host.release_mouse()
        return True
)";

/// A linked python package whose overlay calls `self.host`.
class PythonHostTest : public PythonPluginTest {
 protected:
  void SetUp() override {
    PythonPluginTest::SetUp();
    if (IsSkipped() || HasFatalFailure()) return;
    const fs::path dir = fs::path(scratch_.Path("linked")) / "com.example.pyhost";
    fs::create_directories(dir / "python");
    std::ofstream(dir / "python" / "pyhost.py") << kHostModule;
    std::ofstream(dir / "peregrine-package.json") << R"({
  "format": 1, "id": "com.example.pyhost", "name": "Host", "version": "1.0",
  "requires": { "python": "3.10" }, "python_path": "python",
  "overlays": [ { "id": "com.example.pyhost.o", "display_name": "Host",
                  "implementation": { "python": "pyhost:Thing" } } ]
})";
    fv::desk::InstallResult r;
    desk().LinkPackage(dir.string(), &r);
    const fv::desk::PackageInfo* p = desk().packages()->Find("com.example.pyhost");
    ASSERT_NE(p, nullptr);
    ASSERT_EQ(p->state, PackageState::kLoaded) << p->reason;
    host_ = desk().types().Find("com.example.pyhost.o")->factory();
    ASSERT_NE(host_, nullptr);
    ASSERT_NE(host_->AsHostAware(), nullptr);
  }

  fv::app::PropertyValue Get(const std::string& key) {
    fv::app::PropertyValue v;
    const fv::Status s = host_->AsProperties()->GetProperty(key, &v);
    EXPECT_TRUE(s.ok()) << s.message;
    return v;
  }

  std::shared_ptr<fv::Overlay> host_;
};

TEST_F(PythonHostTest, HostIsNoneOutsideTheStack) {
  EXPECT_FALSE(Get("has_host").b);
  ASSERT_TRUE(desk().overlays().Add(host_).ok());
  EXPECT_TRUE(Get("has_host").b);
  ASSERT_TRUE(desk().overlays().Remove(host_).ok());
  EXPECT_FALSE(Get("has_host").b);
}

TEST_F(PythonHostTest, RedrawsFromAWorkerThread) {
  ASSERT_TRUE(desk().overlays().Add(host_).ok());
  const int before = fake_.shell().invalidate_calls;
  fv::app::PropertyValue n;
  n.type = fv::app::PropertyType::kInt;
  n.i = 3;
  const fv::Status s = host_->AsProperties()->SetProperty("worker_redraws", n);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_TRUE(desk().FlushOverlayRedraw());
  EXPECT_EQ(fake_.shell().invalidate_calls, before + 1);
  EXPECT_FALSE(desk().FlushOverlayRedraw());
}

TEST_F(PythonHostTest, CapturesAndReleasesTheMouse) {
  ASSERT_TRUE(desk().overlays().Add(host_).ok());
  EXPECT_FALSE(desk().OverlayPointerDrag(20, 20));  // no editor, no capture
  ASSERT_TRUE(host_->OnMouseDown(fv::MouseEvent{}));
  EXPECT_EQ(desk().overlays().mouse_capture(), host_.get());
  EXPECT_TRUE(desk().OverlayPointerDrag(20, 20));
  EXPECT_TRUE(desk().OverlayPointerUp(20, 20));
  EXPECT_EQ(desk().overlays().mouse_capture(), nullptr);
}

TEST_F(PythonHostTest, ElevationIsNoneWithoutACatalog) {
  ASSERT_TRUE(desk().overlays().Add(host_).ok());
  EXPECT_TRUE(Get("elevation_none").b);
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

TEST_F(PythonRefusalTest, AnEditorThatIsNotAnOverlayEditorFails) {
  // Its own module name: Python keeps the first import of a module for the process.
  const fs::path dir = fs::path(scratch_.Path("linked")) / "com.example.pynoeditor";
  fs::create_directories(dir / "python");
  std::ofstream(dir / "python" / "pynoeditor.py")
      << "import pyfvw\nclass Thing(pyfvw.overlay.Overlay):\n    pass\nclass Ed:\n    pass\n";
  std::ofstream(dir / "peregrine-package.json") << R"({
  "format": 1, "id": "com.example.pynoeditor", "name": "No Editor", "version": "1.0",
  "requires": { "python": "3.10" }, "python_path": "python",
  "overlays": [ { "id": "com.example.pynoeditor.o", "display_name": "No Editor",
                  "implementation": { "python": "pynoeditor:Thing" },
                  "editor": { "python": "pynoeditor:Ed" } } ]
})";
  fv::desk::InstallResult r;
  desk().LinkPackage(dir.string(), &r);
  const fv::desk::PackageInfo* p = desk().packages()->Find("com.example.pynoeditor");
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->state, PackageState::kFailed);
  EXPECT_NE(p->reason.find("is not a pyfvw.app.OverlayEditor subclass"), std::string::npos) << p->reason;
}

}  // namespace
