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

// AttributeExpressions.cpp: implementation of the CAttributeExpressions class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "AttributeExpressions.h"
#include "DelimitedParser.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif


////////////////////////////////////////////////////////////////////////////////
// Method: CVPFFormatHandler
////////////////////////////////////////////////////////////////////////////////
CVPFFormatHandler::CVPFFormatHandler(LPCTSTR     szFormat,
                                     const TCHAR tcDelimiter)
                  : m_strFormat(szFormat),
                    m_nOrdinal(-1)
{
  // Set the first character
  *m_szLastEntry = _T('\0');

  // Now parse the format
  ParseFormat(tcDelimiter);
}

////////////////////////////////////////////////////////////////////////////////
// Method: ~CVPFFormatHandler
////////////////////////////////////////////////////////////////////////////////
CVPFFormatHandler::~CVPFFormatHandler()
{
  // Clean up
  RemoveAllMappings();
}

////////////////////////////////////////////////////////////////////////////////
// Method: SetFormat
////////////////////////////////////////////////////////////////////////////////
void CVPFFormatHandler::SetFormat(LPCTSTR     szFormat,
                                  const TCHAR tcDelimiter)
{
  // clear out what we had saved
  *m_szLastEntry = _T('\0');
  m_nOrdinal     = -1;

  // set up the new format
  m_strFormat = szFormat;

  // Now parse the format
  ParseFormat(tcDelimiter);
}

////////////////////////////////////////////////////////////////////////////////
// Method: GetFieldOrdinal
////////////////////////////////////////////////////////////////////////////////
long CVPFFormatHandler::GetFieldOrdinal(LPCTSTR szFieldName) const
{
  // Make sure we have something
  if (m_strFormat.IsEmpty())
  {
    return -1;
  }

  // See if this is our last entry
  if (0 == ::_tcscmp(m_szLastEntry, szFieldName))
  {
    return m_nOrdinal;
  }

  // We start looking again
  m_nOrdinal = -1;
  POSITION pos    = m_mappings.GetHeadPosition();

  // Either way lets save the last lookup
  CStringToOrdinal::CopyFieldString(m_szLastEntry, szFieldName);

  // Loop till we find it or the end
  while ((-1 == m_nOrdinal) && (NULL != pos))
  {
    // Get our next entry
    CStringToOrdinal* pStoO = m_mappings.GetNext(pos);
    if (0 == ::_tcscmp(m_szLastEntry, pStoO->String()))
    {
      // We found it
      m_nOrdinal = pStoO->Ordinal();
    }
  }  
    
  // return what we found
  return m_nOrdinal;

}

////////////////////////////////////////////////////////////////////////////////
// Method: ParseFormat
////////////////////////////////////////////////////////////////////////////////
void CVPFFormatHandler::ParseFormat(const TCHAR tcDelimiter)
{
  // get rid of everything
  RemoveAllMappings();

  // Make sure we have something
  if (m_strFormat.IsEmpty())
  {
    return;
  }

  // Now lets set up our first search
  long nOrdinal   = 0;
  long nCharIndex = 0;
  long nLength    = m_strFormat.GetLength();
  bool bEndFound  = false;
  LPCTSTR szTemp = m_strFormat;

  // Loop till we find the end
  while (!bEndFound)
  {
    // Find the next tab
    long nNextDelim = m_strFormat.Find(tcDelimiter, nCharIndex);

    // See if we found something
    if (-1 == nNextDelim)
    {
      // Copy the entry we must be at the end
      CStringToOrdinal* pStoO = 
        new CStringToOrdinal(szTemp + nCharIndex, 
                             nLength - nCharIndex,
                             nOrdinal);
      m_mappings.AddTail(pStoO);


      // We found the end
      bEndFound = true;
    }
    else
    {
      // We found a delimiter
      CStringToOrdinal* pStoO = new CStringToOrdinal(szTemp + nCharIndex, 
                                                     nNextDelim - nCharIndex,
                                                     nOrdinal);
      m_mappings.AddTail(pStoO);

      // Now bump over one
      nOrdinal++;

      // Either way move to that point
      nCharIndex = nNextDelim + 1;

      // See if we are at the end
      if (nCharIndex == nLength)
      {
        // We are at the end
        bEndFound = true;
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// Method: RemoveAllMappings
////////////////////////////////////////////////////////////////////////////////
void CVPFFormatHandler::RemoveAllMappings()
{
  // get rid of everything
  while (!m_mappings.IsEmpty())
  {
    CStringToOrdinal* pTail = m_mappings.RemoveTail();
    delete pTail;
    pTail = NULL;
  }
}

////////////////////////////////////////////////////////////////////////////////
// Method: GetOrdinalEntry
////////////////////////////////////////////////////////////////////////////////
CString CVPFFormatHandler::GetOrdinalEntry(LPCTSTR szValues,
                                           long    nOrdinal,
                                           TCHAR   tcDelimiter)
{
  // Loop till we get to the correct ordinal
  while ((NULL != szValues) && (*szValues != _T('\0')) && (nOrdinal > 0))
  {
    // Find our next delimiter
    szValues = ::_tcschr(szValues, tcDelimiter);

    if (NULL != szValues)
    {
      // We found another seperator decrement the ordinal
      nOrdinal--;

      // Now skip over it
      szValues++;
    }
  }

  // See what we have
  if ((NULL == szValues) || (0 != nOrdinal))
  {
    // Return an empty one
    return CString();
  }

  // Lets keep the beginning
  LPCTSTR szTemp = szValues;
  long    nLen   = 0;

  // Now lets find the end
  while ((*szValues != _T('\0')) && (*szValues != tcDelimiter))
  {
    // Increment our values
    szValues++;

    // We are larger by one
    nLen++;
  }

  // Build up our string
  CString strFormed(szTemp, nLen);

  // Return what we found
  return strFormed;
}

////////////////////////////////////////////////////////////////////////////////
// Method: GetValueForString
////////////////////////////////////////////////////////////////////////////////
bool CECDISValues::GetValueForString(LPCTSTR szAttName,
                                     double& dblValue) const
{
  bool bResult = true;

  // See if it is one of the special ones
  if (0 == ::_tcsncmp(szAttName, _T("isdm"), 4))
  {
    dblValue = ISDM();
  }
  else if (0 == ::_tcsncmp(szAttName, _T("idsm"), 4))
  {
    dblValue = IDSM();
  }
  else if (0 == ::_tcsncmp(szAttName, _T("ssdc"), 4))
  {
    dblValue = SSDC();
  }
  else if (0 == ::_tcsncmp(szAttName, _T("msdc"), 4))
  {
    dblValue = MSDC();
  }
  else if (0 == ::_tcsncmp(szAttName, _T("mssc"), 4))
  {
    dblValue = MSSC();
  }
  else
  {
    // Nothing matches so return a bad value
    bResult = false;;
  }

  // let them know how we did
  return bResult;
}

////////////////////////////////////////////////////////////////////////////////
// Method: GetValue
////////////////////////////////////////////////////////////////////////////////
const CAEValue* CAEAttributeList::GetValue(LPCTSTR szAttName) const
{
  // See if this is an easy one
  if (m_strLast == szAttName)
  {
    return &m_valLast;
  }

  // Our start of string
  LPCTSTR szStartOfString = NULL;

  // See if we have a format line
  if (NULL == m_pFormatHandler)
  {
    // No format handler so do it the old way look for our string 
    szStartOfString = ::_tcsstr(m_szAttList, szAttName);

    // See if we found something
    if (NULL != szStartOfString)
    {
      // Bump past the name
      szStartOfString += ::_tcslen(szAttName);

      // BAE originally searched for "nam"  Changing the search to "nam="
      // prevents the need to increment the pointer.  But, check it in case
      // we missed a call to this function
      if (_T('=') == *szStartOfString)
         // Bump past the = sign
         szStartOfString++;

      // This will assert in the case a search failed or a call was made to find "nam"
      ASSERT(_T('=') == *(szStartOfString-1));
    }
  }
  else
  {
     // We have a format handler look for our string in the format list
    long nOrdinal = m_pFormatHandler->GetFieldOrdinal(szAttName);

    // See if we have this
    if (-1 != nOrdinal)
    {
      // Lets find our ordinal
      szStartOfString = m_szAttList;

      // Loop till we get to the correct ordinal
      while ((_T('\0') != *szStartOfString) && (nOrdinal > 0))
      {
        // Find our next delimiter
        szStartOfString = ::_tcschr(szStartOfString, m_tcDelimiter);

        if (NULL != szStartOfString)
        {
          // We found another seperator decrement the ordinal
          nOrdinal--;

          // Now skip over it
          szStartOfString++;
        }
      }
    
      // See what we have
      if ((NULL == szStartOfString) || (0 != nOrdinal))
      {
        // Return an empty one we should never be here
        ASSERT(FALSE);
        return NULL;
      }
    }
  }

  // See if we have this
  if (NULL == szStartOfString)
  {
    // We will try to look for a ECDIS value
    double dblValue;

    if (m_values.GetValueForString(szAttName, dblValue))
    {     
      // Set our last entry
      m_strLast = szAttName;
  
      // We found it set up the value and then return it
      m_valLast.Init(dblValue);
      return &m_valLast;
    }
    else
    {
      // We did not find it
      return NULL;
    }
  }
  else
  {
    // Set our last entry since we found something
    m_strLast = szAttName;

    // See if we can find the end of string
    long nLength = 0;
    LPCTSTR szCurr = szStartOfString;

    while ((_T('\0') != *szCurr) && (m_tcDelimiter != *szCurr))
    {
      // Bump it forward
      nLength++;
      szCurr++;
    }

    // This is an easy one simply copy the rest
    m_valLast.Init(szStartOfString, nLength);
  }

  return &m_valLast;
}

////////////////////////////////////////////////////////////////////////////////
// Method: CAttributeExpression
////////////////////////////////////////////////////////////////////////////////
CAttributeExpression::CAttributeExpression(long    nConditionIndex)
                     : m_nConditionIndex(nConditionIndex),
                       m_arrConnectors(),
                       m_arrEntries()
{
}


////////////////////////////////////////////////////////////////////////////////
// Method: ~CAttributeExpression
////////////////////////////////////////////////////////////////////////////////
CAttributeExpression::~CAttributeExpression()
{
  long nCount = m_arrEntries.GetSize();

  // While we have more stuff
  for (long nIndex = 0; nIndex < nCount; nIndex++)
  {
    // Get the last entry
    CAEEntry* pEntry = m_arrEntries.GetAt(nIndex);
    delete pEntry;
  }

  // Now remove all of them
  m_arrConnectors.RemoveAll();
  m_arrEntries.RemoveAll();
}


////////////////////////////////////////////////////////////////////////////////
// Method: AddEntry
////////////////////////////////////////////////////////////////////////////////
void CAttributeExpression::AddEntry(AE_CONNECTORS conn,
                                    CAEEntry*     pEntry)
{
  // Add these guys to our array of entries
  m_arrConnectors.Add(conn);
  m_arrEntries.Add(pEntry);
}

////////////////////////////////////////////////////////////////////////////////
// Method: Evaluate
////////////////////////////////////////////////////////////////////////////////
bool CAttributeExpression::Evaluate(const CAEAttributeList& aeAttributes) const
{
  // See how we are doing so far
  bool bResult        = true;
  bool bStartingNew   = true;
  bool bNeedNextMajor = false;

  // Get the number of entries
  long nEntries = m_arrEntries.GetSize();
  AE_CONNECTORS connLast = AEC_NONE;
  
  // Evaluate till we fail
  for (long nIndex = 0; (nIndex < nEntries); nIndex++)
  {
    // Get the current connector
    AE_CONNECTORS connCurr = m_arrConnectors.GetAt(nIndex);
    
    // See if we need the next major 
    if (!bNeedNextMajor)
    {
      // Get this entry
      CAEEntry*     pEntry   = m_arrEntries.GetAt(nIndex);

      // Get the current result
      bool bCurrResult = pEntry->Evaluate(aeAttributes);

      // If this is the first one
      if (bStartingNew)
      {
        bResult      = bCurrResult;
        bStartingNew = false;
      }
      else if (AEC_or == connLast) // Not the first so do the last oper
      {
        bResult |= bCurrResult;
      }
      else if (AEC_and == connLast)
      {
         bResult &= bCurrResult;
      }
    }
       
    // Now process the results based on our last operation
    switch (connCurr)
    {
      case AEC_AND:
        // if we are false going into an AND we are toast
        if (!bResult)
        {
          // We cannot succeed
          bResult        = false;
          bNeedNextMajor = true;
        }
        else
        {
          // We are starting the next phase
          bStartingNew   = true;
          bNeedNextMajor = false;
        }
        break;
 
      case AEC_OR:
        // If we are true going into an OR we are done
        if (bResult)
        {
          // We can succeed
          bResult        = true;
          bNeedNextMajor = true;
        }
        else
        {
          // We are starting the next phase
          bStartingNew   = true;
          bNeedNextMajor = false;
        }
        break;
    }

    // Now save our last connection type
    connLast = connCurr;
  }

  // Return what we found
  return bResult;
}

////////////////////////////////////////////////////////////////////////////////
// Method: Compare
////////////////////////////////////////////////////////////////////////////////
inline
bool CAEValue::Compare(const CAEValue& other,
                       AE_OPERATIONS   oper) const
{
  // Check the easy ones first
  if ((AEVT_LONG == m_aeValueType) 
   || (AEVT_DOUBLE == m_aeValueType))
  {
    // Check our comparison operators
    if (AEO_EQUAL == oper)
    {
      return (m_dblValue == other.m_dblValue);
    }
    else if (AEO_NOT_EQUAL == oper)
    {
      return (m_dblValue != other.m_dblValue);
    }
    else if (AEO_LESS_THAN == oper)
    {
      return (m_dblValue < other.m_dblValue);
    }
    else if (AEO_GREATER_THAN == oper)
    {
      return (m_dblValue > other.m_dblValue);
    }
    else if (AEO_LESS_THAN_OR_EQUAL == oper)
    {
      return (m_dblValue <= other.m_dblValue);
    }
    else
    {
      return (m_dblValue >= other.m_dblValue);
    }
  }
  else
  {
    // Must be a string
    if (AEO_EQUAL == oper)
    {
      return operator==(other);
    }
    else if (AEO_NOT_EQUAL == oper)
    {
      return operator!=(other);
    }
    else
    {
      return false;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// Method: Convert
////////////////////////////////////////////////////////////////////////////////

void CAEValue::Convert()
{
   // Lets see what the string is made of
  if (!m_strValue.IsEmpty())
  {
   // We have something see if it is an attribute
    TCHAR tc = m_strValue[0];
    TCHAR tcQuote = _T('"');
    if (tcQuote == tc)
    {
      // We have a string
      m_strValue.TrimLeft(tcQuote);
      m_strValue.TrimRight(tcQuote);
    } 
    else if (!::IsCharAlpha(tc))
    {  
      // Attempt to find a dot
      if (-1 == m_strValue.Find(_T('.')))
      {
        // we are numeric at least to start with try to convert it
        TCHAR* szEnd;
        long nValue = ::_tcstol(m_strValue, &szEnd, 10);

        // Make sure the end is the end
        if (_T('\0') == *szEnd)
        {
          // We found the end so this is numeric
          m_dblValue    = nValue;
          m_aeValueType = AEVT_LONG;
        }
      }
      else
      {
        // we are numeric at least to start with try to convert it
        TCHAR* szEnd;
        double dblValue = ::_tcstod(m_strValue, &szEnd);

        // Make sure the end is the end
        if (_T('\0') == *szEnd)
        {
          // We found the end so this is numeric
          m_dblValue    = dblValue;
          m_aeValueType = AEVT_DOUBLE;
        }
      }
    }
    else if (::IsCharLower(tc))
    {
      // We assume all lower case is an attribute
      m_aeValueType = AEVT_ATTRIB;
    }
    
    else if (m_strValue == _T("NULL"))
    {
      // Empty the string
      m_dblValue = 0.0;
      m_strValue.Empty();
    }
  }
}


////////////////////////////////////////////////////////////////////////////////
// Method: CAttributeExpressions
////////////////////////////////////////////////////////////////////////////////
CAttributeExpressions::CAttributeExpressions()
                      : m_arrExpressions()
{
}

////////////////////////////////////////////////////////////////////////////////
// Method: ~CAttributeExpressions
////////////////////////////////////////////////////////////////////////////////
CAttributeExpressions::~CAttributeExpressions()
{
  // Get rid of all of the entries
  RemoveAll();
}

////////////////////////////////////////////////////////////////////////////////
// Method: AddEntry
////////////////////////////////////////////////////////////////////////////////
void CAttributeExpressions::AddEntry(CAttributeExpression* pEntry)
{
  // A little sanity check
  ASSERT(NULL != pEntry);

  // See how far we are in there
  long nCurrIndex    = m_arrExpressions.GetSize();
  long nDesiredIndex = pEntry->ConditionIndex();
  ASSERT(0 <= nDesiredIndex);
  ASSERT(nCurrIndex <= nDesiredIndex);

  // Loop till we get there
  while (nCurrIndex < nDesiredIndex)
  {
    // Add an empty entry
    m_arrExpressions.Add(NULL);

    // Bump our index forward
    nCurrIndex++;
  }

  // We need to now add our entry
  m_arrExpressions.Add(pEntry);
}

////////////////////////////////////////////////////////////////////////////////
// Method: RemoveAll
////////////////////////////////////////////////////////////////////////////////
void CAttributeExpressions::RemoveAll()
{
  long nCount = m_arrExpressions.GetSize();

  // While we have more stuff
  for (long nIndex = 0; nIndex < nCount; nIndex++)
  {
    // Get the last entry
    CAttributeExpression* pEntry = m_arrExpressions.GetAt(nIndex);
    delete pEntry;
  }

  // Now clear out the list
  m_arrExpressions.RemoveAll();
}

////////////////////////////////////////////////////////////////////////////////
// Method: Open
////////////////////////////////////////////////////////////////////////////////
bool CAttributeExpressions::Open(LPCTSTR szFileName)
{
  CDelimitedParser delimParser;

  // Lets open our file
  if (!delimParser.Open(szFileName))
  {
    // Trouble opening the file
    TRACE(_T("CAttributeExpressions::Open: Could not open file: %s\n"),
          szFileName);

    return false;
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
    TRACE(_T("CAttributeExpressions::Open: Could not read file: %s\n"),
          szFileName);

    return false;
  }

  // Build up our FACC entries
  CAttributeExpression* pAttExpr = NULL;

  CString  strLastFACC;
  bool     bLineIsGood = true;

   long    nCondIndex;
   long    nSequence;
   LPCTSTR strAttribute;
   long    nOperation;
   LPCTSTR strValue;
   long    nConnector;

  // Read all of our next lines
  while (bLineIsGood && delimParser.ReadLine())
  {
    // Assume nothing
    bLineIsGood = false;

    if (delimParser.ParseLong(nCondIndex)        // Condition index
     && delimParser.ParseLong(nSequence)         // Sequence
     && delimParser.ParseString(strAttribute)    // Attribute
     && delimParser.ParseLong(nOperation)        // Operation
     && delimParser.ParseString(strValue)        // Value
     && delimParser.ParseLong(nConnector))       // Connector
    {
      // Well it looks like things are good
      bLineIsGood = true;

      // Lets see if we need a new one
      if ((NULL == pAttExpr) 
       || (pAttExpr->ConditionIndex() != nCondIndex))
      {
        // Create a new entry
        pAttExpr = new  CAttributeExpression(nCondIndex);

        // Now add the actual entry
        AddEntry(pAttExpr);
      }

      // Now add on the expression
      CAEValue  value(strValue);
      CAEEntry* pAEEntry = new CAEEntry(strAttribute,
        static_cast<CAEValue::AE_OPERATIONS>(nOperation),
        value);

      // Now add on this entry
      pAttExpr->AddEntry(
        static_cast<CAttributeExpression::AE_CONNECTORS>(nConnector),
        pAEEntry);
    }
  }

  // See if we had any troubles
  if (!bLineIsGood)
  {
    // Trouble parsine the file
    TRACE(_T("CAttributeExpressions::Open: Could not parse file: %s\n"),
          szFileName);

    // Indicate to the user things are bad
    return false;
  }

  // If we are here we passed
  return true;
}


////////////////////////////////////////////////////////////////////////////////
// Method: Evaluate
////////////////////////////////////////////////////////////////////////////////
bool CAttributeExpressions::Evaluate(long                    nIndex,
                                     const CAEAttributeList& aeAttributes) const
{
  if (nIndex < 0)
     return true;

  // Make sure this index is not past the end
  if (m_arrExpressions.GetSize() <= nIndex)
  {
    // We do not have an entry here so we pass
    return true;
  }

  // We may have an entry lets see
  const CAttributeExpression* pEntry = m_arrExpressions.GetAt(nIndex);

  // If we don't have an entry again we passed
  if (NULL == pEntry)
  {
    return true;
  }

  // Let the entry decide
  bool bResult = pEntry->Evaluate(aeAttributes);

  return bResult;
}
          
