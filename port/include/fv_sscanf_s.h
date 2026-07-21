// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_sscanf_s.h — POSIX implementation of MSVC's sscanf_s for the FalconView
// port. fvw_core has ~100 call sites; their buffer sizes are runtime values
// interleaved in the argument list, so call sites cannot be mechanically
// rewritten to plain sscanf. Included by fv_compat.h on non-Windows.
//
// Semantics implemented (the subset fvw_core uses):
//  * %s, %c and %[...] consume a (char* buffer, size) argument pair; output is
//    truncated to fit and (for %s / %[) always null-terminated. Like MSVC, %c
//    does not null-terminate.
//  * '*' suppression, explicit widths, and h/hh/l/ll/L/j/z/t length modifiers
//    pass through to the underlying sscanf.
//  * Returns the number of assigned conversions; EOF if the input is exhausted
//    before the first assignment (approximation of C11 Annex K semantics —
//    MSVC's invalid-parameter handler behavior is NOT emulated: a zero-size or
//    null buffer is treated as a matching failure instead of aborting).
//
// ABI note: MSVC callers pass the size as int, unsigned or size_t. On the
// LP64 little-endian targets we support (arm64/x86-64 macOS, Linux), every
// integer vararg occupies one 8-byte slot and reading the low 32 bits yields
// the correct value for any realistic buffer size; sizes are read as
// unsigned int. Do not use this header on big-endian targets without fixing
// va_arg_size() below.

#pragma once

#ifndef _WIN32

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace fv_compat_detail {

inline unsigned va_arg_size(va_list& ap) { return va_arg(ap, unsigned); }

// Appends "%n"-instrumented conversion spec pieces; returns chars consumed
// from the input, or -1 on matching failure.
inline int scan_one(const char* in, const char* spec, void* target) {
  int consumed = -1;
  char spec_n[160];
  std::snprintf(spec_n, sizeof spec_n, "%s%%n", spec);
  if (target != nullptr)
    std::sscanf(in, spec_n, target, &consumed);
  else
    std::sscanf(in, spec_n, &consumed);
  return consumed;
}

inline int vsscanf_s_impl(const char* buffer, const char* format, va_list ap) {
  if (buffer == nullptr || format == nullptr) return EOF;
  const char* in = buffer;
  const char* f = format;
  int assigned = 0;
  bool attempted = false;  // true once any conversion/literal was attempted

  while (*f) {
    if (std::isspace(static_cast<unsigned char>(*f))) {
      while (std::isspace(static_cast<unsigned char>(*in))) ++in;
      ++f;
      continue;
    }
    if (*f != '%') {  // literal: must match exactly
      if (*in != *f) return assigned == 0 && *in == '\0' ? EOF : assigned;
      ++in;
      ++f;
      continue;
    }

    // --- parse one conversion spec starting at '%' ---
    ++f;
    if (*f == '%') {  // "%%": matches optional whitespace then '%'
      while (std::isspace(static_cast<unsigned char>(*in))) ++in;
      if (*in != '%') return assigned;
      ++in;
      ++f;
      continue;
    }

    bool suppress = false;
    if (*f == '*') {
      suppress = true;
      ++f;
    }
    unsigned width = 0;
    bool has_width = false;
    while (std::isdigit(static_cast<unsigned char>(*f))) {
      width = width * 10 + static_cast<unsigned>(*f - '0');
      has_width = true;
      ++f;
    }
    char mods[4] = {0};
    size_t nmods = 0;
    while (std::strchr("hlLjzt", *f) && nmods < 3) mods[nmods++] = *f++;
    const char conv = *f;
    if (conv == '\0') break;

    // Extract a %[...] set verbatim (']' first in the set is literal).
    char set[128] = {0};
    if (conv == '[') {
      const char* s = f + 1;
      const char* e = s;
      if (*e == '^') ++e;
      if (*e == ']') ++e;
      while (*e && *e != ']') ++e;
      if (*e != ']') return assigned;  // malformed format
      size_t len = static_cast<size_t>(e - s);
      if (len >= sizeof set) return assigned;
      std::memcpy(set, s, len);
      f = e;  // now points at ']'
    }
    ++f;  // past conversion char

    attempted = true;
    char spec[150];
    int consumed;

    if (conv == 's' || conv == '[') {
      char* out = nullptr;
      unsigned size = 0;
      if (!suppress) {
        out = va_arg(ap, char*);
        size = va_arg_size(ap);
        if (out == nullptr || size == 0) return assigned;  // matching failure
        unsigned max_chars = size - 1;
        if (max_chars == 0) return assigned;
        if (!has_width || width > max_chars) width = max_chars;
      }
      if (conv == '[')
        std::snprintf(spec, sizeof spec, "%%%s%s%u[%s]", suppress ? "*" : "",
                      "", width ? width : 0u, set);
      else
        std::snprintf(spec, sizeof spec, "%%%s%u%s", suppress ? "*" : "",
                      width ? width : 0u, "s");
      if (!suppress && width == 0) return assigned;
      // Suppressed with no width: rebuild without the bogus "0" width.
      if (suppress && width == 0) {
        if (conv == '[')
          std::snprintf(spec, sizeof spec, "%%*[%s]", set);
        else
          std::snprintf(spec, sizeof spec, "%%*s");
      }
      consumed = scan_one(in, spec, out);
      if (consumed < 0) return assigned == 0 && *in == '\0' ? EOF : assigned;
      in += consumed;
      if (!suppress) ++assigned;
      continue;
    }

    if (conv == 'c') {
      unsigned count = has_width ? width : 1;
      char* out = nullptr;
      if (!suppress) {
        out = va_arg(ap, char*);
        unsigned size = va_arg_size(ap);
        if (out == nullptr || size < count) return assigned;
      }
      if (std::strlen(in) < count)
        return assigned == 0 ? EOF : assigned;
      if (!suppress) {
        std::memcpy(out, in, count);
        ++assigned;
      }
      in += count;
      continue;
    }

    if (conv == 'n') {
      if (!suppress) *va_arg(ap, int*) = static_cast<int>(in - buffer);
      continue;
    }

    // Numeric and pointer conversions: delegate wholesale.
    if (has_width)
      std::snprintf(spec, sizeof spec, "%%%s%u%s%c", suppress ? "*" : "",
                    width, mods, conv);
    else
      std::snprintf(spec, sizeof spec, "%%%s%s%c", suppress ? "*" : "", mods,
                    conv);
    void* out = suppress ? nullptr : va_arg(ap, void*);
    consumed = scan_one(in, spec, out);
    if (consumed < 0) return assigned == 0 && *in == '\0' ? EOF : assigned;
    in += consumed;
    if (!suppress) ++assigned;
  }

  (void)attempted;
  return assigned;
}

}  // namespace fv_compat_detail

inline int sscanf_s(const char* buffer, const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  int result = fv_compat_detail::vsscanf_s_impl(buffer, format, ap);
  va_end(ap);
  return result;
}

#endif  // !_WIN32
