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


////////////////////////////////////////////////////////////////////////////////
// DelimitedParser.cpp : Implementation of the CDelimitedParser class.
//
// Description: Implementation of our parsing class
//
// Author:      Louis Framarini, Jr., Created: 06/11/03
////////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "DelimitedParser.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// Method: CDelimitedParser
////////////////////////////////////////////////////////////////////////////////
CDelimitedParser::CDelimitedParser(TCHAR tcDelimiter)
                 : m_file(),
                   m_bOpen(false),
                   m_nStringLength(0),
                   m_Token(NULL),
                   m_TokenContext(NULL)
{
   m_tcDelimiter[0] = tcDelimiter;
   m_tcDelimiter[1] = 0;
}

////////////////////////////////////////////////////////////////////////////////
// Method: ~CDelimitedParser
////////////////////////////////////////////////////////////////////////////////
CDelimitedParser::~CDelimitedParser()
{
  // Nothing to do everything cleans up on it's own
}

////////////////////////////////////////////////////////////////////////////////
// Method: Open
////////////////////////////////////////////////////////////////////////////////
bool CDelimitedParser::Open(LPCTSTR szFileName)
{
  // We should never be here and already be open
  ASSERT(!m_bOpen);

  // Close it just in case
  Close();

  // Now attempt to open the file
  if (!m_file.Open(szFileName, CFile::modeRead | CFile::shareDenyWrite | CFile::osSequentialScan))
  {
    // We failed 
    return false;
  }

  // Indicate that we are open
  m_bOpen = true;

  // We did it return true
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// Method: Close
////////////////////////////////////////////////////////////////////////////////
void CDelimitedParser::Close()
{
  // If we are already open
  if (m_bOpen)
  {
    // Then shut it down
    m_file.Close();
    m_bOpen = false;
  }
}

////////////////////////////////////////////////////////////////////////////////
// Method: SkipField
////////////////////////////////////////////////////////////////////////////////
bool CDelimitedParser::SkipField()
{
   // First see if it is empty
   if (m_bOpen)
   {
      LPCTSTR ignore;
      ParseString(ignore);

      return true;
   }
   else
   {
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
// Method: ParseString
////////////////////////////////////////////////////////////////////////////////
bool CDelimitedParser::ParseString(LPCTSTR& strCurrent)
{
   // First see if it is empty
   if (m_bOpen)
   {
      if (m_Token == NULL)
      {
         m_TokenContext = NULL;
         m_Token = ::strtok_s(m_strCurrent, m_tcDelimiter, &m_TokenContext);
      }
      else
      {
         if (*m_TokenContext == *m_tcDelimiter)
         {
            m_TokenContext++;
            m_Token = "";
         }
         else
         {
            m_Token = strtok_s(NULL, m_tcDelimiter, &m_TokenContext);
         }
      }

      if (m_Token == NULL)
      {
         strCurrent = "";
      }
      else
      {
         strCurrent = m_Token;
      }

      return true;
   }
   else
   {
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
// Method: ParseLong
////////////////////////////////////////////////////////////////////////////////
bool CDelimitedParser::ParseLong(long&  nValue,
                                 bool   bOptional)
{
  // Default the value
  nValue = 0;

  // First get our current string
  LPCTSTR strCurrent;
  if (!ParseString(strCurrent))
  {
    // Nothing there
    return false;
  }

  // Now convert the current value
  TCHAR* szEnd;
  nValue = ::_tcstol(strCurrent, &szEnd, 10);

  if (nValue != 0 || strCurrent[0] == '0')
     return true;

  // Make sure the end is the end
  if (_T('\0') != *szEnd)
  {
    // Indicate we had an error if it is required
    if (!bOptional)
    {
      return false;
    }
    else
    {
      nValue = -1;
    }
  }

  // As always if we are here things are good
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// Method: ParseDouble
////////////////////////////////////////////////////////////////////////////////
bool CDelimitedParser::ParseDouble(double&  dblValue,
                                   bool   bOptional)
{
  // Default the value
  dblValue = 0.0;

  // First get our current string
  LPCTSTR strCurrent;
  if (!ParseString(strCurrent))
  {
    // Nothing there
    return false;
  }

  // Now convert the current value
  TCHAR* szEnd;
  dblValue = ::_tcstod(strCurrent, &szEnd);

  if (dblValue != 0)
     return true;

  // Make sure the end is the end
  if (_T('\0') != *szEnd)
  {
    // Indicate we had an error if it is required
    if (!bOptional)
    {
      return false;
    }
    else
    {
      dblValue = -1.0;
    }
  }

  // As always if we are here things are good
  return true;
}

