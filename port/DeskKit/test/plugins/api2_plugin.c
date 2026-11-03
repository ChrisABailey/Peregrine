/* SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (C) 2026 Chris Bailey
 * Part of Peregrine, a cross-platform port of FalconView(tm).
 * See COPYING.LESSER and NOTICE.md for the full licensing picture. */

/** A plugin that claims a future major API version; the host must refuse it. */
#include "peregrine/fv_plugin.h"

FV_PLUGIN_EXPORT int fv_plugin_entry(const fv_host_api* host, fv_plugin_api* out) {
  (void)host;
  out->api_major = 2;
  out->api_minor = 0;
  return FV_OK;
}
