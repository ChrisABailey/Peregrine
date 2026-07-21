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



// mercator.cpp

#include "stdafx.h"

#include <math.h>

#include "geo_tool.h"

Mercator::Mercator(degrees_t center_lon, double scale)
{
	set_center_lon(center_lon);
	m_R = WGS84_R2_METERS / scale;
}

double Mercator::lon_to_x(degrees_t lon) 
{
   degrees_t diff = lon - m_center_lon;

   if (diff < -HALF_WORLD_DEG)
      diff += WORLD_DEG;
   else if (diff > HALF_WORLD_DEG)
      diff -= WORLD_DEG;

	return PI*m_R*(diff)/180.0;
}

double Mercator::lat_to_y(degrees_t lat)
{
	return m_R*log(tan(PI/4+DEG_TO_RAD(lat)/2));
}

degrees_t Mercator::x_to_lon(double x)
{
   degrees_t diff = RAD_TO_DEG(x/m_R+DEG_TO_RAD(m_center_lon));

   if (diff < -HALF_WORLD_DEG)
      diff += WORLD_DEG;
   else if (diff > HALF_WORLD_DEG)
      diff -= WORLD_DEG;

	return diff;
}

degrees_t Mercator::y_to_lat(double y)
{
	return RAD_TO_DEG(PI/2-2*atan(exp(-y/m_R)));
}

void Mercator::set_center_lon(degrees_t lon)
{
   ASSERT(lon >= -180.0 && lon <= 180.0);

   if (lon >= -180.0 && lon <= 180.0)
	   m_center_lon = lon;
}

double Mercator::get_min_x()
{
   return -PI*m_R;
}

double Mercator::get_max_x()
{
   return PI*m_R;
}

// Since the Mercator projection can not handle the poles, you have to move
// points at the poles slightly off the pole in order to be able to use rhumb
// line range and bearing functions and the like.  This function will fudge a
// latitude for you as needed.  It returns TRUE if the value is changed, FALSE
// otherwise.
boolean_t GEO_fudge_polar_lat_for_rhumb_line(degrees_t &lat)
{
   if (lat > 89.999999)
   {
      lat = 89.999999;
      return TRUE;
   }

   if (lat < -89.999999)
   {
      lat = -89.999999;
      return TRUE;
   }

   return FALSE;
}
