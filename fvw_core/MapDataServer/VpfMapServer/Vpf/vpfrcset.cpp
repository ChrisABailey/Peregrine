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

#include "stdafx.h"
#include "vpfrcset.h"
#include "indexes.h"
#include "tables.h"
#include "variant.h"
#include "vpfdb.h"
#include "math.h"    // for floor()
#ifdef _WIN32
#include "ComErrorObject.h"
#endif  // POSIX: fv_compat provides the error macros

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif



/*
#define THROW_VPF_EXCEPTION(err_msg)\
{\
   VPFException* e = new VPFException;\
   e->m_error_message = err_msg;\
   e->m_error_type = CFileException::fileNotFound;\
   throw e;\
}
*/

#ifdef _WIN32
char* GetAnsiString(const CString &s, UINT nCodePage) 
{
	int nSize = s.GetLength();

	CComBSTR sBstr = s;

	char *pAnsiString = new char[nSize+1];

	WideCharToMultiByte(nCodePage, 0, sBstr, nSize+1, pAnsiString, nSize+1, NULL, NULL);

	return pAnsiString;
}
#else
// POSIX port (2026-07-20): CString is already narrow here, so the Windows
// round trip (CString -> CComBSTR -> WideCharToMultiByte(CP_ACP)) collapses
// to a copy. Identical for the ASCII text VPF tables carry; a true multibyte
// code page would differ, but none reaches this reader. Callers delete[]
// the result, as on Windows.
char* GetAnsiString(const CString &s, UINT /*nCodePage*/)
{
	int nSize = s.GetLength();

	char *pAnsiString = new char[nSize+1];
	memcpy(pAnsiString, (LPCSTR)s, nSize);
	pAnsiString[nSize] = 0;

	return pAnsiString;
}
#endif


//------------------------------------------------------------------------------
const TCHAR * VPFKeyType_to_String(const int type)
{
   const TCHAR * return_value;

   switch (type)
   {
      case VPF_KEY_PRIMARY:    return_value = _T("VPF_KEY_PRIMARY");      break;
      case VPF_KEY_UNIQUE:     return_value = _T("VPF_KEY_UNIQUE");       break;
      case VPF_KEY_NON_UNIQUE: return_value = _T("VPF_KEY_NON_UNIQUE");   break;
      default:                 return_value = _T("<invalid enum value>"); break;
   }

   return return_value;
}

//------------------------------------------------------------------------------
const TCHAR * VPFType_to_String(const int type)
{
   CString return_value;


   if (VPF_NULL & type)               return_value += _T("VPF_NULL, ");
   if (VPF_2COORD_SHORT_FLOAT & type) return_value += _T("VPF_2COORD_SHORT_FLOAT, ");
   if (VPF_2COORD_LONG_FLOAT & type)  return_value += _T("VPF_2COORD_LONG_FLOAT, ");
   if (VPF_3COORD_SHORT_FLOAT & type) return_value += _T("VPF_3COORD_SHORT_FLOAT, ");
   if (VPF_3COORD_LONG_FLOAT & type)  return_value += _T("VPF_3COORD_LONG_FLOAT, ");
   if (VPF_TEXT & type)               return_value += _T("VPF_TEXT, ");
   if (VPF_LATIN1_TEXT & type)        return_value += _T("VPF_LATIN1_TEXT, ");
   if (VPF_FULL_LATIN_TEXT & type)    return_value += _T("VPF_FULL_LATIN_TEXT, ");
   if (VPF_MULTI_LINGUAL_TEXT & type) return_value += _T("VPF_MULTI_LINGUAL_TEXT, ");
   if (VPF_FLOAT_SHORT & type)        return_value += _T("VPF_FLOAT_SHORT, ");
   if (VPF_FLOAT_LONG & type)         return_value += _T("VPF_FLOAT_LONG, ");
   if (VPF_INT_SHORT & type)          return_value += _T("VPF_INT_SHORT, ");
   if (VPF_INT_LONG & type)           return_value += _T("VPF_INT_LONG, ");
   if (VPF_DATE_TIME & type)          return_value += _T("VPF_DATE_TIME, ");
   if (VPF_TRIPLET_ID & type)         return_value += _T("VPF_TRIPLET_ID, ");
   if (return_value.IsEmpty())        return_value  = _T("<invalid enum value>");

   return return_value;
}

//------------------------------------------------------------------------------
CString remove_first_token(char delimiter, CString& source)
{
   int index;
   CString return_value;

   index = source.Find(delimiter);
   return_value = source.Left(index++);
   source = source.Right(source.GetLength() - index);

   return return_value;
}

// inpmrovement over the previous function, no return by value.
void remove_first_token(char delimiter, CString& source, CString& destination)
{
   int index;
   //CString return_value;

   index = source.Find(delimiter);
   destination = source.Left(index++);
   source = source.Right(source.GetLength() - index);

   //return return_value;
}

/*
//------------------------------------------------------------------------------
void CFileException_to_VPFException(CFileException& file_err, VPFException* vpf_err)
{
   if (NULL == vpf_err)
      vpf_err = new VPFException;

   const int          buffer_size     = 100;
   const unsigned int buffer_capacity = buffer_size - 1;

   file_err.GetErrorMessage(vpf_err->m_error_message.GetBuffer(buffer_size), buffer_capacity, NULL);
   vpf_err->m_error_message.ReleaseBuffer();

   vpf_err->m_error_message = "File Exception:  " + vpf_err->m_error_message;
   vpf_err->m_error_type    = file_err.m_cause;
}
*/

//------------------------------------------------------------------------------
//------------------------------- VPFRecordset ---------------------------------
//------------------------------------------------------------------------------
VPFRecordset::VPFRecordset(const CString &path_to_table)
{
   ASSERT(!path_to_table.IsEmpty());
   m_path_to_table = path_to_table;

   m_dbpath          = path_to_table;
   m_library         = NULL;


   m_current         = -1;
   m_record_count    =  0;
   m_row_length      =  0;

   m_record_contents.id = -1;
   m_record_contents.data.reserve(5);

	for (int i=0;i<5;i++)
      m_record_contents.data.push_back(new VPFVariant());
		//m_record_contents.data[i] = new VPFVariant();

   //m_fields.SetSize(0, 5);
   m_fields.reserve(0);

   m_table_name.Empty();

   m_thematic_index     = NULL;
   m_spatial_index      = NULL;
   m_var_length_index   = NULL;
   m_narrative_table    = NULL;

	m_file_handle = INVALID_HANDLE_VALUE;
}

VPFRecordset::VPFRecordset(VPFLibrary* library)
{
   ASSERT(library);

   m_library         = library;
   m_dbpath          = library->get_path();
   m_current         = -1;
   m_record_count    =  0;
   m_row_length      =  0;

   m_record_contents.id = -1;
   //m_record_contents.data.SetSize(5, 5);
   m_record_contents.data.reserve(5);

	// NOTE (port 2026-07-20): was "m_record_contents.data[i] = new
	// VPFVariant();" - reserve() leaves the vector EMPTY, so those five
	// assignments wrote past the end (the commented-out MFC original,
	// CArray::SetSize(5,5), did size it). Undefined behavior on both
	// platforms; the sibling ctor above already uses push_back. Fixed
	// rather than preserved: this is memory corruption, not a numeric
	// quirk, and the bit-faithful rule does not extend to UB.
	for (int i=0;i<5;i++)
		m_record_contents.data.push_back(new VPFVariant());

   m_fields.reserve(0);

   m_table_name.Empty();

   m_thematic_index     = NULL;
   m_spatial_index      = NULL;
   m_var_length_index   = NULL;
   m_narrative_table    = NULL;

	m_file_handle = INVALID_HANDLE_VALUE;
}

//------------------------------ ~VPFRecordset ---------------------------------
VPFRecordset::~VPFRecordset()
{
   close();

   if (m_thematic_index)
      delete m_thematic_index;

   if (m_spatial_index)
      delete m_spatial_index;

   if (m_var_length_index)
      delete m_var_length_index;

   if (m_narrative_table)
      delete m_narrative_table;


   std::vector<VPFVariant *>::iterator it;

   for (it = m_record_contents.data.begin(); it != m_record_contents.data.end(); it++)
   {
      delete *it;
   }

   m_record_contents.data.clear();
/*
	for (int i=0;i<m_record_contents.data.size();i++)
		delete m_record_contents.data[i];

   m_record_contents.data.clear();
*/
   m_fields.clear();
}

//------------------------------ read_in_header --------------------------------
int VPFRecordset::read_in_header(CString& header)
{
   ASSERT(is_open());

   char  char_buf          = '\0';
   long  header_size       = 0;
   //long  file_size         = 0;
   char* buf               = NULL;
   bool  is_little_endian  = true;

   // In a VPF table file, the first item in the file is a 4-byte integer
   // that gives the size of the rest of the header.
   m_current_file_pos = m_file_ptr;
	// NOTE (port 2026-07-20): VPF stores this count as a 4-byte integer
	// (MIL-STD-2407 "long integer"). Win32 long is 4 bytes, but LP64 long is
	// 8, so the original read 8 bytes and advanced 8 - garbage header size
	// and a misaligned cursor. int32_t is exact on both platforms.
	header_size = *(int32_t *)m_current_file_pos;
	m_current_file_pos += sizeof(int32_t);
   ASSERT((DWORD)(header_size+4) <= m_file_size);


   // Allocate a buffer for the header plus a NULL
   // terminator, and read in the header
   buf = (char*) malloc(header_size + 1);
	memcpy(buf, m_current_file_pos, header_size);
	m_current_file_pos += header_size;
   buf[header_size] = '\0';


   // The second item is an optional character, either an "M" or an "L".
   // "L" means the file is ordered least-significant byte first, and
   // "M" means the file is ordered most-significant byte first.  If this
   // character is not presents, then the default is "L".
   char_buf = buf[0];
   if (char_buf == 'M')
      is_little_endian = false;

   //.ecr.todo. Test with a big-endian file
   if (!is_little_endian)
   {
      ::MessageBox(NULL, _T("FalconView's VPF implementation doesn't support Big-Endian yet."), _T(""), MB_OK);
   }


   // The third item is a semi-colon that separates this part of the header
   // from the rest of the header.
   if (char_buf != ';')
   {
      ASSERT(char_buf == 'L' || char_buf == 'M');
      char_buf = buf[1];
   }

   // If we don't now have a semi-colon in char_buf, then our file is
   // not a valid VPF table.
   if (char_buf != ';')
   {
      CString Error;
      Error = _T("Table header is not properly formatted.\n")
                           _T("This may not be a VPF table.");

      WriteToLogFile(_bstr_t(Error));
      return FAILURE;
   }

   // Assign the buffer to "header", then skip the byte indicator and semi-colon
   header = buf;
   if (buf[0] == ';')
      header = header.Right(header_size - 1);
   else
      header = header.Right(header_size - 2);
   free(buf);

   return SUCCESS;
}

//----------------------------------- open -------------------------------------
int VPFRecordset::open(const CString& table_name)
{
   //DVL Use member variable in case it was specified by constructor
	CString path(m_path_to_table);


	// DVL If there is no db or library, we constructed the object with the path, use member
	if (path.IsEmpty())
   {
      // NOTE Robert: For a recordset, I need the table name and path,
      // when opening dht and lat, m_library is NULL so I get the path
      // from the database. To eliminate this dependency, the caller 
      // should provide the complete path to the table!
//      if ( m_library != NULL )
//         path = m_library->get_path();
//      else
         path = m_dbpath;
   }

   //CFileException file_err = CFileException::fileNotFound;

	m_file_path = path + table_name;
	const int indx = m_file_path.ReverseFind('\\');
	const int str_length = m_file_path.GetLength();
	m_file_name = m_file_path.Right(str_length - indx - 1);

	// if a file is already open the clean close
	if (is_open())
      close();

	m_file_handle = CreateFile(m_file_path, GENERIC_READ, FILE_SHARE_READ,
		                     NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL); 

	if (m_file_handle != INVALID_HANDLE_VALUE)
	{
      CString  header, field_meta_data, narrative_table_name;

		// retrieve the size of the file
		m_file_size = GetFileSize(m_file_handle, NULL);

		// create the file mapping
		m_file_mapping_handle = CreateFileMapping(m_file_handle, 
			NULL, PAGE_READONLY, 0, 0, NULL);

		// retrieve a pointer to the file
		m_file_ptr = (BYTE *)MapViewOfFile(m_file_mapping_handle,
			FILE_MAP_READ, 0, 0, 0);

      m_table_name = table_name;

      read_in_header(header);

      // Get the Table description
      //m_table_desc = remove_first_token(';', header);
      remove_first_token(';', header, m_table_desc);

      // Get the name of the Narrative Table and set it up
      //narrative_table_name = remove_first_token(';', header);
      remove_first_token(';', header, narrative_table_name);
      if (!narrative_table_name.IsEmpty())
         m_narrative_table = new VPFNarrativeTable(narrative_table_name, this);

      // Get the meta data for the fields
      //field_meta_data = remove_first_token(';', header);
      remove_first_token(';', header, field_meta_data);

      // Setup the list of VPFFieldInfo objects
      setup_field_info_list(field_meta_data);

      // we already read in the header, so set the start position here
		m_start_file_pos = m_current_file_pos;

      // Set the row length; -1 is used to indicate variable-length records
      for (size_t i = 0; i < m_fields.size(); i++)
      {
         if (!m_fields[i].is_fixed_length())
         {  // If any field is of variable length, we are done
            m_row_length = -1;
            break;
         }
         else
         {
            m_row_length += m_fields[i].m_length;
         }
      }

      // Set m_record_count, either by calculation or from the index
      if (m_row_length >= 0)
      {
         // If all the fields have fixed lengths, then the
         // number of records in the table is simply the size
         // of the data area divided by the size of a row
			m_record_count = (m_file_ptr + m_file_size - m_start_file_pos) 
				/ m_row_length;
      }
      else
      {
         // If there are variable-length fields, then there will be a
         // variable-length index.  Open it and prepare it for use.
         m_var_length_index = new VPFVariableLengthIndex(this);

         if (!m_var_length_index || !m_var_length_index->is_open())
            return FAILURE;

         m_record_count = m_var_length_index->get_record_count();
      }

      if (m_record_count < 1)
      {
         // The file might not have any records in it.  For some reason that
         // is NOT an unusual case in VPF
         close();
         m_file_handle = INVALID_HANDLE_VALUE;
      }
      else
         // Set the first record in the table as current
         move_first();

      // this function does nothing, just remove the call
      //on_open();

      return SUCCESS;
	}
   else
      return FAILURE;
}

//----------------------------------- open -------------------------------------
int VPFRecordset::open(const CString& /*table_type*/, degrees_t /*lat*/, degrees_t /*lon*/,
                        const CString& /*table_name*/)
{
   // This function not implemented 

   ASSERT(false);
   return FAILURE;
}

//---------------------------- decode_triplet_id -------------------------------
void VPFRecordset::decode_triplet_id(TCHAR triplet_id, int lengths[3])
{
   int mask[] = { 12, 48, 192 };

   for (int i = 1; i < 4; ++i)
   {
		lengths[3-i] = (triplet_id & mask[i-1]) >> (i << 1);
		if (lengths[3-i] == 3)
			lengths[3-i] = 4;
   }
}
//-------------------------------- read_field ----------------------------------
int VPFRecordset::read_field(const VPFFieldInfo &field, VPFVariant* new_variant)
{
   // DVL :: we do not need the value in the next call, this is just to save
   // constructor calls to VPFVariant.
   //static VPFVariant  new_variant;

   int         length      = 0;
   int         lengths[4]  = {0,0,0,0};
   int         num_coords  = 1;
   //char*       buf         = NULL;


   // If the type is a triplet-id, then we will need
   // to "decode" the length information in the length
   // byte to determine the length of the stored data.
   if (field.m_type == VPF_TRIPLET_ID)
   {
      char triplet_id = *(char *)m_current_file_pos;
		m_current_file_pos += sizeof(char);

      decode_triplet_id(triplet_id, lengths);
      length = lengths[0] + lengths[1] + lengths[2] + lengths[3];
   }
   // If the field is fixed-length, then the length will
   // already be defined in the VPFFieldInfo struct.
   else if (field.m_length != -1)
   {
      length = field.m_length;
   }
   // If it's variable-length, then the length is
   // stored at the beginning of the field, so we
   // need to read it from the disk first.
   else
   {
		length = *(long int *)m_current_file_pos;
      m_current_file_pos += sizeof(long int);

      // If the data that the length is describing is a coordinate string
      // or array, then the length is describing the number of coordinate
      // tuples, not just the length of the data.  For this reason, it is
      // necessary to multiply the length attribute times the size of the
      // corresponding coordinate tuple to determine its actual length.
      num_coords = length;
      if (field.m_type == VPF_3COORD_SHORT_FLOAT)
         length *= 12;
      else if (field.m_type == VPF_2COORD_SHORT_FLOAT)
         length *=  8;
      else if (field.m_type == VPF_3COORD_LONG_FLOAT)
         length *= 24;
      else if (field.m_type == VPF_2COORD_LONG_FLOAT)
         length *= 16;
   }


   // Allocate a buffer and read the data
   // DVL :: Allocate 1 extra in case this is text (null character) so as to 
   // avoid a later call to realloc under the text cases.  The buffer will be destroyed
   // very soon in any case, so this does not really matter.  In addition, it would be
   // faster to read directly to the final location instead of using a buffer.  This
   // change will probably be made at some point to gain speed.
   //buf = (char*) malloc(length+1);
   //m_file.Read(buf, length);

   // Set the type
   new_variant->m_type = field.m_type;

   // Based on the type, set the data from the buffer
   switch (new_variant->m_type)
   {
      case VPF_NULL:
         break;

      case VPF_FLOAT_SHORT:
			new_variant->m_float_short = *(float *)m_current_file_pos;
         m_current_file_pos += sizeof(float);
         break;

      case VPF_FLOAT_LONG:
         new_variant->m_float_long = *(double *)m_current_file_pos;
			m_current_file_pos += sizeof(double);
         break;

      case VPF_INT_SHORT:
         new_variant->m_int_short = *(short *)m_current_file_pos;
			m_current_file_pos += sizeof(short);
         break;

      case VPF_INT_LONG:
         // VPF "long integer" fields are 4 bytes on disk (see above).
         new_variant->m_int_long = *(int32_t *)m_current_file_pos;
			m_current_file_pos += sizeof(int32_t);
         break;

		case VPF_DATE_TIME:
      {
			char buf[21];
         length = 20;
			memcpy(buf, m_current_file_pos, length);
			m_current_file_pos += length;
			buf[length] = '\0';
         new_variant->m_text = buf;  // m_text is a CString, not a pointer
         new_variant->m_text.TrimRight();

         break;
      }

      case VPF_TEXT:
      case VPF_LATIN1_TEXT:
      case VPF_FULL_LATIN_TEXT:
      case VPF_MULTI_LINGUAL_TEXT:
      {
         // DVL :: realloc removed in favor of larger malloc above
         //buf = (char*) realloc(buf, length + 1);
			char *buf = (char*) malloc(length+1);
			memcpy(buf, m_current_file_pos, length);
			m_current_file_pos += length;
			buf[length] = '\0';
         new_variant->m_text = buf;  // m_text is a CString, not a pointer
         new_variant->m_text.TrimRight();
			free(buf);

         break;
      }

      // Both the following cases have the same data type, there are just a different
      // number of coordinates.  For this reason, the two cases have been combined,
      // and the loop that reads the values is set to run for either 2 or 3 values.
      case VPF_3COORD_SHORT_FLOAT:
      {
			new_variant->m_3coord_float_coords = (coord3_float_t *)
				malloc(sizeof(coord3_float_t) * num_coords);
			memcpy(new_variant->m_3coord_float_coords, m_current_file_pos, length);
			m_current_file_pos += length;
			new_variant->m_num_coords = num_coords;

         break;
      }

      case VPF_2COORD_SHORT_FLOAT:
      {
			new_variant->m_2coord_float_coords = (coord2_float_t *)
				malloc(sizeof(coord2_float_t) * num_coords);
			memcpy(new_variant->m_2coord_float_coords, m_current_file_pos, length);
			m_current_file_pos += length;
			new_variant->m_num_coords = num_coords;

         break;
      }

      // Both the following cases have the same data type, there are just a different
      // number of coordinates.  For this reason, the two cases have been combined,
      // and the loop that reads the values is set to run for either 2 or 3 values.
      case VPF_2COORD_LONG_FLOAT:
      {
			new_variant->m_2coord_double_coords = (coord2_double_t *)
				malloc(sizeof(coord2_double_t) * num_coords);
			memcpy(new_variant->m_2coord_double_coords, m_current_file_pos, length);
			m_current_file_pos += length;
			new_variant->m_num_coords = num_coords;

         break;
      }

      case VPF_3COORD_LONG_FLOAT:
      {
			new_variant->m_3coord_double_coords = (coord3_double_t *)
				malloc(sizeof(coord3_double_t) * num_coords);
			memcpy(new_variant->m_3coord_double_coords, m_current_file_pos, length);
			m_current_file_pos += length;
			new_variant->m_num_coords = num_coords;

         break;
      }

      case VPF_TRIPLET_ID:
         {
				new_variant->m_triplet_id.id      = 0;
				new_variant->m_triplet_id.tile_id = 0;
				new_variant->m_triplet_id.ext_id  = 0;
				
				memcpy(&(new_variant->m_triplet_id.id), m_current_file_pos, lengths[0]);
				m_current_file_pos += lengths[0];

				memcpy(&(new_variant->m_triplet_id.tile_id), m_current_file_pos, lengths[1]);
				m_current_file_pos += lengths[1];

				memcpy(&(new_variant->m_triplet_id.ext_id), m_current_file_pos, lengths[2]);
				m_current_file_pos += lengths[2];
				
				break;
         }

      default:
      {
         ASSERT(false);
         CString Error;
         Error.Format(_T("Undefined VPF data type. (%d)\nCritical VPF error."), new_variant->m_type);
         WriteToLogFile(_bstr_t(Error));
         return FAILURE;
      }
   }

   return SUCCESS;
}

//------------------------------- read_record ----------------------------------
int VPFRecordset::read_record()
{
   //ASSERT(!is_bof());
   //ASSERT(!is_eof());

   if (is_bof() || is_eof())
      return FAILURE;

   // We don't need to re-read the record if we already have it
   if (m_current != m_record_contents.id)
   {
      seek_to_record(m_current);

      clear_record_contents();

      m_record_contents.id = m_current;

      const int size = m_fields.size();

      // we initially allocated 5 VPFVariants for the data array.  If this
		// isn't enough, then increase the size here by a factor of 5
		int current_size = m_record_contents.data.size();
		if (size > current_size)
		{
			int new_size = current_size;
			while (new_size < size)
				new_size += 5;
			m_record_contents.data.reserve(new_size);
			for (int i = current_size; i<new_size;i++)
            m_record_contents.data.push_back(new VPFVariant());
				//m_record_contents.data[i] = new VPFVariant();
		}

      // DVL :: no modification here, but that really depends on how CArray treats
      // it internally.  It should make a copy for insertion in the array.
      for (int i = 0; i < size; i++)
         if (read_field(m_fields[i], m_record_contents.data[i]) != SUCCESS)
            return FAILURE;
   }

   // Notify child that the record has changed
   on_set_position();

   return SUCCESS;
}

//-------------------------- clear_record_contents -----------------------------
void VPFRecordset::clear_record_contents()
{
   m_record_contents.id = -1;

	for(size_t i=0;i<m_record_contents.data.size();i++)
      m_record_contents.data[i]->remove_coords_list();
}

//------------------------------ seek_to_record --------------------------------
void VPFRecordset::seek_to_record(int record_num)
{
   ASSERT(record_num >= 0);
   ASSERT(record_num < m_record_count);

   //DWORD filepos = 0;

   // Special Case: if we want to seek to the first record, it doesn't matter
   // whether or not there is a variable-length index file. We just need to
   // seek to m_pos_data_start. That way we don't waste lots of time on an
   // unnecessary multiplication or an unnecessary disk access.
   if (record_num == 0)
      m_current_file_pos = m_start_file_pos;

   else if (m_row_length != -1)
		m_current_file_pos = m_start_file_pos + (record_num * m_row_length);

   else
		m_current_file_pos = m_file_ptr + 
		m_var_length_index->get_filepos_for_record(record_num);
}


//-------------------------- setup_field_info_list -----------------------------
void VPFRecordset::setup_field_info_list(CString& meta_data)
{
   CStringArray   field_strings;
   CString        string;
   VPFFieldInfo   new_field;
   bool           done = false;

   // Step 1 - split the contiguous string of field info into
   // separate strings, one for each field defined in the table
   while (!done)
   {
      //string = remove_first_token(':', meta_data);
      remove_first_token(':', meta_data, string);
      if (!string.IsEmpty())
         field_strings.Add(string);
      else
         done = true;
   }

   // the meta_data string should now be empty
   ASSERT(meta_data.IsEmpty());

   // Step 2 - split apart each field info string, and set
   // the appropriate properties in the VPFFieldInfo struct
   for (int i = 0; i < field_strings.GetSize(); i++)
   {
      CString       current_string = field_strings[i];

      // Note: Using th enew remove_first_token() is slower WHY??
      //CString       temp_string;

      // 1 - field name
      new_field.set_name(remove_first_token('=', current_string));
      //remove_first_token('=', current_string, temp_string);
      //new_field.set_name(temp_string);

      // 2 - field type
      new_field.set_type(remove_first_token(',', current_string));
      //remove_first_token(',', current_string, temp_string);
      //new_field.set_type(temp_string);

      // 3 - field length
      new_field.set_length(remove_first_token(',', current_string));
      //remove_first_token(',', current_string, temp_string);
      //new_field.set_length(temp_string);

      // 4 - key type
      new_field.set_key_type(remove_first_token(',', current_string));
      //remove_first_token(',', current_string, temp_string);
      //new_field.set_key_type(temp_string);

      // 5 - description
      new_field.set_desc(remove_first_token(',', current_string));
      //remove_first_token(',', current_string, temp_string);
      //new_field.set_desc(temp_string);

      // 6 - value description table name
      new_field.set_value_description_table(remove_first_token(',', current_string));
      //remove_first_token(',', current_string, temp_string);
      //new_field.set_value_description_table(temp_string);

      // 7 - thematic index name
      new_field.set_thematic_index(remove_first_token(',', current_string));
      //remove_first_token(',', current_string, temp_string);
      //new_field.set_thematic_index(temp_string);

      // 8 - column narrative table name
      new_field.set_column_narrative_table(remove_first_token(',', current_string));
      //remove_first_token(',', current_string, temp_string);
      //new_field.set_column_narrative_table(temp_string);

      new_field.set_ordinal_position(i);

      m_fields.push_back(new_field);
   }
}

//---------------------------------- close -------------------------------------
void VPFRecordset::close()
{
   m_fields.clear();

   clear_record_contents();

	if (is_open())
   {
	   UnmapViewOfFile(m_file_ptr);
	   CloseHandle(m_file_mapping_handle);
	   CloseHandle(m_file_handle);

	   // NOTE (port 2026-07-20): the handles were left dangling here, so
	   // is_open() still reported true after close() and the destructor
	   // closed them a second time. On Win32 that double CloseHandle can
	   // close an unrelated, recycled handle; on POSIX it is a
	   // use-after-free. Resetting is correct on both platforms.
	   m_file_handle = INVALID_HANDLE_VALUE;
	   m_file_mapping_handle = NULL;
	   m_file_ptr = NULL;
   }
}

//--------------------------------- get_name -----------------------------------
CString VPFRecordset::get_name()
{
   return m_table_name;
}

//----------------------------- get_record_count -------------------------------
int VPFRecordset::get_record_count()
{
   int return_value = -1;

   if (!is_open())
      ASSERT(false);
   else
      return_value =  m_record_count;

   return return_value;
}

//---------------------------------- is_bof ------------------------------------
bool VPFRecordset::is_bof() const
{
   // Returns TRUE under 2 conditions:
   //    1. we've scrolled back before the first record
   //    2. there are no records in the table
   if (m_current == -1 || m_record_count == 0)
      return true;
   else
      return false;
}

//---------------------------------- is_eof ------------------------------------
bool VPFRecordset::is_eof() const
{
   // Returns TRUE under 2 conditions:
   //    1. we've scrolled past the last record
   //    2. there are no records in the table
   if (m_current == INT_MAX || m_record_count == 0)
      return true;
   else
      return false;
}

//--------------------------------- is_open ------------------------------------
bool VPFRecordset::is_open() const
{
   return (m_file_handle != INVALID_HANDLE_VALUE);
}

//------------------------ establish_limits_for_find ---------------------------
void VPFRecordset::establish_limits_for_find(const vpf_find_type type,
   int& start_index, int& end_index, int& step) const
{
   switch (type)
   {
      // DVL :: This function needs to be able to jump to a particular tile and 
      // set start_index to the first entry on a particular tile.  Otherwise, 
      // this is going to be very slow

      case VPF_NEXT:
         start_index = m_current + 1;
         end_index   = m_record_count - 1;
         step = 1;
         break;

      case VPF_PREV:
         start_index = m_current - 1;
         end_index   = 0;
         step = -1;
         break;

      case VPF_FIRST:
         start_index = 0;
         end_index   = m_record_count - 1;
         step = 1;
         break;

      case VPF_LAST:
         start_index = m_record_count - 1;
         end_index   = 0;
         step = -1;
         break;
   }
}

//------------------------------- parse_filter ---------------------------------
bool VPFRecordset::parse_filter(const CString& filter,
   CString& logical_operator, CString& comparatorA, CString& comparatorB,
   CString& fieldA, CString& valueA, CString& fieldB, CString& valueB)
{
   bool     done = false;
   int      index = -1;

   CString str = filter;
   str.TrimLeft();
   str.TrimRight();

   // get the index of the first mathematical operator
   index = str.FindOneOf(_T("=<>!"));
   if (index < 0) return false;

   // fieldA is the string to the left of the first mathematical operator
   fieldA = str.Left(index);
   fieldA.TrimRight();

   str = str.Right(str.GetLength() - index);

   // comparatorA is the first mathematical operator
   comparatorA = str.SpanExcluding(_T(" "));
   if (comparatorA.GetLength() < 1) return false;

   str = str.Right(str.GetLength() - comparatorA.GetLength());

   // valueA is either the rest of the string OR the string before the logical operator
   index = str.Find(_T(" AND "));
   if (index < 0)
      index = str.Find(_T(" OR "));

   if (index < 0)
   {
      str.TrimLeft();
      valueA = str;
      done = true;
   }
   else
   {
      valueA = str.Left(index);
      valueA.TrimLeft();
      valueA.TrimRight();
   }

   if (!done)
   {
      str = str.Right(str.GetLength() - index);
      str.TrimLeft();

      // get the logical operator
      logical_operator = str.SpanExcluding(_T(" "));
      str = str.Right(str.GetLength() - logical_operator.GetLength());
      str.TrimLeft();

      // get the index of the second mathematical operator
      index = str.FindOneOf(_T("=<>!"));
      if (index < 0) return false;

      // fieldB is the string to the left of the second mathematical operator
      fieldB = str.Left(index);
      fieldB.TrimRight();

      str = str.Right(str.GetLength() - index);

      // comparatorB is the second mathematical operator
      comparatorB = str.SpanExcluding(_T(" "));
      if (comparatorB.GetLength() < 1) return false;

      str = str.Right(str.GetLength() - comparatorB.GetLength());

      // valueB is the rest of the string
      valueB = str;
      valueB.TrimLeft();
   }

   return true;
}

//----------------------------------- find -------------------------------------
// Currently, this "find" function can only handle logical expressions with up
// to 2 items.  For example, it can hande "id = 12 AND type = 'TOWER'", but no
// more complexity than that.  It also cannot handle NOT as a logical operator.
//------------------------------------------------------------------------------
bool VPFRecordset::find(vpf_find_type type, const CString& filter)
{

   bool FoundMatch = false;


   // All other VPFRecordset find functions call this function.
   if ((type == VPF_NEXT || type == VPF_PREV) && m_current == -1)
   {
      ASSERT(false);
//      ERR_report("Cannot execute database \"find\" operation.  Current record is undefined.");
      return false;
   }

   //
   // Determine the beginning and ending positions for the find,
   // as well as the step interval for iterating through the set
   //
   int start_index = 0;
   int end_index   = 0;
   int step        = 1;

   establish_limits_for_find(type, start_index, end_index, step);

   //-------------------------------

   CString  logical_operator;
   CString  comparatorA, comparatorB;
   CString  fieldA, valueA, fieldB, valueB;

   const bool ok_so_far = parse_filter(filter, logical_operator, comparatorA, comparatorB,
      fieldA, valueA, fieldB, valueB);

   if (!ok_so_far) return false;
   //-------------------------------

   //
   // Determine if valueA and valueB are strings or numbers, and
   // setup comparison variables for them
   //
   const bool valueA_is_string =  (valueA.GetLength() >= 2) &&
      (valueA.Left(1) == _T("'")) && (valueA.Right(1) == _T("'"));

   const bool valueB_is_string =  (valueB.GetLength() >= 2) &&
      (valueB.Left(1) == _T("'")) && (valueB.Right(1) == _T("'"));

   const CString str_valueA = (valueA_is_string ? _T("<none>") : valueA.Mid(1, valueA.GetLength() - 2));
   const CString str_valueB = (valueB_is_string ? _T("<none>") : valueB.Mid(1, valueB.GetLength() - 2));
//   const CString str_valueA = (valueA_is_string ? valueA.Mid(1, valueA.GetLength() - 2) : _T("<none>"));
//   const CString str_valueB = (valueB_is_string ? valueB.Mid(1, valueB.GetLength() - 2) : _T("<none>"));


	char *chrValueA = GetAnsiString(valueA, CP_ACP);
	char *chrValueB = GetAnsiString(valueB, CP_ACP);

   const double num_valueA = atof(chrValueA);
   const double num_valueB = atof(chrValueB);

	delete [] chrValueA;
	delete [] chrValueB;
   //const double num_valueA = atof(valueA);
   //const double num_valueB = atof(valueB);


   //-------------------------------

   //
   // Iterate through the recordset, looking for the appropriate values
   //
//   for (int i = start_index; i != end_index; i += step)

   // DVL :: HACK ALERT!!!
   // 
   // This is really not proper, the thematic index should be used or something, but 
   // there are other priorities just not, and this does the job, if ever so slowly
   // getting to the start of the tile,... Performance within a tile is good since
   // we are generally reading sequentially through the table.  This algorithm
   // requires that!
   //
   // On the Very first search, start at the requested location in the first tile
   static int StartPoint = (int)num_valueA;
   // After that, if we are on the same tile (i.e. next item, just continue where we left off.
   // In case we missed it, start again from the beginning,... to check all items in the list if needed
   int MayBeTwice = 2;
   int i=0; // i will be initialized below, this block "uninit" warning message
   while((!FoundMatch) && (MayBeTwice>0))
   {
   for (i = StartPoint; i <= end_index; i += step)
   {
      set_absolute_position(i);
//      set_absolute_position((int)num_valueA);//x-x-x-x-x
      //.ecr.todo. - MAJOR HACK ALERT - MAJOR HACK ALERT - MAJOR HACK ALERT - MAJOR HACK ALERT
      // For now, this is a MAJOR!!!! hack.  Since VPF has only one tile of data,
      // then the tile_id we passed in is going to be the same no matter what, and
      // the primitive_id's are in numerical order.  SO - tile_id #1 and prim_id #12
      // is at position 12.  So since it's very late at night, I can forgo completing
      // the implementation of VPFThematicIndex, and give this version to the folks
      // going out to Salt Lake City.
      //
      // dvl -- the code below was non-functional (variant_valueB was not initialized, 
      // but the return value of this function was not checked -- since the offset was
      // actually known already (i.e. "set_absolute_position((int)num_valueA)" above)
      // the tile_id did not really come into play. The loop here had been disabled.
      // By starting at the num_valueA we can skip a small part of the index so VVOD
      // still appears fast.  But, this function is very slow.  It probably should be
      // a 2d lookup and not an iterated list (reads file each time!)
      //
      // The lookup fails on the 

      bool found_valueA = false;
      bool found_valueB = false;

      VPFVariant *variant_valueA;
      variant_valueA = get_field_value(fieldA);

      VPFVariant *variant_valueB;
      variant_valueB = get_field_value(fieldB);

      if (variant_valueA==NULL || variant_valueB==NULL)
         return false;

      // A
      if (valueA_is_string)
      {
         if (variant_valueA->m_text == str_valueA)
            found_valueA = true;
      }
      else
      {
         switch (variant_valueA->m_type)
         {
            case VPF_FLOAT_SHORT:
               if (variant_valueA->m_float_short == num_valueA)
                  found_valueA = true;
               break;

            case VPF_FLOAT_LONG:
               if (variant_valueA->m_float_long == num_valueA)
                  found_valueA = true;
               break;

            case VPF_INT_SHORT:
               if (variant_valueA->m_int_short == num_valueA) 
                  found_valueA = true;
               break;

            case VPF_INT_LONG:
               // num_valueA as prim_ID is a 0 based index, but the primitive it is not,... error?
               // What other values get looked up.  Is this a special case?
               if (variant_valueA->m_int_long == num_valueA+1)
                  found_valueA = true;
               break;

            default:
               found_valueA = false;
               break;
         }
      }

      // B
      if (valueB_is_string)
      {
         if (variant_valueB->m_text == str_valueB)
            found_valueB = true;
      }
      else
      {
         switch (variant_valueB->m_type)
         {
            case VPF_FLOAT_SHORT:
               if (variant_valueB->m_float_short == num_valueB)
                  found_valueB = true;
               break;

            case VPF_FLOAT_LONG:
               if (variant_valueB->m_float_long == num_valueB)
                  found_valueB = true;
               break;

            case VPF_INT_SHORT:
               if (variant_valueB->m_int_short == num_valueB)
                  found_valueB = true;
               break;

            case VPF_INT_LONG:
               if (variant_valueB->m_int_long == num_valueB)
                  found_valueB = true;
               break;

            default:
               found_valueB = false;
               break;
         }
      }

      // logical
      if (logical_operator == _T("AND"))
      {
         FoundMatch = (found_valueA && found_valueB);
      }
      else if (logical_operator == _T("OR"))
      {
         FoundMatch = (found_valueA || found_valueB);
      }
      else if (logical_operator.IsEmpty())
      {
         FoundMatch = (found_valueA) ? true : false;
      }

   if (FoundMatch)
      break;
   }

   if(!FoundMatch)
   {
      // We did not find it in the list.
      end_index = StartPoint;
      StartPoint = 0;
      MayBeTwice --;
   }
   }

   // Save last searched point so we can continue a sequential search from there,...
   // If there is a count left in MayBeTwice, it was found on the first pass,...
   StartPoint = (MayBeTwice) ? i : 0;

   return (FoundMatch);
}

//-------------------------------- find_first ----------------------------------
bool VPFRecordset::find_first(const CString& filter)
{
   return find(VPF_FIRST, filter);
}

//-------------------------------- find_last -----------------------------------
bool VPFRecordset::find_last(const CString& filter)
{
   return find(VPF_LAST, filter);
}

//-------------------------------- find_next -----------------------------------
bool VPFRecordset::find_next(const CString& filter)
{
   return find(VPF_NEXT, filter);
}

//-------------------------------- find_prev -----------------------------------
bool VPFRecordset::find_prev(const CString& filter)
{
   return find(VPF_PREV, filter);
}

//-------------------------- get_absolute_position -----------------------------
int VPFRecordset::get_absolute_position()
{
   return m_current;
}

//------------------------------- get_bookmark ---------------------------------
DWORD VPFRecordset::get_bookmark()
{
   ASSERT(false);
   return 0;
}

//--------------------------- get_percent_position -----------------------------
float VPFRecordset::get_percent_position()
{
   // Need to add 1 to m_current before calculating b/c it's a zero-based
   // index, and if the recordset is on the 0th record, it's not at 0%
   float current     = (float)m_current + 1;
   float upper_bound = (float)m_record_count;

   return (current / upper_bound);
}

//----------------------------------- move -------------------------------------
int VPFRecordset::move(int num_rows)
{
   ASSERT(is_open());

   if (is_eof())
   {
//      ERR_report("Cannot perform VPFRecordset move operation.  Recordset is at EOF.");

      return FAILURE;
   }

   if (is_bof())
   {
//      ERR_report("Cannot perform VPFRecordset move operation.  Recordset is at BOF.");

      return FAILURE;
   }

   m_current += num_rows;

   // If we're above the upper limit, set current
   // to INT_MAX and seek to the end of the file
   if (m_current >= m_record_count)
   {
      m_current = INT_MAX;
      m_current_file_pos = m_file_ptr + m_file_size - 1;
   }
   // If we're below the lower limit, set current to -1
   // and seek to the beginning of the data area
   else if (m_current < 0)
   {
      m_current = -1;
		m_current_file_pos = m_start_file_pos;
   }
   // Else seek to the record
   else
   {
      seek_to_record(m_current);
   }

   read_record();

   return SUCCESS;
}

//-------------------------------- move_first ----------------------------------
int VPFRecordset::move_first()
{
   ASSERT(is_open());

   if (m_record_count == 0)
   {
//      ERR_report("Cannot perform VPFRecordset move operation.  Recordset has no records.");
      return FAILURE;
   }

   m_current = 0;

   //  seek_to_record(m_current);
   return read_record();
}

//-------------------------------- move_last -----------------------------------
int VPFRecordset::move_last()
{
   ASSERT(is_open());

   if (m_record_count == 0)
   {
//      ERR_report("Cannot perform VPFRecordset move operation.  Recordset has no records.");
      return FAILURE;
   }

   m_current = m_record_count - 1;

   //seek_to_record(m_current);
   return read_record();
}

//-------------------------------- move_next -----------------------------------
int VPFRecordset::move_next()
{
   return move(1);
}

//-------------------------------- move_prev -----------------------------------
int VPFRecordset::move_prev()
{
   return move(-1);
}

//-------------------------- set_absolute_position -----------------------------
int VPFRecordset::set_absolute_position(int position)
{
   ASSERT(is_open());
   ASSERT(position >= 0);
   ASSERT(position < m_record_count);

   if (position < 0 || position >= m_record_count)
   {
//      ERR_report("Cannot set VPFRecordset position out of bounds.");
      return FAILURE;
   }
   else
   {
      m_current = position;
      seek_to_record(m_current);
   }

   return read_record();
}

//------------------------------- set_bookmark ---------------------------------
void VPFRecordset::set_bookmark(DWORD /*bookmark*/)
{
   ASSERT(false);
}

//--------------------------- set_percent_position -----------------------------
void VPFRecordset::set_percent_position(float position)
{
   ASSERT(position >= 0 && position <= 100);
   ASSERT(is_open());

   float upper_bound         = 0;
   float calculated_position = 0;
   int   new_position        = 0;

   // Convert position to a value between 0 and 1
   position = position / 100;

   // Make sure upper_bound is 1-based
   upper_bound = (float) m_record_count;

   // Calculate the percent position
   calculated_position = position * upper_bound;

   // Forcibly round down at first...
   new_position = (int) floor(calculated_position);

   // Then if the decimal portion of calculated_position
   // is above .5, round up
   if ((calculated_position - new_position) > .5)
      new_position++;

   set_absolute_position(new_position);
}

//------------------------------ get_field_info --------------------------------
const VPFFieldInfo* VPFRecordset::get_field_info(int index)
{
   ASSERT(index >= 0);

   if (index < static_cast<int>(m_fields.size()))
   {
      return &m_fields[index];
   }

   return NULL;
}

//------------------------------ get_field_info --------------------------------
const VPFFieldInfo* VPFRecordset::get_field_info(const TCHAR * field_name)
{
   const int nFields = m_fields.size();

   int index;
   for (index = 0; index < nFields; index++)
   {
      if (m_fields[index].m_name == field_name)
         break;
   }

   if (index == nFields)
   {
      // No matching field was found, return NULL
      return 0;
   }

   return get_field_info(index);
}

//----------------------------- get_field_count --------------------------------
int VPFRecordset::get_field_count()
{
   return m_fields.size();
}

//----------------------------- get_field_value --------------------------------
void VPFRecordset::get_field_value(const CString& name, VPFVariant*& value)
{
   value = get_field_value(name);
}

//----------------------------- get_field_value --------------------------------
void VPFRecordset::get_field_value(int index, VPFVariant*& value)
{
   value = get_field_value(index);
}

//----------------------------- get_field_value --------------------------------
VPFVariant* VPFRecordset::get_field_value(const CString& name)
{
   // Find the index of the field with the name "name" and
   // call the other version of get_field_info() with it.
   const int FieldCount = m_fields.size();

   int index;
   for (index = 0; index < FieldCount; index++)
   {
      if (m_fields[index].m_name == name)
         break;// Once we've found the right index, break out of the for loop.
   }

   // If the index is >= the size, then we did
   // not find a field with the given name
   if (index >= FieldCount)
   {
      CString Error;
      Error.Format(_T("Field value (%s) in (%s) does not exist."), (LPCSTR)name, (LPCSTR)get_file_path());
      WriteToLogFile(_bstr_t(Error));
      return NULL;
   }

   return get_field_value(index);
}

//----------------------------- get_field_value --------------------------------
VPFVariant* VPFRecordset::get_field_value(int index)
{
   // NOTE: all other versions of get_field_value() end up using this
   // implementation.  This makes it MUCH easier to keep all version
   // synchronized with each other.

   ASSERT(m_current != -1);
   if (m_current == -1)
      return NULL;

   if (m_current != m_record_contents.id)
   {
      // This used to simply call read_record.  But this method of calling 
      // read_record allowed some paths to not update the record contents
      // after seeking to a different record.  If this ASSERT hits, find out 
      // what path allowed the current record number to change without updating
      // the record.
      ASSERT(false); 

      if (read_record() != SUCCESS)
         return NULL;
   }

   return m_record_contents.data[index];
}

//------------------------------ get_class_name --------------------------------
const TCHAR * VPFRecordset::get_class_name()
{
   return _T("VPFRecordset");
}

//----------------------------- on_set_position --------------------------------
void VPFRecordset::on_set_position()
{
}

//--------------------------------- on_open ------------------------------------
void VPFRecordset::on_open()
{
}

//------------------------------- verify_field ---------------------------------
bool VPFRecordset::verify_field(const TCHAR *name, const int intended_type,
   const int intended_length, VPFKeyType intended_key_type, bool is_mandatory)
{
   const VPFFieldInfo *info = get_field_info(name);

   if (info == NULL)
   {
      if (is_mandatory)
         return false;
      else
         return true;
   }

   if (!(info->m_type & intended_type))
   {
      return false;
   }

   // 0 is used to specify that the caller doesn't care about length.
   // It is used when the type doesn't need a length, like long float,
   // or short int.  Their length is predefined.
   // For text, -1 is used to specify variable length.  The intended
   // length should not be verified when the text length is variable,
   // because individual product specifications may mandate a specific
   // length when the VPF spec only requires a variable length.
   bool need_to_check_length;
   need_to_check_length = (intended_length != 0) && (intended_length != -1);

   if (need_to_check_length && info->m_length != intended_length)
      return false;

   if (info->m_key_type != intended_key_type)
      return false;

   // If we get this far, then there is no
   // exception to be thrown, so delete it
   return true;
}


//----------------------------- verify_filename --------------------------------
bool VPFRecordset::verify_filename(const TCHAR *filename)
{
   // CompareNoCase returns true for identical strings
   return (m_file_name.CompareNoCase(filename) == 0);
}


//------------------------------------------------------------------------------
//------------------------------- VPFFieldInfo ---------------------------------
//------------------------------------------------------------------------------
VPFFieldInfo::VPFFieldInfo()
{
   //.ecr.todo. Figure out if these are the appropriate default values
   m_name.Empty();
   m_desc.Empty();

   m_type               = VPF_NULL;
   m_key_type           = VPF_KEY_NON_UNIQUE;
   m_length             = -1;
   m_ordinal_position   = -1;
   m_required           = true;
   m_allow_zero_length  = false;

   m_foreign_name.Empty();
   m_default_value.Empty();

   m_value_description_table.Empty();
   m_thematic_index.Empty();
   m_column_narrative_table.Empty();
}

//------------------------------- VPFFieldInfo ---------------------------------
VPFFieldInfo::VPFFieldInfo(const VPFFieldInfo& info)
: m_name(info.m_name),
  m_desc(info.m_desc),
  m_type(info.m_type),
  m_key_type(info.m_key_type),
  m_length(info.m_length),
  m_ordinal_position(info.m_ordinal_position),
  m_required(info.m_required),
  m_allow_zero_length(info.m_allow_zero_length),
  m_foreign_name(info.m_foreign_name),
  m_default_value(info.m_default_value),
  m_value_description_table(info.m_value_description_table),
  m_thematic_index(info.m_thematic_index),
  m_column_narrative_table(info.m_column_narrative_table)
{
}

//-------------------------------- operator= -----------------------------------
VPFFieldInfo &VPFFieldInfo::operator=(const VPFFieldInfo& info)
{
   m_name                     = info.m_name;
   m_desc                     = info.m_desc;
   m_type                     = info.m_type;
   m_key_type                 = info.m_key_type;
   m_length                   = info.m_length;
   m_ordinal_position         = info.m_ordinal_position;
   m_required                 = info.m_required;
   m_allow_zero_length        = info.m_allow_zero_length;
   m_foreign_name             = info.m_foreign_name;
   m_default_value            = info.m_default_value;
   m_value_description_table  = info.m_value_description_table;
   m_thematic_index           = info.m_thematic_index;
   m_column_narrative_table   = info.m_column_narrative_table;

   return *this;
}

//------------------------------ ~VPFFieldInfo ---------------------------------
VPFFieldInfo::~VPFFieldInfo()
{
}

//--------------------------------- set_name -----------------------------------
void VPFFieldInfo::set_name(CString value)
{
   ASSERT(!value.IsEmpty());
   ASSERT(value != _T("-"));// "-" is the VPF 'blank' character

   m_name = value;
}

//--------------------------------- set_desc -----------------------------------
void VPFFieldInfo::set_desc(CString value)
{
   ASSERT(!value.IsEmpty());

   if (value == _T("-"))
      m_desc.Empty();
   else
      m_desc = value;
}

//--------------------------------- set_type -----------------------------------
void VPFFieldInfo::set_type(CString value)
{
   ASSERT(!value.IsEmpty());
   ASSERT(value != _T("-"));// "-" is the VPF 'blank' character

   switch (value[0])
   {
      case 'T':
      case 't': m_type = VPF_TEXT;               break;

      case 'L':
      case 'l': m_type = VPF_LATIN1_TEXT;        break;

      case 'N':
      case 'n': m_type = VPF_FULL_LATIN_TEXT;    break;

      case 'M':
      case 'm': m_type = VPF_MULTI_LINGUAL_TEXT; break;

      case 'F':
      case 'f': m_type = VPF_FLOAT_SHORT;        break;

      case 'R':
      case 'r': m_type = VPF_FLOAT_LONG;         break;

      case 'S':
      case 's': m_type = VPF_INT_SHORT;          break;

      case 'I':
      case 'i': m_type = VPF_INT_LONG;           break;

      case 'C':
      case 'c': m_type = VPF_2COORD_SHORT_FLOAT; break;

      case 'B':
      case 'b': m_type = VPF_2COORD_LONG_FLOAT;  break;

      case 'Z':
      case 'z': m_type = VPF_3COORD_SHORT_FLOAT; break;

      case 'Y':
      case 'y': m_type = VPF_3COORD_LONG_FLOAT;  break;

      case 'D':
      case 'd': m_type = VPF_DATE_TIME;          break;

      case 'X':
      case 'x': m_type = VPF_NULL;               break;

      case 'K':
      case 'k': m_type = VPF_TRIPLET_ID;         break;

      default:  m_type = VPF_NULL;
   }
}

//------------------------------- set_key_type ---------------------------------
void VPFFieldInfo::set_key_type(CString value)
{
   ASSERT(!value.IsEmpty());
   ASSERT(value != _T("-"));// "-" is the VPF 'blank' character

   switch (value[0])
   {
      case 'P':
      case 'p': m_key_type = VPF_KEY_PRIMARY;    break;
      case 'U':
      case 'u': m_key_type = VPF_KEY_UNIQUE;     break;
      case 'N':
      case 'n': m_key_type = VPF_KEY_NON_UNIQUE; break;
   }
}

//-------------------------------- set_length ----------------------------------
int VPFFieldInfo::set_length(CString value)
{
   ASSERT(!value.IsEmpty());
   ASSERT(value != _T("-"));// "-" is the VPF 'blank' character

   // If this is to be a variable length field, set
   // the length to -1 so we know the length varies
   if (value == _T("*"))
   {
      m_length = -1;
   }

   // Else set it to the length specified
   else
   {
      int   multiplier  =  1;
      char *x           = NULL;

      switch (m_type)
      {
         case VPF_TEXT:
            multiplier = 1;  break;

         case VPF_LATIN1_TEXT:
            multiplier = 1;  break;

         case VPF_FULL_LATIN_TEXT:
            multiplier = 1;  break;

         case VPF_MULTI_LINGUAL_TEXT:
            multiplier = 1;  break;

         case VPF_FLOAT_SHORT:
            multiplier = 4;  break;

         case VPF_FLOAT_LONG:
            multiplier = 8;  break;

         case VPF_INT_SHORT:
            multiplier = 2;  break;

         case VPF_INT_LONG:
            multiplier = 4;  break;

         case VPF_2COORD_SHORT_FLOAT:
            multiplier = 8;  break;

         case VPF_2COORD_LONG_FLOAT:
            multiplier = 16; break;

         case VPF_3COORD_SHORT_FLOAT:
            multiplier = 12; break;

         case VPF_3COORD_LONG_FLOAT:
            multiplier = 24; break;

         case VPF_DATE_TIME:
            multiplier = 20; break;

         case VPF_NULL:
            multiplier = 0;  break;

         case VPF_TRIPLET_ID:
            multiplier = 1;  break;//.ecr.todo. double-check this multiplier value

         default:
         {
            ASSERT(false);
            CString Error;
            Error.Format(_T("Undefined VPF data type. (%d)\nCritical VPF error."), m_type);
            WriteToLogFile(_bstr_t(Error));
            return FAILURE;
         }
      }

		char *chrValue = GetAnsiString(value, CP_ACP);
      m_length = strtol(chrValue, &x, 10) * multiplier;

		delete [] chrValue;
		//m_length = strtol(value, &x, 10) * multiplier;
   }

   return SUCCESS;
}

//--------------------------- set_ordinal_position -----------------------------
void VPFFieldInfo::set_ordinal_position(int value)
{
   ASSERT(value >= 0 && value < 256);

   m_ordinal_position = (short)value;
}

//------------------------------- set_required ---------------------------------
void VPFFieldInfo::set_required(bool value)
{
   m_required = value;
}

//-------------------------- set_allow_zero_length -----------------------------
void VPFFieldInfo::set_allow_zero_length(bool value)
{
   m_allow_zero_length = value;
}

//----------------------------- set_foreign_name -------------------------------
void VPFFieldInfo::set_foreign_name(CString value)
{
   //.ecr.todo. I still don't know if this will be used
   //ASSERT(!value.IsEmpty());

   if (value == _T("-") || value.IsEmpty() )
      m_foreign_name.Empty();
   else
      m_foreign_name = value;
}

//---------------------------- set_default_value -------------------------------
void VPFFieldInfo::set_default_value(CString value)
{
   ASSERT(!value.IsEmpty());
}

//----------------------- set_value_description_table --------------------------
void VPFFieldInfo::set_value_description_table(CString value)
{
   //ASSERT(!value.IsEmpty());

   if (value == _T("-") || value.IsEmpty() )
      m_value_description_table.Empty();
   else
      m_value_description_table = value;
}

//---------------------------- set_thematic_index ------------------------------
void VPFFieldInfo::set_thematic_index(CString value)
{
   //ASSERT(!value.IsEmpty());

   if (value == _T("-") || value.IsEmpty() )
      m_thematic_index.Empty();
   else
      m_thematic_index = value;
}

//------------------------ set_column_narrative_table --------------------------
void VPFFieldInfo::set_column_narrative_table(CString value)
{
   //ASSERT(!value.IsEmpty());

   if (value == _T("-") || value.IsEmpty() )
      m_column_narrative_table.Empty();
   else
      m_column_narrative_table = value;
}

//----------------------------- is_fixed_length --------------------------------
bool VPFFieldInfo::is_fixed_length() const
{
   return ((m_length >= 0) && (m_type!=VPF_TRIPLET_ID));
}
