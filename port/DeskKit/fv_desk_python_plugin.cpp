// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_python_plugin.cpp — finds and opens libperegrine_python.

#include "fv_desk_python_plugin.h"

#include <filesystem>
#include <map>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "fvkit/log.h"

namespace fv {
namespace desk {
namespace {

namespace fs = std::filesystem;

struct BridgeTable {
  std::mutex mutex;
  std::string path;
  std::string python_home;
  PythonBridge* bridge = nullptr;           ///< the one that loaded; never freed
  std::map<std::string, Status> failed;     ///< path → why it did not
};

BridgeTable& Table() {
  static BridgeTable* t = new BridgeTable;
  return *t;
}

#if defined(_WIN32)
constexpr char kBridgeFile[] = "peregrine_python.dll";
#elif defined(__APPLE__)
constexpr char kBridgeFile[] = "libperegrine_python.dylib";
#else
constexpr char kBridgeFile[] = "libperegrine_python.so";
#endif

/// Opens `path` and starts Python through its entry point.
Status OpenBridge(const std::string& path, const std::string& python_home, PythonBridge** out) {
  std::error_code ec;
  if (!fs::exists(path, ec))
    return Status::Error(kNotFound, "Python support is not installed (" + path + ")");
  PythonBridgeOpen open = nullptr;
#if defined(_WIN32)
  const std::wstring wide = fs::path(path).wstring();
  HMODULE h = LoadLibraryExW(wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (h == nullptr)
    return Status::Error(kIoError, "Python 3 not found: cannot load " + path + " (error " +
                                       std::to_string(GetLastError()) + ")");
  open = reinterpret_cast<PythonBridgeOpen>(GetProcAddress(h, "fv_python_bridge_open"));
#else
  // RTLD_GLOBAL: extension modules imported by package code resolve the
  // Python C API from the libpython this pulls in.
  void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
  if (h == nullptr) {
    const char* e = dlerror();
    return Status::Error(kIoError, std::string("Python 3 not found: ") +
                                       (e != nullptr ? e : "cannot load " + path));
  }
  open = reinterpret_cast<PythonBridgeOpen>(dlsym(h, "fv_python_bridge_open"));
#endif
  if (open == nullptr) return Status::Error(kIoError, path + " lacks fv_python_bridge_open");
  std::string error;
  PythonBridge* bridge = open(python_home.c_str(), &error);
  if (bridge == nullptr)
    return Status::Error(kInternal, "Python did not start: " + (error.empty() ? "unknown" : error));
  *out = bridge;
  return Status::Ok();
}

}  // namespace

void SetPythonBridgeLocation(const std::string& path, const std::string& python_home) {
  BridgeTable& t = Table();
  std::lock_guard<std::mutex> lock(t.mutex);
  t.path = path;
  t.python_home = python_home;
}

std::string DefaultPythonBridgePath() {
#if defined(_WIN32)
  HMODULE self = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&DefaultPythonBridgePath), &self))
    return std::string();
  wchar_t buf[MAX_PATH];
  const DWORD n = GetModuleFileNameW(self, buf, MAX_PATH);
  if (n == 0 || n == MAX_PATH) return std::string();
  return (fs::path(buf).parent_path() / kBridgeFile).string();
#else
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(&DefaultPythonBridgePath), &info) == 0 ||
      info.dli_fname == nullptr)
    return std::string();
  std::error_code ec;
  const fs::path self = fs::weakly_canonical(info.dli_fname, ec);
  return ((ec ? fs::path(info.dli_fname) : self).parent_path() / kBridgeFile).string();
#endif
}

Status LoadPythonBridge(PythonBridge** out) {
  BridgeTable& t = Table();
  std::lock_guard<std::mutex> lock(t.mutex);
  if (t.bridge != nullptr) {
    *out = t.bridge;
    return Status::Ok();
  }
  const std::string path = t.path.empty() ? DefaultPythonBridgePath() : t.path;
  if (path.empty()) return Status::Error(kNotFound, "Python support is not installed");
  auto f = t.failed.find(path);
  if (f != t.failed.end()) return f->second;
  PythonBridge* bridge = nullptr;
  const Status s = OpenBridge(path, t.python_home, &bridge);
  if (!s.ok()) {
    t.failed[path] = s;
    FV_LOG_WARNING("python: " << s.message);
    return s;
  }
  t.bridge = bridge;
  FV_LOG_INFO("python: Python " << bridge->Version() << " from " << path);
  *out = bridge;
  return Status::Ok();
}

}  // namespace desk
}  // namespace fv
