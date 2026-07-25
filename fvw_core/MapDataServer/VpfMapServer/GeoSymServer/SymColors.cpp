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
// SymColors.cpp : Implementation of the CSymColors class.
//
// Description: Holds the list of colors in the color table.
//
// Author:      Louis Framarini, Jr., Created: 06/19/03
////////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "SymColors.h"
#include "DelimitedParser.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//
// Our threshold away from black
//

const long      CSymColors::s_nBlackThreshold   = 5;
const COLORREF  CSymColors::s_clrBlackThreshold = RGB(5, 5, 5);

////////////////////////////////////////////////////////////////////////////////
// Method: CSymColors
////////////////////////////////////////////////////////////////////////////////
CSymColors::CSymColors()
           : m_arrColors()
{
}

////////////////////////////////////////////////////////////////////////////////
// Method: ~CSymColors
////////////////////////////////////////////////////////////////////////////////
CSymColors::~CSymColors()
{

}

////////////////////////////////////////////////////////////////////////////////
// Method: Open
////////////////////////////////////////////////////////////////////////////////
bool CSymColors::Open(LPCTSTR szFileName)
{
  CDelimitedParser delimParser;

  // Lets open our file
  if (!delimParser.Open(szFileName))
  {
    // Trouble opening the file
    TRACE(_T("CSymColors::Open: Could not open file: %s\n"),
          szFileName);

    return NULL;
  }

  // Now lets parse the file start by skipping over till we find
  // a line that starts with a semicolon
  bool bSemiFound = false;

  // See if this line is our semicolon
  while (!bSemiFound && delimParser.ReadLine())
  {
    if (delimParser.CurrentText()[0] == _T(';'))
    {
      bSemiFound = true;
    }
  }

  // If the semicolon was not found we have an error
  if (!bSemiFound)
  {
    // Trouble reading the file
    TRACE(_T("CSymColors::Open: Could not read file: %s\n"),
          szFileName);

    return NULL;
  }

  // Assume the line is good
  bool     bLineIsGood = true;

  // Read all of our next lines
  while (bLineIsGood && delimParser.ReadLine())
  {
    // Assume nothing
    bLineIsGood = false;

    long    nIndex;
    long    nRedComp;
    long    nGreenComp;
    long    nBlueComp;
   
    // Now attempt to parse each line
    if (delimParser.ParseLong(nIndex)            // Index
     && delimParser.SkipField()                  // name
     && delimParser.ParseLong(nRedComp)          // Red
     && delimParser.ParseLong(nGreenComp)        // Green
     && delimParser.ParseLong(nBlueComp))        // Blue
    {
      // Just in case
      ASSERT(m_arrColors.GetSize() == nIndex);

      // Well it looks like things are good
      bLineIsGood = true;

      // Add this one to the list
      m_arrColors.Add(RGB(nRedComp, nGreenComp, nBlueComp));
    }
  }

  // See if we had any troubles
  if (!bLineIsGood)
  {
    // Trouble parsine the file
    TRACE(_T("CSymColors::Open: Could not parse file: %s\n"),
          szFileName);

    // Indicate to the user things are bad
    return false;
  }

  // If we are here we passed
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// Method: CSymColorAdjuster::Setup
////////////////////////////////////////////////////////////////////////////////
void CSymColorAdjuster::Setup(int nBrightness, int nContrast, bool ReplaceFill, COLORREF FillColor, COLORREF OrigColor)
{
   // Set the brightness and contrast member variables.
   m_nBrightness = nBrightness;
   m_nContrast = nContrast;

   m_bReplaceFill = ReplaceFill;
   m_FillColor = FillColor & 0x00ffffff; // Force to a legal COLORREF value
   m_OrigColor = OrigColor & 0x00ffffff;

   // Short-circuit on zero adjustment of both values.
   m_bAdjust = (m_nBrightness != 0) || (m_nContrast != 0);

   if (m_bAdjust) 
   {
      // Perform range validation (-100..+100 for both values).
      if (m_nBrightness > 100)
         m_nBrightness = 100;
      else if (m_nBrightness < -100)
         m_nBrightness = -100;

      if (m_nContrast > 100)
         m_nContrast = 100;
      else if (m_nContrast < -100)
         m_nContrast = -100;

      // Adjust the intensity level (must be done before contrast).
      double nFactor = abs(m_nBrightness) / 100.0;
      if (m_nBrightness < 0)
         nFactor = 1.0 - nFactor;

      int i;
      for (i = 0; i < NUM_COLORS; i++) 
      {
         if (m_nBrightness >= 0)
            m_table[i] = i + (unsigned char)((255 - i) * nFactor);
         else
            m_table[i] = (unsigned char)(i * nFactor);
      }

      // Now adjust the contrast level if non-zero (must be done after intensity).
      if (m_nContrast != 0) 
      {
         double ratioContrast;
         if (m_nContrast > 0)
            ratioContrast = (m_nContrast / 25.0) + 1.0;
         else
            ratioContrast = (m_nContrast + 100.0) / 100.0;

         for (int i = 0; i < NUM_COLORS; i++ ) 
         {
            int r = (int)(ratioContrast * (m_table[i] - 128) + 128);
            if (r > 255) r = 255; else if (r < 0) r = 0;
            m_table[i] = (unsigned char)(r);
         }
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
// Method: CSymColorAdjuster::Adjust
////////////////////////////////////////////////////////////////////////////////
COLORREF CSymColorAdjuster::Adjust(COLORREF rgb) const
{
   // Short-circuit if no adjustment is necessary.
   if (!m_bAdjust) 
   {
      return (rgb);
   }

   //#define AVOID_PROXIMITY_TO_WHITE

   const BYTE THRESHOLD = 250;

   // Decompose the RGB into individual values.
   BYTE r = min(GetRValue(rgb), THRESHOLD);
   BYTE g = min(GetGValue(rgb), THRESHOLD);
   BYTE b = min(GetBValue(rgb), THRESHOLD);

   // Return the adjusted color.
   //#ifdef AVOID_PROXIMITY_TO_WHITE
      return (RGB(m_table[r], m_table[g], m_table[b]));
   //#else
   // return (RGB(m_table[r], m_table[g], m_table[b]));
   //#endif
}
