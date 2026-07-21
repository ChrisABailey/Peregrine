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

// mapscales.h

#ifndef MAPSCALES_H
#define MAPSCALES_H

typedef /* [v1_enum] */ 
enum MapScaleUnitsEnum
    {	MAP_SCALE_DENOMINATOR	= 0,
		MAP_SCALE_NM	= 1,
		MAP_SCALE_MILE	= 2,
		MAP_SCALE_KILOMETER	= 3,
		MAP_SCALE_METERS	= 4,
		MAP_SCALE_YARDS	= 5,
		MAP_SCALE_FEET	= 6,
		MAP_SCALE_INCHES	= 7,
		MAP_SCALE_ARC_DEGREES	= 8,
		MAP_SCALE_ARC_MINUTES	= 9,
		MAP_SCALE_ARC_SECONDS	= 10,
		MAP_SCALE_WORLD	= 11
    } 	MapScaleUnitsEnum;

typedef /* [v1_enum] */ 
enum MapProjectionEnum
    {	MAP_PROJECTION_ALL	= -1,
		MAP_PROJECTION_UNKNOWN	= 0,
		MAP_PROJECTION_EQUAL_ARC	= 1,
		MAP_PROJECTION_POLAR	= 2
    } 	MapProjectionEnum;

typedef /* [v1_enum] */ 
enum DataSourceTypeEnum
    {	DS_UNKNOWN	= 0,
		DS_REMOVABLE	= 2,
		DS_FIXED	= 3,
		DS_REMOTE	= 4,
		DS_CDROM	= 5,
		DS_RAMDISK	= 6,
		DS_RMDS	= 7,
		DS_JUKEBOX	= 8
    } 	DataSourceTypeEnum;

typedef /* [v1_enum] */ 
enum DataSourceOfflineType
    {	DS_ONLINE	= 0,
		DS_OFFLINE	= 1,
		DS_MANUAL_OFFLINE	= 2
    } 	DataSourceOfflineType;

typedef 
enum PointDataTypeMaskEnum
    {	POINT_DATA_ELEVATION	= 1
    } 	PointDataTypeMaskEnum;

typedef /* [v1_enum] */ 
enum ElevationUnitsEnum
    {	ELEV_UNITS_METERS	= 0,
		ELEV_UNITS_FEET	= 1
    } 	ElevationUnitsEnum;

#define TRY_BLOCK try
#define CATCH_BLOCK_RET catch (...) { return -1; }
//#define THROW_ERROR_MSG(hrf, src, msg) \
//		throw _com_error(hrf, CComErrorObject(hrf, src, msg));





#endif  // ifndef MAPSCALES_H