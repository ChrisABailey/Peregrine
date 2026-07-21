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



/*------------------------------------------------------------------
-  FILE NAME:           io.c
-  LIBRARY NAME:        file
-
-  DESCRIPTION:
-
-     This file contains functions to read and write numeric values in little
-  endian format.  On a big-endian machine, BIG_ENDIAN needs to be defined for
-  the functions to work correctly.
-
-  Note that the read functions only return SUCCESS or FAILURE.  They do not
-  return EOF.  End of file on a read is treated as a FAILURE.
-
-  PUBLIC FUNCTIONS:
-
-      FIL_get_real_4
-      FIL_get_real_8
-      FIL_get_int_4
-      FIL_get_int_2
-      FIL_get_int_1
-      FIL_mget_int_2
-      FIL_put_real_4
-      FIL_put_real_8
-      FIL_put_int_4
-      FIL_put_int_2
-      FIL_put_int_1
-
-  PRIVATE FUNCTIONS: NONE
-
-  STATIC FUNCTIONS:
-
-  PUBLIC VARIABLES: NONE
-
-  PRIVATE VARIABLES: NONE
-
-  REVISION HISTORY:
-       $Log: io.cpp $
//Revision 1.1  1996/03/19  12:32:39  kevin
//Initial revision
//
 * Revision 1.1  1994/10/26  09:10:17  gue
 * Initial revision
 * 
 * Revision 1.1  1994/02/26  11:16:18  gue
 * Initial revision
 * 
-------------------------------------------------------------------*/

/*------------------------------------------------------------------
-                            Includes
-------------------------------------------------------------------*/

#include "stdafx.h"
#include "file.h"

// -----------------------------------------------------------------
// Static function prototypes
// -----------------------------------------------------------------

static void mbyteswap_8(void *eight_byte_ptr, int count);
static void mbyteswap_4(void *four_byte_ptr, int count);
static void mbyteswap_2(void *two_byte_ptr, int count);

// -----------------------------------------------------------------
// -----------------------------------------------------------------
static inline
void byteswap_8(void* in, void* out)
{
   ((unsigned char*)out)[0] = ((unsigned char*)in)[7];
   ((unsigned char*)out)[1] = ((unsigned char*)in)[6];
   ((unsigned char*)out)[2] = ((unsigned char*)in)[5];
   ((unsigned char*)out)[3] = ((unsigned char*)in)[4];
   ((unsigned char*)out)[4] = ((unsigned char*)in)[3];
   ((unsigned char*)out)[5] = ((unsigned char*)in)[2];
   ((unsigned char*)out)[6] = ((unsigned char*)in)[1];
   ((unsigned char*)out)[7] = ((unsigned char*)in)[0];
}

static inline
void byteswap_8(void* ptr)
{
   unsigned char tmp[8];

   tmp[0] = ((unsigned char*)ptr)[0];
   tmp[1] = ((unsigned char*)ptr)[1];
   tmp[2] = ((unsigned char*)ptr)[2];
   tmp[3] = ((unsigned char*)ptr)[3];
   tmp[4] = ((unsigned char*)ptr)[4];
   tmp[5] = ((unsigned char*)ptr)[5];
   tmp[6] = ((unsigned char*)ptr)[6];
   tmp[7] = ((unsigned char*)ptr)[7];

   ((unsigned char*)ptr)[0] = tmp[7];
   ((unsigned char*)ptr)[1] = tmp[6];
   ((unsigned char*)ptr)[2] = tmp[5];
   ((unsigned char*)ptr)[3] = tmp[4];
   ((unsigned char*)ptr)[4] = tmp[3];
   ((unsigned char*)ptr)[5] = tmp[2];
   ((unsigned char*)ptr)[6] = tmp[1];
   ((unsigned char*)ptr)[7] = tmp[0];
}

static inline
void byteswap_4(void* in, void* out)
{
   ((unsigned char*)out)[0] = ((unsigned char*)in)[3];
   ((unsigned char*)out)[1] = ((unsigned char*)in)[2];
   ((unsigned char*)out)[2] = ((unsigned char*)in)[1];
   ((unsigned char*)out)[3] = ((unsigned char*)in)[0];
}

static inline
void byteswap_4(void* ptr)
{
   unsigned char tmp[4];

   tmp[0] = ((unsigned char*)ptr)[0];
   tmp[1] = ((unsigned char*)ptr)[1];
   tmp[2] = ((unsigned char*)ptr)[2];
   tmp[3] = ((unsigned char*)ptr)[3];

   ((unsigned char*)ptr)[0] = tmp[3];
   ((unsigned char*)ptr)[1] = tmp[2];
   ((unsigned char*)ptr)[2] = tmp[1];
   ((unsigned char*)ptr)[3] = tmp[0];

}

static inline
void byteswap_2(void* in, void* out)
{
   ((unsigned char*)out)[0] = ((unsigned char*)in)[1];
   ((unsigned char*)out)[1] = ((unsigned char*)in)[0];
}

static inline
void byteswap_2(void* ptr)
{
   unsigned char tmp[2];

   tmp[0] = ((unsigned char*)ptr)[0];
   tmp[1] = ((unsigned char*)ptr)[1];

   ((unsigned char*)ptr)[0] = tmp[1];
   ((unsigned char*)ptr)[1] = tmp[0];
}

static inline
int read_and_swap_8(FILE* fp, void* ptr)
{
   unsigned char in_buf[8];
   unsigned char* out_buf = (unsigned char*) ptr;

   if (8 != fread(in_buf, 1, 8, fp))
      return FAILURE;

#if 0

   int i;
   for(i = 7; i >=0; i--)
      out_buf[7-i] = in_buf[i];

#else

   byteswap_8(in_buf, out_buf);

#endif

   return SUCCESS;
}

static inline
int read_and_swap_4(FILE* fp, void* ptr)
{
   unsigned char in_buf[4];
   unsigned char* out_buf = (unsigned char*) ptr;

   if (4 != fread(in_buf, 1, 4, fp))
      return FAILURE;

   byteswap_4(in_buf, out_buf);

   return SUCCESS;
}

static inline
int read_and_swap_2(FILE* fp, void* ptr)
{
   unsigned char in_buf[2];
   unsigned char* out_buf = (unsigned char*) ptr;

   if (2 != fread(in_buf, 1, 2, fp))
      return FAILURE;

   byteswap_2(in_buf, out_buf);

   return SUCCESS;
}

/*------------------------------------------------------------------
-  FUNCTION NAME:    FIL_get_real_4
-  PROGRAMMER:       Rob Gue, Vinny Sollicito
-  DATE:             January 1994
-
-  PURPOSE:
-
-      Read a 4-byte little endian floating point value.
-
-  PARAMETERS:
-
-      in:           file pointer for the file to read from
-
-      f4:           where to put the floating point value
-
-  RETURN VALUES:
-
-      SUCCESS
-      FAILURE
-
-  PRECONDITIONS: NONE
-
-  EXTERNALS MODIFIED: NONE
-
-  REQUIRED INCLUDES:
-
-      <stdio.h>
-      file.h
-      common.h
-
-  DESCRIPTION: see Purpose
-------------------------------------------------------------------*/

int FIL_read_real_4(FILE *in, FLOAT4 *f4, BOOL little_endian)
{
   if (little_endian)
      return FIL_get_little_real_4(in, f4);
   else
      return FIL_get_big_real_4(in, f4);
}

int FIL_get_big_real_4(FILE *in, FLOAT4 *f4)
{
   return read_and_swap_4(in, f4);
}

int FIL_get_little_real_4(FILE *in, FLOAT4 *f4)
{
   if (1 != fread(f4, 4, 1, in))
      return FAILURE;
   else
      return SUCCESS;
}

/*------------------------------------------------------------------
-  FUNCTION NAME:        FIL_get_real_8
-  PROGRAMMER:           Rob Gue, Vinny Sollicito
-  DATE:                 January 1994
-
-  PURPOSE:
-
-      Read an 8-byte little endian floating point value.
-
-  PARAMETERS:
-
-      in:           file pointer for the file to read from
-
-      f8:           where to put the floating point value
-
-  RETURN VALUES:
-
-      SUCCESS
-      FAILURE
-
-  PRECONDITIONS: NONE
-
-  EXTERNALS MODIFIED: NONE
-
-  REQUIRED INCLUDES:
-
-      <stdio.h>
-      file.h
-      common.h
-
-  DESCRIPTION: see Purpose
-------------------------------------------------------------------*/

int FIL_read_real_8(FILE* in, FLOAT8* f8, BOOL little_endian)
{
   if (little_endian)
      return FIL_get_little_real_8(in, f8);
   else
      return FIL_get_big_real_8(in, f8);
}

int FIL_get_big_real_8(FILE *in, FLOAT8 *f8)
{
   return read_and_swap_8(in, f8);
}

int FIL_get_little_real_8(FILE *in, FLOAT8 *f8)
{
   if (1 != fread(f8, 8, 1, in))
      return FAILURE;
   else
      return SUCCESS;
}

/*------------------------------------------------------------------
-  FUNCTION NAME:        FIL_get_int_4
-  PROGRAMMER:           Rob Gue, Vinny Sollicito
-  DATE:                 January 1994
-
-  PURPOSE:
-
-      Read a 4-byte little endian integer value.
-
-  PARAMETERS:
-
-      in:               file pointer for file to read from
-
-      i4:               where to put the integer value
-
-  RETURN VALUES:
-
-      SUCCESS
-      FAILURE
-
-  PRECONDITIONS: NONE
-
-  EXTERNALS MODIFIED: NONE
-
-  REQUIRED INCLUDES:
-
-      <stdio.h>
-      common.h
-      file.h
-
-  DESCRIPTION: see Purpose
-------------------------------------------------------------------*/

int FIL_read_int_4(FILE *in, INT4 *i4, BOOL little_endian)
{
   if (little_endian)
      return FIL_get_little_int_4(in, i4);
   else
      return FIL_get_big_int_4(in, i4);
}

int FIL_get_big_int_4(FILE *in, INT4 *i4)
{
   return read_and_swap_4(in, i4);
}

int FIL_get_little_int_4(FILE *in, INT4 *i4)
{
   if (1 != fread(i4, 4, 1, in))
      return FAILURE;
   else
      return SUCCESS;
}

// ------------------------------------------------------------------

int FIL_read_uint_4(FILE *in, UINT4 *ui4, BOOL little_endian)
{
   if (little_endian)
      return FIL_get_little_uint_4(in, ui4);
   else
      return FIL_get_big_uint_4(in, ui4);
}

int FIL_get_big_uint_4(FILE *in, UINT4 *ui4)
{
   return read_and_swap_4(in, ui4);
}

int FIL_get_little_uint_4(FILE *in, UINT4 *ui4)
{
   if (1 != fread(ui4, 4, 1, in))
      return FAILURE;
   else
      return SUCCESS;
}

/*------------------------------------------------------------------
-  FUNCTION NAME:       FIL_get_int_2
-  PROGRAMMER:          Rob Gue, Vinny Sollicito
-  DATE:                January 1994
-
-  PURPOSE:
-
-      Read a 2-byte little endian integer value.
-
-  PARAMETERS:
-
-      in:              file pointer for the file to read from
-
-      i2:              where to put the interger value
-
-  RETURN VALUES:
-
-      SUCCESS
-      FAILURE
-
-  PRECONDITIONS: NONE
-
-  EXTERNALS MODIFIED: NONE
-
-  REQUIRED INCLUDES:
-
-     <stdio.h>
-     file.h
-     common.h
-
-  DESCRIPTION: see Purpose
-------------------------------------------------------------------*/

int FIL_read_int_2(FILE *in, INT2 *i2, BOOL little_endian)
{
   if (little_endian)
      return FIL_get_little_int_2(in, i2);
   else
      return FIL_get_big_int_2(in, i2);
}

int FIL_get_big_int_2(FILE *in, INT2 *i2)
{
   return read_and_swap_2(in, i2);
}

int FIL_get_little_int_2(FILE *in, INT2 *i2)
{
   if (1 != fread(i2, 2, 1, in))
      return FAILURE;
   else
      return SUCCESS;
}

// ------------------------------------------------------------------

int FIL_read_uint_2(FILE *in, UINT2 *ui2, BOOL little_endian)
{
   if (little_endian)
      return FIL_get_little_uint_2(in, ui2);
   else
      return FIL_get_big_uint_2(in, ui2);
}

int FIL_get_big_uint_2(FILE *in, UINT2 *ui2)
{
   return read_and_swap_2(in, ui2);
}

int FIL_get_little_uint_2(FILE *in, UINT2 *ui2)
{
   if (1 != fread(ui2, 2, 1, in))
      return FAILURE;
   else
      return SUCCESS;
}

/*------------------------------------------------------------------
-  FUNCTION NAME:       FIL_get_int_1
-  PROGRAMMER:          Rob Gue, Vinny Sollicito
-  DATE:                January 1994
-
-  PURPOSE:
-
-      Read a 1-byte integer value.
-
-  PARAMETERS:
-
-      in:              file pointer for the file to read from
-
-      i1:              where to put the interger value
-
-  RETURN VALUES:
-
-      SUCCESS
-      FAILURE
-
-  PRECONDITIONS: NONE
-
-  EXTERNALS MODIFIED: NONE
-
-  REQUIRED INCLUDES:
-
-     <stdio.h>
-     file.h
-     common.h
-
-  DESCRIPTION: see Purpose
-------------------------------------------------------------------*/

int FIL_get_int_1(FILE *in, INT1 *i1)
{
   if (1 != fread(i1, 1, 1, in))
      return FAILURE;
   else
      return SUCCESS;
}

int FIL_get_uint_1(FILE *in, UINT1 *ui1)
{
   if (1 != fread(ui1, 1, 1, in))
      return FAILURE;
   else
      return SUCCESS;
}

/*------------------------------------------------------------------
-  FUNCTION NAME:       FIL_mget_int_2
-  PROGRAMMER:          Rob Gue, Vinny Sollicito
-  DATE:                January 1994
-
-  PURPOSE:
-
-     Read multiple consecutive 2-byte little-endian integer values from a file.
-
-  PARAMETERS:
-
-      in:             file pointer for the file to read stuff from
-
-      data:           where to put the values read
-
-      item_count:     the number of 2-byte integer values to read
-
-  RETURN VALUES:
-
-      SUCCESS
-      FAILURE
-
-  PRECONDITIONS: NONE
-
-  EXTERNALS MODIFIED: NONE
-
-  REQUIRED INCLUDES:
-
-      <stdio.h>
-      file.h
-      common.h
-
-  DESCRIPTION: see Purpose
-------------------------------------------------------------------*/

size_t FIL_mread_int_2(FILE *in, INT2 *data, size_t item_count, 
   BOOL little_endian)
{
   if (little_endian)
      return FIL_mget_little_int_2(in, data, item_count);
   else
      return FIL_mget_big_int_2(in, data, item_count);
}

size_t FIL_mget_big_int_2(FILE *in, INT2 *data, size_t item_count)
{
   size_t num_items_read = FIL_mget_little_int_2(in, data, item_count);

   mbyteswap_2(data, (int)num_items_read);

   return num_items_read;
}

size_t FIL_mget_little_int_2(FILE *in, INT2 *data, size_t item_count)
{
   size_t num_items_read;

   num_items_read = fread(data, 2, item_count, in);

   return num_items_read;
} 

static
void mbyteswap_8(void *eight_byte_ptr, int count)
{
   int i=0;
   FLOAT8* ptr = (FLOAT8*) eight_byte_ptr;

   while (i<count)
   {
      byteswap_8(ptr++);
      i++;
   }
}

static
void mbyteswap_4(void *four_byte_ptr, int count)
{
   int i=0;
   INT4* ptr = (INT4*) four_byte_ptr;

   while (i<count)
   {
      byteswap_4(ptr++);
      i++;
   }
}

static
void mbyteswap_2(void *two_byte_ptr, int count)
{
   int i=0;
   INT2* ptr = (INT2*) two_byte_ptr;

   while (i<count)
   {
      byteswap_2(ptr++);
      i++;
   }
}

// ------------------------------------------------------------------
// ------------------------------------------------------------------

int FIL_put_big_real_4(FILE *in, FLOAT4 *data)
{
   int i;
   unsigned char *in_buf;
   unsigned char out_buf[4];

   in_buf = (unsigned char *)data; 

   for(i = 3; i >=0; i--)
      out_buf[3-i] = in_buf[i];

   if (4 != fwrite(out_buf, 1, 4, in))
      return FAILURE;

   return SUCCESS;
}

int FIL_put_little_real_4(FILE *in, FLOAT4 *data)
{
   if (1 != fwrite(data, 4, 1, in))
      return FAILURE;

   return SUCCESS;
}

// ------------------------------------------------------------------

int FIL_put_big_real_8(FILE *in, FLOAT8 *data)
{

   int i;
   unsigned char *in_buf;
   unsigned char out_buf[8];

   in_buf = (unsigned char *)data;

   for(i = 7; i >=0; i--)
      out_buf[7-i] = in_buf[i];

   if (8 != fwrite(out_buf, 1, 8, in))
      return FAILURE;

   return SUCCESS;
} 

int FIL_put_little_real_8(FILE *in, FLOAT8 *data)
{
   if (1 != fwrite(data, 8, 1, in))
      return FAILURE;

   return SUCCESS;
} 

// ------------------------------------------------------------------

int FIL_put_big_int_4(FILE *in, INT4 *data)  
{
   int i;
   unsigned char *in_buf;
   unsigned char out_buf[4];

   in_buf = (unsigned char *)data;

   for(i = 3; i >=0; i--)
      out_buf[3-i] = in_buf[i];

   if (4 != fwrite(out_buf, 1, 4, in))
      return FAILURE;

   return SUCCESS;
}

int FIL_put_little_int_4(FILE *in, INT4 *data)  
{
   if (1 != fwrite(data, 4, 1, in))
      return FAILURE;

   return SUCCESS;
}

// ------------------------------------------------------------------

int FIL_put_little_uint_4(FILE *in, UINT4 *data)  
{
   if (1 != fwrite(data, 4, 1, in))
      return FAILURE;

   return SUCCESS;
}

// ------------------------------------------------------------------

int FIL_put_big_int_2(FILE *in, INT2 *data)
{
   unsigned char *in_buf;
   unsigned char out_buf[2]; 

   in_buf = (unsigned char *)data; 

   out_buf[1] = in_buf[0]; 
   out_buf[0] = in_buf[1]; 

   if (2 != fwrite(out_buf, 1, 2, in)) 
      return FAILURE; 

   return SUCCESS;
}

int FIL_put_int_2(FILE *in, INT2 *data)
{
   if (1 != fwrite(data, 2, 1, in)) 
      return FAILURE; 

   return SUCCESS;  
}

// ------------------------------------------------------------------

int FIL_put_big_uint_2(FILE *in, UINT2 *data)
{
   unsigned char *in_buf;
   unsigned char out_buf[2]; 

   in_buf = (unsigned char *)data; 

   out_buf[1] = in_buf[0]; 
   out_buf[0] = in_buf[1]; 

   if (2 != fwrite(out_buf, 1, 2, in)) 
      return FAILURE; 

   return SUCCESS;
}

int FIL_put_uint_2(FILE *in, UINT2 *data)
{
   if (1 != fwrite(data, 2, 1, in)) 
      return FAILURE; 

   return SUCCESS;  
}

// ------------------------------------------------------------------

int FIL_put_int_1(FILE *in, INT1 *data)
{
   if (1 != fwrite(data, 1, 1, in))
      return FAILURE;

   return SUCCESS;
}

// ------------------------------------------------------------------
