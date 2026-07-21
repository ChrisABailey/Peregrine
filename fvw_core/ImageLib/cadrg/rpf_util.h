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



#ifndef RPF_UTIL_H
#define RPF_UTIL_H 1

#include "file.h"

/*
 *  read and convert an ascii field to an UINT2
 */
int read_auint_2(FILE* fp, size_t field_size, UINT2* val);

/*
 *  read and convert an ascii field to an UINT4
 */
int read_auint_4(FILE* fp, size_t field_size, UINT4* val);

class rpf_string
{

private:

   char* m_string;
   rpf_string();  // Block default constructor
   unsigned short m_length;

public:

   rpf_string(unsigned int len)
   {
      m_length = len;

      // allow for null character on end of string
      m_string = new char[m_length + 1];

      // Set the string to empty
      m_string[0] = '\0';

      // terminate the string, this will not be overwritten
      m_string[m_length] = '\0';
   }

   ~rpf_string()
   {
      delete [] m_string;
   }

   operator const char*() const { return (const char*) m_string; }

   int read(FILE* fp)
   {
      if (fread(m_string, 1, m_length, fp) != m_length)
         // fail if full string was not read
         return FAILURE;

      return SUCCESS;
   }
}; 

#endif