// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// pyfvw_common.h — the two things the module's translation units share.
//
// The bindings were one file until A6; `pyfvw.app` is big enough (a registry, a
// shell interface, the flows, editors and picking) to be its own TU, and these
// are the only pieces both halves need: the Status carrier the exception
// translator re-raises as pyfvw.FvError, and the one correct way to take a
// Python overlay into a C++ shared_ptr.

#pragma once

#include <pybind11/pybind11.h>

#include <memory>
#include <utility>

#include "fvkit/geo.h"
#include "fvkit/overlay/overlay.h"

namespace pyfvw {

namespace py = pybind11;

// C++-side carrier; the translator in pyfvw_module.cpp re-raises it as
// pyfvw.FvError. One definition, so both TUs throw the same type.
struct FvErrorCpp {
  fv::Status status;
};

inline void ThrowIfError(const fv::Status& s) {
  if (!s.ok()) throw FvErrorCpp{s};
}

// A Python overlay as a C++ shared_ptr, WITHOUT losing its overrides.
//
// This is the trampoline-lifetime trap contract D1 was written for, in its
// sharpest form. pybind11's holder owns the C++ half; the PYTHON half — the
// subclass instance carrying on_draw and every capability method — is owned by
// the interpreter, and if the last Python reference drops while C++ still holds
// the object, the overrides silently vanish and the overlay draws nothing.
// `OverlayManager.add` avoids it with py::keep_alive, which only works because
// the manager is right there in the call.
//
// A FACTORY has no such anchor: the descriptor's factory is called from deep
// inside a session flow with no Python object in scope to keep alive. So the
// returned shared_ptr ALIASES the Python object: it points at the C++ overlay
// and owns a reference to the py::object, which in turn owns the C++ half. The
// deleter drops that reference under the GIL, which is what actually frees
// everything, in the right order.
inline std::shared_ptr<fv::Overlay> OverlayFromPython(py::object obj) {
  if (obj.is_none()) return nullptr;
  fv::Overlay* raw = obj.cast<fv::Overlay*>();
  if (raw == nullptr) return nullptr;
  return std::shared_ptr<fv::Overlay>(
      raw, [held = std::move(obj)](fv::Overlay*) mutable {
        py::gil_scoped_acquire gil;
        held = py::object();
      });
}

}  // namespace pyfvw
