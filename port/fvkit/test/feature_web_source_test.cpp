// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// The feature-source registry, the web feature source over recorded
/// responses, and the feature overlay opening a source spec
/// (port/web-features-plan.md WF5).

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "fvkit/app/source_spec.h"
#include "fvkit/feature/feature_source.h"
#include "fvkit/feature/style.h"
#include "fvkit/feature/web/remote_layer.h"
#include "fvkit/feature/web/web_source.h"
#include "fvkit/overlay/feature_overlay.h"
#include "web_fake_http.h"

namespace fv {
namespace {

namespace fs = std::filesystem;

std::string Fixture(const std::string& name) {
  std::ifstream in(std::string(FV_WEB_FIXTURES) + "/" + name, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  EXPECT_FALSE(ss.str().empty()) << name;
  return ss.str();
}

const std::string kNws =
    "https://mapservices.weather.noaa.gov/eventdriven/rest/services/WWA/watch_warn_adv/FeatureServer";

/// A source answering "test://" specs with a two-feature memory layer, and
/// counting its opens.
struct TestSource {
  int opens = 0;
  explicit TestSource(const std::string& name = "test") : name_(name) {
    FeatureSourceDesc d;
    d.name = name;
    d.matches = [](const std::string& spec) { return spec.rfind("test://", 0) == 0; };
    d.open = [this](const std::string& spec, FeatureSourceResult* out) {
      ++opens;
      FeatureSchema schema;
      FieldDef kind;
      kind.name = "kind";
      schema.fields.push_back(kind);
      auto layer = std::make_shared<MemoryFeatureLayer>(app::SplitSourceSpec(spec).layer, schema);
      for (const char* k : {"buoy", "light"}) {
        Feature f;
        f.geometry = Geometry::Point(GeoPoint{32.8, -79.9});
        f.values.push_back(std::string(k));
        layer->Insert(&f);
      }
      out->layer = layer;
      FeatureStyle style;
      style.label.field = "kind";
      WriteFeatureStyleJson(style, &out->style_json);
      return Status::Ok();
    };
    EXPECT_TRUE(RegisterFeatureSource(d).ok());
  }
  ~TestSource() { UnregisterFeatureSource(name_); }

 private:
  std::string name_;
};

TEST(FeatureSource, TheRegistryRoutesSpecsToTheSourceClaimingThem) {
  EXPECT_FALSE(IsFeatureSourceSpec("test://a|layername=marks"));
  FeatureSourceResult r;
  EXPECT_EQ(OpenFeatureSource("test://a", &r).code, kNotFound);
  {
    TestSource source;
    EXPECT_TRUE(IsFeatureSourceSpec("test://a|layername=marks"));
    EXPECT_FALSE(IsFeatureSourceSpec("/tmp/marks.gpkg"));
    ASSERT_TRUE(OpenFeatureSource("test://a|layername=marks", &r).ok());
    EXPECT_EQ(r.layer->Name(), "marks");
    EXPECT_EQ(r.layer->Count(), 2u);
    FeatureSourceDesc again;
    again.name = "test";
    again.matches = [](const std::string&) { return false; };
    again.open = [](const std::string&, FeatureSourceResult*) { return Status::Ok(); };
    EXPECT_EQ(RegisterFeatureSource(again).code, kInvalidArg);
    EXPECT_EQ(RegisterFeatureSource(FeatureSourceDesc()).code, kInvalidArg);
  }
  EXPECT_FALSE(IsFeatureSourceSpec("test://a"));
}

TEST(FeatureSource, AReadOnlyViewRefusesEveryEdit) {
  auto inner = std::make_shared<MemoryFeatureLayer>("m", FeatureSchema());
  ReadOnlyFeatureLayer view(inner, "no edits here");
  Feature f;
  f.geometry = Geometry::Point(GeoPoint{1, 2});
  Status s = view.Insert(&f);
  EXPECT_EQ(s.code, kUnsupported);
  EXPECT_EQ(s.message, "no edits here");
  EXPECT_EQ(view.Delete(1).code, kUnsupported);
  EXPECT_EQ(inner->Insert(&f).code, kOk);
  EXPECT_EQ(view.Count(), 1u);
}

TEST(WebSource, TheServiceKindFollowsTheUrl) {
  EXPECT_TRUE(web::IsWebFeatureSpec("https://example.test/api|layername=x"));
  EXPECT_TRUE(web::IsWebFeatureSpec("HTTP://example.test/"));
  EXPECT_FALSE(web::IsWebFeatureSpec("/data/https.gpkg"));
  EXPECT_EQ(web::WebServiceKindOf(kNws), web::WebServiceKind::kEsri);
  EXPECT_EQ(web::WebServiceKindOf(kNws + "/?f=pjson"), web::WebServiceKind::kEsri);
  EXPECT_EQ(web::WebServiceKindOf("https://x.test/arcgis/rest/services/Roads/MapServer"),
            web::WebServiceKind::kEsri);
  EXPECT_EQ(web::WebServiceKindOf("https://api.weather.gc.ca/"), web::WebServiceKind::kOgcApi);
}

TEST(WebSource, AnEsriLayerOpensCachedWithItsServiceStyle) {
  auto http = std::make_shared<web::FakeHttp>();
  http->Route(kNws + "?f=json", Fixture("esri/nws/service.json"));
  http->Route(kNws + "/1?f=json", Fixture("esri/nws/layer1.json"));
  const fs::path dir = fs::temp_directory_path() / "fv_web_source_AnEsriLayerOpens";
  fs::remove_all(dir);
  {
    web::WebSourceOptions options;
    options.cache_dir = dir.string();
    FeatureSourceDesc source = web::MakeWebFeatureSource(http, options);
    FeatureSourceResult r;
    ASSERT_TRUE(source.open(app::JoinSourceSpec(kNws, "1"), &r).ok());
    auto* remote = dynamic_cast<web::RemoteFeatureLayer*>(r.layer.get());
    ASSERT_NE(remote, nullptr);
    EXPECT_EQ(remote->Name(), "WatchesWarnings");
    EXPECT_TRUE(fs::exists(web::WebCachePath(dir.string(), kNws)));
    FeatureStyle style;
    ASSERT_TRUE(ReadFeatureStyleJson(r.style_json, &style).ok());
    EXPECT_EQ(style.renderer, FeatureRenderer::kCategorised);
    EXPECT_EQ(style.categories.size(), 111u);

    // A second layer of the same service reads the service document once.
    FeatureSourceResult again;
    ASSERT_TRUE(source.open(kNws + "|layername=1", &again).ok());
    const std::vector<std::string> asked = http->requests();
    EXPECT_EQ(std::count(asked.begin(), asked.end(), kNws + "?f=json"), 1);
    // No layer named: the first the service lists.
    FeatureSourceResult first;
    EXPECT_EQ(source.open(kNws, &first).code, kNotFound);  // layer 0 is not recorded
    EXPECT_NE(std::find(asked.begin(), asked.end(), kNws + "/1?f=json"), asked.end());
  }
  fs::remove_all(dir);
}

TEST(WebSource, ListingAServiceGivesASpecPerLayerAndSharesTheServiceWithOpen) {
  auto http = std::make_shared<web::FakeHttp>();
  http->Route(kNws + "?f=json", Fixture("esri/nws/service.json"));
  http->Route(kNws + "/1?f=json", Fixture("esri/nws/layer1.json"));
  const fs::path dir = fs::temp_directory_path() / "fv_web_source_ListingAService";
  fs::remove_all(dir);
  {
    web::WebSourceOptions options;
    options.cache_dir = dir.string();
    FeatureSourceDesc source = web::MakeWebFeatureSource(http, options);
    ASSERT_TRUE(source.list);
    std::vector<FeatureSourceLayer> layers;
    ASSERT_TRUE(source.list(kNws + "/", &layers, nullptr).ok());
    ASSERT_EQ(layers.size(), 2u);
    EXPECT_EQ(layers[1].spec, app::JoinSourceSpec(kNws, "1"));
    EXPECT_EQ(layers[1].title, "WatchesWarnings");
    EXPECT_EQ(layers[1].geometry_type, GeometryType::kMultiPolygon);
    FeatureSourceResult r;
    ASSERT_TRUE(source.open(layers[1].spec, &r).ok());
    const std::vector<std::string> asked = http->requests();
    EXPECT_EQ(std::count(asked.begin(), asked.end(), kNws + "?f=json"), 1);
  }
  fs::remove_all(dir);
}

TEST(WebSource, AFeatureOverlayOpensASourceSpecReadOnly) {
  TestSource source;
  FeatureOverlay overlay;
  const std::string spec = "test://harbour|layername=marks";
  ASSERT_TRUE(overlay.FileOpen(spec).ok());
  EXPECT_TRUE(overlay.from_source());
  EXPECT_TRUE(overlay.is_read_only());
  EXPECT_EQ(overlay.Name(), "marks");
  EXPECT_EQ(overlay.layer()->Count(), 2u);
  EXPECT_EQ(overlay.style().label.field, "kind");  // the source's style
  EXPECT_EQ(overlay.AsEditTarget(), nullptr);
  EXPECT_EQ(overlay.EditHintAt(PixelPoint{0, 0}), FeatureOverlay::kReadOnlySourceHint);
  Feature f;
  f.geometry = Geometry::Point(GeoPoint{1, 2});
  EXPECT_EQ(overlay.layer()->Insert(&f).code, kUnsupported);
  EXPECT_EQ(overlay.FileSaveAs(spec, FeatureOverlay::kFormatGeoPackage).code, kUnsupported);

  // The style travels as a configuration property.
  app::Properties* props = overlay.ConfigurationProperties();
  ASSERT_NE(props, nullptr);
  FeatureStyle changed = overlay.style();
  changed.label.field.clear();
  changed.symbol.marker_size_px = 20;
  overlay.SetStyle(changed);
  const std::string saved = props->GetString("style");
  EXPECT_EQ(saved.find('\n'), std::string::npos);

  FeatureOverlay reopened;
  ASSERT_TRUE(reopened.FileOpen(spec).ok());
  EXPECT_EQ(reopened.style().label.field, "kind");
  ASSERT_TRUE(reopened.ConfigurationProperties()->SetProperty("style", app::PropertyValue::String(saved)).ok());
  EXPECT_EQ(reopened.style(), changed);

  // Saved as a GeoPackage it is an ordinary, editable file layer.
  const fs::path dir = fs::temp_directory_path() / "fv_web_source_ReadOnly";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const std::string gpkg = (dir / "marks.gpkg").string();
  ASSERT_TRUE(overlay.FileSaveAs(gpkg, FeatureOverlay::kFormatGeoPackage).ok());
  EXPECT_FALSE(overlay.from_source());
  EXPECT_NE(overlay.AsEditTarget(), nullptr);
  EXPECT_EQ(overlay.ConfigurationProperties(), nullptr);
  EXPECT_EQ(overlay.layer()->Count(), 2u);
  fs::remove_all(dir);
}

}  // namespace
}  // namespace fv
