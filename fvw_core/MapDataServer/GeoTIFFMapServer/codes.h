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

// codes.h

#ifndef CODES_H
#define CODES_H 1

#define CODES_NUM_STATES 51
#define CODES_NUM_GEOTIFF_UTM_ZONE 281
#define CODES_COUNTY_NUM 3150
#define CODES_NUM_SP_CODES 250

#ifndef SUCCESS
#define SUCCESS 0
#endif

#ifndef FAILURE
#define FAILURE -1
#endif

#ifndef PI
#define HALF_PI 1.57079632679489661923
#define PI      3.14159265358979323846
#define TWO_PI  6.2831853071795862
#endif


typedef struct
{
	char name[20];
	int geotiff_utm_zone;
} geotiff_utm_zone_t;

typedef struct
{
	char name[30];
	int fips_code;
	int utm_zone;
	char abbrev[3];
} fips_state_codes_t;

typedef struct
{
	long geotiff_code;
	char name[30];
	long fips_zone;
	long datum;
	long units;
} state_plane_code_t;



class CCodes
{
public:
	CCodes();
	~CCodes();

protected:
	int get_next_field(char *input, int *pos, std::string & data);

public:
	int get_state_name(int ndx, std::string &name);
	int get_fips_code_from_name(std::string name, int *fipscode);
	int get_name_from_fips_code(int fipscode, std::string &name);
	int get_geotiff_utm_zone_name(int ndx, std::string &name);
	int get_geotiff_utm_zone_from_name(std::string name, int *utm_zone);
	int get_sp_geotiff_code_from_name(std::string spcode, int *geotiff_code, int *units);
	int get_default_units_from_geotiff_sp_code(int geotiff_code, int *units);
	int get_name_from_geotiff_sp_code(int geotiff_code, std::string &name);
	int get_geotiff_code_number_from_index(int ndx, int *geotiff_code);
	int get_geotiff_code_string_from_index(int ndx, std::string & spcodestring);
	int geotiff_to_fips(long geotiff_code, long *fips_zone, long *datum);
	int fips_to_geotiff(int fips_zone, int datum, int *geotiff_code);
	int decode_county(int ndx, std::string &county_name, std::string &county_type, std::string &state_name,
					  std::string &state_abbrev, int *spc27, int *spc83, int *state_fips,
					  int *county_fips, int *fips, double *lat, double *lon, int *utm_zone);



};

#endif //#ifndef CODES_H