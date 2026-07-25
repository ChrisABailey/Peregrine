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
// SymColors.h: Header for the CSymColors class.
//
// Description: Interface for the color lookup table used by the
//              symbols.
//
// Author:      Louis Framarini, Jr., Created: 06/19/03
////////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_SYMCOLORS_H__D485EF7E_2CA0_40C0_84D3_290E71074775__INCLUDED_)
#define AFX_SYMCOLORS_H__D485EF7E_2CA0_40C0_84D3_290E71074775__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifdef _WIN32
#include <afxtempl.h>
#endif

class CSymColors
{
public:

  //////////////////////////////////////////////////////////////////////////////
  //Method:      CSymColors
  //Description:
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
	CSymColors();

  //////////////////////////////////////////////////////////////////////////////
  //Method:      ~CSymColors
  //Description:
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
	virtual ~CSymColors();

  //////////////////////////////////////////////////////////////////////////////
  //Method:      Open
  //Description:
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
  bool Open(LPCTSTR szFileName);

  //////////////////////////////////////////////////////////////////////////////
  //Method:      GetColor
  //Description:
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
  COLORREF GetColor(long nIndex) const;

  static void AdjustForBlack(COLORREF& clr);

protected:

  CArray<COLORREF, COLORREF>   m_arrColors;

  static const long            s_nBlackThreshold;
  static const COLORREF        s_clrBlackThreshold;

};

//
// Inline methods
//

////////////////////////////////////////////////////////////////////////////////
// Method: GetColor
////////////////////////////////////////////////////////////////////////////////
inline
COLORREF CSymColors::GetColor(long nIndex) const
{
  // See if we have enough
  if (nIndex >= m_arrColors.GetSize())
  {
    return RGB(0, 0, 0);
  }

  // We have a valid index
  return m_arrColors.GetAt(nIndex);
}

////////////////////////////////////////////////////////////////////////////////
// Method: GetColor
////////////////////////////////////////////////////////////////////////////////
inline
void CSymColors::AdjustForBlack(COLORREF& clr)
{
  long nRed             = GetRValue(clr);
  long nGreen           = GetGValue(clr);
  long nBlue            = GetBValue(clr);
  if ((s_nBlackThreshold > nRed) 
   && (s_nBlackThreshold > nGreen) 
   && (s_nBlackThreshold > nBlue))
  {
    clr = s_clrBlackThreshold;
  }
}

class CSymColorAdjuster
{
public:
	
	//////////////////////////////////////////////////////////////////////////////
	//Method:      CSymColorAdjuster
	//Description:
	//Arguments:   N/A
	//Returns:     N/A
	//////////////////////////////////////////////////////////////////////////////
	CSymColorAdjuster(int nBrightness = 0, int nContrast = 0, bool ReplaceFill=false, COLORREF FillColor=RGB(0xff,0,0), COLORREF OrigColor=RGB(0xff,0,0))
	{ 
		m_bAdjust = (m_nBrightness != 0) || (m_nContrast != 0);

		m_bReplaceFill = ReplaceFill;
		m_FillColor = FillColor & 0x00ffffff; // Force to a legal COLORREF value
		m_OrigColor = OrigColor & 0x00ffffff;

		if (m_bAdjust)
			Setup(nBrightness, nContrast, m_bReplaceFill, m_FillColor, m_OrigColor); 
	}
	
	//////////////////////////////////////////////////////////////////////////////
	//Method:      Setup
	//Description:
	//Arguments:   nBrightness - brightness adjustment factor (-100..+100, 0 = none)
	//             nContrast   - contrast adjustment factor (-100..+100, 0 = none)
	//Returns:     N/A
	//////////////////////////////////////////////////////////////////////////////
	void Setup(int nBrightness = 0, int nContrast = 0, bool ReplaceFill=false, COLORREF FillColor=RGB(0xff,0,0), COLORREF OrigColor=RGB(0xff,0,0));
	
	//////////////////////////////////////////////////////////////////////////////
	//Method:      GetBrightness
	//Description:
	//Arguments:   N/A
	//Returns:     The brightness adjustment factor (range -100 to +100, 0 = none)
	//////////////////////////////////////////////////////////////////////////////
	int GetBrightness() const { return (m_nBrightness); }
	
	//////////////////////////////////////////////////////////////////////////////
	//Method:      GetContrast
	//Description:
	//Arguments:   N/A
	//Returns:     The contrast adjustment factor (range -100 to +100, 0 = none)
	//////////////////////////////////////////////////////////////////////////////
	int GetContrast() const { return (m_nContrast); }
	
	//////////////////////////////////////////////////////////////////////////////
	//Method:      Adjust
	//Description:
	//Arguments:   rgb - the RGB color value to adjust
	//Returns:     The RGB color value with brightness/contrast applied
	//////////////////////////////////////////////////////////////////////////////
	COLORREF Adjust(COLORREF rgb) const;
	
	//////////////////////////////////////////////////////////////////////////////
	//Method:      GetFillColor
	//Description:
	//Arguments:   rgb - the original RGB color value
	//Returns:     replaced fill color if specifiec
	//////////////////////////////////////////////////////////////////////////////
	COLORREF GetFillColor(COLORREF rgb) const 
	{
		return (m_bReplaceFill && rgb==m_OrigColor) ? m_FillColor : rgb; 
	}
	
private:
	
	enum { NUM_COLORS = 256 };
	
	bool m_bAdjust;			// flag to indicate if an adjustment is necessary
	int  m_nBrightness;		// the brightness adjustment -100..100, default is 0
	int  m_nContrast;		// the contrast adjustment -100..100, default is 0
	unsigned char m_table[NUM_COLORS]; // the pre-computed color conversion table

	bool m_bReplaceFill;	// Flag on whether icon fill color should be replaced
	COLORREF m_FillColor;	// Fill color to use when ReplaceFill flag is set
	COLORREF m_OrigColor;	// Original Fill color - Not all fills are replaced, be specific
};

#endif // !defined(AFX_SYMCOLORS_H__D485EF7E_2CA0_40C0_84D3_290E71074775__INCLUDED_)
