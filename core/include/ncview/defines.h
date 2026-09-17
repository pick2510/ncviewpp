/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 2026 Dominik Strebel
 * Copyright (C) 1993 through 2024 by David W. Pierce
 *
 * This program  is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as 
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License, version 3, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * David W. Pierce
 * davidwilliampierce@gmail.com
 */

/*
	ncview.defines.h

	#defines, and structure definitions
*/

#pragma once

#ifdef HAVE_UDUNITS2
/* MinGW's netcdf.h (included via ncview/includes.h, above us in every TU)
 * already #defines MSC_EXTRA for its own DLL-export purposes; udunits2.h
 * unconditionally #defines the same name for its own, unrelated Windows
 * DLL-export handling, which errors under -Werror without this. */
#ifdef MSC_EXTRA
#undef MSC_EXTRA
#endif
#include <udunits2.h>
#endif

#include <memory>
#include <string>
#include <vector>

/* X11's <X11/X.h> visual-class constant, value 3, used by util.cc's
 * data_to_pixels() to decide whether to run pixel values through the
 * indexed-colormap pixel_transform table. Defined here (rather than pulling
 * in X11 headers) so core stays UI-free; ncview_ui sets options.display_type
 * to this value if/when it still needs indexed-colormap emulation. */
#define PseudoColor 3

#define PROGRAM_ID		"Ncview 2.1.11 David W. Pierce 7 November 2024"
#define PROGRAM_VERSION_STRING	"2.1.11"
constexpr double APP_RES_VERSION = 1.93;

/******************** Buttons in the user interface **********************/
enum class Button {
	Rewind		= 1,
	Backwards	= 2,
	Pause		= 3,
	Forward		= 4,
	Fastforward	= 5,
	ColormapSelect	= 6,	/* this is also a label */
	InvertPhysical	= 7,
	InvertColormap	= 8,
	Minimum		= 9,
	Maximum		= 10,
	Quit		= 11,
	Blowup		= 12,	/* this is also a label */
	Restart		= 13,
	Transform	= 14,	/* this is also a label */
	Dimset		= 15,
	Range		= 16,
	BlowupType	= 17,	/* this is also a label */
	/* No Button::Skip: unlike Label::Skip (still a real, used label slot,
	 * see main_window.cc), the button counterpart was dead upstream --
	 * no dispatch case ever handled it (it would hit `default:` ->
	 * exit(-1) if reached) and nothing referenced it -- removed here per
	 * the OOP_redesign plan's Step 7/9c cleanup note. */
	Edit		= 19,
	Info		= 20,
	Print		= 21,
	Options		= 22,
};

/***************************************************************************
 * These are the overlays we know about
 */
#define OVERLAY_NONE			0
#define OVERLAY_P8DEG			1
#define OVERLAY_P08DEG			2
#define OVERLAY_USA			3
#define OVERLAY_CUSTOM			4

constexpr int OVERLAY_N_OVERLAYS = 5;


/***************************************************************************
 * General purpose writable labels in the user interface. Each numbered
 * slot (L1..L6) also has a semantic name for the one thing it's actually
 * used to display (Title, ScanvarName, ...) -- both names are the same
 * enumerator, exactly as the old LABEL_TITLE/LABEL_1-style #define aliasing
 * was.
 */
enum class Label {
	L1		= 1,
	L2		= 2,
	L3		= 3,
	L4		= 4,
	/* Specific purpose writable labels in the user interface. */
	ColormapName	= 5,	/* this is also a button */
	Blowup		= 6,	/* this is also a button */
	Transform	= 7,
	CcInfo1		= 8,
	CcInfo2		= 9,
	BlowupType	= 10,	/* this is also a button */
	L5		= 11,
	Skip		= 12,	/* this is also a button */
	L6		= 13,

	Title		= L1,
	ScanvarName	= L2,
	ScanPlace	= L3,
	DataExtrema	= L4,
	DataValue	= L5,
	ScalarDims	= L6,	/* information from the scalar dims */
};

/*****************************************************************************/
/* Transforming the data before turning it into pixels is supported */
constexpr int N_TRANSFORMS = 4;
enum class Transform { None = 1, Low = 2, Hi = 3, Center = 4 };

/*****************************************************************************
 * Maximum number of X-Y plot windows which can pop up, and the max
 * number of lines on one plot.
 */
constexpr int MAX_PLOT_XY = 10;
constexpr int MAX_LINES_PER_PLOT = 5;

/*****************************************************************************
 * This is for the popup windows which show all of a variable's attributes.
 */
constexpr int MAX_DISPLAY_POPUPS = 10;

/*****************************************************************************/
/* Types of file data formats supported */
constexpr int FILE_TYPE_NETCDF = 1;

/*****************************************************************************/
/* Maximum name length of a variable */
constexpr int MAX_VAR_NAME_LEN = 4095;

/*****************************************************************************/
/* Maximum name length of a file */
constexpr int MAX_FILE_NAME_LEN = 4095;

/*****************************************************************************/
/* Maximum name length of a recdim units */
constexpr int MAX_RECDIM_UNITS_LEN = 4095;

/*****************************************************************************/
/* Possible interpretations for the change_view routine; either change
 * the specified number of FRAMES or the specified PERCENT.
 */
constexpr int FRAMES = 1;
constexpr int PERCENT = 2;

/*****************************************************************************/
/* Truncate displayed strings which are longer than this */
constexpr int MAX_DISPLAYED_STRING_LENGTH = 250;

/*****************************************************************************/
/* What dimension button sets we have */
enum class Dimension { X = 1, Y = 2, Scan = 3, None = 4 };

/*****************************************************************************/
/* Button-press modification indicators */
enum class Modifier { M1 = 1, M2 = 2, M3 = 3, M4 = 4 };

/*****************************************************************************/
/* Messages which a dialog popup can return */
enum class Message { OK = 1, Cancel = 2 };

/*****************************************************************************/
/* This is used in x_interface.c, even though it has nothing to do with
 * the X interface, because the X mechanism has a way of
 * reading in resource files, and there is no point in reading in TWO
 * different configuration files.  Sigh.  For use of this, see routine
 * 'check_app_res' in file x_interface.c
 */
constexpr int DEFAULT_DELTA_STEP = 10;

/*****************************************************************************/
/* Ways in which the file's min and max can be calculated */
enum class MinMaxMethod { Fast = 1, Med = 2, Slow = 3, Exhaust = 4 };

/*****************************************************************************/
/* Data which has the fill_value is IGNORED.  It is assumed to represent
 * out of domain or out of range data.  Netcdf has its own values for this
 * which replace this value, so in Netcdf implementations, this particular
 * value is not the one which is actually used.
 */
constexpr float DEFAULT_FILL_VALUE = 1.0e35f;

/*******************************************************************
 * Ways to expand a small pixmap into a large one.
 */
enum class BlowupType { Replicate = 1, Bilinear = 2 };

/*******************************************************************
 * Ways to contract a large pixmap into a small one.
 */
enum class ShrinkMethod { Mean = 0, Mode = 1 };

/*********************************************************************
 * Possible states which the data inside the current buffer can be in
 */
enum class ViewDataStatus { Valid = 1, Invalid = 2, Edited = 3 };

/*******************************************************************
 * Ways of handling the variable-select area.  We can either list
 * all the variables, or make a pull-down menu for selecting them.
 */
enum class VarselStyle { List = 1, Menu = 2 };

/*******************************************************************
 * Recognized standards by which the time axis may be described
 */
enum class TimeStandard {
	Udunits = 1,	/* Ex: units="days since 1900-01-01" */
	Epic0   = 2,	/* Ex: units="True Julian Day" w/att epic_code=624 */
	Months  = 3,	/* Ex: units="months", Jan 1 AD = month 1 */
};

/*******************************************************************
 * Kinds of time-like granularity.
 */
enum class TimeGranularity { Sec = 1, Min = 2, Hour = 3, Day = 4, Month = 5, Year = 6 };

/*******************************************************************
 * Maximum number of scalar "coord" attributes we can have for
 * variable.
 */
constexpr int MAX_SCALAR_COORDS = 20;

/*******************************************************************
 *
 * 	The main concept here is the 'variable'.  Variables are
 *	things which might possibly be displayed by ncview.  Variables
 *	live in one or more files, and within each of those files 
 *	have a size, and minimum and maximum values.  Different 
 *	variables can be in different files, but if you have the 
 *	same variable in different files it must have the EXACT SAME
 *	layout in all files, with the exception of the first index
 *	(which is the time index in netCDF files).  So, you can have
 *	20 time entries in the first file, then 7 in the second, and
 *	14 in the third; but you can't have the resolution of the 
 *	variable be different in the different files.
 *
 ********************************************************************/

/*****************************************************************************/
typedef unsigned char ncv_pixel;/* If you change this, make sure to change
				 * routine 'data_to_pixels' in util.c!  It
				 * assumes a size of one byte.  Some of the
				 * X routines do also.
				 */

/*****************************************************************************
 * A specific set of data for netCDF-type files.  These won't necessarily
 * be applicable to different types of data file formats.
 */
struct NetCDFOptions {
	int	valid_range_set,
		valid_min_set,
		valid_max_set,
		scale_factor_set,
		add_offset_set;

	float	valid_range[2],
		valid_min,
		valid_max,
		scale_factor,
		add_offset;

};

struct NCVar;	/* forward declaration -- NCDim_map_info::var_i_map is a non-owning back-pointer to
		 * the NCVar it maps; NCVar itself is defined below since it owns FDBlist/NCDim/
		 * NCDim_map_info via std::vector<std::unique_ptr<...>>. */

/* Full definition in ncview/dataset.h. FDBlist only ever holds a
 * non-owning pointer to one -- Dataset owns the actual open file (and
 * closes it on destruction), and since many variables in the same
 * physical file end up with their own FDBlist entry all pointing at the
 * one file Dataset opened for it, ownership can't live on FDBlist itself. */
class NetCDFFile;

/*****************************************************************************/
/* This describes the file which the relevant variable lives in */
struct FDBlist {
	NetCDFFile *file = nullptr;	/* owned by Dataset; see id() below */
	int	index = 0;	/* starts at 0, increments by 1 for each file associated
				 * with this variable */
	std::string	filename;

	/* The netCDF fileid this FDBlist's file was opened with. Defined in
	 * dataset.cc, where NetCDFFile is a complete type -- kept as a method
	 * rather than reverting to a plain int field so every existing
	 * fdb->id-style callsite (there are ~60 of them, all reads) only
	 * needed `->id` -> `->id()`, not a wider rewrite. */
	int id() const;
	std::unique_ptr<NetCDFOptions>	aux_data;	/* For specific datafile implementations */
	std::vector<size_t>	var_size;	/* Multi-dimensional size of variables which live in this file */
	float	data_min = 0, data_max = 0; /* for a specific variable in the file */

	/* Following is an ugly hack for an ugly problem.  Basically, different files can have
	 * different units for the unlimited dimension, and some people actually do this.  So
	 * we must store the recdim units for each file.  In a way this is a property more of
	 * the dimensions, so maybe should be in the NCDim structure somehow, but the units live
	 * in each file and have a 1-1 association with each file, so I'm putting them here.
	 */
	std::string	recdim_units;
#ifdef HAVE_UDUNITS2
	ut_unit	*ut_unit_ptr = nullptr;	/* only non-null if ut_parse worked on these units; owned by udunits2, not us */
#endif
};

/*****************************************************************************/
/* The dimension structure.  This is more for convienence and efficiency
 * than because dimensions are so fundamental; actually, it's the variables
 * which are more important.
 */
struct NCDim {
	std::string	name, long_name, units;
	int	units_change = 0;	/* if 1, then a virtully concatenated timelike dimension has different units in different input files */
	float	min = 0, max = 0;
	std::vector<float> values;
	int	have_calc_minmax = 0;  /* 0 initially, 1 after min & max have been calculated */
	size_t	size = 0;
	int	timelike = 0;	/* 0 if NOT timelike, 1 if is.  If is, MUST */
				/* have an identified time standard (below). */
	TimeStandard	time_std;	/* TimeStandard::Udunits, Epic0, Months */
	std::string	calendar;	/* ONLY applicable if time_std==TimeStandard::Udunits; can be any CF-1.0 value. Defaults to "standard" */
	TimeGranularity	tgran; 		/* time granularity; i.e., frequency of entries (daily, hourly, etc) */
	int	global_id = 0;	/* Used internally, goes from 1..total number of dims we know about */
	int	is_lat = 0, is_lon = 0; /* Just a guess if these are lat/lon. Used to put on coastlines automatically */
};

/*****************************************************************************/
/* A dimension can be "mapped", by which it means that, for example, the lat
 * or lon coordinates are two dimensional, and a variable is supplied that
 * gives the lat and/or lon values as a function of X and Y.
 */
struct NCDim_map_info {

	NCVar	*var_i_map = nullptr;	/* the "var that I map"; non-owning back-pointer */
	std::string	coord_att;		/* Contents of the "coordinates" attribute */
	std::string	coord_var_name;	/* Name of the VAR in the file that holds mapping info */
	int	coord_var_ndims = 0;	/* # of dims in the mapping var */
	std::string	coord_var_units;	/* if the coord var has a units att, this records it */
	std::vector<size_t> coord_var_size;	/* size of the mapping var (MULTIDIMENSIONAL) */
	std::vector<int> matching_var_dims;	/* This has n_dims equal to the DATA VARIABLE, NOT the coord var! */
	std::vector<float> data_cache;		/* Cached info from the mapping var */
	std::vector<size_t> index_place_factor;	/* Array of size var_i_map->n_dims, is 0 or factor to mult loc by */
	int	scalar_all_same = 0;	/* ==1 iff is a scalar coord var AND all vals are identical; ==0 otherwise */

	/* Set once, at scalar-coord discovery time (handle_dim_mapping_scalar()),
	 * the same way a real dimension's own timelike-ness is determined in
	 * handle_time_dim() -- lets view_construct_scalar_coord_str() print a
	 * scalar coordinate like "XTIME" as a calendar date (via fmt_time())
	 * instead of the bare "<value> <units>" it otherwise falls back to. */
	int	timelike = 0;
	std::string	calendar;	/* only meaningful if timelike; CF-1.0 value, may be empty ("standard") */
};

/*****************************************************************************/
/* Here it is: the variable structure.  Aspects of the variable which are
 * different from file to file are kept in the pointed-to file descriptor
 * blocks (FDBs).
 */
struct NCVar {
	std::string	name;
	float	fill_value = 0;			/* Any data with this special
						 * value will be IGNORED. It
						 * is assumed to indicate
						 * out-of-range or out-of-domain
						 * data.
						 */
	bool	have_set_range = false;	/* have we set the valid range for this var yet? */
	int	n_dims = 0;				/* how many dimensions this var has */
	std::vector<std::unique_ptr<FDBlist>> files;	/* What files this variable lives in */
	int	is_virtual = 0;			/* Boolean -- true if this var lives
						 * in more than one input file, false
						 * otherwise.
						 */
	std::vector<FDBlist*> timestep_2_fdb;	/* Files can only be virtually concatenated
	  					 * along the first (timelike) dimension.
						 * This gives pointers (non-owning; owned by
						 * 'files' above) to the FDBlist element that
						 * each individual timestep of the var lives
						 * in. So it is dimension size[0]. Note that
						 * many entries can point to the same target.
						 * For example, if a sequency of data files
						 * with a year's worth of monthly data is
						 * given, then the first 12 entries of this
						 * will all point to the first file's FDBlist.
						 * Because this can only be filled out
						 * AFTER we have processed all the files,
						 * it is done in a slighly strange place...
						 * in routine Dataset::cacheScalarCoordInfo().
						 */
	float	global_min = 0, global_max = 0,		/* These are diffferent from the */
	        user_min = 0, user_max = 0;	 	/* min & max in the FDBs because these
					 	* are global, rather than local to
					 	* a file.
					 	*/
	int	user_set_blowup = 0;		/* Initializes to -99999, then saves user-specified
	  					 * value of 'blowup' for this var so it can be
						 * used again if we leave this var & then come back
						 */
	int	auto_set_no_range = 0;		/* '1' if we autoset a range of -1,1 based
						 * on not having a valid range for this var
						 */
	std::vector<size_t> size;			/* The accumulated size of
						 * this variable, from all
						 * the files which hold it.
						 */
	int  	effective_dimensionality = 0;	/* # of entries in 'size' array > 1 */
	std::vector<std::unique_ptr<NCDim>> dim;	/* An array of 'n_dim' entries.  This
	 					 * is only filled out for
	 					 * scannable dimensions!! If
	 					 * the dim is not scannable,
	 					 * a null entry is inserted instead.
	 					 */
	std::vector<std::unique_ptr<NCDim_map_info>> dim_map_info;	/* array of ndims entries
						   that hold information describing
						   the 2-D mapping for this dim.
						   This vector itself should never be
						   empty of entries (it always has ndims
						   slots), but the entries CAN BE NULL,
						   in which event that dim has
						   no mapping.  Note that the
						   dim mapping is a function of
						   the VARIABLE rather than the
						   dim, which is an oddity of the
						   way CF conventions handle mapping.
						 */
	std::vector<std::unique_ptr<NCDim_map_info>> scalar_dim_map_info; 	/* A variable can also
						   specify SCALAR coordinate "vars",
						   for example, "height" for a 2d
						   field in (lon,lat). This holds
						   the scalar info. The reason this
						   is not part of athe dim_map_info
						   array is that array has exactly
						   ndims entries, describing how
						   each dim in the var is mapped.
						   This array can have many more
						   scalar "dims", locating the field
						   in space or time. They are really
						   different concepts but the CF
						   convention puts them both in
						   the "coordinates" attribute.
						   */
};

/*****************************************************************************/
/* Our current view--the view is the 2D field which is being color-contoured.
 */
struct View {
	NCVar	*variable;
	std::vector<size_t>	var_place;	/* Where we currently are in that var's space, in that file */
	std::vector<float>	data;		/* The actual 2-D data to colorcontour */
	ViewDataStatus	data_status;	/* Either valid, invalid, or edited (changed) */
	std::vector<ncv_pixel>	pixels;	/* Scaled, replicated, byte array version of data */
	int	x_axis_id, 	/* which axes the 2-D data lies on.  'scan' */
		y_axis_id,	/* is the one accessed by the pushbuttons */
		scan_axis_id;
	int	skip;		/* Number of time entries to stride each time */
	int	plot_XY_axis,	/* Which axis to plot along in XY plots */
		plot_XY_nlines;	/* # of XY lines for this variable on current plot */
	size_t	plot_XY_position[MAX_LINES_PER_PLOT][10];

	/* Formerly free functions in view.cc taking a View* as their first
	 * parameter -- moved onto the type itself since none of them pop
	 * dialogs, arm timers, or call any other in_/x_-prefixed UI function
	 * (view.cc's other ~40-odd functions mix state with UI calls and
	 * deliberately stayed free -- see PORTING.md's "OOP_redesign:
	 * view.cc" section for the full triage). All were already `static`
	 * (file-local to view.cc) with no external callers, so converting
	 * them is a pure in-file mechanical rename -- no header outside
	 * view.cc needed updating, and adding these member functions doesn't
	 * disqualify View from remaining an aggregate (tests/test_pixels.cc's
	 * `View view{};` keeps compiling): C++17 only bars user-declared
	 * constructors, virtual functions, and private/protected *data*
	 * members from an aggregate, not member functions. Bodies live in
	 * view.cc, right where the free functions they replace used to be. */

	/* Formerly init_view(): allocates and default-initializes a new View
	 * for 'var'. Caller owns the result. */
	static View *create( NCVar *var );

	/* True when this View has a real 2-D image to work on, i.e. both
	 * display axes actually resolved to a dimension of the variable.
	 *
	 * False in two situations, both of which leave size[], dim[],
	 * dim_map_info[] and var_place[] indexable only OUT OF BOUNDS -- these
	 * are std::vectors, so [-1] is [SIZE_MAX], a silent heap read or write
	 * eight bytes before the buffer:
	 *   - a 1-D variable is selected. initialDetermineScanAxes()'s case 1
	 *     sets y_axis_id = -1 by design, and set_scan_variable()'s
	 *     plot-and-return path then skips allocStorage()/fillViewData()
	 *     entirely, so `data` is empty too.
	 *   - the axes could not be determined at all (the degrade paths
	 *     Phase 12e added), which sets all three ids to -1.
	 *
	 * Phase 13b: every caller that indexes by x_axis_id/y_axis_id must
	 * check this first. Upstream guarded exactly one of them
	 * (ViewerController::reportPosition(), whose comment names the
	 * scenario) and missed the other thirteen, which is the user-reported
	 * "change from 1d to 3d back and forward multiple times and it can
	 * crash". */
	bool has2dAxes() const { return (x_axis_id >= 0) && (y_axis_id >= 0); }

	/* has2dAxes() plus a data buffer actually sized for it. The two are
	 * separate conditions, not one: set_scan_variable()'s 1-D path skips
	 * allocStorage() altogether, and it is also skipped for a variable
	 * whose effective_dimensionality is 1 but whose x/y axes did resolve
	 * (e.g. a (time=1, lat=1, lon=50) field), so a valid-looking pair of
	 * axis ids is not on its own evidence that `data` exists. Anything
	 * that reads or writes data[]/pixels[] must check this one;
	 * allocStorage() itself checks has2dAxes(), being what makes this
	 * true. */
	bool has2dImage() const { return has2dAxes() && !data.empty(); }

	void determineScanAxes( NCVar *var, View *old_view );
	void setScanPlace( NCVar *var, View *old_view );
	void calculateBlowup( NCVar *var, int val_to_set_to );
	void allocStorage();
	void fillViewData();
	bool hasMissingData() const;

	/* Phase 2 of the view.cc triage (PORTING.md): also file-local
	 * (static, no external callers), also state-mutating, but each
	 * makes exactly one direct UI call as part of that mutation --
	 * kept inline rather than split into a separate controller-side
	 * call, the same precedent Dataset::checkRanges (ncview/dataset.h)
	 * already set for calling in_dialog() directly from a state method
	 * when the call is small and unconditionally part of the operation. */
	void setScanButtons();
	void setAxis( Dimension dimension, char *new_dim_name );
	void showCurrentDimValues();
	void labelDimensions();
	void flipIfInverted();

	/* Phase 3 (of the earlier OOP_redesign view.cc triage -- not this
	 * plan's later, differently-numbered Phase 2): functions confirmed
	 * to have NO internal `view == NULL` guard of their own (unlike
	 * draw()/stepView()/reportPosition()/etc., which are called from
	 * contexts -- expose events, mouse clicks before any variable is
	 * selected -- where the active view can genuinely not exist yet, and
	 * so live on ViewerSession/ViewerController instead, which own the
	 * unique_ptr and can check once internally: see PORTING.md's
	 * "refine the architecture" Phase 2 entry). Each of these is only
	 * ever reached once a variable is already selected, so converting
	 * them to View methods (called via `view->...`) is exactly as safe
	 * as Phase 1/2's conversions -- verified by tracing every call site,
	 * not assumed. */
	void applyCurDimPlace( int dimid, NCDim *dim, size_t place );
	void setScanDims();
	/* Formerly set_scan_view(): jumps the scan axis to an absolute frame
	 * index during normal navigation/playback. Distinct from
	 * setScanPlace() above (Phase 1), which is the one-time initial
	 * axis/place setup performed when switching variables. */
	void scanToPlace( size_t scan_place );
	void changeBlowup( int delta, int redraw_flag, int view_var_is_valid );
	void setRange();
	void setRangeFrame();
	void setRangeLabels( float min, float max );
	void initSaveframes();
	void setDataeditPlace();
	void dataEdit();
	void changeDat( size_t index, float new_val );
	void dataEditDump();
	void plotXYSc( size_t *start, size_t *count );
	void setXYPlotAxis( char *label );
	void plotXYFmtXVal( float val, int dimindex, char *s, size_t s_len );
	void information();
	void redrawDimensionInfo();
	/* Formerly view_check_new_data(): also has no internal view==NULL
	 * guard -- only reached via a timer armed from
	 * ViewerController::draw() (which stays a session-owning method, not
	 * a plain View one -- Phase 2 -- since it has its own view==NULL
	 * guard: it's also called from expose events before any variable is
	 * selected) once view is already known valid, never directly from
	 * an expose/click event itself. */
	void checkNewData( int unused );

	/* Phase 4b of the "refine the architecture" plan (dissolving
	 * util.cc): data_to_pixels() already took a View* as its sole
	 * argument, so this is a straight method move, same pattern as the
	 * rest of this class. Bodies live in the new render_pipeline.cc,
	 * alongside the free helper functions (close_enough, clip_f,
	 * util_mean, util_mode) it and expandData/contractData below share --
	 * those have no natural "this" (they operate on raw float arrays with
	 * no View involved) and stay free functions in the same file. */
	int dataToPixels();

private:
	/* Also formerly free (util.cc), also already taking a View* first --
	 * moved alongside dataToPixels() above. Both were `static` (file-local
	 * to util.cc) with dataToPixels() as their only caller, so they stay
	 * private here too. */
	void expandData( float *big_data, size_t array_size );
	void contractData( float *small_data, float fill_value );


	/* Implementation details of determineScanAxes()/setScanPlace() above
	 * -- each had no callers outside the one public method it now
	 * belongs to. */
	void initialDetermineScanAxes( NCVar *var );
	void reDetermineScanAxes( NCVar *var, View *old_view );
	void initialSetScanPlace( NCVar *var );
	void reSetScanPlace( NCVar *var, View *old_view );
	/* Phase 2 postscript: formerly the file-local static free function
	 * view_construct_scalar_coord_str() -- both its call sites were
	 * already View methods (see core/src/view.cc). */
	void constructScalarCoordStr( char *str, int slen );
};

/* OOP_redesign plan's name for this struct once it's owned via unique_ptr
 * by the (formerly raw-pointer) global `view` in view.cc, rather than a
 * type rename -- View's dozens of `View *`/`View **` parameters throughout
 * view.cc/util.cc/overlay.cc and the by-value `View v{}`/`View view{}`
 * fixtures in tests/test_pixels.cc and tests/test_view_data_edit.cc stay
 * exactly as they are; only the global's ownership changed. */
using ViewState = View;

/*****************************************************************************/
/* program options */

/* Options for the overlay feature */
struct OverlayOptions {
	int	doit;
	std::vector<int>	overlay;
};

/* Forward declaration only -- ncview/viewer_session.h (which fully defines
 * ViewerSession, and includes this header itself) can't be included here
 * without a cycle. Options's constructor only needs a reference parameter
 * type at this point; its body, which needs the full ViewerSession
 * definition, is defined out-of-line in viewer_session.cc. */
class ViewerSession;

/* OOP_redesign plan, Step 9a: every field below is a reference member,
 * bound once (in the constructor, defined in viewer_session.cc) to the
 * corresponding field of one of ViewerSession's four grouped settings
 * structs (RenderSettings, PlaybackSettings, SessionDisplayPrefs,
 * StartupSettings -- see ncview/viewer_session.h for the grouping and the
 * rationale). Options's storage now genuinely lives on ViewerSession; this
 * struct is a flat, named view onto it, so every one of the ~300+ existing
 * `options.<field>` read/write call sites across core/, ui/, and tests/
 * keeps compiling and behaving identically -- no call site needed to
 * change. There is exactly one Options object in the program (the global
 * `options` in ncview.cc), constructed from `g_app.session`.
 */
struct Options {
	explicit Options( ViewerSession &session );

	int	&invert_physical,
		&invert_colors,
		&t_conv,
		&debug,
		&show_sel,
		&no_autoflip,
		&no_char_dims,
		&private_colormap,
		&want_extra_info,
		&n_colors,
		&n_extra_colors,	/* Supposedly for black, white, etc., but not used much nowadays */
		&small,
		&dump_frames,
		&no_1d_vars,
		&delta_step,	/* if > 0, percent of total frames to step when pressing the
				 * 'forward' or 'backward' button and holding down the Ctrl
				 * key; if < 0, absolute number of frames to step.
				 */
		&listsel_max,	/* if # of vars is more than this, auto switch from VARSEL_LIST to VARSEL_MENU */
		&color_by_ndims,	/* if 1, then button is color coded by # of effective dims */
		&beep_on_restart,
		&stop_on_restart,
		&auto_overlay,	/* if 1, then tries to figure out if coastlines should automatically be added */
		&blowup,
		&maxsize_pct,	/* -1 if a width/height pair specified instead */
		&maxsize_width,	/* in pixels */
		&maxsize_height,	/* in pixels */
		&blowup_default_size,
		&display_type;	/* This uses std 'X' defines; PseudoColor, DirectColor, etc */

	Transform	&transform;
	MinMaxMethod	&min_max_method;
	VarselStyle	&varsel_style;	/* can be VarselStyle::List or VarselStyle::Menu */
	ShrinkMethod	&shrink_method;

	std::string	&calendar;	/* This OVERRIDES any 'calendar' attribute in the data file; empty means "not set" */

	BlowupType	&blowup_type;	/* can be BlowupType::Replicate or BlowupType::Bilinear */

	int	&autoscale;	/* If TRUE, then tries to automatically scale colors for EACH frame.  Much slower!! */

	int	&save_frames;	/* If true, try to save frames in core for faster display */
	float	&frame_delay;	/* Normalied to be between 0.0 and 1.0 */

	int	&enable_group_sel;	/* TRUE if we have some vars in groups, so interface must incl. grp selection */

	int	&missval_r, &missval_g, &missval_b;	/* 0-255 values of R, G, B for missing data */

	float	&scale, &offset;	/* These do NOT refer to the scale & offset in the netcdf file. They are for changing units of data */
				/* SCALE IS APPLIED FIRST. So to conv C to F, use -scale 1.8 -offset 32 */

	std::unique_ptr<OverlayOptions> &overlay;
};

/***********************************************************************************************************/
/* How a printed page is laid out. Where the output goes (printer vs file,
 * which printer, paper, orientation) is the native print dialog's job --
 * see in_print() in ncview/interface.h. */
struct PrintOptions {
	float	page_x_margin, page_upper_y_margin,	/* In inches */
		page_lower_y_margin;
	int	font_size,
		leading,
		header_font_size;		/* In points */
	std::string	font_name;		/* One of the FLTK base face names -- see in_print() */
	int	include_outline,
		include_id,
		include_title,
		include_axis_labels,
		include_extra_info,
		test_only;
};

/* What core hands the UI to lay out on a printed page -- see in_print()
 * in ncview/interface.h. */
struct PrintInfo {
	std::string	title;				/* long var name + units, centered above image */
	std::string	x_axis_label, y_axis_label;
	std::vector<std::string>	extra_info;	/* one line each, below image */
	std::string	id_stamp;			/* user + date, rotated at the page edge */
	size_t		width, height;			/* of pixels[], in ncv_pixel units */
	const ncv_pixel	*pixels;			/* borrowed; valid for the duration of the in_print() call only */
};

