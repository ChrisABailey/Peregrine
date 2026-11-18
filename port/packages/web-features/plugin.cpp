// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// plugin.cpp — registers the peregrine.web package's feature source: web
/// service URLs open as cached feature layers over libcurl.
#include <filesystem>
#include <memory>

#include "curl_http.h"
#include "fv_desk_user_settings.h"
#include "fvkit/feature/web/web_source.h"
#include "peregrine/fv_native.h"

#ifndef PEREGRINE_WEB_VERSION
#define PEREGRINE_WEB_VERSION "0.0.0"
#endif

namespace {

/// Sent on every request; public services ask clients to name themselves.
constexpr char kUserAgent[] = "Peregrine/" PEREGRINE_WEB_VERSION " (github.com/ChrisABailey/Peregrine)";

}  // namespace

FV_NATIVE_PLUGIN_BUILD_ID()

FV_NATIVE_EXPORT int fv_native_register(fv::desk::NativeRegistrar* r) {
  fv::web::WebSourceOptions options;
  options.cache_dir = fv::desk::DefaultUserCacheDir();
  // No home directory: the cache lives as long as the temporary directory does.
  if (options.cache_dir.empty())
    options.cache_dir = (std::filesystem::temp_directory_path() / "peregrine").string();
  auto http = std::make_shared<peregrine_web::CurlHttpClient>(kUserAgent);
  return r->AddFeatureSource("web", fv::web::MakeWebFeatureSource(http, options)).ok() ? 0 : 1;
}
