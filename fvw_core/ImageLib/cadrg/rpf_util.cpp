// Copyright (c) 1994-2011 Georgia Tech Research Corporation, Atlanta, GA
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



#include "stdafx.h"
#include "rpf_util.h"

int read_auint_2(FILE* fp, size_t field_size, UINT2* val)
{
   const size_t MAX_FIELD_SIZE = 128;

   char field[MAX_FIELD_SIZE+1];   

   if (field_size > MAX_FIELD_SIZE)
   {
      // field too small
      return FAILURE;
   }

   if (field_size != fread(field, 1, field_size, fp))
   {
      // fread failed
      return FAILURE;
   }
   field[field_size] = '\0';

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   if (sscanf_s(field, "%hu", val) != 1)
   {
      // sscanf failed
      return FAILURE;
   }

   return SUCCESS;
}

int read_auint_4(FILE* fp, size_t field_size, UINT4* val)
{
   const size_t MAX_FIELD_SIZE = 128;

   char field[MAX_FIELD_SIZE+1];   

   if (field_size > MAX_FIELD_SIZE)
   {
      // field too small
      return FAILURE;
   }

   if (field_size != fread(field, 1, field_size, fp))
   {
      // fread failed
      return FAILURE;
   }
   field[field_size] = '\0';

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
   if (sscanf_s(field, "%u", val) != 1)
   {
      // sscanf failed
      return FAILURE;
   }

   return SUCCESS;
}
