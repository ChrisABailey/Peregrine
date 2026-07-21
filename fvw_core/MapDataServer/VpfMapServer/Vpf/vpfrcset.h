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

#if !defined(VPFRECORDSET_H__89E5255B_8652_11D3_8663_00105A9B4838__INCLUDED_)
#define VPFRECORDSET_H__89E5255B_8652_11D3_8663_00105A9B4838__INCLUDED_

//#include "common.h"     // for degrees_t
#include "vpf_d.h"

// Forward declarations
struct VPFFieldInfo;
class  VPFThematicIndex;
class  VPFSpatialIndex;
class  VPFVariableLengthIndex;
class  VPFNarrativeTable;
class  VPFLibrary;
class  VPFVariant;

//------------------------------------------------------------------------------
//------------------------------- VPFRecordset ---------------------------------
//------------------------------------------------------------------------------
// VPFRecordset class is the object model for a VPF table, as defined in the
// VPF specification (MIL-STD-2407), section 5.4.1.
//
// The VPFRecordset class is modeled after the CDaoRecordset class from MFC.
// The reason for this is that the DAO classes are in wide use, and developers
// are familiar with them, so the learning curve for a developer is minimized.
// e
// In addition, the DAO classes have already been designed by Microsoft, and
// re-designing a database class system would be re-inventing the wheel.
//
// The VPF specification (MIL-STD-2407) defines a VPF table in section 5.4.1.
//------------------------------------------------------------------------------
class VPFRecordset
{
public:
// Enum
   enum vpf_find_type
   {
      VPF_NEXT,
      VPF_PREV,
      VPF_FIRST,
      VPF_LAST
   };

//--- Construction ----------------------------------------
public:
   VPFRecordset                     (const CString &path_to_table);
   VPFRecordset                     (VPFLibrary* library);
   virtual ~VPFRecordset            ();

//--- Public Methods --------------------------------------
// The following public methods are base on the applicable
// functions in CDaoRecordset.  Since a VPF recordset is a
// read-only recordset, all of the update capabilities in
// CDaoRecordset will not be available in VPFRecordset.
//---------------------------------------------------------
public:
   int        open                  (const CString& table_name);
   void       close                 ();

   CString    get_name              ();
   int        get_record_count      ();
   bool       is_bof                () const;
   bool       is_eof                () const;
   bool       is_open               () const;

   bool       find                  (vpf_find_type type, const CString& filter);
   bool       find_first            (const CString& filter);
   bool       find_last             (const CString& filter);
   bool       find_next             (const CString& filter);
   bool       find_prev             (const CString& filter);

   int        get_absolute_position ();
   DWORD      get_bookmark          ();
   float      get_percent_position  ();

   int        move                  (int num_rows);
   int        move_first            ();
   int        move_last             ();
   int        move_next             ();
   int        move_prev             ();

   int        set_absolute_position (int position);
   void       set_bookmark          (DWORD bookmark);
   void       set_percent_position  (float position);

   const VPFFieldInfo* get_field_info(int index);
   const VPFFieldInfo* get_field_info(const TCHAR * field_name);
   int        get_field_count       ();
   void       get_field_value       (const CString& name, VPFVariant*& value);
   void       get_field_value       (int index, VPFVariant*& value);

   // These were return by value, this is to reduce the number of VPFVariant constructor
   // calls which topped 1.3 million in VMAP
   VPFVariant* get_field_value       (const CString& name);
   VPFVariant* get_field_value       (int index);

	CString get_file_name() { return m_file_name; }
	CString get_file_path() { return m_file_path; }

//---------------------------------------------------------
// The following public methods are applicable to the VPF
// database specification only.  They are not based on any
// class in MFC.
//---------------------------------------------------------
   int open(const CString& table_type, degrees_t lat, degrees_t lon,
      const CString& table_name);

//--- Protected Methods -----------------------------------
protected:
   // This function provides the capability for classes derived
   // from VPFRecordset to set their own member data after the
   // position is set. This function will always get called
   // when set_absolute_position() or set_percent_position()
   // is called().
   virtual void on_set_position();

   // This function provides the capability for classes derived
   // from VPFRecordset to add their own data verification after
   // the set is opened. This function will always get called
   // when open() is called().
   virtual void on_open();

   // Derived classes use this to verify that the given field number
   // has all the same attributes as those passed in.  If not, then
   // it uses the virtual function get_class_name() to create a
   // class-specific VPFException and throw it.
   bool verify_field(const TCHAR *name, const int intended_type,
    const int intended_length, VPFKeyType intended_key_type,
    bool is_mandatory = true);

   // Derived classes use this to verify that the filename that was
   // opened when this VPFRecordset-derived object was opened is the
   // correct filename. VPF requires that certain filenames are
   // reserved for certain table types, and this enforces that rule.
   bool verify_filename(const TCHAR *filename);

   // Derived classes implement this so that verify_field() and
   // verify_filename() can throw an exception with the appropriate
   // class name in the message.
   virtual const TCHAR *get_class_name();

	// Hides the details of using the variable-length index from the rest of
   // the recordset operations.  If there is variable-length data, then it
   // uses the variable-length index file.  If not, then it does the math.
   // In either case, it then calls CFile::Seek() to set the file pointer
   // to the appropriate location.
   void seek_to_record(int record_num);

public:
	   // Given a colon-delimited string representing the meta-data
   // for the fields in the table (see MIL-STD-2407), this
   // populates the m_fields list with VPFFieldInfo objects.
   void setup_field_info_list(CString& meta_data);


//--- Private Methods -------------------------------------
private:

   // These indexes and tables are friends of VPFRecordset so
   // they can access the path information in the recordset.
   // Their locations are defined in VPF documentation as relative
   // to the recordset to which they "belong" (i.e. the recordset
   // that they index, or that they provide more information for).
   friend class VPFSpatialIndex;
   friend class VPFVariableLengthIndex;
   friend class VPFNarrativeTable;

   // This constructor is private so no one can instantiate
   // a VPFRecordset without a valid path
   VPFRecordset();

   // Seeks to the beginning of m_file, reads in the header length,
   // and then reads in the header, assigning it to the CString.
   int read_in_header(CString& header);

   // Reads a field off disk, advancing the file pointer to the
   // end of the field, returning the value it contains
   int read_field(const VPFFieldInfo &field, VPFVariant *new_variant);

   // Reads an entire record off disk, advancing the file pointer
   // to the end of the record, putting the values of the fields
   // into the m_record_contents array
   int read_record();

   // Empties the m_record_contents array, freeing memory as necessary
   void clear_record_contents();

   // If the data is a triplet-id, then the leading byte of the data is
   // encoded in a such a manner as to allow dynamic data data lengths
   // on a per-record basis.  This function "decodes" the byte and then
   // returns the lengths of the pieces of data.
   void decode_triplet_id(TCHAR triplet_id, int lengths[3]);

   // This is a helper function for the find() operation.  Given the type of
   // find (i.e. find next, find previous, etc.) this will set up the start
   // and end points of the search as well as the direction that we will
   // scroll through the recordset.  e.g. find last would give the start_index
   // as the last record, the end_index as the first record, and a step of -1.
   void establish_limits_for_find(const vpf_find_type type,
      int& start_index, int& end_index, int& step) const;

   // This is a helper function for the find() operation.  Given a simplified
   // text search string, it will determine what comparisons need to be made.
   // It is static because it does not refer to ANY member data for this class.
   static bool parse_filter(const CString& filter, CString& logical_operator,
      CString& comparatorA, CString& comparatorB, CString& fieldA,
      CString& valueA, CString& fieldB, CString& valueB);

//--- Member Data -----------------------------------------
protected:
   CString      m_dbpath;         // used to be created with dp as parameter, now, just its path
   VPFLibrary*  m_library;        // pointer back to its library;

   // Static values - these should ALL be set by the end of open()
   CString      m_table_name;     // table name as given in the open() call
   CString      m_table_desc;     // table description from header
   CString      m_path_to_table;  // absolute path to this table

	// members used to perform file mapping.  File mapping is over twice
	// as fast as using a CFile
	CString m_file_name;
	CString m_file_path;
	HANDLE m_file_handle;
	HANDLE m_file_mapping_handle;
	DWORD m_file_size;
	BYTE *m_file_ptr;
	BYTE *m_start_file_pos;
	BYTE *m_current_file_pos;

   // describes the fields present in the table
   //CArray<VPFFieldInfo, VPFFieldInfo&> m_fields;
   std::vector<VPFFieldInfo> m_fields;

   int          m_record_count;
   int          m_row_length;

   VPFThematicIndex*       m_thematic_index;
   VPFSpatialIndex*        m_spatial_index;
   VPFVariableLengthIndex* m_var_length_index;
   VPFNarrativeTable*      m_narrative_table;

   // Dynamic values - these will vary with use of the recordset
   int          m_current;        // current record (zero-based index)

   // The contents of the record most recently read from disk.
   struct tag_record_contents
   {
      int id;
      //CArray<VPFVariant *, VPFVariant *> data;
      std::vector<VPFVariant *> data;
   } m_record_contents;
};


//------------------------------------------------------------------------------
//------------------------------- VPFFieldInfo ---------------------------------
//------------------------------------------------------------------------------
// In continuing with the parallels between DAO and VPF database access, the
// VPFFieldInfo structure holds all the information necessary about a VPF field.
//------------------------------------------------------------------------------
struct VPFFieldInfo
{
   CString     m_name;
   CString     m_desc;
   VPFType     m_type;
   VPFKeyType  m_key_type;
   int         m_length;
   short       m_ordinal_position;
   bool        m_required;
   bool        m_allow_zero_length;
   CString     m_foreign_name;
   CString     m_default_value;

   CString     m_value_description_table;
   CString     m_thematic_index;
   CString     m_column_narrative_table;

//--- Construction ----------------------------------------
public:
   VPFFieldInfo();
   VPFFieldInfo(const VPFFieldInfo& info);
   virtual ~VPFFieldInfo();
   VPFFieldInfo &operator=(const VPFFieldInfo& info);

//--- Public Methods --------------------------------------
public:
   // Returns whether the type is a fixed-length field
   bool is_fixed_length              () const;

//--- Private Methods -------------------------------------
private:
   // The setup_field_info_list() function needs to be a friend so
   // it can set call the VPFFieldInfo functions necessary to set
   // its data appropriately.
   friend void VPFRecordset::setup_field_info_list(CString& meta_data);

   // The following 8 methods are used to set values directly from
   // the field information given in the header.  For this reason,
   // they all take a CString containing part of the header text.
   void set_name                     (CString value);
   void set_type                     (CString value);
   int  set_length                   (CString value);
   void set_key_type                 (CString value);
   void set_desc                     (CString value);
   void set_value_description_table  (CString value);
   void set_thematic_index           (CString value);
   void set_column_narrative_table   (CString value);

   // The following 5 methods are used to set values indirectly.
   void set_ordinal_position         (int value);
   void set_required                 (bool value);
   void set_allow_zero_length        (bool value);
   void set_foreign_name             (CString value);
   void set_default_value            (CString value);
};


//-------------------------------------00---------------------------------------


#endif // !defined(VPFRECORDSET_H__89E5255B_8652_11D3_8663_00105A9B4838__INCLUDED_)
