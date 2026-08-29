// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_dted_instance_posix.h — POSIX stand-in for Utility/DTEDManager.h's
// CDTEDInstance (a COM IDted/IGeoid holder used by CTransform::InvXfrmRPC00B
// for height-sensitive RPC refinement). IsValid() == FALSE takes the
// original's documented "No DTED available" fallback path — the transform
// still converges, without terrain-height refinement.
//
// TODO(later): back this with fv::DtedCell + fv::GeoidCalculator via
// fv_interfaces.h to restore height sensitivity headless.

#pragma once

class CDTEDInstance {
 public:
  BOOL IsValid() const { return FALSE; }
  // Never called when IsValid() is FALSE (all uses are guarded), but the
  // compiler needs declarations:
  struct NullService* IDted() const { return nullptr; }
  struct NullService* IGeoid() const { return nullptr; }
};
