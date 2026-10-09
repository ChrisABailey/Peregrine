/* SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (C) 2026 Chris Bailey
 * Part of Peregrine, a cross-platform port of FalconView(tm).
 * See COPYING.LESSER and NOTICE.md for the full licensing picture. */

/** fv_log_c.h — C-callable entry to the application log (fvkit/log.h).
 *
 * For C sources and for fvw_core code reached through fv_compat.h's POSIX
 * stubs. Levels match fv::LogLevel. The Windows product does not link this;
 * keep calls behind the same non-Windows guard as the stub that makes them. */
#ifndef FV_LOG_C_H_
#define FV_LOG_C_H_

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FV_LOG_C_ERROR 0
#define FV_LOG_C_WARNING 1
#define FV_LOG_C_INFO 2
#define FV_LOG_C_DEBUG 3

/** Nonzero when a sink wants `level`. */
int fv_log_enabled(int level);
/** Logs `message` (a trailing newline is dropped). */
void fv_log_write(int level, const char* file, int line, const char* message);
/** Formats with vsnprintf and logs; nothing is formatted when `level` is filtered. */
void fv_log_vprintf(int level, const char* file, int line, const char* fmt, va_list ap);

#ifdef __cplusplus
}
#endif

#endif /* FV_LOG_C_H_ */
