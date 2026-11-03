// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// peregrine_python.cpp — libperegrine_python, the bridge between Peregrine
/// and an embedded CPython (port/extensions-plan.md §3).
///
/// The library embeds pyfvw, so `import pyfvw` inside Peregrine is the app's
/// own build over the shared core. A python overlay is a
/// pyfvw.overlay.Overlay subclass, whose trampoline is already an
/// fv::Overlay; a python tool is a function called with a
/// pyfvw.app.ToolContext. The interpreter is started once, its GIL released,
/// and every call into Python takes the GIL on whatever thread makes it.

#include <pybind11/embed.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include <memory>
#include <mutex>
#include <string>

#include "fv_desk_python_plugin.h"
#include "fvkit/log.h"
#include "pyfvw_common.h"

namespace py = pybind11;
using namespace pybind11::literals;

namespace {

using fv::Status;

/// What a python tool's `ctx` reaches; valid only while the tool runs.
struct PyToolContext {
  fv::desk::ToolContext* context = nullptr;
  std::string package_dir;

  fv::desk::ToolContext& Live() const {
    if (context == nullptr)
      throw pyfvw::FvErrorCpp{
          Status::Error(fv::kInvalidArg, "a ToolContext is valid only while its tool runs")};
    return *context;
  }
};

/// pyfvw.app.ToolContext, added to the embedded module once Python starts.
void BindToolContext() {
  py::module_ app = py::module_::import("pyfvw").attr("app");
  py::class_<PyToolContext, std::shared_ptr<PyToolContext>>(
      app, "ToolContext",
      "What a package tool may do while it runs. Do not keep it: it is valid "
      "only for the call.")
      .def_readonly("package_dir", &PyToolContext::package_dir,
                    "The installed package's root directory.")
      .def(
          "open_file",
          [](const PyToolContext& c, const std::string& path) {
            pyfvw::ThrowIfError(c.Live().OpenFile(path));
          },
          "path"_a,
          "Opens a file as an overlay of the type that owns its extension. "
          "Raises FvError (UNSUPPORTED, INTERRUPTED or IO_ERROR) when it cannot.")
      .def(
          "request_redraw", [](const PyToolContext& c) { c.Live().RequestRedraw(); },
          "Asks the shell to repaint the map.");
}

/// A Python object shared by C++ copies without touching its refcount; the
/// last copy drops the reference under the GIL.
using SharedPy = std::shared_ptr<py::object>;

SharedPy Share(py::object o) {
  return SharedPy(new py::object(std::move(o)), [](py::object* p) {
    py::gil_scoped_acquire gil;
    delete p;
  });
}

/// "module:attr" → the attribute, importing the module. kInvalidArg for a
/// malformed spec; kNotFound for a missing attribute; kInternal when the
/// import raises. The GIL must be held.
Status Import(const std::string& spec, py::object* out) {
  const size_t colon = spec.find(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 == spec.size())
    return Status::Error(fv::kInvalidArg, "'" + spec + "' is not module:name");
  const std::string module = spec.substr(0, colon);
  const std::string name = spec.substr(colon + 1);
  try {
    py::object o = py::module_::import(module.c_str());
    size_t start = 0;
    while (start <= name.size()) {
      const size_t dot = name.find('.', start);
      const std::string part = name.substr(start, dot == std::string::npos ? dot : dot - start);
      if (!py::hasattr(o, part.c_str()))
        return Status::Error(fv::kNotFound, "module " + module + " has no " + name);
      o = o.attr(part.c_str());
      if (dot == std::string::npos) break;
      start = dot + 1;
    }
    *out = std::move(o);
    return Status::Ok();
  } catch (py::error_already_set& e) {
    return Status::Error(fv::kInternal, "import " + module + ": " + e.what());
  }
}

/// A raised Python exception as a Status, keeping an FvError's code.
Status FromPython(py::error_already_set& e, const std::string& what) {
  int code = fv::kInternal;
  try {
    if (py::hasattr(e.value(), "code")) code = e.value().attr("code").cast<int>();
    else if (e.matches(PyExc_OSError)) code = fv::kIoError;
  } catch (py::error_already_set&) {
    PyErr_Clear();
  } catch (const py::cast_error&) {
  }
  return Status::Error(code, what + ": " + e.what());
}

class Bridge : public fv::desk::PythonBridge {
 public:
  std::string Version() const override {
    py::gil_scoped_acquire gil;
    py::object v = py::module_::import("sys").attr("version_info");
    return std::to_string(v.attr("major").cast<int>()) + "." +
           std::to_string(v.attr("minor").cast<int>());
  }

  Status AddPath(const std::string& dir) override {
    py::gil_scoped_acquire gil;
    try {
      py::list path = py::module_::import("sys").attr("path");
      if (!path.contains(dir)) path.insert(0, dir);
      return Status::Ok();
    } catch (py::error_already_set& e) {
      return FromPython(e, "sys.path");
    }
  }

  Status ResolveOverlay(const std::string& spec, const std::string& display_name,
                        std::function<std::shared_ptr<fv::Overlay>()>* out) override {
    py::gil_scoped_acquire gil;
    py::object cls;
    const Status s = Import(spec, &cls);
    if (!s.ok()) return s;
    py::object base = py::module_::import("pyfvw").attr("overlay").attr("Overlay");
    if (!PyType_Check(cls.ptr()) || PyObject_IsSubclass(cls.ptr(), base.ptr()) != 1) {
      PyErr_Clear();
      return Status::Error(fv::kNotFound, spec + " is not a pyfvw.overlay.Overlay subclass");
    }
    SharedPy held = Share(std::move(cls));
    *out = [held, spec, display_name]() -> std::shared_ptr<fv::Overlay> {
      py::gil_scoped_acquire gil;
      try {
        std::shared_ptr<fv::Overlay> o = pyfvw::OverlayFromPython((*held)());
        // As for the C kind, the manifest's display name names the overlay.
        if (o != nullptr) o->SetName(display_name);
        return o;
      } catch (py::error_already_set& e) {
        FV_LOG_WARNING("python: " << spec << ": " << e.what());
        return nullptr;
      } catch (const py::cast_error& e) {
        FV_LOG_WARNING("python: " << spec << ": " << e.what());
        return nullptr;
      }
    };
    return Status::Ok();
  }

  Status ResolveTool(const std::string& spec, const std::string& package_dir,
                     fv::desk::ToolRun* out) override {
    py::gil_scoped_acquire gil;
    py::object fn;
    const Status s = Import(spec, &fn);
    if (!s.ok()) return s;
    if (!PyCallable_Check(fn.ptr())) return Status::Error(fv::kNotFound, spec + " is not callable");
    SharedPy held = Share(std::move(fn));
    *out = [held, spec, package_dir](fv::desk::ToolContext& context) -> Status {
      py::gil_scoped_acquire gil;
      auto ctx = std::make_shared<PyToolContext>();
      ctx->context = &context;
      ctx->package_dir = package_dir;
      Status result = Status::Ok();
      try {
        (*held)(ctx);
      } catch (py::error_already_set& e) {
        result = FromPython(e, "python tool " + spec);
      }
      ctx->context = nullptr;
      return result;
    };
    return Status::Ok();
  }
};

}  // namespace

#if defined(_WIN32)
#define FV_BRIDGE_EXPORT extern "C" __declspec(dllexport)
#else
#define FV_BRIDGE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

/// Starts Python (once) and answers the bridge; null with `error` set.
FV_BRIDGE_EXPORT fv::desk::PythonBridge* fv_python_bridge_open(const char* python_home,
                                                               std::string* error) {
  static std::mutex mutex;
  static Bridge* bridge = nullptr;
  static std::string failed;  // Python cannot be started twice in one process
  std::lock_guard<std::mutex> lock(mutex);
  if (bridge != nullptr) return bridge;
  if (!failed.empty()) {
    if (error != nullptr) *error = failed;
    return nullptr;
  }
  try {
    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    config.parse_argv = 0;
    // The app owns SIGINT and the rest; Python must not install handlers.
    config.install_signal_handlers = 0;
    if (python_home != nullptr && *python_home != '\0') {
      const PyStatus st = PyConfig_SetBytesString(&config, &config.home, python_home);
      if (PyStatus_Exception(st)) {
        PyConfig_Clear(&config);
        if (error != nullptr) *error = "python.home: cannot use " + std::string(python_home);
        return nullptr;
      }
    }
    py::initialize_interpreter(&config, 0, nullptr, /*add_program_dir_to_path=*/false);
    BindToolContext();
  } catch (const std::exception& e) {
    failed = e.what();
    if (error != nullptr) *error = failed;
    return nullptr;
  }
  // Released for good: the render worker and the UI thread each take it per call.
  PyEval_SaveThread();
  bridge = new Bridge;
  return bridge;
}
