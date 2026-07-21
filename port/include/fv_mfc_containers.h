// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_mfc_containers.h — minimal MFC container emulation for the FalconView
// port (playbook: DEDICATED HEADER; CArray/CList/CMap appear in ~106 files).
// Implements the POSITION iteration idiom over std:: containers. Grown on
// demand, test-first — add methods only when a ported module calls them.

#pragma once

#ifndef _WIN32

#include <cstddef>
#include <list>
#include <map>
#include <vector>

#include "fv_compat.h"    // LPCTSTR
#include "fv_cstring.h"   // for CStringList, below

#ifndef __POSITION_DEFINED
#define __POSITION_DEFINED
struct __POSITION;  // opaque, as in MFC
typedef __POSITION* POSITION;
#endif

template <class TYPE, class ARG_TYPE = const TYPE&>
class CList {
  typedef std::list<TYPE> impl_t;

 public:
  int GetCount() const { return (int)m_l.size(); }
  bool IsEmpty() const { return m_l.empty(); }
  void RemoveAll() { m_l.clear(); }

  POSITION AddTail(ARG_TYPE v) {
    m_l.push_back(v);
    return to_pos(std::prev(m_l.end()));
  }
  POSITION AddHead(ARG_TYPE v) {
    m_l.push_front(v);
    return to_pos(m_l.begin());
  }

  POSITION GetHeadPosition() const {
    return m_l.empty() ? nullptr : to_pos(const_cast<impl_t&>(m_l).begin());
  }
  POSITION GetTailPosition() const {
    return m_l.empty() ? nullptr
                       : to_pos(std::prev(const_cast<impl_t&>(m_l).end()));
  }

  // MFC GetNext: returns element at pos, then advances pos (null at end)
  TYPE& GetNext(POSITION& pos) {
    typename impl_t::iterator it = from_pos(pos);
    TYPE& v = *it;
    ++it;
    pos = (it == m_l.end()) ? nullptr : to_pos(it);
    return v;
  }
  TYPE& GetPrev(POSITION& pos) {
    typename impl_t::iterator it = from_pos(pos);
    TYPE& v = *it;
    pos = (it == m_l.begin()) ? nullptr : to_pos(std::prev(it));
    return v;
  }
  // MFC Find: first element equal to v, or NULL. (startAfter is unused by
  // ported callers, so it is not emulated.)
  POSITION Find(ARG_TYPE v) const {
    for (typename impl_t::iterator it = const_cast<impl_t&>(m_l).begin();
         it != const_cast<impl_t&>(m_l).end(); ++it)
      if (*it == v) return to_pos(it);
    return nullptr;
  }

  TYPE& GetAt(POSITION pos) { return *from_pos(pos); }
  const TYPE& GetAt(POSITION pos) const { return *from_pos(pos); }
  TYPE& GetHead() { return m_l.front(); }
  TYPE& GetTail() { return m_l.back(); }

  void RemoveAt(POSITION pos) { m_l.erase(from_pos(pos)); }
  TYPE RemoveHead() {
    TYPE v = m_l.front();
    m_l.pop_front();
    return v;
  }

 private:
  // POSITION <-> iterator: store iterators in a stable side table would be
  // heavyweight; instead rely on std::list iterator stability and encode the
  // node pointer. std::list iterators survive insert/erase-elsewhere exactly
  // like MFC POSITIONs do.
  static POSITION to_pos(typename impl_t::iterator it) {
    return reinterpret_cast<POSITION>(&*it);
  }
  typename impl_t::iterator from_pos(POSITION pos) {
    for (typename impl_t::iterator it = m_l.begin(); it != m_l.end(); ++it)
      if (reinterpret_cast<POSITION>(&*it) == pos) return it;
    return m_l.end();
  }
  typename impl_t::const_iterator from_pos(POSITION pos) const {
    for (typename impl_t::const_iterator it = m_l.begin(); it != m_l.end();
         ++it)
      if (reinterpret_cast<POSITION>(const_cast<TYPE*>(&*it)) == pos)
        return it;
    return m_l.end();
  }

  impl_t m_l;
};

template <class TYPE, class ARG_TYPE = const TYPE&>
class CArray {
 public:
  int GetSize() const { return (int)m_v.size(); }
  int GetCount() const { return (int)m_v.size(); }
  void SetSize(int n) { m_v.resize((size_t)n); }
  void RemoveAll() { m_v.clear(); }
  int Add(ARG_TYPE v) {
    m_v.push_back(v);
    return (int)m_v.size() - 1;
  }
  TYPE& GetAt(int i) { return m_v[(size_t)i]; }
  const TYPE& GetAt(int i) const { return m_v[(size_t)i]; }
  void SetAt(int i, ARG_TYPE v) { m_v[(size_t)i] = v; }
  TYPE& operator[](int i) { return m_v[(size_t)i]; }
  const TYPE& operator[](int i) const { return m_v[(size_t)i]; }
  TYPE* GetData() { return m_v.data(); }

 private:
  std::vector<TYPE> m_v;
};

// CMap<KEY, ARG_KEY, VALUE, ARG_VALUE> — MFC signature kept so shared headers
// declare members identically on both platforms. The ARG_ types are only
// MFC's by-value/by-ref pass hints and are unused here.
//
// DEVIATION (2026-07-20): MFC's CMap is a HASH table, so GetStartPosition/
// GetNextAssoc walk in bucket order; this one is a std::map, so it walks in
// KEY-SORTED order. Callers that iterate must not depend on the order — VPF's
// library/coverage maps only iterate to enumerate names, and sorted order is
// the more predictable of the two.
template <class KEY, class ARG_KEY, class VALUE, class ARG_VALUE>
class CMap {
  typedef std::map<KEY, VALUE> impl_t;

 public:
  int GetCount() const { return (int)m_m.size(); }
  bool IsEmpty() const { return m_m.empty(); }
  void RemoveAll() { m_m.clear(); }

  // MFC sizes its hash table up front; std::map needs no such hint.
  void InitHashTable(unsigned /*buckets*/, BOOL /*allocNow*/ = TRUE) {}

  void SetAt(const KEY& k, const VALUE& v) { m_m[k] = v; }
  VALUE& operator[](const KEY& k) { return m_m[k]; }

  // MFC returns TRUE/FALSE and writes the value only on a hit.
  bool Lookup(const KEY& k, VALUE& v) const {
    typename impl_t::const_iterator it = m_m.find(k);
    if (it == m_m.end()) return false;
    v = it->second;
    return true;
  }
  bool RemoveKey(const KEY& k) { return m_m.erase(k) > 0; }

  POSITION GetStartPosition() const {
    return m_m.empty() ? nullptr : to_pos(m_m.begin());
  }
  // MFC GetNextAssoc: yields the pair at pos, then advances pos (null at end).
  void GetNextAssoc(POSITION& pos, KEY& k, VALUE& v) const {
    typename impl_t::const_iterator it = from_pos(pos);
    if (it == m_m.end()) {
      pos = nullptr;
      return;
    }
    k = it->first;
    v = it->second;
    ++it;
    pos = (it == m_m.end()) ? nullptr : to_pos(it);
  }

 private:
  // Pair addresses are stable in std::map, exactly like MFC POSITIONs.
  static POSITION to_pos(typename impl_t::const_iterator it) {
    return reinterpret_cast<POSITION>(
        const_cast<typename impl_t::value_type*>(&*it));
  }
  typename impl_t::const_iterator from_pos(POSITION pos) const {
    for (typename impl_t::const_iterator it = m_m.begin(); it != m_m.end();
         ++it)
      if (to_pos(it) == pos) return it;
    return m_m.end();
  }

  impl_t m_m;
};

// MFC's CStringList is exactly CList<CString, LPCTSTR>.
typedef CList<CString, LPCTSTR> CStringList;

#endif  // !_WIN32
