// fv_autoptr.h — a C++17-legal stand-in for std::auto_ptr (removed in C++17).
//
// ImageLib's smart-pointer typedefs (BytePtr, CImagePtr, ...) were
// std::auto_ptr, whose defining trait is transfer-of-ownership on copy
// (`a = b;` moves the pointer out of b). Many ImageLib .cpp files rely on
// that exact behaviour (e.g. Image.cpp: `m_apbOldImg = apbImg; // transfer`).
// std::unique_ptr rejects those copies, so a blanket unique_ptr swap would
// break call sites across the (mostly Windows-only, not-yet-ported) tree.
//
// fvw_auto_ptr reproduces auto_ptr's transfer-on-copy AND adds real move
// support, so every existing call site keeps compiling unchanged on both
// platforms while the literal std::auto_ptr disappears. Migrate individual
// typedefs to std::unique_ptr (with std::move at the transfer sites) as each
// consuming file is ported and can be compiler-verified. NOTE: like
// auto_ptr, the destructor uses scalar `delete` — several call sites store
// `new T[]` here (a pre-existing latent bug); preserved as-is, fix per-file
// when migrating to unique_ptr<T[]>.

#pragma once

#include <cstddef>

template <class T>
class fvw_auto_ptr {
 public:
  explicit fvw_auto_ptr(T* p = nullptr) : p_(p) {}
  // transfer-on-copy (auto_ptr semantics): source is emptied
  fvw_auto_ptr(fvw_auto_ptr& o) : p_(o.release()) {}
  fvw_auto_ptr(fvw_auto_ptr&& o) noexcept : p_(o.release()) {}
  fvw_auto_ptr& operator=(fvw_auto_ptr& o) {
    reset(o.release());
    return *this;
  }
  fvw_auto_ptr& operator=(fvw_auto_ptr&& o) noexcept {
    reset(o.release());
    return *this;
  }
  ~fvw_auto_ptr() { delete p_; }

  T* get() const { return p_; }
  T* release() {
    T* t = p_;
    p_ = nullptr;
    return t;
  }
  void reset(T* p = nullptr) {
    if (p_ != p) {
      delete p_;
      p_ = p;
    }
  }
  T& operator*() const { return *p_; }
  T* operator->() const { return p_; }

 private:
  T* p_ = nullptr;
};
