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

// polar_utils.cpp
//

#include "stdafx.h"
#include "polar_utils.h"
#include <math.h>

#define RAD_TO_DEG(radians)       (((double)(radians)) * 57.295779513082322)

void PolarUtil::polar_xy_to_lat(double x, double y, double POL_pix_const, bool north_pole, double &lat)
{
	lat = 90.0 - (sqrt(x*x + y*y) / (POL_pix_const / 360.0));
	
	if (!north_pole)
		lat = -lat;
}

void PolarUtil::north_polar_xy_to_lon(double x, double y, double &lon)
{
	if (x < 0)
	{
	   lon = -RAD_TO_DEG(acos(-y / sqrt(x*x + y*y)));
	}
	else if (x == 0)
	{
		if (y > 0)
			lon = 180.0;
		else
			lon = 0.0;
	}
	else if (x > 0)
	{
		lon = RAD_TO_DEG(acos(-y / sqrt(x*x + y*y)));
	}
}

void PolarUtil::south_polar_xy_to_lon(double x, double y, double &lon)
{
	if (x < 0)
	{
	   lon = -RAD_TO_DEG(acos(y / sqrt(x*x + y*y)));
	}
	else if (x == 0)
	{
		if (y >= 0)
			lon = 0.0;
		else
			lon = 180.0;;
	}
	else if (x > 0)
	{
		lon = RAD_TO_DEG(acos(y / sqrt(x*x + y*y)));
	}
}

void PolarUtil::polar_xy_to_lon(double x, double y, bool north_pole, double &lon)
{
	if (north_pole)
		north_polar_xy_to_lon(x, y, lon);
	else
		south_polar_xy_to_lon(x, y, lon);
}