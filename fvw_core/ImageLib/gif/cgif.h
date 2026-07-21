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

// cgif.h

#ifndef CGIF_H
#define CGIF_H

#include "gif.h"

#ifndef SUCCESS
#define SUCCESS 0
#endif

#ifndef FAILURE
#define FAILURE -1
#endif


class CGif
{
public:
	CGif(){}

	CString m_filename;
	int m_image_width;
	int m_image_height;

	int load(CString filename, int &width, int &height, int &bits_pixel, CString & error_msg);
	int get_gif_rgb_subimage(CString filename, int start_x, int start_y, int width, int height, BYTE *img,
							   CString & error_msg);
	int get_file_info(CString filename, CString & info);
	int decode_interlace_line(IMAGE *Image, int y);

	int create_multi_image_tiff_overview(int *err_code, CString &err_msg, IImageLibCallback *callback);


};

#endif  // ifndef CGIF_H