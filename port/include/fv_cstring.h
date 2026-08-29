// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_cstring.h — minimal narrow-char CString for the FalconView port
// (playbook: DEDICATED HEADER; CString appears in ~547 fvw_core files).
// Grown strictly on demand, test-first. Semantics follow MFC's CStringA.
//
// LAYOUT CONTRACT: like MFC, the object is exactly one char* pointing at a
// NUL-terminated buffer. FalconView code relies on this by passing CString
// by value through printf-style varargs ("%s"); keep sizeof(CString) ==
// sizeof(char*) and the pointer as the only member, or that idiom breaks.

#pragma once

#ifndef _WIN32

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <strings.h>

class CString {
 public:
  CString() : m_p(dup("")) {}
  CString(const char* s) : m_p(dup(s ? s : "")) {}
  // MFC CString(LPCTSTR, int): first n chars (n<=strlen). Used by GeoSym's
  // token slicing (CVPFFormatHandler::GetOrdinalEntry, CAEValue::Init).
  CString(const char* s, int n) : m_p(nullptr) {
    if (s == nullptr || n < 0) n = 0;
    m_p = (char*)malloc((size_t)n + 1);
    if (n > 0) memcpy(m_p, s, (size_t)n);
    m_p[n] = '\0';
  }
  CString(const std::string& s) : m_p(dup(s.c_str())) {}
  CString(const CString& o) : m_p(dup(o.m_p)) {}
  CString(CString&& o) noexcept : m_p(o.m_p) { o.m_p = dup(""); }
  ~CString() { free(m_p); }

  CString& operator=(const CString& o) {
    if (this != &o) assign(o.m_p);
    return *this;
  }
  CString& operator=(const char* s) {
    assign(s ? s : "");
    return *this;
  }

  // MFC: implicit conversion to C string
  operator const char*() const { return m_p; }

  int GetLength() const { return (int)strlen(m_p); }
  bool IsEmpty() const { return m_p[0] == '\0'; }
  void Empty() { assign(""); }

  // MFC Compare = strcmp semantics
  int Compare(const char* other) const { return strcmp(m_p, other); }
  int CompareNoCase(const char* other) const {
    return strcasecmp(m_p, other);
  }

  int Find(char ch) const {
    const char* q = strchr(m_p, ch);
    return q ? (int)(q - m_p) : -1;
  }
  // MFC Find with a start index; -1 if the start is past the end.
  int Find(char ch, int start) const {
    if (start < 0) start = 0;
    if ((size_t)start > strlen(m_p)) return -1;
    const char* q = strchr(m_p + start, ch);
    return q ? (int)(q - m_p) : -1;
  }
  int Find(const char* sub) const {
    const char* q = strstr(m_p, sub);
    return q ? (int)(q - m_p) : -1;
  }
  int ReverseFind(char ch) const {
    const char* q = strrchr(m_p, ch);
    return q ? (int)(q - m_p) : -1;
  }
  // MFC FindOneOf: index of the first char that appears in `set`, or -1.
  int FindOneOf(const char* set) const {
    const char* q = strpbrk(m_p, set);
    return q ? (int)(q - m_p) : -1;
  }
  // MFC SpanExcluding: the leading run of chars NOT in `set` (the whole
  // string if none of them appear).
  CString SpanExcluding(const char* set) const {
    return CString(std::string(m_p, strcspn(m_p, set)));
  }
  // MFC SpanIncluding: the leading run of chars that ARE in `set`.
  CString SpanIncluding(const char* set) const {
    return CString(std::string(m_p, strspn(m_p, set)));
  }

  // printf-style, like MFC CStringA::Format
  void Format(const char* fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    char* buf = (char*)malloc((size_t)n + 1);
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    free(m_p);
    m_p = buf;
  }

  CString Right(int nCount) const {  // last nCount chars, clamped
    size_t len = strlen(m_p);
    if (nCount < 0) nCount = 0;
    size_t n = (size_t)nCount > len ? len : (size_t)nCount;
    return CString(m_p + (len - n));
  }
  CString Left(int nCount) const {
    size_t len = strlen(m_p);
    if (nCount < 0) nCount = 0;
    size_t n = (size_t)nCount > len ? len : (size_t)nCount;
    return CString(std::string(m_p, n));
  }
  CString Mid(int nFirst) const {
    size_t len = strlen(m_p);
    if (nFirst < 0) nFirst = 0;
    return CString(m_p + ((size_t)nFirst > len ? len : (size_t)nFirst));
  }
  CString Mid(int nFirst, int nCount) const {
    size_t len = strlen(m_p);
    if (nFirst < 0) nFirst = 0;
    if (nCount < 0) nCount = 0;
    size_t start = (size_t)nFirst > len ? len : (size_t)nFirst;
    size_t n = (size_t)nCount > len - start ? len - start : (size_t)nCount;
    return CString(std::string(m_p + start, n));
  }

  char GetAt(int i) const { return m_p[i]; }
  void SetAt(int i, char c) { m_p[i] = c; }

  void TrimRight() {
    size_t len = strlen(m_p);
    while (len > 0 && isspace((unsigned char)m_p[len - 1])) m_p[--len] = 0;
  }
  void TrimLeft() {
    char* q = m_p;
    while (*q && isspace((unsigned char)*q)) ++q;
    if (q != m_p) memmove(m_p, q, strlen(q) + 1);
  }
  // MFC overloads that trim a specific character (not whitespace). Used by
  // GeoSym's CAEValue::Convert to strip surrounding quotes.
  void TrimRight(char ch) {
    size_t len = strlen(m_p);
    while (len > 0 && m_p[len - 1] == ch) m_p[--len] = 0;
  }
  void TrimLeft(char ch) {
    char* q = m_p;
    while (*q == ch && *q) ++q;
    if (q != m_p) memmove(m_p, q, strlen(q) + 1);
  }

  void MakeUpper() {
    for (char* q = m_p; *q; ++q)
      *q = (char)toupper((unsigned char)*q);
  }
  void MakeLower() {
    for (char* q = m_p; *q; ++q)
      *q = (char)tolower((unsigned char)*q);
  }

  CString& operator+=(const char* s) {
    size_t a = strlen(m_p), b = strlen(s);
    char* buf = (char*)malloc(a + b + 1);
    memcpy(buf, m_p, a);
    memcpy(buf + a, s, b + 1);
    free(m_p);
    m_p = buf;
    return *this;
  }
  CString& operator+=(const CString& s) { return *this += s.m_p; }
  CString& operator+=(char c) {
    char two[2] = {c, 0};
    return *this += two;
  }

  bool operator==(const char* s) const { return strcmp(m_p, s) == 0; }
  bool operator!=(const char* s) const { return strcmp(m_p, s) != 0; }
  // CRITICAL: ordering must compare CONTENTS. Without these, a
  // std::map<CString, T> (i.e. an emulated CMap) falls back to the implicit
  // const char* conversion and orders by POINTER VALUE -- inserts appear to
  // work, then Lookup() misses at random. (Found via CMap<CString, ...> in
  // VPFDatabase::library_exists, 2026-07-20.)
  bool operator<(const CString& o) const { return strcmp(m_p, o.m_p) < 0; }
  bool operator>(const CString& o) const { return strcmp(m_p, o.m_p) > 0; }
  bool operator<=(const CString& o) const { return strcmp(m_p, o.m_p) <= 0; }
  bool operator>=(const CString& o) const { return strcmp(m_p, o.m_p) >= 0; }
  bool operator==(const CString& o) const { return strcmp(m_p, o.m_p) == 0; }
  bool operator!=(const CString& o) const { return strcmp(m_p, o.m_p) != 0; }

  // MFC GetBuffer: modifiable buffer with capacity >= nMinBufLength chars.
  // (Callers here use the result transiently as a char* argument, without
  // ReleaseBuffer; the string content is preserved.)
  char* GetBuffer(int nMinBufLength) {
    size_t need = (size_t)(nMinBufLength > 0 ? nMinBufLength : 0) + 1;
    size_t have = strlen(m_p) + 1;
    if (need > have) {
      char* buf = (char*)malloc(need);
      memcpy(buf, m_p, have);
      free(m_p);
      m_p = buf;
    }
    return m_p;
  }
  void ReleaseBuffer(int /*nNewLength*/ = -1) {}

  std::string std_str() const { return std::string(m_p); }  // port helper

 private:
  static char* dup(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    memcpy(p, s, n);
    return p;
  }
  void assign(const char* s) {
    char* p = dup(s);
    free(m_p);
    m_p = p;
  }

  char* m_p;  // sole member — see LAYOUT CONTRACT above
};

static_assert(sizeof(CString) == sizeof(char*),
              "CString must stay pointer-sized for varargs pass-through");

inline CString operator+(const CString& a, const char* b) {
  CString r(a);
  r += b;
  return r;
}
inline CString operator+(const char* a, const CString& b) {
  CString r(a);
  r += b;
  return r;
}
inline CString operator+(const CString& a, const CString& b) {
  CString r(a);
  r += b;
  return r;
}
// CRITICAL: without these two, `str + '\\'` does NOT concatenate. CString
// converts implicitly to const char*, so the compiler silently picks built-in
// POINTER ARITHMETIC and yields a pointer 92 bytes past the buffer. MFC has
// these overloads, which is why the original code is correct on Windows.
// (Found in VPFLibrary::get_path(), 2026-07-20, via AddressSanitizer.)
inline CString operator+(const CString& a, char b) {
  CString r(a);
  r += b;
  return r;
}
inline CString operator+(char a, const CString& b) {
  char two[2] = {a, 0};
  CString r(two);
  r += b;
  return r;
}

// minimal MFC CStringArray over std::vector
#include <vector>
class CStringArray {
 public:
  int GetSize() const { return (int)m_v.size(); }
  int GetUpperBound() const { return (int)m_v.size() - 1; }
  void RemoveAll() { m_v.clear(); }
  int Add(const CString& s) {
    m_v.push_back(s);
    return (int)m_v.size() - 1;
  }
  CString& GetAt(int i) { return m_v[(size_t)i]; }
  const CString& GetAt(int i) const { return m_v[(size_t)i]; }
  CString& operator[](int i) { return m_v[(size_t)i]; }
  const CString& operator[](int i) const { return m_v[(size_t)i]; }

 private:
  std::vector<CString> m_v;
};

#endif  // !_WIN32
