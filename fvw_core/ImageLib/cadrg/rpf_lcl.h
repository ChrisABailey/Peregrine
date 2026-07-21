// Copyright (c) 1994-2009 Georgia Tech Research Corporation, Atlanta, GA
// This file is part of FalconView(tm).

// FalconView(tm) is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// FalconView(tm) is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public License
// along with FalconView(tm).  If not, see <http://www.gnu.org/licenses/>.

// FalconView(tm) is a trademark of Georgia Tech Research Corporation.



#ifndef RPF_LCL_H
#define RPF_LCL_H 1

#include "file.h"

#define GET_UINT_4(fp, ptr, le)  if (SUCCESS != FIL_read_uint_4(fp, ptr, le)) \
                                    return FAILURE
#define GET_UINT_2(fp, ptr, le)  if (SUCCESS != FIL_read_uint_2(fp, ptr, le)) \
                                    return FAILURE
#define GET_UINT_1(fp, ptr)      if (SUCCESS != FIL_get_uint_1(fp, ptr)) \
                                    return FAILURE
#define GET_REAL_8(fp, ptr, le)  if (SUCCESS != FIL_read_real_8(fp, ptr, le)) \
                                    return FAILURE
#define FREAD(ptr, elm_size, num_items, fp)  \
                    if ((num_items) != fread(ptr, elm_size, num_items, fp)) \
                       return FAILURE
#define FSEEK(fp, offset, place)     if (0 != fseek(fp, offset, place)) \
                                        return FAILURE

#define READ_RPF_STRING(rpf_str, fp) \
   if (rpf_str.read(fp) != SUCCESS)  \
      return FAILURE;

#endif