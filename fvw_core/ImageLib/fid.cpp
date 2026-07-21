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

// fid.cpp

#include "stdafx.h"
#include "fid.h"
#include "util.h"
#include <math.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <string.h>



// ****************************************************************
// ****************************************************************

int CFid::open( const CString& filename  )
{
   INT pos = filename.Right( 5 ).Find( _T('.') );
   m_filename = ( pos < 0 )
      ? filename // No extension found
      : filename.Left( filename.ReverseFind( '.' ) );
   m_filename += _T(".fid");
   return ReadFile();
}
   
int CFid::ReadFile()
{
   INT iResult = FAILURE;
   FILE* pf = NULL;
   try
   {
      do
      {
         fopen_s( &pf, m_filename, "rt");
         if ( pf == NULL )
            return FAILURE;

         CHAR buf[201];

         // Read the sentinel string
         fgets( buf, 200, pf );
         if ( buf != strstr( buf, GetSentinalString() ) )
            break;   // Failed

         m_list.clear();

         while ( !feof( pf ) )
         {
            // read the next line
            fgets( buf, 200, pf );
            if ( feof( pf ) )
               break;

            buf[200] = '\0';

            CString tstr = buf;
            tstr.TrimRight();
            INT len = tstr.GetLength();
            INT pos = tstr.Find(':');
            if ( pos > 0 )
               m_list.insert( FidMap::value_type( tstr.Left( pos ), tstr.Mid( pos + 1 ) ) );
         }  // Value loop

         iResult = SUCCESS;
      } while ( FALSE );
   } catch ( ... ) { }

   if ( pf != NULL )
      fclose( pf );

   return iResult;
}  // End of ReadFile()


// ****************************************************************
// ****************************************************************

int CFid::save() const
{
	if ( m_filename.GetLength() < 5 )
		return FAILURE;

   if ( m_list.empty() )
		return FAILURE;

#ifdef _WIN32
   CreateAllDirectories( CT2W( m_filename ) );
#endif  // POSIX: caller ensures the directory exists

	FILE* pf = NULL;
   fopen_s( &pf, m_filename, "wb" );
	if ( pf == NULL)
		return FAILURE;

   fprintf( pf, "%s\r\n", GetSentinalString() );

   for ( FidMap::const_iterator it = m_list.begin(); it != m_list.end(); it++ )
   {
#ifdef UNICODE
      fprintf( pf, "%S: %S\r\n", it->first, it->second );
#else
      fprintf( pf, "%s: %s\r\n", (LPCSTR)it->first, (LPCSTR)it->second );
#endif
   }

	// close file
	fclose( pf );

	return SUCCESS;
}

// ****************************************************************
// ****************************************************************

int CFid::save_as( const CString& filename )
{
	m_filename = filename;
	return save();
}

// ****************************************************************
// ****************************************************************

int CFid::remove_key( const CString& key )
{
   return m_list.erase( key ) == 0 ? FAILURE : SUCCESS;
}

// ****************************************************************
// ****************************************************************

int CFid::get_key( const CString& key, CString& value ) const
{
   FidMap::const_iterator it = m_list.find( key );
   if ( it == m_list.end() )
      return FAILURE;

   value = it->second;
   return SUCCESS;
}

// ****************************************************************
// ****************************************************************

int CFid::get_projected_cs_type(int *cs_type, CString & cs_type_str) const
{
	BOOL found = FALSE;
	CString tstr, value;
	int rslt;

	rslt = get_key(FID_CS_TYPE, value);
	if (rslt == SUCCESS)
	{
		found = TRUE;
		*cs_type = atoi(value);
	}
	else
		return FAILURE;

	// get optional string
	rslt = get_key(FID_CS_TYPE_STR, value);
	if (rslt == SUCCESS)
	{
		cs_type_str = value;
	}

	return SUCCESS;
}


// ****************************************************************
// ****************************************************************

int CFid::set_projected_cs_type(int cs_type, const CString& cs_type_str)
{
	CString type_str;

	// check for valid cs type
	if ((cs_type < 26703) || (cs_type > 32760))
		return FAILURE;

	// check to see if it is already there
	remove_key(FID_CS_TYPE);
	remove_key(FID_CS_TYPE_STR);

	type_str.Format( _T("%d"), cs_type );
   m_list.insert( FidMap::value_type( FID_CS_TYPE, type_str ) );

   if (cs_type_str.GetLength() > 0)
      m_list.insert( FidMap::value_type( FID_CS_TYPE_STR, cs_type_str ) );

	return SUCCESS;
}


// ****************************************************************
// ****************************************************************

int CFid::get_histogram( PUINT hist ) const  // 256 luminance values
{
	int j, k, ndx, rslt;
	CString key, value, tstr;

	if (hist == NULL)
		return FAILURE;

	try
	{
		for (k=0; k<16; k++)
		{
			key.Format("Histogram%02d", k);
			rslt = get_key(key, value);
			if (rslt != SUCCESS)
				return FAILURE;
			for (j=0; j<16; j++)
			{
				ndx = (k * 16) + j;
				tstr = value.Mid(j*5, 4);
				hist[ndx] = atoi(tstr);
			}
		}
	}
	catch (...)
	{
		return FAILURE;
	}

	return SUCCESS;
}

// ****************************************************************
// ****************************************************************

int CFid::set_histogram( const PUINT hist )  // 256 luminance value counts
{
	int j, k, maxval, val, ndx;
	CString key, value, tstr;

	if (hist == NULL)
		return FAILURE;

	try
	{
		// find the highest value
		maxval = 0;
		for (k=0; k<256; k++)
		{
			if ((int) hist[k] > maxval)
				maxval = hist[k];
		}

		// scale the histogram if necessary
		if (maxval > 9999)
		{
			double scale;

			scale = 9999.0 / (double) maxval;
			for (k=0; k<256; k++)
			{
				val = (int) (((double) hist[k] * scale) + 0.5);
				hist[k] = val;
			}
		}

		// set the histogram values
		for (k=0; k< 16; k++)
		{
			key.Format("Histogram%02d", k);
			value = "";
			for (j=0; j<16; j++)
			{
				ndx = (k * 16) + j;
				tstr.Format("%04d ", hist[ndx]);
				value += tstr;
			}
         m_list.insert( FidMap::value_type( key, value ) );
		}
	}
	catch (...)
	{
		return FAILURE;
	}

	return SUCCESS;
}  // end of set_histogram


// ****************************************************************
// ****************************************************************
#if 0
int CFid::get_tile_offsets(int *cnt, int **offset) 
{
	int j, k, ndx, rslt, linecnt, icnt;
	CString key, value, tstr;

	key = "Tile Offset Count";
	rslt = get_key(key, value);
	if (rslt != SUCCESS)
		return rslt;

	icnt = atoi(value);

	*offset = (int*) calloc(icnt, sizeof(int));
	linecnt = icnt / 10;
	linecnt++;

	try
	{
		for (k=0; k<linecnt; k++)
		{
			key.Format("Tile Offsets%05d", k);
			rslt = get_key(key, value);
			if (rslt != SUCCESS)
				return FAILURE;
			for (j=0; j<10; j++)
			{
				ndx = (k * 10) + j;
				if (ndx > icnt)
					continue;
				tstr = value.Mid(j*11, 10);
				(*offset)[ndx] = atoi(tstr);
			}
		}
	}
	catch (...)
	{
		return FAILURE;
	}

	return SUCCESS;
}

// ****************************************************************
// ****************************************************************

int CFid::set_tile_offsets(int cnt, int *offset) 
{
	int j, k, ndx, linecnt;
	CString key, value, tstr;
	CFidItem *item;

	if (offset == NULL)
		return FAILURE;

	key = "Tile Offset Count";
	value.Format("%d", cnt);
	item = new CFidItem;
	item->m_key = key;
	item->m_value = value;
	m_list.AddTail(item);

	// put 10 offsets on a line
	linecnt = cnt / 10;
	linecnt++;

	try
	{
		// set the histogram values
		for (k=0; k< linecnt; k++)
		{
			key.Format("Tile Offsets%05d", k);
			value = "";
			for (j=0; j<10; j++)
			{
				ndx = (k * 10) + j;
				if (ndx >= cnt)
					tstr = "0000000000 ";
				else
					tstr.Format("%010d ", offset[ndx]);
				value += tstr;
			}
			item = new CFidItem;
			item->m_key = key;
			item->m_value = value;
			m_list.AddTail(item);
		}
	}
	catch (...)
	{
		return FAILURE;
	}

	return SUCCESS;
}
// end of set_tile_offsets
#endif

// ****************************************************************
// ****************************************************************

int CFid::get_contrast(int *minval, int *ctrval, int *maxval) const
{
	int rslt;
	CString value;
	
	rslt = get_key(FID_CONTRAST, value);
	if (rslt != SUCCESS)
		return FAILURE;

	if (value.GetLength() < 11)
		return FAILURE;

	*minval = atoi(value.Mid(0, 3));
	*ctrval = atoi(value.Mid(4, 3));
	*maxval = atoi(value.Mid(8, 3));

	return SUCCESS;
}

// ****************************************************************
// ****************************************************************

int CFid::set_contrast(int minval, int ctrval, int maxval)
{
   CString tstr;
   if ( minval < 0 || minval > ctrval || ctrval > maxval || maxval > 255 )
      return FAILURE;

	remove_key( FID_CONTRAST );
	tstr.Format( _T("%03d %03d %03d"), minval, ctrval, maxval );
   m_list.insert( FidMap::value_type( FID_CONTRAST, tstr ) );

	return SUCCESS;
}


// ****************************************************************
// ****************************************************************

int CFid::get_position(int *startx, int *starty, double *zoom) const
{
	int rslt;
	CString value;
	
	rslt = get_key(FID_POSITION, value);
	if (rslt != SUCCESS)
		return FAILURE;

	if (value.GetLength() < 19)
		return FAILURE;

	*startx = atoi(value.Mid(0, 5));
	*starty = atoi(value.Mid(6, 5));
	*zoom = atof(value.Mid(12, 8));

	return SUCCESS;
}


// ****************************************************************
// ****************************************************************

int CFid::set_position( int startx, int starty, double zoom  )
{
	CString tstr;

	if ((startx < 0) || (startx > 99999))
		return FAILURE;
	if ((starty < 0) || (starty > 99999))
		return FAILURE;
	if ((zoom <= 0.0) || (zoom > 8.0))
		return FAILURE;

	remove_key( FID_POSITION );
	tstr.Format( _T("%05d %05d %08.6f"), startx, starty, zoom );
   m_list.insert( FidMap::value_type( FID_POSITION, tstr ) );

	return SUCCESS;
}

// ****************************************************************
// ****************************************************************
// ****************************************************************


// ****************************************************************
// ****************************************************************

int CGid::make_gid_name( const CString& filename, CString& gidname, BOOL old_style ) const
{
	CString name, tstr, key, value, ext, path, src_path;
	int pos, len, k;
	CUtil util;
	char ch;

	if (old_style)
	{
		pos = filename.ReverseFind('.');

      gidname = ( pos < 0 )
         ? filename : filename.Left( pos - 1 );
		gidname += _T(".gid");
	   return SUCCESS;
	}

	ext = util.extract_extension(filename);
	ext.MakeLower();

	path = util.get_default_destination(); 
	path += "\\gid\\";
			
	// check that the default directory exists, if not create it
	if (_access(path, 0))
		CreateDirectory(path, NULL);

	src_path = util.extract_path(filename);
	src_path.MakeLower();
	len = src_path.GetLength();
	for (k=0; k<len; k++)
	{
		ch = src_path.GetAt(k);
		if ((ch == '\\') || (ch == ':'))
			src_path.SetAt(k, '_');
	}

	name = util.extract_filename(filename);
	name.MakeLower();

	// remove the extension
	pos = name.ReverseFind('.');
	name = name.Left(pos);
	name += "_";
	name += ext;
	name += ".gid";

	gidname = path + src_path + name;
	return SUCCESS;
}

// ****************************************************************
// ****************************************************************

int CGid::open( const CString& filename )
{
	if ( SUCCESS != make_gid_name( filename, m_filename, FALSE ) )
		return FAILURE;

   return ReadFile();
}  // End of CGid::open()

// ****************************************************************
// ****************************************************************

int CGid::get_contrast( const CString& filename, int *minval, int *ctrval, int *maxval)
{
   if ( SUCCESS != open( filename ) )
		return FAILURE;

   return CFid::get_contrast( minval, ctrval, maxval );
}

// ****************************************************************
// ****************************************************************

int CGid::set_contrast( const CString& filename, int minval, int ctrval, int maxval )
{
   if ( SUCCESS != open( filename ) )
		return FAILURE;

   return CFid::set_contrast( minval, ctrval, maxval );
}


// ****************************************************************
// ****************************************************************

int CGid::get_position( const CString& filename, int *startx, int *starty, double *zoom )
{
   if ( SUCCESS != open( filename ) )
		return FAILURE;

   return CFid::get_position( startx, starty, zoom );
}


// ****************************************************************
// ****************************************************************

int CGid::set_position( const CString& filename, int startx, int starty, double zoom )
{
   if ( SUCCESS != open( filename ) )
		return FAILURE;

   return CFid::set_position( startx, starty, zoom );
}


// ****************************************************************
// ****************************************************************

int CGid::get_formula( const CString& filename, CString& form_name,
   CString& red_form, CString& grn_form, CString& blu_form )
{
   if ( SUCCESS != open( filename ) )
		return FAILURE;

	if ( SUCCESS != get_key( FORMULA_NAME, form_name )
         || SUCCESS != get_key( RED_FORMULA, red_form )
         || SUCCESS != get_key( GRN_FORMULA, grn_form )
         || SUCCESS != get_key( BLU_FORMULA, blu_form ) )
		return FAILURE;

   return SUCCESS;
}


// ****************************************************************
// ****************************************************************

int CGid::set_formula( const CString& filename, const CString& form_name,
         const CString& red_form, const CString& grn_form, const CString& blu_form )
{
	open( filename );    // Ignore missing file

   remove_key( FORMULA_NAME ); // Remove existing values
	remove_key( RED_FORMULA );
	remove_key( GRN_FORMULA );
   remove_key( BLU_FORMULA );
	
   m_list.insert( FidMap::value_type( FORMULA_NAME, form_name ) );
   m_list.insert( FidMap::value_type( RED_FORMULA, red_form ) );
   m_list.insert( FidMap::value_type( GRN_FORMULA, grn_form ) );
   m_list.insert( FidMap::value_type( BLU_FORMULA, blu_form ) );

	return save();
}


// ****************************************************************
// ****************************************************************

int CGid::open_gid_file_for_read( const CString& filename)
{
	CUtil util;
	int rslt;
	CString name, tstr;

	rslt = open(filename);
	if (rslt != SUCCESS)
	{
		CString path;
		char buf[300];
		int pos;

		// they the PFPS georect directory
		path = util.get_default_source();
		if (path.GetLength() < 8)
		{
#ifdef _WIN32
			// try the current installation directory
			GetModuleFileName(NULL, buf, 200);
			path = buf;
			pos = path.ReverseFind('\\');
			path = path.Left(pos+1);
			path += "fid";
#endif  // POSIX: no install dir; get_default_source only
		}
		else
		{
			path.MakeLower();
			pos = path.Find("georect");
			path = path.Left(pos);
			path += "data\\georect";
		}

		// try writing it to the thumbs directory
		util.create_directory(path);
		tstr = util.extract_filename(filename);
		name = path;
		name += "\\";
		name += tstr;
		rslt = open(name);
	}

	return rslt;
}
// end of open_gid_file_for_read

// ****************************************************************
// ****************************************************************

int CGid::open_gid_file_for_write( const CString& filename)
{
	CUtil util;
	int rslt;
	CString name, tstr;

	rslt = open(filename);
	if (rslt != SUCCESS)
	{
		FILE *fp = NULL;
		CString path;
		char buf[300];
		int pos;

		// test for directory writability
		path = util.extract_path(filename);
		path += "\\test.txt";
		fopen_s( &fp, path, "wb");
		if (fp != NULL)
		{
			fclose(fp);
			DeleteFile(path);
			return SUCCESS;
		}

		tstr = util.extract_filename(filename);

		// they the PFPS georect directory
		path = util.get_default_source();
		if (path.GetLength() < 8)
		{
#ifdef _WIN32
			// try the current installation directory
			GetModuleFileName(NULL, buf, 200);
			path = buf;
			pos = path.ReverseFind('\\');
			path = path.Left(pos+1);
			path += "fid";
#endif  // POSIX: no install dir; get_default_source only
		}
		else
		{
			path.MakeLower();
			pos = path.Find("georect");
			path = path.Left(pos);
			path += "data\\georect";
		}

		// try writing it to the thumbs directory
		util.create_directory(path);
		name = path;
		name += "\\";
		name += tstr;
		rslt = open(name);
	}

	return rslt;
}
// end of open_gid_file_for_write

// ****************************************************************
// ****************************************************************


