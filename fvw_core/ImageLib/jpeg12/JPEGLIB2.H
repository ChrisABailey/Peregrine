
/*
 * jpeglib.h
 *
 * Copyright (C) 1991-1998, Thomas G. Lane.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file defines the application interface for the JPEG library.
 * Most applications using the library need only include this file,
 * and perhaps jerror.h if they want to know the exact error codes.
 */

#ifndef JPEGLIB2_H
#define JPEGLIB2_H

/*
 * First we include the configuration files that record how this
 * installation of the JPEG library is set up.  jconfig.h can be
 * generated automatically for many systems.  jmorecfg.h contains
 * manual configuration options that most people need not worry about.
 */

#ifndef JCONFIG_INCLUDED	/* in case jinclude.h already did */
#include "jconfig.h"		/* widely used configuration options */
#endif
#include "jmorecfg.h"		/* seldom changed options */


/* Version ID for the JPEG library.
 * Might be useful for tests like "#if JPEG_LIB_VERSION >= 60".
 */

#define JPEG_LIB_VERSION  62	/* Version 6b */


/* Various constants determining the sizes of things.
 * All of these are specified by the JPEG standard, so don't change them
 * if you want to be compatible.
 */

#define DCTSIZE		    8	/* The basic DCT block is 8x8 samples */
#define DCTSIZE2	    64	/* DCTSIZE squared; # of elements in a block */
#define NUM_QUANT_TBLS      4	/* Quantization tables are numbered 0..3 */
#define NUM_HUFF_TBLS       4	/* Huffman tables are numbered 0..3 */
#define NUM_ARITH_TBLS      16	/* Arith-coding tables are numbered 0..15 */
#define MAX_COMPS_IN_SCAN   4	/* JPEG limit on # of components in one scan */
#define MAX_SAMP_FACTOR     4	/* JPEG limit on sampling factors */
/* Unfortunately, some bozo at Adobe saw no reason to be bound by the standard;
 * the PostScript DCT filter can emit files with many more than 10 blocks/MCU.
 * If you happen to run across such a file, you can up D_MAX_BLOCKS_IN_MCU
 * to handle it.  We even let you do this from the jconfig.h file.  However,
 * we strongly discourage changing C_MAX_BLOCKS_IN_MCU; just because Adobe
 * sometimes emits noncompliant files doesn't mean you should too.
 */
#define C_MAX_BLOCKS_IN_MCU   10 /* compressor's limit on blocks per MCU */
#ifndef D_MAX_BLOCKS_IN_MCU
#define D_MAX_BLOCKS_IN_MCU   10 /* decompressor's limit on blocks per MCU */
#endif


/* Data structures for images (arrays of samples and of DCT coefficients).
 * On 80x86 machines, the image arrays are too big for near pointers,
 * but the pointer arrays can fit in near memory.
 */

typedef JSAMPLE12 *JSAMPROW12;	/* ptr to one image row of pixel samples. */
typedef JSAMPROW12 *JSAMPARRAY12;	/* ptr to some rows (a 2-D sample array) */
typedef JSAMPARRAY12 *JSAMPIMAGE12;	/* a 3-D sample array: top index is color */

typedef JCOEF JBLOCK[DCTSIZE2];	/* one block of coefficients */
typedef JBLOCK *JBLOCKROW;	/* pointer to one row of coefficient blocks */
typedef JBLOCKROW *JBLOCKARRAY;		/* a 2-D array of coefficient blocks */
typedef JBLOCKARRAY *JBLOCKIMAGE;	/* a 3-D array of coefficient blocks */

typedef JCOEF *JCOEFPTR;	/* useful in a couple of places */


/* Types for JPEG compression parameters and working tables. */


/* DCT coefficient quantization tables. */

typedef struct {
  /* This array gives the coefficient quantizers in natural array order
   * (not the zigzag order in which they are stored in a JPEG DQT marker).
   * CAUTION: IJG versions prior to v6a kept this array in zigzag order.
   */
  UINT16 quantval[DCTSIZE2];	/* quantization step for each coefficient */
  /* This field is used only during compression.  It's initialized FALSE when
   * the table is created, and set TRUE when it's been output to the file.
   * You could suppress output of a table by setting this to TRUE.
   * (See jpeg_suppress_tables for an example.)
   */
  boolean sent_table;		/* TRUE when table has been output */
} JQUANT_TBL12;


/* Huffman coding tables. */

typedef struct {
  /* These two fields directly represent the contents of a JPEG DHT marker */
  UINT8 bits[17];		/* bits[k] = # of symbols with codes of */
				/* length k bits; bits[0] is unused */
  UINT8 huffval[256];		/* The symbols, in order of incr code length */
  /* This field is used only during compression.  It's initialized FALSE when
   * the table is created, and set TRUE when it's been output to the file.
   * You could suppress output of a table by setting this to TRUE.
   * (See jpeg_suppress_tables for an example.)
   */
  boolean sent_table;		/* TRUE when table has been output */
} JHUFF_TBL12;


/* Basic info about one component (color channel). */

typedef struct {
  /* These values are fixed over the whole image. */
  /* For compression, they must be supplied by parameter setup; */
  /* for decompression, they are read from the SOF marker. */
  int component_id;		/* identifier for this component (0..255) */
  int component_index;		/* its index in SOF or cinfo->comp_info[] */
  int h_samp_factor;		/* horizontal sampling factor (1..4) */
  int v_samp_factor;		/* vertical sampling factor (1..4) */
  int quant_tbl_no;		/* quantization table selector (0..3) */
  /* These values may vary between scans. */
  /* For compression, they must be supplied by parameter setup; */
  /* for decompression, they are read from the SOS marker. */
  /* The decompressor output side may not use these variables. */
  int dc_tbl_no;		/* DC entropy table selector (0..3) */
  int ac_tbl_no;		/* AC entropy table selector (0..3) */
  
  /* Remaining fields should be treated as private by applications. */
  
  /* These values are computed during compression or decompression startup: */
  /* Component's size in DCT blocks.
   * Any dummy blocks added to complete an MCU are not counted; therefore
   * these values do not depend on whether a scan is interleaved or not.
   */
  JDIMENSION width_in_blocks;
  JDIMENSION height_in_blocks;
  /* Size of a DCT block in samples.  Always DCTSIZE for compression.
   * For decompression this is the size of the output from one DCT block,
   * reflecting any scaling we choose to apply during the IDCT step.
   * Values of 1,2,4,8 are likely to be supported.  Note that different
   * components may receive different IDCT scalings.
   */
  int DCT_scaled_size;
  /* The downsampled dimensions are the component's actual, unpadded number
   * of samples at the main buffer (preprocessing/compression interface), thus
   * downsampled_width = ceil(image_width * Hi/Hmax)
   * and similarly for height.  For decompression, IDCT scaling is included, so
   * downsampled_width = ceil(image_width * Hi/Hmax * DCT_scaled_size/DCTSIZE)
   */
  JDIMENSION downsampled_width;	 /* actual width in samples */
  JDIMENSION downsampled_height; /* actual height in samples */
  /* This flag is used only for decompression.  In cases where some of the
   * components will be ignored (eg grayscale output from YCbCr image),
   * we can skip most computations for the unused components.
   */
  boolean component_needed;	/* do we need the value of this component? */

  /* These values are computed before starting a scan of the component. */
  /* The decompressor output side may not use these variables. */
  int MCU_width;		/* number of blocks per MCU, horizontally */
  int MCU_height;		/* number of blocks per MCU, vertically */
  int MCU_blocks;		/* MCU_width * MCU_height */
  int MCU_sample_width;		/* MCU width in samples, MCU_width*DCT_scaled_size */
  int last_col_width;		/* # of non-dummy blocks across in last MCU */
  int last_row_height;		/* # of non-dummy blocks down in last MCU */

  /* Saved quantization table for component; NULL if none yet saved.
   * See jdinput.c comments about the need for this information.
   * This field is currently used only for decompression.
   */
  JQUANT_TBL12 * quant_table;

  /* Private per-component storage for DCT or IDCT subsystem. */
  void * dct_table;
} jpeg_component_info12;


/* The script for encoding a multiple-scan file is an array of these: */

typedef struct {
  int comps_in_scan;		/* number of components encoded in this scan */
  int component_index[MAX_COMPS_IN_SCAN]; /* their SOF/comp_info[] indexes */
  int Ss, Se;			/* progressive JPEG spectral selection parms */
  int Ah, Al;			/* progressive JPEG successive approx. parms */
} jpeg_scan_info12;

/* The decompressor can save APPn and COM markers in a list of these: */

typedef struct jpeg_marker_struct12 * jpeg_saved_marker_ptr12;

struct jpeg_marker_struct12 {
  jpeg_saved_marker_ptr12 next;	/* next in list, or NULL */
  UINT8 marker;			/* marker code: JPEG_COM, or JPEG_APP0+n */
  unsigned int original_length;	/* # bytes of data in the file */
  unsigned int data_length;	/* # bytes of data saved at data[] */
  JOCTET * data;		/* the data contained in the marker */
  /* the marker length word is not counted in data_length or original_length */
};

/* Known color spaces. */

typedef enum {
	JCS_UNKNOWN12,		/* error/unspecified */
	JCS_GRAYSCALE12,		/* monochrome */
	JCS_RGB12,		/* red/green/blue */
	JCS_YCbCr12,		/* Y/Cb/Cr (also known as YUV) */
	JCS_CMYK12,		/* C/M/Y/K */
	JCS_YCCK12,		/* Y/Cb/Cr/K */
	JCS_MS12,		/* 4-band multispectral */
} J_COLOR_SPACE12;

/* DCT/IDCT algorithm options. */

typedef enum {
	JDCT_ISLOW12,		/* slow but accurate integer algorithm */
	JDCT_IFAST12,		/* faster, less accurate integer method */
	JDCT_FLOAT12		/* floating-point: accurate, fast on fast HW */
} J_DCT_METHOD12;

#ifndef JDCT_DEFAULT		/* may be overridden in jconfig.h */
#define JDCT_DEFAULT  JDCT_ISLOW12
#endif
#ifndef JDCT_FASTEST		/* may be overridden in jconfig.h */
#define JDCT_FASTEST  JDCT_IFAST12
#endif

/* Dithering options for decompression. */

typedef enum {
	JDITHER_NONE12,		/* no dithering */
	JDITHER_ORDERED12,	/* simple ordered dither */
	JDITHER_FS12		/* Floyd-Steinberg error diffusion dither */
} J_DITHER_MODE12;


/* Common fields between JPEG compression and decompression master structs. */

#define jpeg_common_fields12 \
  struct jpeg_error_mgr12 * err;	/* Error handler module */\
  struct jpeg_memory_mgr12 * mem;	/* Memory manager module */\
  struct jpeg_progress_mgr12 * progress; /* Progress monitor, or NULL if none */\
  void * client_data;		/* Available for use by application */\
  boolean is_decompressor;	/* So common code can tell which is which */\
  int global_state		/* For checking call sequence validity */

/* Routines that are to be used by both halves of the library are declared
 * to receive a pointer to this structure.  There are no actual instances of
 * jpeg_common_struct, only of jpeg_compress_struct12 and jpeg_decompress_struct12.
 */
struct jpeg_common_struct12 
{
  jpeg_common_fields12;		/* Fields common to both master struct types */
  /* Additional fields follow in an actual jpeg_compress_struct12 or
   * jpeg_decompress_struct12.  All three structs must agree on these
   * initial fields!  (This would be a lot cleaner in C++.)
   */
};

typedef struct jpeg_common_struct12 * j_common_ptr12;
typedef struct jpeg_compress_struct12 * j_compress_ptr12;
typedef struct jpeg_decompress_struct12 * j_decompress_ptr2;


/* Master record for a compression instance */

struct jpeg_compress_struct12 {
  jpeg_common_fields12;		/* Fields shared with jpeg_decompress_struct12 */

  /* Destination for compressed data */
  struct jpeg_destination_mgr12 * dest;

  /* Description of source image --- these fields must be filled in by
   * outer application before starting compression.  in_color_space must
   * be correct before you can even call jpeg_set_defaults().
   */

  JDIMENSION image_width;	/* input image width */
  JDIMENSION image_height;	/* input image height */
  int input_components;		/* # of color components in input image */
  J_COLOR_SPACE12 in_color_space;	/* colorspace of input image */

  double input_gamma;		/* image gamma of input image */

  /* Compression parameters --- these fields must be set before calling
   * jpeg_start_compress().  We recommend calling jpeg_set_defaults() to
   * initialize everything to reasonable defaults, then changing anything
   * the application specifically wants to change.  That way you won't get
   * burnt when new parameters are added.  Also note that there are several
   * helper routines to simplify changing parameters.
   */

  int data_precision;		/* bits of precision in image data */

  int num_components;		/* # of color components in JPEG image */
  J_COLOR_SPACE12 jpeg_color_space; /* colorspace of JPEG image */

  jpeg_component_info12 * comp_info;
  /* comp_info[i] describes component that appears i'th in SOF */
  
  JQUANT_TBL12 * quant_tbl_ptrs[NUM_QUANT_TBLS];
  /* ptrs to coefficient quantization tables, or NULL if not defined */
  
  JHUFF_TBL12 * dc_huff_tbl_ptrs[NUM_HUFF_TBLS];
  JHUFF_TBL12 * ac_huff_tbl_ptrs[NUM_HUFF_TBLS];
  /* ptrs to Huffman coding tables, or NULL if not defined */
  
  UINT8 arith_dc_L[NUM_ARITH_TBLS]; /* L values for DC arith-coding tables */
  UINT8 arith_dc_U[NUM_ARITH_TBLS]; /* U values for DC arith-coding tables */
  UINT8 arith_ac_K[NUM_ARITH_TBLS]; /* Kx values for AC arith-coding tables */

  int num_scans;		/* # of entries in scan_info array */
  const jpeg_scan_info12 * scan_info; /* script for multi-scan file, or NULL */
  /* The default value of scan_info is NULL, which causes a single-scan
   * sequential JPEG file to be emitted.  To create a multi-scan file,
   * set num_scans and scan_info to point to an array of scan definitions.
   */

  boolean raw_data_in;		/* TRUE=caller supplies downsampled data */
  boolean arith_code;		/* TRUE=arithmetic coding, FALSE=Huffman */
  boolean optimize_coding;	/* TRUE=optimize entropy encoding parms */
  boolean CCIR601_sampling;	/* TRUE=first samples are cosited */
  int smoothing_factor;		/* 1..100, or 0 for no input smoothing */
  J_DCT_METHOD12 dct_method;	/* DCT algorithm selector */

  /* The restart interval can be specified in absolute MCUs by setting
   * restart_interval, or in MCU rows by setting restart_in_rows
   * (in which case the correct restart_interval will be figured
   * for each scan).
   */
  unsigned int restart_interval; /* MCUs per restart, or 0 for no restart */
  int restart_in_rows;		/* if > 0, MCU rows per restart interval */

  /* Parameters controlling emission of special markers. */

  boolean write_JFIF_header;	/* should a JFIF marker be written? */
  UINT8 JFIF_major_version;	/* What to write for the JFIF version number */
  UINT8 JFIF_minor_version;
  /* These three values are not used by the JPEG code, merely copied */
  /* into the JFIF APP0 marker.  density_unit can be 0 for unknown, */
  /* 1 for dots/inch, or 2 for dots/cm.  Note that the pixel aspect */
  /* ratio is defined by X_density/Y_density even when density_unit=0. */
  UINT8 density_unit;		/* JFIF code for pixel size units */
  UINT16 X_density;		/* Horizontal pixel density */
  UINT16 Y_density;		/* Vertical pixel density */
  boolean write_Adobe_marker;	/* should an Adobe marker be written? */
  
  /* State variable: index of next scanline to be written to
   * jpeg_write_scanlines().  Application may use this to control its
   * processing loop, e.g., "while (next_scanline < image_height)".
   */

  JDIMENSION next_scanline;	/* 0 .. image_height-1  */

  /* Remaining fields are known throughout compressor, but generally
   * should not be touched by a surrounding application.
   */

  /*
   * These fields are computed during compression startup
   */
  boolean progressive_mode;	/* TRUE if scan script uses progressive mode */
  int max_h_samp_factor;	/* largest h_samp_factor */
  int max_v_samp_factor;	/* largest v_samp_factor */

  JDIMENSION total_iMCU_rows;	/* # of iMCU rows to be input to coef ctlr */
  /* The coefficient controller receives data in units of MCU rows as defined
   * for fully interleaved scans (whether the JPEG file is interleaved or not).
   * There are v_samp_factor * DCTSIZE sample rows of each component in an
   * "iMCU" (interleaved MCU) row.
   */
  
  /*
   * These fields are valid during any one scan.
   * They describe the components and MCUs actually appearing in the scan.
   */
  int comps_in_scan;		/* # of JPEG components in this scan */
  jpeg_component_info12 * cur_comp_info[MAX_COMPS_IN_SCAN];
  /* *cur_comp_info[i] describes component that appears i'th in SOS */
  
  JDIMENSION MCUs_per_row;	/* # of MCUs across the image */
  JDIMENSION MCU_rows_in_scan;	/* # of MCU rows in the image */
  
  int blocks_in_MCU;		/* # of DCT blocks per MCU */
  int MCU_membership[C_MAX_BLOCKS_IN_MCU];
  /* MCU_membership[i] is index in cur_comp_info of component owning */
  /* i'th block in an MCU */

  int Ss, Se, Ah, Al;		/* progressive JPEG parameters for scan */

  /*
   * Links to compression subobjects (methods and private variables of modules)
   */
  struct jpeg_comp_master * master;
  struct jpeg_c_main_controller * main;
  struct jpeg_c_prep_controller * prep;
  struct jpeg_c_coef_controller * coef;
  struct jpeg_marker_writer * marker;
  struct jpeg_color_converter * cconvert;
  struct jpeg_downsampler * downsample;
  struct jpeg_forward_dct * fdct;
  struct jpeg_entropy_encoder * entropy;
  jpeg_scan_info12 * script_space; /* workspace for jpeg_simple_progression */
  int script_space_size;
};


/* Master record for a decompression instance */

struct jpeg_decompress_struct12 {
  jpeg_common_fields12;		/* Fields shared with jpeg_compress_struct12 */

  /* Source of compressed data */
  struct jpeg_source_mgr12 * src;

  /* Basic description of image --- filled in by jpeg_read_header(). */
  /* Application may inspect these values to decide how to process image. */

  JDIMENSION image_width;	/* nominal image width (from SOF marker) */
  JDIMENSION image_height;	/* nominal image height */
  int num_components;		/* # of color components in JPEG image */
  J_COLOR_SPACE12 jpeg_color_space; /* colorspace of JPEG image */

  /* Decompression processing parameters --- these fields must be set before
   * calling jpeg_start_decompress().  Note that jpeg_read_header() initializes
   * them to default values.
   */

  J_COLOR_SPACE12 out_color_space; /* colorspace for output */

  unsigned int scale_num, scale_denom; /* fraction by which to scale image */

  double output_gamma;		/* image gamma wanted in output */

  boolean buffered_image;	/* TRUE=multiple output passes */
  boolean raw_data_out;		/* TRUE=downsampled data wanted */

  J_DCT_METHOD12 dct_method;	/* IDCT algorithm selector */
  boolean do_fancy_upsampling;	/* TRUE=apply fancy upsampling */
  boolean do_block_smoothing;	/* TRUE=apply interblock smoothing */

  boolean quantize_colors;	/* TRUE=colormapped output wanted */
  /* the following are ignored if not quantize_colors: */
  J_DITHER_MODE12 dither_mode;	/* type of color dithering to use */
  boolean two_pass_quantize;	/* TRUE=use two-pass color quantization */
  int desired_number_of_colors;	/* max # colors to use in created colormap */
  /* these are significant only in buffered-image mode: */
  boolean enable_1pass_quant;	/* enable future use of 1-pass quantizer */
  boolean enable_external_quant;/* enable future use of external colormap */
  boolean enable_2pass_quant;	/* enable future use of 2-pass quantizer */

  /* Description of actual output image that will be returned to application.
   * These fields are computed by jpeg_start_decompress().
   * You can also use jpeg_calc_output_dimensions() to determine these values
   * in advance of calling jpeg_start_decompress().
   */

  JDIMENSION output_width;	/* scaled image width */
  JDIMENSION output_height;	/* scaled image height */
  int out_color_components;	/* # of color components in out_color_space */
  int output_components;	/* # of color components returned */
  /* output_components is 1 (a colormap index) when quantizing colors;
   * otherwise it equals out_color_components.
   */
  int rec_outbuf_height;	/* min recommended height of scanline buffer */
  /* If the buffer passed to jpeg_read_scanlines() is less than this many rows
   * high, space and time will be wasted due to unnecessary data copying.
   * Usually rec_outbuf_height will be 1 or 2, at most 4.
   */

  /* When quantizing colors, the output colormap is described by these fields.
   * The application can supply a colormap by setting colormap non-NULL before
   * calling jpeg_start_decompress; otherwise a colormap is created during
   * jpeg_start_decompress or jpeg_start_output.
   * The map has out_color_components rows and actual_number_of_colors columns.
   */
  int actual_number_of_colors;	/* number of entries in use */
  JSAMPARRAY12 colormap;		/* The color map as a 2-D pixel array */

  /* State variables: these variables indicate the progress of decompression.
   * The application may examine these but must not modify them.
   */

  /* Row index of next scanline to be read from jpeg_read_scanlines().
   * Application may use this to control its processing loop, e.g.,
   * "while (output_scanline < output_height)".
   */
  JDIMENSION output_scanline;	/* 0 .. output_height-1  */

  /* Current input scan number and number of iMCU rows completed in scan.
   * These indicate the progress of the decompressor input side.
   */
  int input_scan_number;	/* Number of SOS markers seen so far */
  JDIMENSION input_iMCU_row;	/* Number of iMCU rows completed */

  /* The "output scan number" is the notional scan being displayed by the
   * output side.  The decompressor will not allow output scan/row number
   * to get ahead of input scan/row, but it can fall arbitrarily far behind.
   */
  int output_scan_number;	/* Nominal scan number being displayed */
  JDIMENSION output_iMCU_row;	/* Number of iMCU rows read */

  /* Current progression status.  coef_bits[c][i] indicates the precision
   * with which component c's DCT coefficient i (in zigzag order) is known.
   * It is -1 when no data has yet been received, otherwise it is the point
   * transform (shift) value for the most recent scan of the coefficient
   * (thus, 0 at completion of the progression).
   * This pointer is NULL when reading a non-progressive file.
   */
  int (*coef_bits)[DCTSIZE2];	/* -1 or current Al value for each coef */

  /* Internal JPEG parameters --- the application usually need not look at
   * these fields.  Note that the decompressor output side may not use
   * any parameters that can change between scans.
   */

  /* Quantization and Huffman tables are carried forward across input
   * datastreams when processing abbreviated JPEG datastreams.
   */

  JQUANT_TBL12 * quant_tbl_ptrs[NUM_QUANT_TBLS];
  /* ptrs to coefficient quantization tables, or NULL if not defined */

  JHUFF_TBL12 * dc_huff_tbl_ptrs[NUM_HUFF_TBLS];
  JHUFF_TBL12 * ac_huff_tbl_ptrs[NUM_HUFF_TBLS];
  /* ptrs to Huffman coding tables, or NULL if not defined */

  /* These parameters are never carried across datastreams, since they
   * are given in SOF/SOS markers or defined to be reset by SOI.
   */

  int data_precision;		/* bits of precision in image data */

  jpeg_component_info12 * comp_info;
  /* comp_info[i] describes component that appears i'th in SOF */

  boolean progressive_mode;	/* TRUE if SOFn specifies progressive mode */
  boolean arith_code;		/* TRUE=arithmetic coding, FALSE=Huffman */

  UINT8 arith_dc_L[NUM_ARITH_TBLS]; /* L values for DC arith-coding tables */
  UINT8 arith_dc_U[NUM_ARITH_TBLS]; /* U values for DC arith-coding tables */
  UINT8 arith_ac_K[NUM_ARITH_TBLS]; /* Kx values for AC arith-coding tables */

  unsigned int restart_interval; /* MCUs per restart interval, or 0 for no restart */

  /* These fields record data obtained from optional markers recognized by
   * the JPEG library.
   */
  boolean saw_JFIF_marker;	/* TRUE iff a JFIF APP0 marker was found */
  /* Data copied from JFIF marker; only valid if saw_JFIF_marker is TRUE: */
  UINT8 JFIF_major_version;	/* JFIF version number */
  UINT8 JFIF_minor_version;
  UINT8 density_unit;		/* JFIF code for pixel size units */
  UINT16 X_density;		/* Horizontal pixel density */
  UINT16 Y_density;		/* Vertical pixel density */
  boolean saw_Adobe_marker;	/* TRUE iff an Adobe APP14 marker was found */
  UINT8 Adobe_transform;	/* Color transform code from Adobe marker */

  boolean CCIR601_sampling;	/* TRUE=first samples are cosited */

  /* Aside from the specific data retained from APPn markers known to the
   * library, the uninterpreted contents of any or all APPn and COM markers
   * can be saved in a list for examination by the application.
   */
  jpeg_saved_marker_ptr12 marker_list; /* Head of list of saved markers */

  /* Remaining fields are known throughout decompressor, but generally
   * should not be touched by a surrounding application.
   */

  /*
   * These fields are computed during decompression startup
   */
  int max_h_samp_factor;	/* largest h_samp_factor */
  int max_v_samp_factor;	/* largest v_samp_factor */

  int min_DCT_scaled_size;	/* smallest DCT_scaled_size of any component */

  JDIMENSION total_iMCU_rows;	/* # of iMCU rows in image */
  /* The coefficient controller's input and output progress is measured in
   * units of "iMCU" (interleaved MCU) rows.  These are the same as MCU rows
   * in fully interleaved JPEG scans, but are used whether the scan is
   * interleaved or not.  We define an iMCU row as v_samp_factor DCT block
   * rows of each component.  Therefore, the IDCT output contains
   * v_samp_factor*DCT_scaled_size sample rows of a component per iMCU row.
   */

  JSAMPLE12 * sample_range_limit; /* table for fast range-limiting */

  /*
   * These fields are valid during any one scan.
   * They describe the components and MCUs actually appearing in the scan.
   * Note that the decompressor output side must not use these fields.
   */
  int comps_in_scan;		/* # of JPEG components in this scan */
  jpeg_component_info12 * cur_comp_info[MAX_COMPS_IN_SCAN];
  /* *cur_comp_info[i] describes component that appears i'th in SOS */

  JDIMENSION MCUs_per_row;	/* # of MCUs across the image */
  JDIMENSION MCU_rows_in_scan;	/* # of MCU rows in the image */

  int blocks_in_MCU;		/* # of DCT blocks per MCU */
  int MCU_membership[D_MAX_BLOCKS_IN_MCU];
  /* MCU_membership[i] is index in cur_comp_info of component owning */
  /* i'th block in an MCU */

  int Ss, Se, Ah, Al;		/* progressive JPEG parameters for scan */

  /* This field is shared between entropy decoder and marker parser.
   * It is either zero or the code of a JPEG marker that has been
   * read from the data source, but has not yet been processed.
   */
  int unread_marker;

  /*
   * Links to decompression subobjects (methods, private variables of modules)
   */
  struct jpeg_decomp_master * master;
  struct jpeg_d_main_controller * main;
  struct jpeg_d_coef_controller * coef;
  struct jpeg_d_post_controller * post;
  struct jpeg_input_controller2 * inputctl2;
  struct jpeg_marker_reader * marker;
  struct jpeg_entropy_decoder * entropy;
  struct jpeg_inverse_dct * idct;
  struct jpeg_upsampler * upsample;
  struct jpeg_color_deconverter * cconvert;
  struct jpeg_color_quantizer * cquantize;
};


/* "Object" declarations for JPEG modules that may be supplied or called
 * directly by the surrounding application.
 * As with all objects in the JPEG library, these structs only define the
 * publicly visible methods and state variables of a module.  Additional
 * private fields may exist after the public ones.
 */


/* Error handler object */

struct jpeg_error_mgr12 {
  /* Error exit handler: does not return to caller */
  JMETHOD(void, error_exit, (j_common_ptr12 cinfo));
  /* Conditionally emit a trace or warning message */
  JMETHOD(void, emit_message, (j_common_ptr12 cinfo, int msg_level));
  /* Routine that actually outputs a trace or error message */
  JMETHOD(void, output_message, (j_common_ptr12 cinfo));
  /* Format a message string for the most recent JPEG error or message */
  JMETHOD(void, format_message, (j_common_ptr12 cinfo, char * buffer));
#define JMSG_LENGTH_MAX  200	/* recommended size of format_message buffer */
  /* Reset error state variables at start of a new image */
  JMETHOD(void, reset_error_mgr, (j_common_ptr12 cinfo));
  
  /* The message ID code and any parameters are saved here.
   * A message can have one string parameter or up to 8 int parameters.
   */
  int msg_code;
#define JMSG_STR_PARM_MAX  80
  union {
    int i[8];
    char s[JMSG_STR_PARM_MAX];
  } msg_parm;
  
  /* Standard state variables for error facility */
  
  int trace_level;		/* max msg_level that will be displayed */
  
  /* For recoverable corrupt-data errors, we emit a warning message,
   * but keep going unless emit_message chooses to abort.  emit_message
   * should count warnings in num_warnings.  The surrounding application
   * can check for bad data by seeing if num_warnings is nonzero at the
   * end of processing.
   */
  long num_warnings;		/* number of corrupt-data warnings */

  /* These fields point to the table(s) of error message strings.
   * An application can change the table pointer to switch to a different
   * message list (typically, to change the language in which errors are
   * reported).  Some applications may wish to add additional error codes
   * that will be handled by the JPEG library error mechanism; the second
   * table pointer is used for this purpose.
   *
   * First table includes all errors generated by JPEG library itself.
   * Error code 0 is reserved for a "no such error string" message.
   */
  const char * const * jpeg_message_table; /* Library errors */
  int last_jpeg_message;    /* Table contains strings 0..last_jpeg_message */
  /* Second table can be added by application (see cjpeg/djpeg for example).
   * It contains strings numbered first_addon_message..last_addon_message.
   */
  const char * const * addon_message_table; /* Non-library errors */
  int first_addon_message;	/* code for first string in addon table */
  int last_addon_message;	/* code for last string in addon table */
};


/* Progress monitor object */

struct jpeg_progress_mgr12 {
  JMETHOD(void, progress_monitor, (j_common_ptr12 cinfo));

  long pass_counter;		/* work units completed in this pass */
  long pass_limit;		/* total number of work units in this pass */
  int completed_passes;		/* passes completed so far */
  int total_passes;		/* total number of passes expected */
};


/* Data destination object for compression */

struct jpeg_destination_mgr12 {
  JOCTET * next_output_byte;	/* => next byte to write in buffer */
  size_t free_in_buffer;	/* # of byte spaces remaining in buffer */

  JMETHOD(void, init_destination, (j_compress_ptr12 cinfo));
  JMETHOD(boolean, empty_output_buffer, (j_compress_ptr12 cinfo));
  JMETHOD(void, term_destination, (j_compress_ptr12 cinfo));
};


/* Data source object for decompression */

struct jpeg_source_mgr12 {
  const JOCTET * next_input_byte; /* => next byte to read from buffer */
  size_t bytes_in_buffer;	/* # of bytes remaining in buffer */

  JMETHOD(void, init_source, (j_decompress_ptr2 cinfo));
  JMETHOD(boolean, fill_input_buffer, (j_decompress_ptr2 cinfo));
  JMETHOD(void, skip_input_data, (j_decompress_ptr2 cinfo, long num_bytes));
  JMETHOD(boolean, resync_to_restart, (j_decompress_ptr2 cinfo, int desired));
  JMETHOD(void, term_source, (j_decompress_ptr2 cinfo));
};


/* Memory manager object.
 * Allocates "small" objects (a few K total), "large" objects (tens of K),
 * and "really big" objects (virtual arrays with backing store if needed).
 * The memory manager does not allow individual objects to be freed; rather,
 * each created object is assigned to a pool, and whole pools can be freed
 * at once.  This is faster and more convenient than remembering exactly what
 * to free, especially where malloc()/free() are not too speedy.
 * NB: alloc routines never return NULL.  They exit to error_exit if not
 * successful.
 */

#define JPOOL_PERMANENT	0	/* lasts until master record is destroyed */
#define JPOOL_IMAGE	1	/* lasts until done with image/datastream */
#define JPOOL_NUMPOOLS	2

typedef struct jvirt_sarray_control * jvirt_sarray_ptr;
typedef struct jvirt_barray_control * jvirt_barray_ptr;


struct jpeg_memory_mgr12 {
  /* Method pointers */
  JMETHOD(void *, alloc_small, (j_common_ptr12 cinfo, int pool_id,
				size_t sizeofobject));
  JMETHOD(void *, alloc_large, (j_common_ptr12 cinfo, int pool_id,
				     size_t sizeofobject));
  JMETHOD(JSAMPARRAY12, alloc_sarray, (j_common_ptr12 cinfo, int pool_id,
				     JDIMENSION samplesperrow,
				     JDIMENSION numrows));
  JMETHOD(JBLOCKARRAY, alloc_barray, (j_common_ptr12 cinfo, int pool_id,
				      JDIMENSION blocksperrow,
				      JDIMENSION numrows));
  JMETHOD(jvirt_sarray_ptr, request_virt_sarray, (j_common_ptr12 cinfo,
						  int pool_id,
						  boolean pre_zero,
						  JDIMENSION samplesperrow,
						  JDIMENSION numrows,
						  JDIMENSION maxaccess));
  JMETHOD(jvirt_barray_ptr, request_virt_barray, (j_common_ptr12 cinfo,
						  int pool_id,
						  boolean pre_zero,
						  JDIMENSION blocksperrow,
						  JDIMENSION numrows,
						  JDIMENSION maxaccess));
  JMETHOD(void, realize_virt_arrays, (j_common_ptr12 cinfo));
  JMETHOD(JSAMPARRAY12, access_virt_sarray, (j_common_ptr12 cinfo,
					   jvirt_sarray_ptr ptr,
					   JDIMENSION start_row,
					   JDIMENSION num_rows,
					   boolean writable));
  JMETHOD(JBLOCKARRAY, access_virt_barray, (j_common_ptr12 cinfo,
					    jvirt_barray_ptr ptr,
					    JDIMENSION start_row,
					    JDIMENSION num_rows,
					    boolean writable));
  JMETHOD(void, free_pool, (j_common_ptr12 cinfo, int pool_id));
  JMETHOD(void, self_destruct, (j_common_ptr12 cinfo));

  /* Limit on memory allocation for this JPEG object.  (Note that this is
   * merely advisory, not a guaranteed maximum; it only affects the space
   * used for virtual-array buffers.)  May be changed by outer application
   * after creating the JPEG object.
   */
  long max_memory_to_use;

  /* Maximum allocation request accepted by alloc_large. */
  long max_alloc_chunk;
};


/* Routine signature for application-supplied marker processing methods.
 * Need not pass marker code since it is stored in cinfo->unread_marker.
 */
typedef JMETHOD(boolean, jpeg_marker_parser_method12, (j_decompress_ptr2 cinfo));


/* Declarations for routines called by application.
 * The JPP macro hides prototype parameters from compilers that can't cope.
 * Note JPP requires double parentheses.
 */

#ifdef HAVE_PROTOTYPES
#define JPP(arglist)	arglist
#else
#define JPP(arglist)	()
#endif


/* Short forms of external names for systems with brain-damaged linkers.
 * We shorten external names to be unique in the first six letters, which
 * is good enough for all known systems.
 * (If your compiler itself needs names to be unique in less than 15 
 * characters, you are out of luck.  Get a better compiler.)
 */

#ifdef NEED_SHORT_EXTERNAL_NAMES
#define jpeg_std_error12		jStdError
#define jpeg_CreateCompress	jCreaCompress
#define jpeg_CreateDecompress12	jCreaDecompress12
#define jpeg_destroy_compress	jDestCompress
#define jpeg_destroy_decompress12	jDestDecompress
#define jpeg_stdio_dest		jStdDest
#define jpeg_stdio_src12		jStdSrc
#define jpeg_set_defaults	jSetDefaults
#define jpeg_set_colorspace	jSetColorspace
#define jpeg_default_colorspace	jDefColorspace
#define jpeg_set_quality	jSetQuality
#define jpeg_set_linear_quality	jSetLQuality
#define jpeg_add_quant_table	jAddQuantTable
#define jpeg_quality_scaling12	jQualityScaling
#define jpeg_simple_progression	jSimProgress
#define jpeg_suppress_tables	jSuppressTables
#define jpeg_alloc_quant_table12	jAlcQTable
#define jpeg_alloc_huff_table	jAlcHTable
#define jpeg_start_compress12	jStrtCompress
#define jpeg_write_scanlines12	jWrtScanlines
#define jpeg_finish_compress	jFinCompress
#define jpeg_write_raw_data12	jWrtRawData
#define jpeg_write_marker	jWrtMarker
#define jpeg_write_m_header	jWrtMHeader
#define jpeg_write_m_byte	jWrtMByte
#define jpeg_write_tables	jWrtTables
#define jpeg_read_header12	jReadHeader
#define jpeg_start_decompress12	jStrtDecompress
#define jpeg_read_scanlines12	jReadScanlines
#define jpeg_finish_decompress12	jFinDecompress
#define jpeg_read_raw_data12	jReadRawData
#define jpeg_has_multiple_scans12	jHasMultScn
#define jpeg_start_output12	jStrtOutput
#define jpeg_finish_output12	jFinOutput
#define jpeg_input_complete12	jInComplete
#define jpeg_new_colormap	jNewCMap
#define jpeg_consume_input12	jConsumeInput12
#define jpeg_calc_output_dimensions12	jCalcDimensions12
#define jpeg_save_markers12	jSaveMarkers
#define jpeg_set_marker_processor12	jSetMarker
#define jpeg_read_coefficients	jReadCoefs
#define jpeg_write_coefficients	jWrtCoefs
#define jpeg_copy_critical_parameters	jCopyCrit
#define jpeg_abort_compress	jAbrtCompress
#define jpeg_abort_decompress12	jAbrtDecompress
#define jpeg_abort		jAbort
#define jpeg_destroy		jDestroy
#define jpeg_resync_to_restart12	jResyncRestart
#endif /* NEED_SHORT_EXTERNAL_NAMES */


/* Default error-management setup */
EXTERN(struct jpeg_error_mgr12 *) jpeg_std_error12
	JPP((struct jpeg_error_mgr12 * err));

/* Initialization of JPEG compression objects.
 * jpeg_create_compress12() and jpeg_create_decompress() are the exported
 * names that applications should call.  These expand to calls on
 * jpeg_CreateCompress and jpeg_CreateDecompress with additional information
 * passed for version mismatch checking.
 * NB: you must set up the error-manager BEFORE calling jpeg_create_xxx.
 */
#define jpeg_create_compress12(cinfo) \
    jpeg_CreateCompress((cinfo), JPEG_LIB_VERSION, \
			(size_t) sizeof(struct jpeg_compress_struct12))
#define jpeg_create_decompress12(cinfo) \
    jpeg_CreateDecompress12((cinfo), JPEG_LIB_VERSION, \
			  (size_t) sizeof(struct jpeg_decompress_struct12))
EXTERN(void) jpeg_CreateCompress JPP((j_compress_ptr12 cinfo,
				      int version, size_t structsize));
EXTERN(void) jpeg_CreateDecompress12 JPP((j_decompress_ptr2 cinfo,
					int version, size_t structsize));
/* Destruction of JPEG compression objects */
EXTERN(void) jpeg_destroy_compress JPP((j_compress_ptr12 cinfo));
EXTERN(void) jpeg_destroy_decompress12 JPP((j_decompress_ptr2 cinfo));

/* Standard data source and destination managers: stdio streams. */
/* Caller is responsible for opening the file before and closing after. */
EXTERN(void) jpeg_stdio_dest JPP((j_compress_ptr12 cinfo, FILE * outfile));
EXTERN(void) jpeg_stdio_src12 JPP((j_decompress_ptr2 cinfo, FILE * infile));

/* Default parameter setup for compression */
EXTERN(void) jpeg_set_defaults JPP((j_compress_ptr12 cinfo));
/* Compression parameter setup aids */
EXTERN(void) jpeg_set_colorspace JPP((j_compress_ptr12 cinfo,
				      J_COLOR_SPACE12 colorspace));
EXTERN(void) jpeg_default_colorspace JPP((j_compress_ptr12 cinfo));
EXTERN(void) jpeg_set_quality JPP((j_compress_ptr12 cinfo, int quality,
				   boolean force_baseline));
EXTERN(void) jpeg_set_linear_quality JPP((j_compress_ptr12 cinfo,
					  int scale_factor,
					  boolean force_baseline));
EXTERN(void) jpeg_add_quant_table JPP((j_compress_ptr12 cinfo, int which_tbl,
				       const unsigned int *basic_table,
				       int scale_factor,
				       boolean force_baseline));
EXTERN(int) jpeg_quality_scaling12 JPP((int quality));
EXTERN(void) jpeg_simple_progression JPP((j_compress_ptr12 cinfo));
EXTERN(void) jpeg_suppress_tables JPP((j_compress_ptr12 cinfo,
				       boolean suppress));
EXTERN(JQUANT_TBL12 *) jpeg_alloc_quant_table12 JPP((j_common_ptr12 cinfo));
EXTERN(JHUFF_TBL12 *) jpeg_alloc_huff_table JPP((j_common_ptr12 cinfo));

/* Main entry points for compression */
EXTERN(void) jpeg_start_compress12 JPP((j_compress_ptr12 cinfo,
				      boolean write_all_tables));
EXTERN(JDIMENSION) jpeg_write_scanlines12 JPP((j_compress_ptr12 cinfo,
					     JSAMPARRAY12 scanlines,
					     JDIMENSION num_lines));
EXTERN(void) jpeg_finish_compress JPP((j_compress_ptr12 cinfo));

/* Replaces jpeg_write_scanlines when writing raw downsampled data. */
EXTERN(JDIMENSION) jpeg_write_raw_data12 JPP((j_compress_ptr12 cinfo,
					    JSAMPIMAGE12 data,
					    JDIMENSION num_lines));

/* Write a special marker.  See libjpeg.doc concerning safe usage. */
EXTERN(void) jpeg_write_marker
	JPP((j_compress_ptr12 cinfo, int marker,
	     const JOCTET * dataptr, unsigned int datalen));
/* Same, but piecemeal. */
EXTERN(void) jpeg_write_m_header
	JPP((j_compress_ptr12 cinfo, int marker, unsigned int datalen));
EXTERN(void) jpeg_write_m_byte
	JPP((j_compress_ptr12 cinfo, int val));

/* Alternate compression function: just write an abbreviated table file */
EXTERN(void) jpeg_write_tables JPP((j_compress_ptr12 cinfo));

/* Decompression startup: read start of JPEG datastream to see what's there */
EXTERN(int) jpeg_read_header12 JPP((j_decompress_ptr2 cinfo,
				  boolean require_image));
/* Return value is one of: */
#define JPEG_SUSPENDED		0 /* Suspended due to lack of input data */
#define JPEG_HEADER_OK		1 /* Found valid image datastream */
#define JPEG_HEADER_TABLES_ONLY	2 /* Found valid table-specs-only datastream */
/* If you pass require_image = TRUE (normal case), you need not check for
 * a TABLES_ONLY return code; an abbreviated file will cause an error exit.
 * JPEG_SUSPENDED is only possible if you use a data source module that can
 * give a suspension return (the stdio source module doesn't).
 */

/* Main entry points for decompression */
EXTERN(boolean) jpeg_start_decompress12 JPP((j_decompress_ptr2 cinfo));
EXTERN(JDIMENSION) jpeg_read_scanlines12 JPP((j_decompress_ptr2 cinfo,
					    JSAMPARRAY12 scanlines,
					    JDIMENSION max_lines));
EXTERN(boolean) jpeg_finish_decompress12 JPP((j_decompress_ptr2 cinfo));

/* Replaces jpeg_read_scanlines when reading raw downsampled data. */
EXTERN(JDIMENSION) jpeg_read_raw_data12 JPP((j_decompress_ptr2 cinfo,
					   JSAMPIMAGE12 data,
					   JDIMENSION max_lines));

/* Additional entry points for buffered-image mode. */
EXTERN(boolean) jpeg_has_multiple_scans12 JPP((j_decompress_ptr2 cinfo));
EXTERN(boolean) jpeg_start_output12 JPP((j_decompress_ptr2 cinfo,
				       int scan_number));
EXTERN(boolean) jpeg_finish_output12 JPP((j_decompress_ptr2 cinfo));
EXTERN(boolean) jpeg_input_complete12 JPP((j_decompress_ptr2 cinfo));
EXTERN(void) jpeg_new_colormap JPP((j_decompress_ptr2 cinfo));
EXTERN(int) jpeg_consume_input12 JPP((j_decompress_ptr2 cinfo));
/* Return value is one of: */
/* #define JPEG_SUSPENDED	0    Suspended due to lack of input data */
#define JPEG_REACHED_SOS	1 /* Reached start of new scan */
#define JPEG_REACHED_EOI	2 /* Reached end of image */
#define JPEG_ROW_COMPLETED	3 /* Completed one iMCU row */
#define JPEG_SCAN_COMPLETED	4 /* Completed last iMCU row of a scan */

/* Precalculate output dimensions for current decompression parameters. */
EXTERN(void) jpeg_calc_output_dimensions12 JPP((j_decompress_ptr2 cinfo));

/* Control saving of COM and APPn markers into marker_list. */
EXTERN(void) jpeg_save_markers12
	JPP((j_decompress_ptr2 cinfo, int marker_code,
	     unsigned int length_limit));

/* Install a special processing method for COM or APPn markers. */
EXTERN(void) jpeg_set_marker_processor12
	JPP((j_decompress_ptr2 cinfo, int marker_code,
	     jpeg_marker_parser_method12 routine));

/* Read or write raw DCT coefficients --- useful for lossless transcoding. */
EXTERN(jvirt_barray_ptr *) jpeg_read_coefficients JPP((j_decompress_ptr2 cinfo));
EXTERN(void) jpeg_write_coefficients JPP((j_compress_ptr12 cinfo,
					  jvirt_barray_ptr * coef_arrays));
EXTERN(void) jpeg_copy_critical_parameters JPP((j_decompress_ptr2 srcinfo,
						j_compress_ptr12 dstinfo));

/* If you choose to abort compression or decompression before completing
 * jpeg_finish_(de)compress, then you need to clean up to release memory,
 * temporary files, etc.  You can just call jpeg_destroy_(de)compress
 * if you're done with the JPEG object, but if you want to clean it up and
 * reuse it, call this:
 */
EXTERN(void) jpeg_abort_compress JPP((j_compress_ptr12 cinfo));
EXTERN(void) jpeg_abort_decompress12 JPP((j_decompress_ptr2 cinfo));

/* Generic versions of jpeg_abort and jpeg_destroy that work on either
 * flavor of JPEG object.  These may be more convenient in some places.
 */
EXTERN(void) jpeg_abort JPP((j_common_ptr12 cinfo));
EXTERN(void) jpeg_destroy JPP((j_common_ptr12 cinfo));

/* Default restart-marker-resync procedure for use by data source modules */
EXTERN(boolean) jpeg_resync_to_restart12 JPP((j_decompress_ptr2 cinfo,
					    int desired));


/* These marker codes are exported since applications and data source modules
 * are likely to want to use them.
 */

#define JPEG_RST0	0xD0	/* RST0 marker code */
#define JPEG_EOI	0xD9	/* EOI marker code */
#define JPEG_APP0	0xE0	/* APP0 marker code */
#define JPEG_COM	0xFE	/* COM marker code */


/* If we have a brain-damaged compiler that emits warnings (or worse, errors)
 * for structure definitions that are never filled in, keep it quiet by
 * supplying dummy definitions for the various substructures.
 */

#ifdef INCOMPLETE_TYPES_BROKEN
#ifndef JPEG_INTERNALS		/* will be defined in jpegint.h */
struct jvirt_sarray_control { long dummy; };
struct jvirt_barray_control { long dummy; };
struct jpeg_comp_master { long dummy; };
struct jpeg_c_main_controller { long dummy; };
struct jpeg_c_prep_controller { long dummy; };
struct jpeg_c_coef_controller { long dummy; };
struct jpeg_marker_writer { long dummy; };
struct jpeg_color_converter { long dummy; };
struct jpeg_downsampler { long dummy; };
struct jpeg_forward_dct { long dummy; };
struct jpeg_entropy_encoder { long dummy; };
struct jpeg_decomp_master { long dummy; };
struct jpeg_d_main_controller { long dummy; };
struct jpeg_d_coef_controller { long dummy; };
struct jpeg_d_post_controller { long dummy; };
struct jpeg_input_controller2 { long dummy; };
struct jpeg_marker_reader { long dummy; };
struct jpeg_entropy_decoder { long dummy; };
struct jpeg_inverse_dct { long dummy; };
struct jpeg_upsampler { long dummy; };
struct jpeg_color_deconverter { long dummy; };
struct jpeg_color_quantizer { long dummy; };
#endif /* JPEG_INTERNALS */
#endif /* INCOMPLETE_TYPES_BROKEN */


/*
 * The JPEG library modules define JPEG_INTERNALS before including this file.
 * The internal structure declarations are read only when that is true.
 * Applications using the library should not include jpegint.h, but may wish
 * to include jerror.h.
 */

#ifdef JPEG_INTERNALS
#include "jpegint.h"		/* fetch private declarations */
#include "jerror.h"		/* fetch error codes too */
#endif

#endif /* JPEGLIB_H */
