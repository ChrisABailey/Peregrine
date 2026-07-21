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

// cpng.cpp

#include "..//stdafx.h"
extern "C"
{
//#include "pov_file.h"
}
#include "gif.h"
#include "cgif.h"
#include "..//util.h"
#include "..//defines.h"
#include "..//jpeg//jpeg.h"

// ****************************************************************
// ****************************************************************

int CGif::load(CString filename, int &width, int &height, int &bits_pixel, CString & error_msg)
{
//	BOOL palette = FALSE;
//	BYTE *imgdata = NULL;
   const int BUF_LEN = 121;
	char name[BUF_LEN], error[BUF_LEN];
	int rslt;
//	PTSTR name;
	CReadGif gif;

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
	strcpy_s(name, BUF_LEN, filename);

	m_filename = filename;

	IMAGE *Image;
	Image = new IMAGE;
	rslt = gif.Load_Gif_Image(Image, filename.GetBuffer(100), error);
	if (rslt != 0)
	{
		error_msg = error;
		delete Image;
		return FAILURE;
	}
	width = Image->iwidth;
	height = Image->iheight;
	bits_pixel = 24;

	m_image_width = width;
	m_image_height = height;

//	Destroy_Image(Image);

	delete Image;
	return SUCCESS;
}
// end of load

// ****************************************************************
// ****************************************************************
// ****************************************************************

int CGif::decode_interlace_line(IMAGE *Image, int y)
{
	int newy, extra2, extra4, extra8;

	extra8 = (Image->iheight % 8);
	if (extra8 > 0)
		extra8 = 1;
	extra4 = (Image->iheight % 4);
	if (extra4 > 0)
		extra4 = 1;
	extra2 = Image->iheight % 2;

	if ((y % 8) == 0)
	{
		newy = y / 8;
		return newy;
	}
	if ((y % 4) == 0)
	{
		newy = Image->iheight / 8;
		newy += extra8;
		newy += (y - 4) / 8;
		return newy;
	}
	if ((y % 2) == 0)
	{
		newy = Image->iheight / 4;
		newy += extra4;
		newy += (y - 2) / 4;
		return newy;
	}
	newy = Image->iheight / 2;
	newy += extra2;
	newy += y / 2;
	return newy;
	return newy;
}

// ****************************************************************
// ****************************************************************

int CGif::get_gif_rgb_subimage(CString filename, int start_x, int start_y, int width, int height, BYTE *img,
							   CString & error_msg)
{
//	BOOL palette = FALSE;
	int twidth, theight, rslt;
   const int BUF_LEN = 121;
	char name[BUF_LEN], error[BUF_LEN];
	CReadGif gif;

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
	strcpy_s(name, BUF_LEN, filename);

	IMAGE *Image;
	Image = new IMAGE;
	rslt = gif.Read_Gif_Image(Image, filename.GetBuffer(100), error);
	if (rslt != 0)
	{
		error_msg = error;
		delete Image;
		return FAILURE;
	}

	twidth = Image->iwidth;
	theight = Image->iheight;

//	int size = width * height * 3;
	int x, y, pos, outpos, numpix, col, iy;

	numpix = twidth * theight;
	outpos = 0;
	for (y=start_y; y<start_y+height; y++)
	{
		if (y >= theight)
			continue;
		iy = decode_interlace_line(Image, y);
		for (x=start_x; x<start_x+width; x++)
		{
			if (x >= twidth)
				continue;
			pos = y * twidth + x;
			if (Image->Interlaced)
				col = (int) Image->data.map_lines[iy][x];
			else
				col = (int) Image->data.map_lines[y][x];
			img[outpos+0] = (BYTE) Image->Colour_Map[col].Red;
			img[outpos+1] = (BYTE) Image->Colour_Map[col].Green;
			img[outpos+2] = (BYTE) Image->Colour_Map[col].Blue;
			outpos += 3;
		}
	}

	gif.Destroy_Image(Image);

//	for (y=0; y<theight; y++)
//		free(Image->data.map_lines[y]);

//	free(Image->data.map_lines);

//	free(Image->Colour_Map);

//	delete Image;

//	Destroy_Image(Image);

	return SUCCESS;
}
// end of get_gif_rgb_subimage

// ****************************************************************
// *****************************************************************

int CGif::get_file_info(CString filename, CString & info)
{
	CString edit;
	CString tstr, error_msg;
	CString cr("\r\n");
	int rslt, width, height, bits_pixel;
	CReadGif gif;

	// load the file
//	BOOL palette = FALSE;
//	BYTE *imgdata = NULL;
   const int BUF_LEN = 121;
	char name[BUF_LEN], error[BUF_LEN];
//	PTSTR name;

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
	strcpy_s(name, BUF_LEN, filename);

	IMAGE *Image;
	Image = new IMAGE;
	rslt = gif.Load_Gif_Image(Image, filename.GetBuffer(100), error);
	if (rslt != 0)
	{
		error_msg = error;
		delete Image;
		return FAILURE;
	}
	width = Image->iwidth;
	height = Image->iheight;
	bits_pixel = 24;

	tstr.Format("Filename: %s", (LPCSTR)filename );
	edit = tstr;
	edit += cr;
	tstr.Format("Image Size: %dx%d", width, height);
	edit += tstr;
	edit += cr;

	// image representaion
	tstr = "Representation: RGB";
	edit += tstr;
	edit += cr;

	if (Image->Interlaced)
		edit += "Interlaced: YES";
	else
		edit += "Interlaced: NO";
	edit += cr;
	if (Image->IsGif89)
		edit += "Version: GIF89A";
	else
		edit += "Version: GIF87A";
	edit += cr;

	info = edit;

	delete Image;
	return SUCCESS;
}
// end of get_file_info

// *************************************************************
// ****************************************************************


// create a tiled jpeg-encoded tiff file with half the dimensions of the GIF file
//
//  1. Read each tile of the currently loaded image and create jpegs tiles of 1/2 and 1/4 size
//  2. While creating the jpeg tiles keep track of the relative offsets for each tile
//  3. Write the tiff file header
//  4. Calculate the size of the first image so that the first IFD call contain the offset of the
//     second IFD.
//  5. Write the first image tags
//  6. Copy the first image data to the file
//  7. Write the second image tags
//  8. Copy the second image data to the file


int CGif::create_multi_image_tiff_overview(int *err_code, CString &err_msg, IImageLibCallback *callback)
{
	BYTE *timg;
	BYTE bt;
	double percent;
	int x, y, rslt;
	int tile_width, tile_height, tile_size, tile_cnt_x, tile_cnt_y;
	int tile_row, tile_col, tile, imgwidth, imgheight;
	int tile_cnt;
	int i, j;
	int itemnum, itemcnt, length;
	CUtil util;
	CString jpeg_name, overview_name, base_name, tname;
	int num_tiles_across, num_tiles_down, num_tiles, num_tile_pixels;
	FILE *fp = NULL, *jfp = NULL, *subfp[10], *sub_fp = NULL;
	int num_tags = 13;
	CString image_description, software;
	int tile_index;
	BYTE *sub_img = NULL;
	int ifd_offset;
	int num_levels, size, k;
	unsigned short ushort, tag_id, data_type;
	unsigned int uint, count, data_value, data_offset, image_offset;
   const int SUBNAME_LEN = 250;
	char subname[10][SUBNAME_LEN];
	CJpeg jpeg;
	int num_uncompressed_bytes;
	int samp;
   UINT num_compressed_bytes;
	int sub_tile_width, sub_tile_height, sub_tile_size, sub_width, sub_height, level, sub_ndx;
	unsigned int sub_size;
	unsigned int *sub_tile_byte_counts[10];
	int r, g, b, sx, sy, sj;
	int key_filesum;
	BOOL true_black;

	tile_cnt = 1;
	level = 0;

	// calc the number of levels needs to get to 512
	size = m_image_width;
	num_levels = 1;
	while (size > 512)
	{
		num_levels++;
		size /= 2;
	}
	if (m_image_height > m_image_width)
	{
		size = m_image_height;
		num_levels = 1;
		while (size > 512)
		{
			num_levels++;
			size /= 2;
		}
	}

	// calculate the 1 MB filesum of original file
	rslt = util.calc_file_1m_sum(m_filename, &key_filesum, err_msg);
	if (rslt != SUCCESS)
	{
		*err_code = 3;
		err_msg = "Error getting file size code";
		return FAILURE;
	}

	image_description.Format("NumLevels=%d, Overview Image for %s, SrcMsum=%d,", num_levels, (LPCSTR)m_filename, key_filesum);
	software = "ImageLib.dll";

   CString csTempBaseFilespec;
   util.calc_overview_temp_base_file( m_filename, csTempBaseFilespec );
   jpeg_name = csTempBaseFilespec + _T("_ov_temp.jpg");
	
	timg = NULL;

	// calculate tile sizes for non-tiled images
	tile_width = 1024;
	tile_height = 1024;
	tile_size = tile_width * tile_height;
	imgwidth = m_image_width;
	imgheight = m_image_height;
	tile_cnt_x = imgwidth / tile_width;
	tile_cnt_y = imgheight / tile_height;
	if ((imgwidth % tile_width) != 0)
		tile_cnt_x++;
	if ((imgheight % tile_height) != 0)
		tile_cnt_y++;

	num_tiles = tile_cnt_x * tile_cnt_y;

	sub_tile_width = tile_width / 2;
	sub_tile_height = tile_height / 2;
	sub_img = (BYTE*) malloc(sub_tile_width * sub_tile_height * 3);
	if (sub_img == NULL)
	{
		*err_code = 2;
		err_msg = "Error allocating memory";
		return FAILURE;
	}

	// generate the subfile names and open the files
   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
	base_name = jpeg_name.Left(jpeg_name.GetLength()-4);
	for (k=0; k<num_levels; k++)
	{
		sprintf_s(subname[k], SUBNAME_LEN, "%s_%d.dat", (LPCSTR)base_name, k);
      subfp[k] = NULL;
		fopen_s(&(subfp[k]), subname[k], "wb");
		if (subfp[k] == NULL)
		{
			*err_code = 4;
			err_msg.Format("Error opening file: %s", subname[k]);
			goto FAIL;
		}
		sub_tile_byte_counts[k] = (unsigned int*) malloc(num_tiles * sizeof(unsigned int));
		if (sub_tile_byte_counts[k] == NULL)
		{
			*err_code = 2;
			err_msg = "Error allocating memory";
			goto FAIL;
		}
	}

	// restore the initial sub_tile sizes
	sub_tile_width = tile_width / 2;
	sub_tile_height = tile_height / 2;
	sub_tile_size = sub_tile_width * sub_tile_height;
	sub_width = imgwidth / 2;
	sub_height = imgheight / 2;

	num_tiles_across = tile_cnt_x;
	num_tiles_down = tile_cnt_y;
	num_tiles = num_tiles_across * num_tiles_down;
	num_tile_pixels = tile_size;
	num_uncompressed_bytes = num_tile_pixels;

	timg = (BYTE*) malloc(num_tile_pixels * 3);
	if (timg == NULL)
	{
		*err_code = 2;
		err_msg = "Error allocating memory";
		goto FAIL;
	}

	// note that the last low and column of tiles may need to be padded with zeroes
	tile_index = 0;
	itemnum = tile_cnt_y * tile_cnt_x;
	itemcnt = 0;
	for (tile_row=0; tile_row<tile_cnt_y; tile_row++)
	{
		for (tile_col=0; tile_col<tile_cnt_x; tile_col++)
		{
			tile = tile_cnt_x * tile_row + tile_col;
			percent = 100.0 * (double) tile / (double) tile_cnt;
			{
				int min_hpix, max_hpix, min_vpix, max_vpix;
			
				min_hpix = tile_col * tile_width;
				max_hpix = min_hpix + tile_width - 1;
				min_vpix = tile_row * tile_height;
				max_vpix = min_vpix + tile_height - 1;
				rslt = get_gif_rgb_subimage(m_filename, min_hpix, min_vpix, tile_width, tile_height, timg, err_msg);
				if (rslt == FAILURE)
					goto FAIL;
			}

			samp = 2;
			for (level=0; level<num_levels; level++)
			{
				// subsample the tile image for the sub2 image
				for (y=0; y<sub_tile_height; y++)
				{
					for (x=0; x<sub_tile_width; x++)
					{
						j = (y * samp * tile_width) + x * samp;
						sub_ndx = (y * sub_tile_width) + x;
						sub_ndx *= 3;
						// interpolate
						true_black = FALSE;
						r = g = b = 0;
						for (sy=0; sy<samp; sy++)
						{
							for (sx=0; sx<samp; sx++)
							{
								sj = j + (sy * tile_width) + sx;
								r += timg[sj*3+0];
								g += timg[sj*3+1];
								b += timg[sj*3+2];
								if ((timg[sj*3+0] == 0) && (timg[sj*3+1] == 0) && (timg[sj*3+2] == 0))
									true_black = TRUE;
							}
						}
						sub_img[sub_ndx+0] = (BYTE) (r / (samp * samp));
						sub_img[sub_ndx+1] = (BYTE) (g / (samp * samp));
						sub_img[sub_ndx+2] = (BYTE) (b / (samp * samp));
						if (!true_black)
						{
							if (sub_img[sub_ndx+0] == 0)
								sub_img[sub_ndx+0] = 1;
							if (sub_img[sub_ndx+1] == 0)
								sub_img[sub_ndx+1] = 1;
							if (sub_img[sub_ndx+2] == 0)
								sub_img[sub_ndx+2] = 1;
						}
					}
				}
				// write the tile out to a jpeg file for sub2
				jpeg.write_jpeg_file( jpeg_name, sub_tile_width, sub_tile_height, sub_img, 80, err_msg);
            util.get_file_size( jpeg_name, &num_compressed_bytes, err_msg );
				sub_tile_byte_counts[level][tile_index] = num_compressed_bytes;

				// add the jpeg image to the sub2 file
            TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
				fopen_s(&jfp, jpeg_name, "rb");
				if (jfp == NULL)
					goto FAIL;
				fread(sub_img, num_compressed_bytes, 1, jfp);
				fwrite(sub_img, num_compressed_bytes, 1, subfp[level]);
				fclose(jfp);
				samp *= 2;
				sub_tile_width /= 2;
				sub_tile_height /= 2;
			}

			sub_tile_width = tile_width / 2;
			sub_tile_height = tile_height / 2;

			tile_index++;

			itemcnt++;

			double percent = 100.0 * (double) itemcnt / (double) itemnum;
			CString label = "Creating Overview File";
			if (!util.send_user_update_and_continue(callback, percent, label))
			{
				*err_code = USER_ABORT;
				err_msg = "Operation canceled by user";
				goto FAIL;
			}
		}
	}
	
	for (k=0; k<num_levels; k++)
		fclose(subfp[k]);
	free(timg);
	timg = NULL;
	if (sub_img)
		free(sub_img);
	sub_img = NULL;

	// *****************************************************
	// write the tiff file
	// *****************************************************

	// create the file
	rslt = util.calc_overview_name(m_filename, overview_name, err_msg);
	if (rslt != SUCCESS)
		goto FAIL;

   TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
	fopen_s(&fp, overview_name, "wb" );
	if ( fp == NULL )
	{
		*err_code = 4;
		err_msg.Format("Error opening file - %s - for writing", (LPCSTR)overview_name );
		goto FAIL;
	}

	////////////////////////////////////////////////
	// Write the file header
	////////////////////////////////////////////////
	// bytes 0-1
	// write byte order string
	fwrite( "II", 1, 2, fp );

	// bytes 2-3
	// write 42 to identify as a tiff file
	ushort = 42;
	fwrite( &ushort, 2, 1, fp );

	// bytes 4-7
	// write offset of image file directory
	uint = 8;
	fwrite( &uint, 4, 1, fp );

	ifd_offset = 8;

	for (level = 0; level < num_levels; level++)
	{
		// calculate data offset for writing data outside of image file directory
		data_offset = ifd_offset;    // IFP header
		data_offset += sizeof( unsigned short );    // tag count
		data_offset += num_tags * 12;   // 15 tags at 12 bytes each
		data_offset += sizeof( unsigned int );    // terminating zero

		// calculate image offset
		image_offset = data_offset;   // start at data offset
		image_offset += 3 * sizeof( unsigned short );   // bits per sample
		image_offset += num_tiles * sizeof( unsigned int ); // tile offsets
		image_offset += num_tiles * sizeof( unsigned int ); // tile bytes
		image_offset += image_description.GetLength( ) + 1; // image description
		image_offset += software.GetLength( ) + 1; // software
   
		// compute the offset of the next IFD
		ifd_offset = image_offset;
		tname = subname[level];
		util.get_file_size(tname, &sub_size, err_msg);
		ifd_offset += sub_size;

		// image file directory
		// bytes 8-9
		// write number of tags
		ushort = (unsigned short) num_tags;
		fwrite( &ushort, 2, 1, fp );

		// bytes 10-21
		// write image width tag
		tag_id = 256;
		data_type = 3;
		count = 1;
		data_value = sub_width;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 22-33
		// write image length tag
		tag_id = 257;
		data_type = 3;
		count = 1;
		data_value = sub_height;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 34-45
		// write bits per sample tag
		tag_id = 258;
		data_type = 3;
		count = 3;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_offset, 4, 1, fp );
		data_offset += count * sizeof( unsigned short );

		// bytes 46-57
		// write compression tag
		tag_id = 259;
		data_type = 3;
		count = 1;
		data_value = 7;        // jpeg
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 58-69
		// write photometric interpretation tag
		tag_id = 262;
		data_type = 3;
		count = 1;
		data_value = 6;  // YCbCr
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 70-81
		// write image description tag
		tag_id = 270;
		data_type = 2;
		count = image_description.GetLength( ) + 1;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_offset, 4, 1, fp );
		data_offset += count * sizeof( unsigned char );

		// bytes 82-93
		// write samples per pixel tag
		tag_id = 277;
		data_type = 3;
		count = 1;
		data_value = 3;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 84-105
		// write planar configuration tag
		tag_id = 284;
		data_type = 3;
		count = 1;
		data_value = 1;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 106-117
		// write software tag
		tag_id = 305;
		data_type = 2;
		count = software.GetLength( ) + 1;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_offset, 4, 1, fp );
		data_offset += count * sizeof( unsigned char );

		// bytes 118-129
		// write tile width tag
		tag_id = 322;
		data_type = 3;
		count = 1;
		data_value = sub_tile_width;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 130-141
		// write tile length tag
		tag_id = 323;
		data_type = 3;
		count = 1;
		data_value = sub_tile_height;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_value, 4, 1, fp );

		// bytes 142-153
		// write tile offsets tag
		tag_id = 324;
		data_type = 4;
		count = num_tiles;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_offset, 4, 1, fp );
		data_offset += count * sizeof( unsigned int );

		// bytes 154-165
		// write tile byte count tag
		tag_id = 325;
		data_type = 4;
		count = num_tiles;
		fwrite( &tag_id, 2, 1, fp );
		fwrite( &data_type, 2, 1, fp );
		fwrite( &count, 4, 1, fp );
		fwrite( &data_offset, 4, 1, fp );
		data_offset += count * sizeof( unsigned int );

		// bytes 166-169
		// write terminating zero
		if (level >= num_levels - 1)
			uint = 0;
		else
			uint = ifd_offset;
		fwrite( &uint, 4, 1, fp );

		// write bits per sample values
		ushort = 8;
		fwrite( &ushort, 2, 1, fp );
		fwrite( &ushort, 2, 1, fp );
		fwrite( &ushort, 2, 1, fp );

		// write image description
		length = image_description.GetLength( ) + 1;  // include terminating null
		fwrite( image_description.GetBuffer( length ), length, 1, fp );

		// write software
		length = software.GetLength( ) + 1;  // include terminating null
		fwrite( software.GetBuffer( length ), length, 1, fp );

		// write out tile offsets
		uint = image_offset;
		for( i = 0; i < num_tiles; i++ )
		{
			fwrite( &uint, 4, 1, fp );
			uint += sub_tile_byte_counts[level][i];
		}

		// write out tile byte counts
		for( i = 0; i < num_tiles; i++ )
			fwrite( &sub_tile_byte_counts[level][i], 4, 1, fp );


		// write out compressed image data
		tname = subname[level];
      TRACE(_T("This code requires testing due to security changes (Line %d File %s).  Remove this message after this line has been executed."), __LINE__, __FILE__);
		fopen_s(&sub_fp, tname, "rb");
		for (i=0; i< (int) sub_size; i++)
		{
			fread(&bt, 1, 1, sub_fp);
			fwrite(&bt, 1, 1, fp);
		}
		fclose(sub_fp);

		sub_tile_width /= 2;
		sub_tile_height /=2;
		sub_width /= 2;
		sub_height /= 2;
		image_description.Format("Level=%d, Overview Image for %s", level, (LPCSTR)m_filename );
	}

	fclose(fp);

	for (k=0; k<num_levels; k++)
	{
		if (sub_tile_byte_counts[k])
			free(sub_tile_byte_counts[k]);

		tname = subname[level];
		DeleteFile(tname);
	}

	util.clear_callback(callback);
	return SUCCESS;

FAIL:
	if (timg)
		free(timg);

	if (sub_img)
		free(sub_img);
	for (k=0; k<num_levels; k++)
	{
		if (sub_tile_byte_counts[k])
			free(sub_tile_byte_counts[k]);

		tname = subname[k];
		DeleteFile(tname);
	}

	util.clear_callback(callback);
	return FAILURE;
}
// end of create_multi_image_tiff_overview

// ****************************************************************
// ****************************************************************

// computes image histogram
// histogram must be allocated and size of 32768 

/*
int CGif::compute_histogram(CString filename, int *histogram, CString &error_msg)
{
	int input_width, input_height;
	CString file_name, image_type_description, error_message;
	int k, x, y, rslt, pos, off;
	int chunk_size, num_chunks;
	CJpeg jpeg;
	int cnt, trans_inc;

	// open the input file
	rslt = jpeg.load( filename, input_width, input_height, error_msg );
	if (rslt != SUCCESS)
	{
		return FAILURE;
	}

	trans_inc = (input_width * input_height) / 10000;
	trans_inc += 5;

	// clear the frequency arrays
	for (k=0; k<256; k++)
	{
		data->m_freq_red[k] = 0;
		data->m_freq_grn[k] = 0;
		data->m_freq_blu[k] = 0;
	}

	// initialize the histogram
	histogram = (int*) malloc(32768 * sizeof(int));
	if (histogram == NULL)
	{
		return FAILURE;
	}
	for (k=0; k< 32768; k++)
		histogram[k] = 0;

	chunk_size = 100;
	num_chunks = input_height / chunk_size;

	// allocate memory for the output image horizontal line
	BYTE *buf_red, *buf_grn, *buf_blu;
	unsigned int red, grn, blu;
	buf_red = (BYTE*) malloc(input_width * chunk_size);
	buf_grn = (BYTE*) malloc(input_width * chunk_size);
	buf_blu = (BYTE*) malloc(input_width * chunk_size);

	cnt = 0;
	// get the histogram

	BYTE bred, bgrn, bblu;
	CGeoTiff geotiff;

	m_buf_width = 0;

	for (y=0; y<input_height; y++)
	{
		for (x=0; x<input_width; x++)
		{
			get_file_input_pixel(&geotiff, x, y, FALSE, &bred, &bgrn, &bblu);

			// clip to 5 bits
			off = (y * input_width) + x;
			red = (unsigned int) (bred >> 3);
			grn = (unsigned int) (bgrn >> 3);
			blu = (unsigned int) (bblu >> 3);
			pos = (red << 10) + (grn << 5) + blu;
			ASSERT(pos < 32768);
			histogram[pos] += 1;
			ASSERT(red < 256);
			ASSERT(grn < 256);
			ASSERT(blu < 256);
			(data->m_freq_red[red])++;
			(data->m_freq_grn[grn])++;
			(data->m_freq_blu[blu])++;
			cnt++;
		}
	}

	// delete the input buffer
	free(buf_red);
	free(buf_grn);
	free(buf_blu);

	// if there is a transparent color, make sure is shows up in the histogram
//	if (data->m_has_transparent_color)
	{
		// clip to 5 bits
		red = GetRValue(data->m_transparent_color);
		grn = GetGValue(data->m_transparent_color);
		blu = GetBValue(data->m_transparent_color);
		red = (int) (red >> 3);
		grn = (int) (grn >> 3);
		blu = (int) (blu >> 3);
		pos = (red << 10) + (grn << 5) + blu;
		histogram[pos] += trans_inc;
		(data->m_freq_red[red]) += trans_inc;
		(data->m_freq_grn[grn]) += trans_inc;
		(data->m_freq_blu[blu]) += trans_inc;
	}

	CDib dib;

	// get the quant pref from the registry
	CString sdata;
	CUtil util;
	int quant_type;
	sdata.Format("%d", QUANT_VARIANCE);
	sdata = util.get_registry_string("QuantPref", sdata);
	quant_type = atoi(sdata);

	switch(quant_type)
	{
		case QUANT_VARIANCE:	  
			{
				// variance based quantization
				CQuant qnt;
				BYTE *colormap;

				colormap = (BYTE*) malloc(1024);
				qnt.colorquant(data->m_freq_red, data->m_freq_grn, 
									data->m_freq_blu,	histogram, 
									input_height * input_width,
									colormap, 216, 5, m_data->m_index);

				for (k=0; k<216; k++)
				{
					m_output_color_map_red[k] = colormap[k*3+0];
					m_output_color_map_grn[k] = colormap[k*3+1];
					m_output_color_map_blu[k] = colormap[k*3+2];
				}
				free(colormap);
			}
			break;

		case QUANT_MEDIAN_CUT:
			{
				BYTE red[216], green[216], blue[216];

				// use median cut algorithm to reduce colors to 216
				dib.median_cut(histogram, 216, red, green, blue, m_data->m_index);	  

				for (k=0; k<216; k++)
				{
					m_output_color_map_red[k] = red[k];
					m_output_color_map_grn[k] = green[k];
					m_output_color_map_blu[k] = blue[k];
				}
			}
			break;
	}

	free(histogram);

	return SUCCESS;
}
// end of compute_histogram
*/
// ****************************************************************
// ****************************************************************
