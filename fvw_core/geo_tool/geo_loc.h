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



#ifndef GEO_LOC_H
#define GEO_LOC_H 1

/*------------------------------------------------------------------
-                            Includes
-------------------------------------------------------------------*/

#include "common.h"


/*------------------------------------------------------------------
-                       Function Prototypes 
-------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C"
{
#endif

boolean_t geo_east_of(double a, double b, double around_world,
   double half_world);

int geo_bounds_check(double ll_lat, double ll_lon,
   double ur_lat, double ur_lon,
   double p_lat, double p_lon,
   double around_world, double half_world);


#ifdef __cplusplus
}
#endif

#endif /* GEO_LOC_H */
