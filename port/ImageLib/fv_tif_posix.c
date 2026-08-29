// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/*
 * fv_tif_posix.c — POSIX OS glue for the vendored libtiff 3.9.4
 * (fvw_core/ImageLib/tiff). The tree ships only tif_win32.c; this file
 * provides the same entry points (TIFFOpen/TIFFFdOpen, _TIFFmalloc family,
 * default error handlers) over POSIX fds, mirroring stock tif_unix.c
 * behavior including mmap-based reading when the client requests it.
 */

#include "tif_config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "tiffiop.h"
#include "tiffvers.h"

static tsize_t
_tiffReadProc(thandle_t fd, tdata_t buf, tsize_t size)
{
	return ((tsize_t) read((int)(intptr_t) fd, buf, (size_t) size));
}

static tsize_t
_tiffWriteProc(thandle_t fd, tdata_t buf, tsize_t size)
{
	return ((tsize_t) write((int)(intptr_t) fd, buf, (size_t) size));
}

static toff_t
_tiffSeekProc(thandle_t fd, toff_t off, int whence)
{
	return ((toff_t) lseek((int)(intptr_t) fd, (off_t) off, whence));
}

static int
_tiffCloseProc(thandle_t fd)
{
	return (close((int)(intptr_t) fd));
}

static toff_t
_tiffSizeProc(thandle_t fd)
{
	struct stat sb;
	if (fstat((int)(intptr_t) fd, &sb) < 0)
		return (0);
	return ((toff_t) sb.st_size);
}

static int
_tiffMapProc(thandle_t fd, tdata_t* pbase, toff_t* psize)
{
	toff_t size = _tiffSizeProc(fd);
	if (size != (toff_t) -1) {
		*pbase = (tdata_t)
		    mmap(0, size, PROT_READ, MAP_SHARED, (int)(intptr_t) fd, 0);
		if (*pbase != (tdata_t) -1) {
			*psize = size;
			return (1);
		}
	}
	return (0);
}

static void
_tiffUnmapProc(thandle_t fd, tdata_t base, toff_t size)
{
	(void) fd;
	(void) munmap(base, (off_t) size);
}

TIFF*
TIFFFdOpen(int ifd, const char* name, const char* mode)
{
	TIFF* tif;

	tif = TIFFClientOpen(name, mode,
	    (thandle_t)(intptr_t) ifd,
	    _tiffReadProc, _tiffWriteProc,
	    _tiffSeekProc, _tiffCloseProc, _tiffSizeProc,
	    _tiffMapProc, _tiffUnmapProc);
	if (tif)
		tif->tif_fd = ifd;
	return (tif);
}

TIFF*
TIFFOpen(const char* name, const char* mode)
{
	static const char module[] = "TIFFOpen";
	int m, fd;
	TIFF* tif;

	m = _TIFFgetMode(mode, module);
	if (m == -1)
		return ((TIFF*) 0);

	fd = open(name, m | O_CLOEXEC, 0666);
	if (fd < 0) {
		if (errno > 0 && strerror(errno) != NULL)
			TIFFErrorExt(0, module, "%s: %s", name, strerror(errno));
		else
			TIFFErrorExt(0, module, "%s: Cannot open", name);
		return ((TIFF *) 0);
	}

	tif = TIFFFdOpen((int) fd, name, mode);
	if (!tif)
		close(fd);
	return (tif);
}

tdata_t
_TIFFmalloc(tsize_t s)
{
	return ((tdata_t) malloc((size_t) s));
}

void
_TIFFfree(tdata_t p)
{
	free(p);
}

tdata_t
_TIFFrealloc(tdata_t p, tsize_t s)
{
	return ((tdata_t) realloc(p, (size_t) s));
}

void
_TIFFmemset(tdata_t p, int v, tsize_t c)
{
	memset(p, v, (size_t) c);
}

void
_TIFFmemcpy(tdata_t d, const tdata_t s, tsize_t c)
{
	memcpy(d, s, (size_t) c);
}

int
_TIFFmemcmp(const tdata_t p1, const tdata_t p2, tsize_t c)
{
	return (memcmp(p1, p2, (size_t) c));
}

static void
unixWarningHandler(const char* module, const char* fmt, va_list ap)
{
	if (module != NULL)
		fprintf(stderr, "%s: ", module);
	fprintf(stderr, "Warning, ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, ".\n");
}
TIFFErrorHandler _TIFFwarningHandler = unixWarningHandler;

static void
unixErrorHandler(const char* module, const char* fmt, va_list ap)
{
	if (module != NULL)
		fprintf(stderr, "%s: ", module);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, ".\n");
}
TIFFErrorHandler _TIFFerrorHandler = unixErrorHandler;

/* Defined in tif_version.c in stock libtiff; that file is absent from this
 * vendored copy (the Windows build exports it via tifflib.cpp). */
const char*
TIFFGetVersion(void)
{
	return (TIFFLIB_VERSION_STR);
}
