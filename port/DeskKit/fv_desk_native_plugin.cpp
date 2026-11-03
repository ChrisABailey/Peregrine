// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_native_plugin.h"

#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "fvkit/log.h"
#include "peregrine/fv_sdk_build_id.h"

namespace fv {
namespace desk {

Status NativeRegistrar::AddOverlay(const std::string& name, NativeOverlay impl) {
  if (name.empty() || !impl.factory)
    return Status::Error(kInvalidArg, "a native overlay needs a name and a factory");
  if (!overlays_.emplace(name, std::move(impl)).second)
    return Status::Error(kInvalidArg, "native overlay '" + name + "' is already added");
  return Status::Ok();
}

Status NativeRegistrar::AddTool(const std::string& name, ToolRun run) {
  if (name.empty() || !run) return Status::Error(kInvalidArg, "a native tool needs a name and a run");
  if (!tools_.emplace(name, std::move(run)).second)
    return Status::Error(kInvalidArg, "native tool '" + name + "' is already added");
  return Status::Ok();
}

const NativeOverlay* NativeLibrary::FindOverlay(const std::string& name) const {
  auto it = registry.overlays().find(name);
  return it == registry.overlays().end() ? nullptr : &it->second;
}

const ToolRun* NativeLibrary::FindTool(const std::string& name) const {
  auto it = registry.tools().find(name);
  return it == registry.tools().end() ? nullptr : &it->second;
}

const std::string& NativeHostBuildId() {
  static const std::string id = FV_SDK_BUILD_ID;
  return id;
}

Status ReadNativeBuildId(const std::string& path, std::string* id) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::Error(kIoError, "cannot read " + path);
  const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  static const std::string marker = FV_NATIVE_BUILD_ID_MARKER;
  // A universal binary carries one id per slice; they must agree.
  std::string found;
  for (size_t at = bytes.find(marker); at != std::string::npos; at = bytes.find(marker, at + 1)) {
    const size_t begin = at + marker.size();
    const size_t end = bytes.find('\0', begin);
    if (end == std::string::npos) break;
    const std::string one = bytes.substr(begin, end - begin);
    if (one.empty()) continue;  // the bare marker, as a reader's own constant
    if (!found.empty() && one != found)
      return Status::Error(kInvalidArg, path + " carries two build ids");
    found = one;
  }
  if (found.empty()) return Status::Error(kNotFound, path + " carries no native build id");
  *id = found;
  return Status::Ok();
}

namespace {

using BuildIdFn = const char* (*)();
using RegisterFn = int (*)(NativeRegistrar*);

struct NativeTable {
  std::mutex mutex;
  std::map<std::string, std::unique_ptr<NativeLibrary>> loaded;  ///< path → library
  std::map<std::string, Status> failed;                          ///< path → why
};

NativeTable& Table() {
  static NativeTable* t = new NativeTable;  // never destroyed: plugins outlive static teardown
  return *t;
}

/// Opens `path` and looks up the two entry points; false with `why` set.
bool OpenNative(const std::string& path, BuildIdFn* build_id, RegisterFn* reg, std::string* why) {
#if defined(_WIN32)
  const std::wstring wide = std::filesystem::path(path).wstring();
  HMODULE h = LoadLibraryExW(wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (h == nullptr) {
    *why = "cannot load " + path + " (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  *build_id = reinterpret_cast<BuildIdFn>(GetProcAddress(h, "fv_native_build_id"));
  *reg = reinterpret_cast<RegisterFn>(GetProcAddress(h, "fv_native_register"));
#else
  void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (h == nullptr) {
    const char* e = dlerror();
    *why = e != nullptr ? e : "cannot load " + path;
    return false;
  }
  *build_id = reinterpret_cast<BuildIdFn>(dlsym(h, "fv_native_build_id"));
  *reg = reinterpret_cast<RegisterFn>(dlsym(h, "fv_native_register"));
#endif
  if (*build_id == nullptr || *reg == nullptr) {
    *why = path + " lacks fv_native_build_id or fv_native_register";
    return false;
  }
  return true;
}

}  // namespace

Status LoadNativeLibrary(const std::string& path, const std::string& package_dir,
                         const NativeLibrary** out) {
  NativeTable& t = Table();
  std::lock_guard<std::mutex> lock(t.mutex);
  if (auto it = t.loaded.find(path); it != t.loaded.end()) {
    *out = it->second.get();
    return Status::Ok();
  }
  if (auto it = t.failed.find(path); it != t.failed.end()) return it->second;
  auto fail = [&t, &path](int code, const std::string& why) {
    const Status s = Status::Error(code, why);
    t.failed[path] = s;
    FV_LOG_WARNING("native plugin: " << why);
    return s;
  };

  std::string id;
  const Status read = ReadNativeBuildId(path, &id);
  if (!read.ok()) return fail(read.code == kNotFound ? kUnsupported : read.code, read.message);
  if (id != NativeHostBuildId())
    return fail(kUnsupported, "built for Peregrine " + id + "; this is " + NativeHostBuildId());

  std::string why;
  BuildIdFn build_id = nullptr;
  RegisterFn reg = nullptr;
  if (!OpenNative(path, &build_id, &reg, &why)) return fail(kIoError, why);
  // The file scan above is the gate; this catches a marker that is not the
  // one the entry point answers.
  if (FV_NATIVE_BUILD_ID_MARKER + NativeHostBuildId() != build_id())
    return fail(kUnsupported, path + ": fv_native_build_id disagrees with the file");

  auto lib = std::make_unique<NativeLibrary>(package_dir);
  lib->path = path;
  int rc = 0;
  try {
    rc = reg(&lib->registry);
  } catch (const std::exception& e) {
    return fail(kInternal, path + ": fv_native_register threw: " + e.what());
  } catch (...) {
    return fail(kInternal, path + ": fv_native_register threw");
  }
  if (rc != 0) return fail(kInternal, path + ": fv_native_register returned " + std::to_string(rc));
  *out = lib.get();
  t.loaded[path] = std::move(lib);
  FV_LOG_INFO("native plugin: loaded " << path);
  return Status::Ok();
}

}  // namespace desk
}  // namespace fv
