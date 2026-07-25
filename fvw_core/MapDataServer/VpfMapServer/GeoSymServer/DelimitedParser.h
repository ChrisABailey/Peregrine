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
// DelimitedParser.h: Header for the CDelimitedParser class.
//
// Description: This file will assist in parsing a delimited file.
//             
// Author:      Louis Framarini, Jr., Created: 06/11/03
////////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_DELIMITEDPARSER_H__1B1D7BB2_3C19_4D66_A08D_9669D204D87A__INCLUDED_)
#define AFX_DELIMITEDPARSER_H__1B1D7BB2_3C19_4D66_A08D_9669D204D87A__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#define MAX_LINESIZE 1024

class CDelimitedParser  
{
public:

  //////////////////////////////////////////////////////////////////////////////
  //Method:      CDelimitedParser
  //Description: Standard constructor
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
   CDelimitedParser(TCHAR tcDelimiter = _T('|'));

  //////////////////////////////////////////////////////////////////////////////
  //Method:      ~CDelimitedParser
  //Description: Standard destructor
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
   virtual ~CDelimitedParser();

  //////////////////////////////////////////////////////////////////////////////
  //Method:      Open
  //Description: Opens the given file name
  //Arguments:   szFileName - the file to open
  //Returns:     bool - true if the file is open and false otherwise
  //////////////////////////////////////////////////////////////////////////////
  bool Open(LPCTSTR szFileName);

  //////////////////////////////////////////////////////////////////////////////
  //Method:      Close
  //Description: Closes the open file if one exists
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
  void Close();

  //////////////////////////////////////////////////////////////////////////////
  //Method:      ReadLine
  //Description: Reads the next line.
  //Arguments:   N/A
  //Returns:     bool - tue
  //////////////////////////////////////////////////////////////////////////////
  bool ReadLine();

  //////////////////////////////////////////////////////////////////////////////
  //Method:      CurrentText
  //Description: Returns the current text within the file
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
  inline const TCHAR* CurrentText() const;

  //////////////////////////////////////////////////////////////////////////////
  //Method:      SkipField
  //Description: Skips over to the next field
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
  bool SkipField();

  //////////////////////////////////////////////////////////////////////////////
  //Method:      ParseString
  //Description: This method will parse the current string to 
  //             get the current string. It will push the source
  //             string pass the next tab or empty it if at the end.
  //Arguments:   strCurrent - the parse out string
  //Returns:     bool - true if this found another string and false otherwise.
  //////////////////////////////////////////////////////////////////////////////
  bool ParseString(LPCTSTR& strCurrent);

  //////////////////////////////////////////////////////////////////////////////
  //Method:      ParseDouble
  //Description: This method will parse the current string
  //             for the next entry and then attempt to convert it 
  //             to a long.
  //Arguments:   nValue  - the new value
  //Returns:     bool - true if this found another long and false otherwise.
  //////////////////////////////////////////////////////////////////////////////
  bool ParseLong(long&  nValue,
                 bool   bOptional = false);

  //////////////////////////////////////////////////////////////////////////////
  //Method:      ParseDouble
  //Description: This method will parse the current string
  //             for the next entry and then attempt to convert it 
  //             to a double.
  //Arguments:   dblValue  - the new value
  //Returns:     bool - true if this found another double and false otherwise.
  //////////////////////////////////////////////////////////////////////////////
  bool ParseDouble(double&  dblValue,
                   bool     bOptional = false);

protected:


  //////////////////////////////////////////////////////////////////////////////
  //Method:      IsEmpty
  //Description:
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
  bool inline IsEmpty() const;

  //////////////////////////////////////////////////////////////////////////////
  //Method:      Empty
  //Description:
  //Arguments:   N/A
  //Returns:     N/A
  //////////////////////////////////////////////////////////////////////////////
  void inline Empty();

  TCHAR      m_tcDelimiter[2];
  TCHAR      m_strCurrent[MAX_LINESIZE];
  CStdioFile m_file;
  bool       m_bOpen;
  long       m_nCurIndex;
  long       m_nStringLength;

  TCHAR*     m_Token;
   
  TCHAR*     m_TokenContext;


private:

  //
  // Methods that will not be implemented
  //
  CDelimitedParser(const CDelimitedParser& other);
  CDelimitedParser& operator=(const CDelimitedParser& other);
  bool operator==(const CDelimitedParser& other) const;
  bool operator!=(const CDelimitedParser& other) const;
};

//
// Inline methods
//

//////////////////////////////////////////////////////////////////////////////
//Method:      ReadLine
//////////////////////////////////////////////////////////////////////////////  
inline
bool CDelimitedParser::ReadLine()
{
  // Make sure we are open
  if (!m_bOpen)
  {
    return false;
  }

  // Now read the file
  if (!m_file.ReadString(m_strCurrent, MAX_LINESIZE))
  {
    return false;
  }

  // Set our index back
  m_nCurIndex = 0;
  m_Token = NULL;

  // Get the length of the string
  m_nStringLength = ::strnlen_s(m_strCurrent, MAX_LINESIZE);

  ASSERT(m_nStringLength < MAX_LINESIZE);

  // Indicate things are good
  return true;
}

//////////////////////////////////////////////////////////////////////////////
//Method:      CurrentText
//////////////////////////////////////////////////////////////////////////////  
inline
const TCHAR* CDelimitedParser::CurrentText() const
{
  return (const TCHAR*)m_strCurrent;
}

#endif // !defined(AFX_DELIMITEDPARSER_H__1B1D7BB2_3C19_4D66_A08D_9669D204D87A__INCLUDED_)
