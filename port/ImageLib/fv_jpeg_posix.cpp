// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// POSIX implementations of Windows-specific helpers from
// fvw_core/ImageLib/jpeg/jpeg.cpp (which stays Windows-only in Phase 2a —
// it is the CString wrapper class).
//
// jpeg_fread on Windows special-cases memory-mapped/ReadFile-backed FILE*
// streams (CRT internals + SEH); on POSIX every FILE* is a plain stream, so
// this reduces to fread — same result for ordinary files.

#include <cstdio>

size_t jpeg_fread(void* pvbuf, size_t sizeofbuf, FILE* pfile)
{
   return fread(pvbuf, (size_t)1, sizeofbuf, pfile);
}
