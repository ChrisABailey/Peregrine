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

// file.h
//

typedef unsigned char  BYTE;
typedef signed char    INT1;
typedef signed short   INT2;
typedef signed int     INT4;
typedef unsigned char  UINT1;
typedef unsigned short UINT2;
typedef unsigned int   UINT4;
typedef float          FLOAT4;
typedef double         FLOAT8;

#define SUCCESS 0
#define FAILURE -1

#define LITTLE_ENDIAN 1

int FIL_get_real_4(FILE *in, FLOAT4 *f4);
int FIL_get_real_8(FILE *in, FLOAT8 *f8);
int FIL_get_int_4(FILE *in, INT4 *i4);
int FIL_get_int_2(FILE *in, INT2 *i2);
int FIL_get_int_1(FILE *in, INT1 *i1);

#if LITTLE_ENDIAN

#define FIL_get_real_8(x, y) FIL_get_little_real_8(x, y)
#define FIL_get_real_4(x, y) FIL_get_little_real_4(x, y)
#define FIL_get_int_4(x, y) FIL_get_little_int_4(x, y)
#define FIL_get_int_2(x, y) FIL_get_little_int_2(x, y)
#define FIL_mget_int_2(x, y, z) FIL_mget_little_int_2(x, y, z)
#define FIL_get_uint_4(x, y) FIL_get_little_uint_4(x, y)
#define FIL_get_uint_2(x, y) FIL_get_little_uint_2(x, y)

#define FIL_put_real_8(x, y) FIL_put_little_real_8(x, y)
#define FIL_put_real_4(x, y) FIL_put_little_real_4(x, y)
#define FIL_put_int_4(x, y) FIL_put_little_int_4(x, y)
#define FIL_put_int_2(x, y) FIL_put_little_int_2(x, y)
#define FIL_put_int_1(x, y) FIL_put_little_int_1(x, y)
#define FIL_put_uint_2(x, y) FIL_put_little_uint_2(x, y)
#define FIL_put_uint_4(x, y) FIL_put_little_uint_4(x, y)

#elif BIG_ENDIAN

#define FIL_get_real_8(x, y) FIL_get_big_real_8(x, y)
#define FIL_get_real_4(x, y) FIL_get_big_real_4(x, y)
#define FIL_get_int_4(x, y) FIL_get_big_int_4(x, y)
#define FIL_get_int_2(x, y) FIL_get_big_int_2(x, y)
#define FIL_mget_int_2(x, y, z) FIL_mget_big_int_2(x, y, z)
#define FIL_get_uint_4(x, y) FIL_get_big_uint_4(x, y)
#define FIL_get_uint_2(x, y) FIL_get_big_uint_2(x, y)

#define FIL_put_real_8(x, y) FIL_put_big_real_8(x, y)
#define FIL_put_real_4(x, y) FIL_put_big_real_4(x, y)
#define FIL_put_int_4(x, y) FIL_put_big_int_4(x, y)
#define FIL_put_int_2(x, y) FIL_put_big_int_2(x, y)
#define FIL_put_int_1(x, y) FIL_put_big_int_1(x, y)
#define FIL_put_uint_2(x, y) FIL_put_big_uint_2(x, y)
#define FIL_put_uint_4(x, y) FIL_put_big_uint_4(x, y)

#endif

int FIL_get_int_1(FILE *in, INT1 *i1);

int FIL_get_uint_1(FILE *in, UINT1 *ui1);

int FIL_read_real_4(FILE *in, FLOAT4 *f4, BOOL little_endian);
int FIL_get_big_real_4(FILE *in, FLOAT4 *f4);
int FIL_get_little_real_4(FILE *in, FLOAT4 *f4);

int FIL_read_real_8(FILE* in, FLOAT8* f8, BOOL little_endian);
int FIL_get_big_real_8(FILE *in, FLOAT8 *f8);
int FIL_get_little_real_8(FILE *in, FLOAT8 *f8);

int FIL_read_int_4(FILE *in, INT4 *i4, BOOL little_endian);
int FIL_get_big_int_4(FILE *in, INT4 *i4);
int FIL_get_little_int_4(FILE *in, INT4 *i4);

int FIL_read_uint_4(FILE *in, UINT4 *ui4, BOOL little_endian);
int FIL_get_big_uint_4(FILE *in, UINT4 *ui4);
int FIL_get_little_uint_4(FILE *in, UINT4 *ui4);

int FIL_read_int_2(FILE *in, INT2 *i2, BOOL little_endian);
int FIL_get_big_int_2(FILE *in, INT2 *i2);
int FIL_get_little_int_2(FILE *in, INT2 *i2);

int FIL_read_uint_2(FILE *in, UINT2 *ui2, BOOL little_endian);
int FIL_get_big_uint_2(FILE *in, UINT2 *ui2);
int FIL_get_little_uint_2(FILE *in, UINT2 *ui2);

size_t FIL_mread_int_2(FILE *in, INT2 *data, size_t item_count, 
   BOOL little_endian);
size_t FIL_mget_big_int_2(FILE *in, INT2 *data, size_t item_count);
size_t FIL_mget_little_int_2(FILE *in, INT2 *data, size_t item_count);

int FIL_put_little_real_4(FILE *out, FLOAT4 *data);
int FIL_put_little_real_8(FILE *out, FLOAT8 *data);
int FIL_put_little_int_4(FILE *out, INT4 *data);
int FIL_put_little_int_2(FILE *out, INT2 *data);
int FIL_put_little_int_1(FILE *out, INT1 *data);
int FIL_put_little_uint_2(FILE *out, UINT2* data);
int FIL_put_little_uint_4(FILE *out, UINT4* data);

int FIL_put_big_real_4(FILE *out, FLOAT4 *data);
int FIL_put_big_real_8(FILE *out, FLOAT8 *data);
int FIL_put_big_int_4(FILE *out, INT4 *data);
int FIL_put_big_int_2(FILE *out, INT2 *data);
int FIL_put_big_int_1(FILE *out, INT1 *data);
int FIL_put_big_uint_2(FILE *out, UINT2* data);
int FIL_put_big_uint_4(FILE *out, UINT4* data);


