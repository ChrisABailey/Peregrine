// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// The peregrine.web package (port/web-features-plan.md WF5): the libcurl
/// client against a local HTTP server, and the staged package opening an OGC
/// API collection served by it, end to end.

#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "curl_http.h"
#include "fv_desk_fake.h"
#include "fv_desk_packages.h"
#include "fvkit/feature/feature_source.h"
#include "fvkit/feature/web/remote_layer.h"
#include "fvkit/overlay/feature_overlay.h"
#include "scratch.h"

namespace {

using fv::web::HttpRequest;
using fv::web::HttpResponse;

/// A one-thread HTTP/1.0 server on 127.0.0.1. Each answer is chosen by the
/// request path (query string ignored); every request's text is kept.
class LocalServer {
 public:
  struct Answer {
    int status = 200;
    std::string body;
    std::string extra_headers;  ///< each line ending "\r\n"
    int delay_ms = 0;
  };

  LocalServer() {
    listener_ = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    sockaddr_in at{};
    at.sin_family = AF_INET;
    at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(listener_, reinterpret_cast<sockaddr*>(&at), sizeof at);
    socklen_t len = sizeof at;
    getsockname(listener_, reinterpret_cast<sockaddr*>(&at), &len);
    port_ = ntohs(at.sin_port);
    listen(listener_, 16);
    thread_ = std::thread([this] { Serve(); });
  }
  ~LocalServer() {
    stop_ = true;
    shutdown(listener_, SHUT_RDWR);
    close(listener_);
    thread_.join();
  }

  std::string Url(const std::string& path) const {
    return "http://127.0.0.1:" + std::to_string(port_) + path;
  }
  void Route(const std::string& path, Answer a) {
    std::lock_guard<std::mutex> lock(mu_);
    routes_[path] = std::move(a);
  }
  std::vector<std::string> requests() {
    std::lock_guard<std::mutex> lock(mu_);
    return requests_;
  }

 private:
  void Serve() {
    while (!stop_) {
      const int c = accept(listener_, nullptr, nullptr);
      if (c < 0) return;
      std::string request;
      char buf[4096];
      while (request.find("\r\n\r\n") == std::string::npos) {
        const ssize_t n = recv(c, buf, sizeof buf, 0);
        if (n <= 0) break;
        request.append(buf, static_cast<size_t>(n));
      }
      const size_t sp = request.find(' ');
      std::string path = sp == std::string::npos ? "" : request.substr(sp + 1, request.find(' ', sp + 1) - sp - 1);
      path = path.substr(0, path.find('?'));
      Answer a;
      {
        std::lock_guard<std::mutex> lock(mu_);
        requests_.push_back(request);
        auto it = routes_.find(path);
        if (it == routes_.end()) {
          a.status = 404;
          a.body = "{}";
        } else {
          a = it->second;
        }
      }
      if (a.delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(a.delay_ms));
      const std::string response = "HTTP/1.0 " + std::to_string(a.status) + " X\r\nContent-Length: " +
                                   std::to_string(a.body.size()) + "\r\n" + a.extra_headers + "\r\n" + a.body;
      send(c, response.data(), response.size(), 0);
      close(c);
    }
  }

  int listener_ = -1;
  int port_ = 0;
  std::atomic<bool> stop_{false};
  std::thread thread_;
  std::mutex mu_;
  std::map<std::string, Answer> routes_;
  std::vector<std::string> requests_;
};

TEST(CurlHttpClient, FetchesFollowsRedirectsAndReportsStatus) {
  LocalServer server;
  server.Route("/hello", {200, R"({"ok":true})", "Content-Type: application/json\r\n"});
  server.Route("/moved", {302, "", "Location: /hello\r\n"});
  peregrine_web::CurlHttpClient http("Peregrine/test");

  HttpRequest request;
  request.url = server.Url("/moved?x=1");
  request.headers.emplace_back("Accept", "application/json");
  HttpResponse response;
  ASSERT_TRUE(http.Get(request, nullptr, &response).ok());
  EXPECT_EQ(response.status, 200);
  EXPECT_EQ(response.body, R"({"ok":true})");
  bool typed = false;
  for (const auto& h : response.headers) typed = typed || (h.first == "Content-Type" && h.second == "application/json");
  EXPECT_TRUE(typed);
  const std::vector<std::string> seen = server.requests();
  ASSERT_EQ(seen.size(), 2u);
  EXPECT_NE(seen[1].find("User-Agent: Peregrine/test"), std::string::npos);
  EXPECT_NE(seen[1].find("Accept: application/json"), std::string::npos);

  request.url = server.Url("/nothing");
  ASSERT_TRUE(http.Get(request, nullptr, &response).ok());  // any status is a transfer
  EXPECT_EQ(response.status, 404);
}

TEST(CurlHttpClient, RefusesAnOversizedBodyAndStopsWhenCancelled) {
  LocalServer server;
  server.Route("/big", {200, std::string(5000, 'x')});
  server.Route("/slow", {200, "late", "", 2500});
  peregrine_web::CurlHttpClient http("Peregrine/test");

  HttpRequest request;
  request.url = server.Url("/big");
  request.max_body_bytes = 1000;
  HttpResponse response;
  EXPECT_EQ(http.Get(request, nullptr, &response).code, fv::kIoError);

  std::atomic<bool> cancel{false};
  request.url = server.Url("/slow");
  std::thread stopper([&cancel] {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    cancel = true;
  });
  const auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(http.Get(request, &cancel, &response).code, fv::kInterrupted);
  stopper.join();
  // libcurl polls an idle transfer's progress callback about once a second.
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(2000));
}

/// Loads the staged packages with HOME in a scratch directory, so the
/// package's cache lands there.
class WebPackageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    setenv("HOME", scratch_.Path("home").c_str(), 1);
    unsetenv("XDG_CACHE_HOME");
    fake_.desk().LoadPackages(fv::desk::PackageDirs{FV_PACKAGES_DIR, scratch_.Path("user")});
  }
  fv::desk::test::Scratch scratch_;
  fv::desk::FakeDesk fake_{400, 300};
};

TEST_F(WebPackageTest, OpensAnOgcApiCollectionByUrl) {
  const fv::desk::PackageInfo* p = fake_.desk().packages()->Find("peregrine.web");
  ASSERT_NE(p, nullptr);
  ASSERT_EQ(p->state, fv::desk::PackageState::kLoaded) << p->reason;
  EXPECT_TRUE(fv::IsFeatureSourceSpec("https://example.test/arcgis/rest/services/X/FeatureServer"));

  LocalServer server;
  server.Route("/ogc", {200, R"({"title":"Local","links":[
      {"rel":"data","type":"application/json","href":"/ogc/collections"}]})"});
  server.Route("/ogc/collections", {200, R"({"collections":[{"id":"stops","title":"Stops",
      "extent":{"spatial":{"bbox":[[-80.1,32.7,-79.8,32.9]]}}}]})"});
  server.Route("/ogc/collections/stops/items", {200, R"({"type":"FeatureCollection","features":[
      {"type":"Feature","id":1,"geometry":{"type":"Point","coordinates":[-79.93,32.78]},"properties":{"name":"Market"}},
      {"type":"Feature","id":2,"geometry":{"type":"Point","coordinates":[-79.95,32.80]},"properties":{"name":"Citadel"}}]})"});

  const std::string spec = server.Url("/ogc") + "|layername=stops";
  ASSERT_EQ(fake_.desk().session().OpenFile(fv::app::TypeId(), spec), fv::app::FlowResult::kDone)
      << (fake_.shell().errors.empty() ? "" : fake_.shell().errors.back().message);
  auto overlays = fake_.desk().overlays().OfType(fv::FeatureOverlay::kTypeId);
  ASSERT_EQ(overlays.size(), 1u);
  auto* overlay = static_cast<fv::FeatureOverlay*>(overlays[0].get());
  EXPECT_EQ(overlay->Name(), "Stops");
  EXPECT_TRUE(overlay->is_read_only());
  auto* view = dynamic_cast<fv::ReadOnlyFeatureLayer*>(overlay->layer().get());
  ASSERT_NE(view, nullptr);
  auto* remote = dynamic_cast<fv::web::RemoteFeatureLayer*>(view->inner().get());
  ASSERT_NE(remote, nullptr);

  std::vector<fv::Feature> got;
  remote->Query(fv::GeoRect{{32.7, -80.1}, {32.9, -79.8}}, &got);
  ASSERT_TRUE(remote->WaitIdle(10));
  EXPECT_EQ(remote->Count(), 2u);
  EXPECT_TRUE(std::filesystem::exists(scratch_.Path("home") + "/Library/Caches/Peregrine/web-features") ||
              std::filesystem::exists(scratch_.Path("home") + "/.cache/peregrine/web-features"));
  bool named = false;
  for (const std::string& r : server.requests()) named = named || r.find("User-Agent: Peregrine/") != std::string::npos;
  EXPECT_TRUE(named);
}

}  // namespace
