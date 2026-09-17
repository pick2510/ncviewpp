/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 2026 Dominik Strebel
 * Copyright (C) 1993 through 2024 David W. Pierce
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

/******************************************************************************
 *
 *	These routines handle the tricky job of setting the proper "view"
 *	of the data file.  The "view" is the 2-D slice which is color-
 *	contoured in the main display window.  Selecting exactly *what* 
 *	2-D plane to contour from a complex, multi-dimensional, multi-variable
 *	data file is not a trivial task!  If all else fails, be prepared
 *	to enter the view using buttons.
 *
 *******************************************************************************/

/* Include files */
#include "ncview/includes.h"
#include "ncview/dataset.h"	/* NetCDFFile -- file0->title()/dimUnits()/etc. below (Phase 6) */
#include "ncview/defines.h"
#include "ncview/frame_cache.h"
#include "ncview/protos.h"
#include "view_internal.h"

/* External variables */
extern	Options options;

/* Owns the active ViewState (an alias for View -- see defines.h). A
 * unique_ptr rather than a raw pointer so that every reassignment below
 * (set_scan_variable()'s variable switch, invalidate_variable()'s reset)
 * actually deletes whatever it previously owned instead of leaking it --
 * see those functions for the two sites this used to leak from. Bound to
 * g_app.session's own member (OOP_redesign plan, Step 8) rather than
 * owning storage directly -- view.cc stays the file that constructs and
 * mutates it, ViewerSession is just where the storage now lives. */
std::unique_ptr<ViewState> &view = g_app.session.activeView();

/* See comments in routine "ViewerController::draw" (viewer_controller.cc).
 * Not static: shared with viewer_controller.cc via view_internal.h. */
int 	lockout_view_changes = false;

/* Saved x/y values that are on the XY plot, used later for
 * dumping out.
 */
static std::vector<double> plot_XY_xvals, plot_XY_yvals;

/* Saved dimension for the XY plot of the relevant index,
 * so that we know how to format dumps of that dim's values. Note
 * that all lines on a plot have the same X dimension.
 */
static NCDim *plot_XY_dim[MAX_PLOT_XY];

/* Constants local to routines in this file */
#define	BUTTONS_ALL_ON		1
#define	BUTTONS_TIMEAXIS_OFF	2
#define	BUTTONS_ALL_OFF		3
/* Phase 13c: everything that acts on the 2-D picture, for a variable that
 * hasn't got one (View::has2dAxes() false). Distinct from BUTTONS_ALL_OFF,
 * which is the "this variable is unusable, stand down" state
 * invalidate_variable() uses: the controls that still make sense for a 1-D
 * plot -- colormap, invert-colormap, range, info -- stay on here. */
#define	BUTTONS_2D_OFF		4

/* Prototypes applicable to routines used ONLY in this file. Declarations
 * for invalidate_variable/mouse_xy_to_data_xy/view_data_edit_warn moved
 * to view_internal.h (Phase 3e) -- each now also has a caller in
 * viewer_controller.cc, though its definition stays here. beep() moved
 * out entirely: its only caller, stepView(), moved to
 * viewer_controller.cc too. */
static void 		set_buttons( int to_state, ViewerUi &ui );
static void 		draw_file_info( NCVar *var, ViewerSession &session, ViewerUi &ui );
static float 		view_calc_minval_float( float *arr, size_t n );
static float 		view_calc_maxval_float( float *arr, size_t n );
static void 		strip_trailing_zeros( char *s );

#define NFRAMES_RECORD	10
static int    n_new_frame_times=0;			/* Numer of valid entries in following two arrays */
static time_t new_frame_times[NFRAMES_RECORD];		/* TIME that new frame(s) were found */
static time_t new_frame_nframes[NFRAMES_RECORD];	/* NUMBER of new frames found at that time */

/********************************************************************************
 * Make the passed variable the new variable which can be scanned using the
 * buttons.
 */
	int
set_scan_variable( NCVar *var, ViewerSession &session, ViewerUi &ui )
{
	View	*new_view, *old_view;
	size_t	x_size, y_size, scaled_x_size, scaled_y_size;
	long	i;
	int	changed_size, overlay2use;
	float	range_x, range_y;
	NCDim	*xdim, *ydim, *xdim_old, *xdim_new, *ydim_old, *ydim_new;
	std::unique_ptr<ViewState> &view = session.activeView();

	if( options.debug ) {
		fprintf( stderr, "\n\n******************************************\nentering set_scan_variable with var=%s\n", var->name.c_str() );
		fprintf( stderr, "var nims:%d\n", var->n_dims );
		for( i=0; i<var->n_dims; i++ )
			fprintf( stderr, "dim=%ld size=%zu\n", i, var->size[i] );
		}

	ui.in_set_cursor_busy();

	set_buttons( BUTTONS_ALL_ON, ui );
	ui.unlock_plot();

	if( (view == NULL) || (view->x_axis_id == -1) || (view->y_axis_id == -1)) {
		/* A brand new variable to display!  Exciting! */
		if( options.debug )
			fprintf( stderr, "set_scan_variable: initializing view struct for new variable\n" );
		view.reset( View::create( var ) );
		set_blowup_type( options.blowup_type, ui );

		/* Figure out what axes to use for X, Y, and Time,
		 * and set the current place based on those axes
		 */
		if( options.debug )
			fprintf( stderr, "...determining scan axes (NEW)\n" );
		view->determineScanAxes( var, nullptr );
		if( options.debug )
			fprintf( stderr, "...axes ids: scan=%d y=%d x=%d\n", 
				view->scan_axis_id, view->y_axis_id, view->x_axis_id );
		if( var->effective_dimensionality == 1 ) {
			std::vector<size_t> start( view->variable->n_dims );
			std::vector<size_t> count( view->variable->n_dims );
			for( i=0; i<view->variable->n_dims; i++ ) {
				start[i] = view->var_place[i];
				count[i] = 1L;
				}
			count[view->x_axis_id] = view->variable->size[view->x_axis_id];
			if( options.debug )
				fprintf( stderr, "set_scan_variable (A): about to call plot_XY_sc\n" );
			view->plotXYSc( start.data(), count.data() );
			/* Phase 13c: this early return used to skip
			 * setScanButtons() entirely, so the toolbar kept
			 * whatever state the PREVIOUS variable left it in --
			 * BUTTONS_ALL_ON after any ordinary 2-D field. That is
			 * why every 2-D-only control stayed pressable while a
			 * 1-D variable was on screen. */
			view->setScanButtons();
			ui.in_popdown_2d_window();
			ui.in_set_cursor_normal();
			return(0);
			}

		if( options.debug )
			fprintf( stderr, "...setting scan place (NEW)\n" );
		view->setScanPlace( var, nullptr );

		/* Is the current field inverted?  If so, flip it back */
		if( options.debug )
			fprintf( stderr, "...determining if inverted (NEW)\n" );
		view->flipIfInverted();

		/* How big should we initially make the picture? */
		if( options.debug )
			fprintf( stderr, "...calculating blowup (NEW)\n" );
		view->calculateBlowup( var, -99999 );	/* last val is flag meaning to do automatic calculation */

		/* Save the blowup we are using in the var structure so we can
		 * return to it later if we want
		 */
		var->user_set_blowup = options.blowup;
		if( options.debug )
			fprintf( stderr, "... ... new blowup=%d\n", options.blowup );
		}
	else
		{
		/* Make a NEW view structure, and save the old one, 
		 * because we still want to scavenge the information
		 * on what place we are at from the old variable.  This
		 * is so that when you switch variables, it will keep
		 * the same scan dimensions and place, if possible.
		 */
		if( options.debug )
			fprintf( stderr, "set_scan_variable: initializing view struct for old variable\n" );
		old_view = view.get();
		new_view = View::create( var );

		/* Figure out what axes to use for X, Y, and Time,
		 * and set the current place based on those axes and
		 * the previous scan place.
		 */
		if( options.debug )
			fprintf( stderr, "...determining scan axes (PREVIOUS)\n" );
		new_view->determineScanAxes( var, old_view );
		if( var->effective_dimensionality == 1 ) {
			/* unique_ptr::reset() deletes whatever it previously
			 * owned (old_view, i.e. the pre-switch view) before
			 * taking ownership of new_view -- this used to be a
			 * bare pointer reassignment that leaked old_view. */
			view.reset( new_view );
			std::vector<size_t> start( view->variable->n_dims );
			std::vector<size_t> count( view->variable->n_dims );
			for( i=0; i<view->variable->n_dims; i++ ) {
				start[i] = view->var_place[i];
				count[i] = 1L;
				}
			count[view->x_axis_id] = view->variable->size[view->x_axis_id];
			if( options.debug )
				fprintf( stderr, "set_scan_variable (B): about to call plot_XY_sc\n" );
			view->plotXYSc( start.data(), count.data() );
			/* Phase 13c: this early return used to skip
			 * setScanButtons() entirely, so the toolbar kept
			 * whatever state the PREVIOUS variable left it in --
			 * BUTTONS_ALL_ON after any ordinary 2-D field. That is
			 * why every 2-D-only control stayed pressable while a
			 * 1-D variable was on screen. */
			view->setScanButtons();
			ui.in_popdown_2d_window();
			ui.in_set_cursor_normal();
			return(0);
			}
		new_view->setScanPlace( var, old_view );

		/* Calculate new blowup.  If the old var was using the same display dimensions
		 * as the new var is using, then don't modify the current blowup.  Otherwise,
		 * calculate a new blowup.  This is more complicated than it might be because
		 * ncview can process multiple files, each of which might have its own mapping
		 * of dimension id to dimension.  So to really see if we are using the same
		 * dimensions as the previous variable, we have to go through the NAME of the
		 * dimensions.  These are guaranteed to be unique, according to the netCDF 
		 * standard.  (I.e., if two variables are using the "Longitude" dimension, you
		 * can know that it's the SAME Longitude dimension.)
		 */
		xdim_old = old_view->variable->dim[old_view->x_axis_id].get(); 
		ydim_old = old_view->variable->dim[old_view->y_axis_id].get(); 
		xdim_new = new_view->variable->dim[new_view->x_axis_id].get(); 
		ydim_new = new_view->variable->dim[new_view->y_axis_id].get(); 
		if( (xdim_old==NULL) || (ydim_old==NULL) || (xdim_new==NULL) || (ydim_new==NULL) ||
		    (xdim_old->name != xdim_new->name) ||
		    (ydim_old->name != ydim_new->name)) {
			if( options.debug )
				fprintf( stderr, "...axis change, recalculating blowup; old, new X dim=%s, %s; old, new Y dim=%s, %s\n",
					xdim_old->name.c_str(), xdim_new->name.c_str(), ydim_old->name.c_str(), ydim_new->name.c_str() );
			new_view->calculateBlowup( var, var->user_set_blowup );
			/* Save the blowup we are using in the var structure so we can
			 * return to it later if we want
			 */
			var->user_set_blowup = options.blowup;
			}

		/* If the new var has a different shape than the old var,
		 * then clear out any old, previous overlay we might be using.
		 */
		if( (xdim_new == NULL) || (xdim_old == NULL) || (ydim_new == NULL) || (ydim_old == NULL) || 
				(xdim_new->size != xdim_old->size) || (ydim_new->size != ydim_old->size))
			do_overlay(OVERLAY_NONE,NULL,true);

		/* Release the old View entirely -- upstream only freed its
		 * heap-backed members (equivalent to the vector clear()s this
		 * used to do here) and intentionally leaked the malloc'd View
		 * struct itself on every variable switch. Nothing holds a
		 * reference to old_view past this point, and view.reset()
		 * deletes it (the object it currently owns) before taking
		 * ownership of new_view, so there's no parity reason left to
		 * keep leaking it. */
		view.reset( new_view );
		}

	/* Set the tape-recorder style buttons to enable or disabled
	 * state, as appropriate for the selected variable and dimensions.
	 */
	view->setScanButtons();

	/* Allocate storage space for the data */
	view->allocStorage();

	/* Actually read the data in from the file */
	if( options.debug )
		fprintf( stderr, "...reading data from file\n" );
	view->fillViewData();

	if( options.save_frames == true )
		{
		if( options.debug )
			fprintf( stderr, "calling init_saveframes from set_scan_variable\n" );
		view->initSaveframes();
		}

	/* Set the min and maxes of the data */
	if( !view->variable->have_set_range )
		g_app.session.dataset().initMinMax( var, ui );

	/* If we are automatically putting on overlays, do so now */
	xdim = view->variable->dim[view->x_axis_id].get();
	ydim = view->variable->dim[view->y_axis_id].get();
	if( options.auto_overlay && ui.in_report_auto_overlay() && xdim->is_lon && ydim->is_lat ) {
		/* Only put overlay on automatically if range is big enough that
		 * the coastlines can be recognized. 
		 */
		range_x = fabs(xdim->max - xdim->min);
		range_y = fabs(ydim->max - ydim->min);
		if( (range_x > 60.) && (range_y > 60.))
			overlay2use = OVERLAY_P8DEG;
		else if( (range_x > 10.) && (range_y > 10.))
			overlay2use = OVERLAY_P08DEG;
		else
			overlay2use = -1;
		if( (overlay2use != -1) && (! view->hasMissingData()))
			do_overlay(overlay2use,NULL,true);
		}

	/* Convert the data to pixels; return on error condition */
	if( options.debug )
		fprintf( stderr, "...converting data to pixels\n" );
	{
	LockoutViewChangesGuard lockout_guard;
	if( view->dataToPixels() < 0 ) {
		ui.in_timer_clear();
		if( view->variable->global_min == view->variable->global_max )
			invalidate_variable( view->variable, session, ui );
		return( -1 );
		}
	}

	/* put variable and file information on the screen */
	if( options.debug )
		fprintf( stderr, "...putting var & file info on screen\n" );
	draw_file_info( var, session, ui );

	/* put the dimension information on the screen */
	if( options.debug )
		fprintf( stderr, "...putting dimension info on screen\n" );
	view->redrawDimensionInfo();

	/* Draw the frame info on the screen */
	if( options.debug )
		fprintf( stderr, "...putting frame info on screen, scan_axis_id=%d\n", view->scan_axis_id );
	if( view->scan_axis_id == -1 ) 
		view->scanToPlace( 0L );
	else
		view->scanToPlace( view->var_place[view->scan_axis_id] );

	/* Actually draw the color contour map of the data! */
	if( options.debug )
		fprintf( stderr, "...drawing color contour field\n" );
	x_size = view->variable->size[view->x_axis_id];
	y_size = view->variable->size[view->y_axis_id];
	view_get_scaled_size( options.blowup, x_size, y_size, &scaled_x_size, &scaled_y_size );
	changed_size = ui.in_set_2d_size( scaled_x_size, scaled_y_size );
	/* If we increased in size then we don't need to redraw,
	 * because an expansion generates an expose event, which
	 * is registered to call change_view.
	 */
	if( changed_size < 1 )
	 	g_app.controller.stepView(0,FRAMES);

	/* Pop up the window if we are going to use it. */
	ui.in_popup_2d_window();

	ui.in_set_cursor_normal();

	if( options.debug )
		fprintf( stderr, "...recomputing colorbar\n" );
	g_app.controller.recomputeColorbar();

	if( options.debug )
		fprintf( stderr, "exiting set_scan_variable.\n" );
	return( 0 );
}

/*****************************************************************************
 * Vector through this routine when a new display variable has been
 * selected by the user, by pressing some sort of button. Formerly
 * interface_glue.cc, dissolved into the file that owns set_scan_variable().
 */
void
in_variable_selected( const char *var_name )
{
	NCVar	*var;

	if( (var = g_app.session.dataset().findVariable( var_name )) == NULL ) {
		fprintf( stderr, "ncview: in_variable_selected: internal error " );
		fprintf( stderr, "no variable with name >%s< found on variable list\n",
					var_name );
		exit( -1 );
		}

	/* Fixed seam entry point (called by ui/ code by this exact
	 * free-function name), so it's the boundary that still reaches
	 * g_app.session/g_app.ui explicitly -- set_scan_variable() itself,
	 * one call away, no longer does (Phase 11a). */
	set_scan_variable( var, g_app.session, *g_app.ui );
}

/**************************************************************************************/
	void
View::setScanButtons()
{
	View *local_view = this;
	ViewerUi	&ui = *g_app.ui;
	/* Phase 13c: was `static int` -- same leaked-static class Phases 11h
	 * and 12c cleaned up elsewhere. Harmless in practice only because it
	 * is assigned unconditionally on the line below before any read. */
	int		set_state;
	const char	*label    = NULL;
	char		scalar_coord_str[1024];

	set_state = BUTTONS_ALL_ON;

	/* Phase 13c: no 2-D picture at all (a 1-D variable, or the degrade
	 * paths that leave every axis id -1). Checked first and returned from
	 * directly, because the two scan-axis cases below would only narrow a
	 * state that is already a strict superset of them. */
	if( ! local_view->has2dAxes() ) {
		set_buttons( BUTTONS_2D_OFF, ui );
		ui.in_set_label( Label::ScanPlace, "No 2-D display axes" );
		local_view->constructScalarCoordStr( scalar_coord_str, 1020 );
		ui.in_set_label( Label::ScalarDims, scalar_coord_str );
		return;
		}

	/* If there is no scan axis, then disable the buttons
	 * which access it.
	 */
	if( local_view->scan_axis_id == -1 ) {
		set_state = BUTTONS_TIMEAXIS_OFF;
		label     = "No scan axis";
		}

	/* If the scan axis is currently appearing as the X or
	 * Y axis, then disable the buttons which step the
	 * scan axis (since they are ALL being displayed at
	 * the moment!)
	 */
	if( (local_view->scan_axis_id == local_view->x_axis_id) ||
	    (local_view->scan_axis_id == local_view->y_axis_id) ) {
		set_state = BUTTONS_TIMEAXIS_OFF;
		label = "Scan axis is displayed";
		}

	set_buttons( set_state, ui );
	ui.in_set_label( Label::ScanPlace, label );

	local_view->constructScalarCoordStr( scalar_coord_str, 1020 );
	ui.in_set_label( Label::ScalarDims, scalar_coord_str );
}

/********************************************************************************
 * Set the time place of the view to the specified location.
 */
	void
View::scanToPlace( size_t scan_place )
{
	View *view = this;
	ViewerUi	&ui = *g_app.ui;
	char	temp_string[1024], scalar_coord_str[1024];
	std::string view_place;
	size_t	size;
	char	*dim_name;
	double	new_dimval, bound_min, bound_max;
	nc_type	type;
	NCDim	*dim;
	int	has_bounds;

	/* If there is no valid scan axis, immediately return.  That 
	 * case is delt with exclusively at start-up, when selecting
	 * a new variable.
	 */
	if( view->scan_axis_id == -1 )
		return;

	size = view->variable->size[view->scan_axis_id];
	if( scan_place >= size ) {
		fprintf( stderr, "ncview: set_scan_view: internal error; trying to " );
		fprintf( stderr, "set to a place larger than exists\n" );
		fprintf( stderr, "size: %zu   attempted place: %zu\n", size, scan_place+1 );
		fprintf( stderr, "resetting to zero\n" );
		scan_place = 0;
		}
		
	dim = view->variable->dim[view->scan_axis_id].get();
	dim_name = const_cast<char *>(dim->name.c_str());
	view->var_place[view->scan_axis_id] = scan_place;
	view_place = "frame " + std::to_string(scan_place+1) + "/" + std::to_string(size) + " ";

	/* type is the data type of the dimension--can be float or character */
	type = g_app.session.dataset().dimValue( view->variable, view->scan_axis_id, scan_place, &new_dimval,
			temp_string, &has_bounds, &bound_min, &bound_max, view->var_place.data() );
	if( type == NC_DOUBLE ) {
		if( dim->timelike && options.t_conv ) {
			fmt_time( temp_string, 1024, new_dimval, dim, 1 );
			view_place += temp_string;
			if( has_bounds ) {
				snprintf( temp_string, 1023, " (%d bnds:", has_bounds );
				view_place += temp_string;

				fmt_time( temp_string, 1024, bound_min, dim, 0 );
				view_place += temp_string;

				view_place += " -> ";

				fmt_time( temp_string, 1024, bound_max, dim, 0 );
				view_place += temp_string;

				view_place += ")";
				}
			}
		else
			{
			snprintf( temp_string, 1023, "%lg", new_dimval );
			if( has_bounds ) {
				snprintf( temp_string, 1023, " (%d bnds:", has_bounds );
				view_place += temp_string;

				snprintf( temp_string, 1023, "%lg", bound_min );
				view_place += temp_string;

				view_place += " -> ";

				snprintf( temp_string, 1023, "%lg", bound_max );
				view_place += temp_string;

				view_place += ")";
				}
			}
		}
	else
		{ /* don't have to do anything, since string-type dimval
		   * is already in variable "temp_string"
		   */
		}
	ui.in_set_label( Label::ScanPlace, view_place.c_str() );
	ui.in_set_cur_dim_value( dim_name, temp_string );
	view->data_status = ViewDataStatus::Invalid;
	if( options.want_extra_info ) {
		ui.in_set_label( Label::CcInfo2, temp_string );
		}

	/* Construct string showing the values of the scalar coordinates
	 * for this variable, if any
	 */
	view->constructScalarCoordStr( scalar_coord_str, 1020 );
	ui.in_set_label( Label::ScalarDims, scalar_coord_str );
}

/********************************************************************************
 * Checks if the file has grown since we last saw it
 */
	void
View::checkNewData( int unused )
{
	View *view = this;
	ViewerUi	&ui = *g_app.ui;
	size_t 	file_var_size[MAX_NC_DIMS], *t, n_other;
	size_t	i;
	int	has_grown, timelike_index;
	size_t	dt, nt_new, n_scan_entries, n_extra_frames;
	char	message[1024], rate_units[50];
	time_t	tt;
	long	nframes_tot, delta_time;
	float	rate_per_sec, rate_per_min, rate_per_hour, rate_per_day, rate,
		min, max, *data, avg;

	ui.in_timer_clear();

	timelike_index = 0;

	/* We only check for a file being extended if ncview is currently
	 * displaying the last time step.  We do not check if, for example,
	 * a Hoovermuller diagram is being displayed.
	 */
	if( view->scan_axis_id != timelike_index ) 	/* in netcdf v3, simple way to check for time dimension */
		return;

	/* Get size of the var in the last file. Deliberately NOT migrated onto
	 * the tracked NetCDFFile* in Phase 7b: this opens a second, throwaway
	 * fileid for the same path to see the on-disk file's CURRENT size --
	 * the tracked object's fileid is the stale, already-open handle whose
	 * growth is exactly what's being checked for.
	 *
	 * Phase 12d: was netcdf_fi_initialize() + a raw nc_close() -- but
	 * netcdf_fi_initialize() exit()s the whole process if the reopen
	 * fails, which is exactly the wrong behavior for a once-a-second
	 * background poll: a file that's been deleted, replaced, or is
	 * briefly unreadable (another process mid-rewrite) shouldn't kill a
	 * live ncview session. NetCDFFile::open() (Phase 6) is the same
	 * nc_open() call with a testable std::optional failure instead, and
	 * its destructor closes the fileid automatically, so the raw
	 * nc_close() below is gone too, not just relocated. On failure,
	 * report once and stop watching this variable for growth -- not
	 * re-arm and retry every second, which would mean a dialog storm if
	 * the file stays gone (matches View::initSaveframes()'s "degrade
	 * gracefully, don't nag" precedent elsewhere in this file). */
	int t_nc_errcode = NC_NOERR;
	auto t_file = NetCDFFile::open( view->variable->files.back()->filename, &t_nc_errcode );
	if( ! t_file.has_value() ) {
		snprintf( message, sizeof(message),
			"Can't re-open \"%s\" to check for new data (%s); no longer watching this variable for growth.",
			view->variable->files.back()->filename.c_str(), nc_strerror( t_nc_errcode ) );
		in_error( message );
		return;
		}
	t = t_file->varSize( const_cast<char *>(view->variable->name.c_str()) );
	for( i=0; i<static_cast<size_t>(view->variable->n_dims); i++ )
		file_var_size[i] = t[i];
	free( t );

	has_grown = 0;
	if( file_var_size[ timelike_index ] > view->variable->files.back().get()->var_size[ timelike_index ] ) {
		has_grown = 1;
		}

	if( ! has_grown ) {
		ui.in_timer_set( [](){ ::view->checkNewData(0); }, 1000L );
		return;
		}

	dt = file_var_size[timelike_index] - view->variable->files.back().get()->var_size[timelike_index];
	nt_new = view->variable->size[timelike_index] + dt;

	/* Make our informative label */
	tt = time(NULL);	/* Time this new frame was found, in seconds since epoch */
	if( n_new_frame_times == NFRAMES_RECORD ) {
		for( i=0; i<(NFRAMES_RECORD-1); i++ ) {
			new_frame_times[i]   = new_frame_times[i+1];
			new_frame_nframes[i] = new_frame_nframes[i+1];
			}
		n_new_frame_times = NFRAMES_RECORD-1;
		}
	new_frame_times[n_new_frame_times] = tt;
	new_frame_nframes[n_new_frame_times] = dt;
	n_new_frame_times++;

	if( n_new_frame_times < 2 ) 
		snprintf( message, 1023, "New frame found %s", ctime(&tt) );
	else
		{
		nframes_tot = 0;
		for( i=0; i<static_cast<size_t>(n_new_frame_times-1); i++ )
			nframes_tot += new_frame_nframes[i];
		delta_time = new_frame_times[n_new_frame_times-1] - new_frame_times[0];
		rate_per_sec = (float)(nframes_tot)/(float)(delta_time);
		rate_per_min = rate_per_sec   * 60.0;
		rate_per_hour = rate_per_min  * 60.0;
		rate_per_day  = rate_per_hour * 24.0;

		if( rate_per_sec > 0.5 ) {
			rate = rate_per_sec;
			snprintf( rate_units, sizeof(rate_units), "%s", "/sec" );
			}
		else if( rate_per_min > 0.5 ) {
			rate = rate_per_min;
			snprintf( rate_units, sizeof(rate_units), "%s", "/min" );
			}
		else if( rate_per_hour > 0.5 ) {
			rate = rate_per_hour;
			snprintf( rate_units, sizeof(rate_units), "%s", "/hour" );
			}
		else
			{
			rate = rate_per_day;
			snprintf( rate_units, sizeof(rate_units), "%s", "/day" );
			}
		snprintf( message, 1023, "New frame found %s (Rate=%.2f%s)", ctime(&tt), rate, rate_units);
		for( i=0; i<strlen(message); i++ )
			if( message[i] == 10 )
				message[i] = ' ';
		}
	ui.in_set_label( Label::Title, message );

	/* See if we need to reallocate the framestore */
	if( nt_new >= g_app.session.frameCache().nt() ) {
		n_scan_entries = view->variable->size[view->scan_axis_id];
		n_extra_frames = floor( n_scan_entries * 0.2 ) + 1;
		if( n_extra_frames < 25 )
			n_extra_frames = 25;

		if( options.debug )
			printf( "reallocating framestore to new nt=%zu\n", nt_new + n_extra_frames );

		g_app.session.frameCache().growTo( nt_new + n_extra_frames );
		}

	view->variable->size[ timelike_index ] = nt_new;
	view->variable->files.back().get()->var_size[ timelike_index ] += dt;

	/* The newly appended timesteps all live in the last (growing) file;
	 * keep timestep_2_fdb (built once, up front, in
	 * Dataset::cacheScalarCoordInfo()) in sync so looking up one of them
	 * doesn't index past its old, now-too-short length.
	 */
	{
	FDBlist *last_file = view->variable->files.back().get();
	for( size_t k=0; k<dt; k++ )
		view->variable->timestep_2_fdb.push_back( last_file );
	}

	/* Resync so we will read the last time entry */
	nc_sync( view->variable->files.back().get()->id() );

	/* Special check: if we were started with no range in the variable,
	 * but now we have one, then reset the displayed range
	 */
	if( view->variable->auto_set_no_range ) {
		n_other = 1L;
		for( i=0; i<static_cast<size_t>(view->variable->n_dims); i++ )
			if( i != static_cast<size_t>(timelike_index) )
				n_other *= view->variable->size[i];
		std::vector<float> data_buf( n_other );
		data = data_buf.data();
		g_app.session.dataset().getMinMaxOnestep( view->variable, n_other, nt_new, data, &min, &max, 0 );
		if( min != max ) {
			view->variable->auto_set_no_range = 0;
			if( (min < 0) && (max > 0)) {
				if( -min > max ) {
					min = min * 3.0;
					max = -min;
					}
				else
					{
					max = max * 3.0;
					min = -max;
					}
				}
			else if( min >= 0 ) {
				if( min == 0 )
					max = max * 3.0;
				else
					{
					avg = (min+max)*0.5;
					min = avg - (avg-min)*3.0;
					if( min < 0 )
						min = 0;
					max = avg + (max-avg)*3.0;
					}
				}
			else
				{
				if( max == 0 )
					min = min * 3.0;
				else
					{
					avg = (min+max)*0.5;
					min = avg - (avg-min)*3.0;
					max = avg + (max-avg)*3.0;
					if( max > 0 )
						max = 0;
					}
				}
				
			view->variable->user_min = min;
			view->variable->user_max = max;
			view->setRangeLabels( min, max );
			view->data_status = ViewDataStatus::Invalid;
			g_app.session.invalidateAllSaveframes();
			g_app.controller.recomputeColorbar();
			}
		}

	/* Jump to the last frame and display it.
	 * NOTE that this ALSO sets the timer to call this
	 * routine again as a side effect
	 */
	g_app.controller.stepView( dt, FRAMES );
}

/********************************************************************************
 * Determine what axes we should display the data using.  This returns
 * a three element Stringlist*; the first is the 'scan' axis, the second
 * is the 'Y' axis, and the third is the 'X' axis.  If there is no valid
 * scan axis, return a zero length string ("") as that dimension.
 * If old_view is not NULL, then old_view is assumed to be the view which was
 * in use immediately preceeding a change of view; in that case, if 
 * possible, use the same view as before.  The exception to this is if
 * the previous variable was only a 1-d variable; in that case we do not
 * want to limit ourselves to its restrictions if we can avoid them.
 */
	void
View::determineScanAxes( NCVar *var, View *old_view )
{
	View *view = this;

	initialDetermineScanAxes( var );

	if( view->scan_axis_id != -1 )
		view->plot_XY_axis = view->scan_axis_id;
	else if( view->x_axis_id != -1 )
		view->plot_XY_axis = view->x_axis_id;
	/* We can't do an XY plot of dimensions that have a count
	 * of one.  In that case, try to set it to something else.
	 */
	if( view->variable->size[view->plot_XY_axis] == 1 ) {
		if( (view->scan_axis_id != -1) &&
		    (view->variable->size[view->scan_axis_id] > 1))
			view->plot_XY_axis = view->scan_axis_id;

		else if( (view->x_axis_id != -1) &&
		    (view->variable->size[view->x_axis_id] > 1))
			view->plot_XY_axis = view->x_axis_id;

		else if( (view->y_axis_id != -1) &&
		    (view->variable->size[view->y_axis_id] > 1))
			view->plot_XY_axis = view->y_axis_id;
		}

	if( (old_view != NULL) && (old_view->variable->effective_dimensionality > 1))
		{
		reDetermineScanAxes( var, old_view );

		/* We can exit the above code with the X and Y dimensions
		 * the same, if the newly picked variable has less dimensions than
		 * the old one BUT some overlap of dimensions.  Check for
		 * this, and give up on picking the X and Y dimensions this
		 * way if this is the case.
		 */
		if( view->x_axis_id == view->y_axis_id )
			initialDetermineScanAxes( var );

		/* Don't let an axis be BOTH scan AND x or y */
		if( view->x_axis_id == view->scan_axis_id )
			view->scan_axis_id = -1;
		if( view->y_axis_id == view->scan_axis_id )
			view->scan_axis_id = -1;

		/* Final sanity checks! */
		if( (view->x_axis_id == -1) ||
		    (view->y_axis_id == -1) ||
		    (view->x_axis_id > view->variable->n_dims) ||
		    (view->y_axis_id > view->variable->n_dims))
			initialDetermineScanAxes( var );
		}
}

/**************************************************************************************/
	void
View::initialDetermineScanAxes( NCVar *var )
{
	View *view = this;
	Stringlist	*dimlist;
	int		n_dims;
	/* Every fi_scannable_dims()/fi_dim_name_to_id() call below used to
	 * re-derive a bare fileid via ->files.front().get()->id() just to hand
	 * it to a free-function dispatcher; both are now NetCDFFile methods
	 * (Phase 6), so the owning object itself is enough. */
	NetCDFFile *file0 = var->files.front()->file;

	if( options.debug ) fprintf( stderr, "initial_determine_scan_axes: entering for var %s\n", const_cast<char *>(var->name.c_str()) );

	/* Get a list of all possible scannable dimensions */
	dimlist = file0->scannableDims( const_cast<char *>(var->name.c_str()) );

	if( options.debug ) {
		fprintf( stderr, "initial_determine_scan_axes: scannable dims:\n" );
		stringlist_dump( dimlist );
		}

	/* For now, just pick the last two to be the Y and X axes.
	 */
	n_dims = stringlist_len( dimlist );
	switch( n_dims ) {
		case 1:
			view->scan_axis_id = -1;
			view->y_axis_id    = -1;
			view->x_axis_id    = file0->dimNameToId(
					const_cast<char *>(var->name.c_str()),
					(char *)(*dimlist)[0].string.c_str() );
			break;

		case 2:
			view->scan_axis_id = -1;
			view->y_axis_id    = file0->dimNameToId(
					const_cast<char *>(var->name.c_str()),
					(char *)(*dimlist)[0].string.c_str() );
			/* Phase 12e: these 5 checks used to exit(-1) -- an internal
			 * inconsistency between scannableDims() and dimNameToId(),
			 * not a user-facing error, but killing the whole process is
			 * worse than reporting and leaving this View with no usable
			 * axes (matches case 1's own established "-1 is a valid
			 * 'no axis' state" precedent just above). */
			if( view->y_axis_id == -1 ) {
				fprintf( stderr, "initial_determine_scan_axes: internal error: dim >%s< was indicated by routine fi_scannable_dims to be a scannable dim for var >%s<, but routine fi_dim_name_to_id did not find that dim for the var\n",
					(*dimlist)[0].string.c_str(), const_cast<char *>(var->name.c_str()) );
				in_error( "Internal error determining this variable's axes; it may not display correctly." );
				view->scan_axis_id = view->y_axis_id = view->x_axis_id = -1;
				return;
				}
			view->x_axis_id    = file0->dimNameToId(
					const_cast<char *>(var->name.c_str()),
					(char *)(*dimlist)[1].string.c_str() );
			if( view->x_axis_id == -1 ) {
				fprintf( stderr, "initial_determine_scan_axes: internal error: dim >%s< was indicated by routine fi_scannable_dims to be a scannable dim for var >%s<, but routine fi_dim_name_to_id did not find that dim for the var\n",
					(*dimlist)[1].string.c_str(), const_cast<char *>(var->name.c_str()) );
				in_error( "Internal error determining this variable's axes; it may not display correctly." );
				view->scan_axis_id = view->y_axis_id = view->x_axis_id = -1;
				return;
				}
			break;

		default:
			/* By default, set the scan dimension to the first of the scannable
	 		* dims, because that one will be the 'time' dimension by netCDF
	 		* standards. Y/X axes are the last two entries.
	 		*/
			view->scan_axis_id = file0->dimNameToId(
					const_cast<char *>(var->name.c_str()),
					(char *)(*dimlist)[0].string.c_str() );
			if( view->scan_axis_id == -1 ) {
				fprintf( stderr, "initial_determine_scan_axes: internal error: dim >%s< was indicated by routine fi_scannable_dims to be a scannable dim for var >%s<, but routine fi_dim_name_to_id did not find that dim for the var\n",
					(*dimlist)[0].string.c_str(), const_cast<char *>(var->name.c_str()) );
				in_error( "Internal error determining this variable's axes; it may not display correctly." );
				view->scan_axis_id = view->y_axis_id = view->x_axis_id = -1;
				return;
				}

			view->y_axis_id    = file0->dimNameToId(
					const_cast<char *>(var->name.c_str()),
					(char *)(*dimlist)[n_dims-2].string.c_str() );
			if( view->y_axis_id == -1 ) {
				fprintf( stderr, "initial_determine_scan_axes: internal error: dim >%s< was indicated by routine fi_scannable_dims to be a scannable dim for var >%s<, but routine fi_dim_name_to_id did not find that dim for the var\n",
					(*dimlist)[n_dims-2].string.c_str(), const_cast<char *>(var->name.c_str()) );
				in_error( "Internal error determining this variable's axes; it may not display correctly." );
				view->scan_axis_id = view->y_axis_id = view->x_axis_id = -1;
				return;
				}
			view->x_axis_id    = file0->dimNameToId(
					const_cast<char *>(var->name.c_str()),
					(char *)(*dimlist)[n_dims-1].string.c_str() );
			if( view->x_axis_id == -1 ) {
				fprintf( stderr, "initial_determine_scan_axes: internal error: dim >%s< was indicated by routine fi_scannable_dims to be a scannable dim for var >%s<, but routine fi_dim_name_to_id did not find that dim for the var\n",
					(*dimlist)[n_dims-1].string.c_str(), const_cast<char *>(var->name.c_str()) );
				in_error( "Internal error determining this variable's axes; it may not display correctly." );
				view->scan_axis_id = view->y_axis_id = view->x_axis_id = -1;
				return;
				}
			break;
		}

	if( options.debug ) fprintf( stderr, "initial_determine_scan_axes: exiting with axis_ids: scan=%d y=%d x=%d\n",
		view->scan_axis_id, view->y_axis_id, view->x_axis_id );
}

/********************************************************************************
 * Actually go to the data file and read the data in, putting the result
 * in the view structure.
 */
	void
View::fillViewData()
{
	View *v = this;
	int	i;

	if( v->data_status == ViewDataStatus::Valid )
		return;

	/* Phase 13b: count[] is indexed by the axis ids just below, and
	 * getData() writes x_size*y_size floats into v->data -- both are heap
	 * corruption if the axes are unresolved or allocStorage() never ran
	 * for this View (see View::has2dImage()). Silent, because this is an
	 * internal helper several UI paths funnel into; the entry points that
	 * can be driven by a button report the error themselves. */
	if( ! v->has2dImage() )
		return;

	std::vector<size_t> count( v->variable->n_dims );

	/* By default, count of 1 for all uninteresting dimensions */
	for( i=0; i<v->variable->n_dims; i++ )
		count[i] = 1;

	/* Do full count of the X and Y axes so we can display the whole 2D field */
	count[v->x_axis_id] = v->variable->size[v->x_axis_id];
	count[v->y_axis_id] = v->variable->size[v->y_axis_id];

	if( options.debug || options.show_sel ) {
		printf( "-var %s -start \\(", v->variable->name.c_str() );
		for( i=v->variable->n_dims-1; i >= 0; i-- ) {
			printf( "%1zu", 1 + (v->var_place[i]) );
			if( i != 0 )
				printf( "," );
			}
		printf( "\\) -count \\(" );
		for( i=v->variable->n_dims-1; i >= 0; i-- ) {
			printf( "%1zu", count[i] );
			if( i != 0 )
				printf( "," );
			}
		printf( "\\) %s\n", v->variable->files.front()->filename.c_str() );
		}

	g_app.session.dataset().getData( v->variable, v->var_place.data(), count.data(), v->data.data() );

	v->data_status = ViewDataStatus::Valid;
}

/********************************************************************************
 * Alter the amount by which we are blowing up pixels
 */
	void
View::changeBlowup( int delta, int redraw_flag, int view_var_is_valid )
{
	View *view = this;
	ViewerUi	&ui = *g_app.ui;
	size_t	x_size, y_size, scaled_x_size, scaled_y_size;
	char	blowup_label[32];
	int	changed_size;

	ui.in_set_cursor_busy();

	/* Sequence of 'options.blowup' should be: ..., -4, -3, -2, 1, 2, 3, ... */
	if( delta > 0 ) {
		if( options.blowup + delta == 0 ) 
			options.blowup = 2;
		else if( options.blowup + delta == -1 )
			options.blowup = 1;
		else
			options.blowup += delta;
		}
	else if( delta < 0 ) {
		if( options.blowup + delta == 0 ) 
			options.blowup = -2;
		else if( options.blowup + delta == -1 )
			options.blowup = -3;
		else
			options.blowup += delta;
		}

	if( options.blowup > 0 ) 
		snprintf( blowup_label, 31, "M X%1d", options.blowup );
	else
		snprintf( blowup_label, 31, "M 1/%1d", -options.blowup );

        ui.in_set_label( Label::Blowup, blowup_label );

	if( view_var_is_valid ) {
		view->variable->user_set_blowup = options.blowup;
		}

	/* Phase 13b: the blowup itself (label included) has already been
	 * applied above and is a global option, so it is kept; only the part
	 * that resizes this View's pixel buffer from size[x/y_axis_id] is
	 * skipped, those reads being out of bounds with an unresolved axis.
	 * Silent: this is reachable from a scroll-wheel zoom, which fires
	 * continuously. */
	if( ! view->has2dAxes() )
		return;

	x_size       = view->variable->size[view->x_axis_id];
	y_size       = view->variable->size[view->y_axis_id];
	view_get_scaled_size( options.blowup, x_size, y_size, &scaled_x_size, &scaled_y_size );

	view->pixels.resize( scaled_x_size*scaled_y_size );

	if( options.save_frames == true ) {
		if( options.debug )
			fprintf( stderr, "calling init_saveframes from view_change_blowup\n" );
		view->initSaveframes();
		}

	if( redraw_flag ) {
		changed_size = ui.in_set_2d_size( scaled_x_size, scaled_y_size );
		/* Have to test and see if we shrunk; no expose event
		 * is generated in such a case, which would automatically
		 * trigger this call without us having to do it.
		 */
		if( changed_size < 0 )
			g_app.controller.draw( false, false );
		}
	ui.in_set_cursor_normal();
}

/************************************************************************
 * This routine handles the case where the user presses the button 
 * which has the current dimension value, indicating that they want
 * it to change.
 */
/**********************************************************************
 * Shared tail of view_change_cur_dim() (relative step) and
 * view_set_cur_dim_index() (absolute jump, e.g. from a UI slider): given
 * a dimid/dim already resolved and a new place already computed and
 * clamped, push it into view->var_place, refresh whatever UI reflects it
 * (the scan axis's title label if this dim IS the scan axis, or just this
 * dim's own row otherwise), and redraw.
 */
	void
View::applyCurDimPlace( int dimid, NCDim *dim, size_t place )
{
	View *view = this;
	int	has_bounds;
	nc_type	type;
	double	new_dimval, bound_min, bound_max;
	char	temp_string[1024];

	view->var_place[dimid] = place;

	if( dimid == view->scan_axis_id ) {
		/* This dim's own row is stepping the scan axis itself (e.g.
		 * "time" shown both as its own dimension row and as the frame
		 * the animation buttons step through) -- go through
		 * View::scanToPlace() so the "frame N/M <date>" title label
		 * (Label::ScanPlace) stays in sync, not just this row's own
		 * display. change_view() (the play/rewind/forward path)
		 * already does this; the other paths here used to skip it,
		 * so stepping the scan dimension any other way silently went
		 * stale.
		 */
		view->scanToPlace( place );
		}
	else {
		type  = g_app.session.dataset().dimValue( view->variable, dimid, place, &new_dimval, temp_string,
			&has_bounds, &bound_min, &bound_max, view->var_place.data() );
		if( type == NC_DOUBLE ) {
			if( dim->timelike && options.t_conv ) {
				fmt_time( temp_string, 1024, new_dimval, dim, 1 );
				}
			else
				snprintf( temp_string, 1023, "%lg", new_dimval );
			}
		g_app.ui->in_set_cur_dim_value( dim->name.c_str(), temp_string );
		}

	if( options.debug )
		fprintf( stderr, "calling init_saveframes from view_apply_cur_dim_place\n" );

	view->data_status = ViewDataStatus::Invalid;
	view->initSaveframes();

	g_app.controller.draw( true, false ); /* 'true' because we initialized saveframes above */
}

/**********************************************************************
 * This is ultimately what changes what the X and Y dims are.  It is
 * called when the interface button requesting that a change to the
 * scan dimension is pressed, and itself calls the routine which asks
 * the user to make a new selection.
 */
	void
View::setScanDims()
{
	View *view = this;
	ViewerUi	&ui = *g_app.ui;
	Stringlist *dim_list, *new_dim_list = NULL, *inv_dim_list;
	int	   changed_something = false;
	NCVar	   *v;
	char	   *cur_x_name, *cur_y_name;
	char	   scan_dim[256];
	int        new_x_id, new_y_id;
	int        scan_dims_result;
	Message    message;

	/* Phase 13b: the two dim[] reads just below are out of bounds if
	 * either display axis is unresolved -- reached by pressing "Axes"
	 * while a 1-D variable is selected, which this port left possible
	 * because the 2-D pane and its toolbar stayed live. Reported rather
	 * than silent: the user pressed a button and gets nothing back
	 * otherwise. */
	if( ! view->has2dAxes() ) {
		in_error( "This variable has no 2-D display axes to set." );
		return;
		}

	v          = view->variable;
	cur_x_name = const_cast<char *>(v->dim[view->x_axis_id]->name.c_str());
	cur_y_name = const_cast<char *>(v->dim[view->y_axis_id]->name.c_str());
	NetCDFFile *file0 = v->files.front()->file;

	dim_list = file0->scannableDims( const_cast<char *>(v->name.c_str()) );
	snprintf( scan_dim, sizeof(scan_dim), "%s", (*dim_list)[0].string.c_str() );

	/* Pop up the dialog box which asks for the user's selection */
	scan_dims_result = ui.in_set_scan_dims( dim_list, cur_x_name,
				cur_y_name, &new_dim_list );
	/* Phase 13a: this used to compare against Message::Cancel, which is 2,
	 * while in_set_scan_dims() returns a plain 0/1 -- so the check never
	 * fired and the (*new_dim_list)[0] below dereferenced the still-NULL
	 * new_dim_list on every cancel. That was a real SIGSEGV: press "Axes",
	 * press "Cancel", core dump. MainWindow::scanDimsDialog()
	 * (ui/src/main_window_dialogs.cc) returns 0 without ever writing
	 * *new_dim_list on both of its early-out paths (the user cancelled,
	 * and an empty dim_list), so test both the status and the list --
	 * neither alone covers the other.
	 *
	 * The dead comparison was noticed during Phase 1 and written off as a
	 * harmless upstream quirk, on the assumption that a real UI always
	 * populates the list on any path reaching here. This port's own FLTK
	 * dialog doesn't, which is what made it a live crash rather than a
	 * curiosity. */
	if( (scan_dims_result != 1) || (new_dim_list == NULL) )
		return;

	/* A special check: we don't allow transposition of the data
	 * because of the huge performance penalty it would be to
	 * auto-transform it back to the desired configuration.  To
	 * take care of this, if the axes are transposed, then warn
	 * the user, and flip them.
	 * new_dim_list is Y-axis first, then X-axis (see in_set_scan_dims's
	 * own contract).
	 */
	new_y_id = file0->dimNameToId( const_cast<char *>(v->name.c_str()), (char *)(*new_dim_list)[0].string.c_str() );
	new_x_id = file0->dimNameToId( const_cast<char *>(v->name.c_str()), (char *)(*new_dim_list)[1].string.c_str() );
	if( new_x_id < new_y_id ) {
		message = ui.in_dialog( "Transposing the data is not allowed.\nI'm switching the axes....", true );
		if( message == Message::Cancel )
			return;
		inv_dim_list = NULL;
		stringlist_add_string( &inv_dim_list, (*new_dim_list)[1].string.c_str() );
		stringlist_add_string( &inv_dim_list, (*new_dim_list)[0].string.c_str() );
		new_dim_list = inv_dim_list;
		}
	if( new_x_id == new_y_id ) {
		in_error( "Please pick dimensions for the X and\nY axis which are not the same." );
		return;
		}

	ui.in_set_cursor_busy();

	if( strcmp( cur_y_name, (*new_dim_list)[0].string.c_str() ) != 0 ) {
		view->setAxis( Dimension::Y, (char *)(*new_dim_list)[0].string.c_str() );
		changed_something = true;
		}

	if( strcmp( cur_x_name, (*new_dim_list)[1].string.c_str() ) != 0 ) {
		view->setAxis( Dimension::X, (char *)(*new_dim_list)[1].string.c_str() );
		changed_something = true;
		}

	if( changed_something == true ) {
		/* In general, we want to set the scan axis back to the
		 * unlimited axis if possible, because no unlimited
		 * dimension ever comes up in the pop-up box to be able
		 * to set it that way.  Use the previously saved value.
		 */
		view->setAxis( Dimension::Scan, scan_dim );
		view->flipIfInverted();
		view->redrawDimensionInfo();
		view->data_status = ViewDataStatus::Invalid;
		view->allocStorage();
		view->initSaveframes();
		view->setScanButtons();
		g_app.controller.draw( true, false ); /* 'true' because we initialized saveframes above */
		}

	ui.in_set_cursor_normal();
}

/**************************************************************************************/
	void
View::setAxis( Dimension dimension, char *new_dim_name )
{
	View *local_view = this;
	int	new_id, old_id;
	NCVar	*v;

	v      = local_view->variable;
	new_id = -1;
	old_id = -1;
	NetCDFFile *file0 = v->files.front()->file;

	switch( dimension ) {
		case Dimension::X:
			new_id = file0->dimNameToId(
						const_cast<char *>(v->name.c_str()), new_dim_name );
			if( options.debug )
				fprintf( stderr, "setting dim X to %s\n",
						new_dim_name );
			old_id = local_view->x_axis_id;
			local_view->x_axis_id = new_id;
			/* Phase 12e: new_id can legitimately be -1 (dimNameToId()'s
			 * own doc comment: "-1 if the dimension is not found in the
			 * requested variable"). var_place is std::vector<size_t>, so
			 * var_place[-1] is var_place[SIZE_MAX] -- an out-of-bounds
			 * heap write, confirmed under ASan before this guard existed.
			 * The axis id above is still recorded as -1 (the established
			 * "no such axis" sentinel used throughout this file), just
			 * without touching var_place for an id that doesn't exist. */
			if( new_id != -1 )
				local_view->var_place[new_id] = 0L;
			else
				in_error( "The requested X axis dimension was not found for this variable." );
			break;

		case Dimension::Y:
			new_id = file0->dimNameToId(
						const_cast<char *>(v->name.c_str()), new_dim_name );
			if( options.debug )
				fprintf( stderr, "setting dim Y to %s\n",
						new_dim_name );
			old_id = local_view->y_axis_id;
			local_view->y_axis_id = new_id;
			/* Phase 12e: same guard as Dimension::X above. */
			if( new_id != -1 )
				local_view->var_place[new_id] = 0L;
			else
				in_error( "The requested Y axis dimension was not found for this variable." );
			break;

		case Dimension::Scan:
			if( strlen( new_dim_name ) == 0 ) {
				set_buttons( BUTTONS_TIMEAXIS_OFF, *g_app.ui );
				local_view->scan_axis_id = -1;
				return;
				}
			set_buttons( BUTTONS_ALL_ON, *g_app.ui );
			new_id = file0->dimNameToId(
						const_cast<char *>(v->name.c_str()), new_dim_name );
			old_id = local_view->scan_axis_id;
			local_view->scan_axis_id = new_id;
			if( options.debug ) 
				fprintf( stderr, "setting dim SCAN to %s\n", 
						new_dim_name );
			break;

		case Dimension::None:
			new_id = file0->dimNameToId(
						const_cast<char *>(v->name.c_str()), new_dim_name );
			if( options.debug ) 
				fprintf( stderr, "setting dim NONE to %s\n", 
						new_dim_name );
			return;
		}
				
	if( old_id == new_id )
		return;

	g_app.ui->in_indicate_active_dim( dimension, new_dim_name );
}

/**************************************************************************************/
	void
View::allocStorage()
{
	View *view = this;
	size_t	x_size, y_size, scaled_x_size, scaled_y_size;

	/* Allocate storage space for the data in the view structure. Note:
	 * the old malloc()-failure diagnostics (dumping var/axis names before
	 * exit(-1)) are gone -- std::vector::resize() throws std::bad_alloc
	 * instead of returning NULL, and this codebase already treats OOM as
	 * fatal everywhere else via unconditional exit(-1), so an unhandled
	 * bad_alloc terminating the program is the same outcome (a hard
	 * stop), just via a different, standard mechanism. See
	 * modernization.md's Phase 6 notes for the record of this decision.
	 */

	/* Phase 13b: has2dAxes(), not has2dImage() -- this function is what
	 * makes has2dImage() true, so it can only require the axes. With an
	 * unresolved axis the size[] reads below are out of bounds and the
	 * resize() that follows would size the buffers from whatever heap
	 * bytes happened to precede the vector. */
	if( ! view->has2dAxes() )
		return;

	if( view->data_status == ViewDataStatus::Edited )
		view_data_edit_warn( g_app.session, *g_app.ui );
	view->data_status = ViewDataStatus::Invalid;

	x_size       = view->variable->size[view->x_axis_id];
	y_size       = view->variable->size[view->y_axis_id];
	view_get_scaled_size( options.blowup, x_size, y_size, &scaled_x_size, &scaled_y_size );

	view->data.resize( x_size*y_size );
	view->pixels.resize( scaled_x_size*scaled_y_size );
}

/********************************************************************
 * The user has requested that we change the displayed range;
 * pop up appropriate dialog windows, get the new min and max
 * values from the user, and set them.
 */
	void
View::setRange()
{
	View *view = this;
	float	new_min, new_max;
	int	allvars;
	Message	message;

	message = g_app.ui->x_range( view->variable->user_min, view->variable->user_max,
		view->variable->global_min, view->variable->global_max,
		&new_min, &new_max, &allvars );
	if( message == Message::Cancel )
		return;

	view->variable->user_min = new_min;
	view->variable->user_max = new_max;
	view->setRangeLabels( new_min, new_max );
	view->data_status = ViewDataStatus::Invalid;
	g_app.session.invalidateAllSaveframes();
	g_app.controller.draw( true, false ); /* 'true' because we just invalidated all saveframes */

	if( allvars == true ) {
		for( auto &cursor : g_app.session.dataset().variablesMutable() ) {
			cursor->user_min = new_min;
			cursor->user_max = new_max;
			cursor->have_set_range = true;
			}
		}

	g_app.controller.recomputeColorbar();
}

/**************************************************************************************/
	void
View::setRangeLabels( float min, float max )
{
	View *view = this;
	std::string units, var_long_name;
	char	temp_label[4096], extra_label[4096];
	NetCDFFile *file0 = view->variable->files.front()->file;

	units = file0->varUnits( view->variable->name );
	var_long_name = file0->longVarName( view->variable->name );
	if( units.empty() ) {
		snprintf( temp_label, 4095, "displayed range: %g to %g (%g to %g shown)",
			view->variable->global_min,
			view->variable->global_max,
			view->variable->user_min,
			view->variable->user_max );
		if( !var_long_name.empty() )
			snprintf( extra_label, 4095, "%s (%g to %g)",
					limit_string(var_long_name).c_str(),
					view->variable->user_min,
					view->variable->user_max );
		else
			snprintf( extra_label, 4095, "%s (%g to %g)",
					limit_string(view->variable->name).c_str(),
					view->variable->user_min,
					view->variable->user_max );
		}
	else
		{
		snprintf( temp_label, 4095, "displayed range: %g to %g %s (%g to %g shown)",
			view->variable->global_min,
			view->variable->global_max,
			limit_string(units).c_str(),
			view->variable->user_min,
			view->variable->user_max );
		if( !var_long_name.empty() )
			snprintf( extra_label, 4095, "%s (%g to %g %s)",
					limit_string(var_long_name).c_str(),
					view->variable->user_min,
					view->variable->user_max,
					limit_string(units).c_str() );
		else
			snprintf( extra_label, 4095, "%s (%g to %g %s)",
					limit_string(view->variable->name).c_str(),
					view->variable->user_min,
					view->variable->user_max,
					limit_string(units).c_str() );
		}

	g_app.ui->in_set_label( Label::DataExtrema, temp_label );

	if( options.want_extra_info ) {
		g_app.ui->in_set_label( Label::CcInfo1, extra_label );
		}

}

/*************************************************************************
 * Reset the current data range based on the currently showing 
 * frame ONLY, rather than the global min and maxes.
 */
	void
View::setRangeFrame()
{
	g_app.controller.draw( true, true );
}

/**************************************************************************************/
	void
View::initSaveframes()
{
	View *view = this;
	size_t	storage_size, n_scan_entries, xsize, ysize, n_extra_frames, nt, nx, ny;
	char	err_message[132];

	if( options.save_frames == false )
		return;

	/* Phase 13b: xsize/ysize below index size[] by the display axis ids;
	 * with either unresolved the frame-store capacity is computed from
	 * out-of-bounds heap bytes. There is no 2-D field to cache frames of
	 * in that state anyway. */
	if( ! view->has2dAxes() )
		return;

	if( view->scan_axis_id == -1 ) {
		n_scan_entries = 1;
		n_extra_frames = 0;
		}
	else
		{
		n_scan_entries = view->variable->size[view->scan_axis_id];
		n_extra_frames = floor( n_scan_entries * 0.1 ) + 1;
		if( n_extra_frames < 10 )
			n_extra_frames = 10;
		}
	nt = n_scan_entries + n_extra_frames;

	xsize = view->variable->size[view->x_axis_id];
	ysize = view->variable->size[view->y_axis_id];
	view_get_scaled_size( options.blowup, xsize, ysize, &nx, &ny );

	storage_size = nx * ny * nt;

	if( options.debug ) {
		fprintf( stderr, "initializing saveframes:\n" );
		fprintf( stderr, "	n_scan_entries: %zu\n", n_scan_entries );
		fprintf( stderr, "	n_extra_frames: %zu\n", n_extra_frames );
		fprintf( stderr, "	frame size: %zu\n",
				view->variable->size[view->x_axis_id] *
				view->variable->size[view->y_axis_id] );
		fprintf( stderr, "	total storage size:%zu\n", storage_size );
		}

	/* Unlike View::allocStorage()'s hard exit(-1) on allocation failure,
	 * this path was always meant to degrade gracefully (in-core frame
	 * caching is an optional speed optimization, not required for
	 * correctness) -- preserved here by catching std::bad_alloc from
	 * FrameCache::reset() rather than letting it propagate. */
	try {
		g_app.session.frameCache().reset( nt, nx, ny );
		}
	catch( const std::bad_alloc & ) {
		snprintf( err_message, 131, "Can't allocate space for frame store.\nRequested size: %.1f MB",
				(float)(storage_size*sizeof( ncv_pixel ))/1000000. );
		options.save_frames = false;
		in_error( err_message );
		}
}

/**************************************************************************************/
/* Initialize a new view structure and set defaults. Returns a raw,
 * heap-allocated View*; every caller immediately hands it to the global
 * `view` unique_ptr (or, in set_scan_variable()'s variable-switch branch,
 * carries it briefly as a local raw pointer before doing the same), which
 * is what actually owns and eventually deletes it -- see set_scan_variable()
 * and invalidate_variable() above. */
	View *
View::create( NCVar *var )
{
	View *view = new View();
	view->data_status  = ViewDataStatus::Invalid;
	view->x_axis_id    = -1;
	view->y_axis_id    = -1;
	view->scan_axis_id = -1;
	view->skip         =  1;

	view->variable  = var;
	view->var_place.assign( var->n_dims, 0 );

	view->plot_XY_axis   = -1;
	view->plot_XY_nlines = 0;

	return( view );
}

/**************************************************************************************/
	static void
set_buttons( int to_state, ViewerUi &ui )
{
	switch (to_state ) {

	    case BUTTONS_ALL_ON:
		ui.in_set_sensitive( Button::Restart, 	  true );
		ui.in_set_sensitive( Button::Rewind, 	  true );
		ui.in_set_sensitive( Button::Backwards, 	  true );
		ui.in_set_sensitive( Button::Pause, 	  true );
		ui.in_set_sensitive( Button::Forward, 	  true );
		ui.in_set_sensitive( Button::Fastforward, 	  true );
		ui.in_set_sensitive( Button::ColormapSelect, true );
		ui.in_set_sensitive( Button::InvertPhysical, true );
		ui.in_set_sensitive( Button::InvertColormap, true );
		ui.in_set_sensitive( Button::Blowup, 	  true );
		ui.in_set_sensitive( Button::Transform, 	  true );
		ui.in_set_sensitive( Button::Print, 	  true );
		ui.in_set_sensitive( Button::Dimset, 	  true );
		ui.in_set_sensitive( Button::Range, 	  true );
		ui.in_set_sensitive( Button::BlowupType,	  true );
		ui.in_set_sensitive( Button::Edit,	  	  true );
		ui.in_set_sensitive( Button::Info,	  	  true );
		break;

	    case BUTTONS_TIMEAXIS_OFF:
		ui.in_set_sensitive( Button::Restart, 	  false );
		ui.in_set_sensitive( Button::Rewind, 	  false );
		ui.in_set_sensitive( Button::Backwards, 	  false );
		ui.in_set_sensitive( Button::Forward, 	  false );
		ui.in_set_sensitive( Button::Fastforward, 	  false );
		break;
		
	    case BUTTONS_2D_OFF:
		/* The tape-recorder row: a View with no 2-D picture also has no
		 * scan axis to step along (initialDetermineScanAxes()'s case 1
		 * sets both to -1 together), so these would be off anyway. */
		ui.in_set_sensitive( Button::Restart, 	  false );
		ui.in_set_sensitive( Button::Rewind, 	  false );
		ui.in_set_sensitive( Button::Backwards, 	  false );
		ui.in_set_sensitive( Button::Pause, 	  false );
		ui.in_set_sensitive( Button::Forward, 	  false );
		ui.in_set_sensitive( Button::Fastforward, 	  false );
		/* Act on the 2-D picture: each of these was a live crash before
		 * Phase 13b's guards, and is meaningless here even with them. */
		ui.in_set_sensitive( Button::InvertPhysical, false );
		ui.in_set_sensitive( Button::Blowup, 	  false );
		ui.in_set_sensitive( Button::BlowupType,	  false );
		ui.in_set_sensitive( Button::Transform, 	  false );
		ui.in_set_sensitive( Button::Print, 	  false );
		ui.in_set_sensitive( Button::Dimset, 	  false );
		ui.in_set_sensitive( Button::Edit,	  	  false );
		/* Deliberately left ON: ColormapSelect, InvertColormap, Range and
		 * Info all work on the variable rather than on the picture, and
		 * the Phase 13b sweep confirmed each is safe in this state. */
		ui.in_set_sensitive( Button::ColormapSelect, true );
		ui.in_set_sensitive( Button::InvertColormap, true );
		ui.in_set_sensitive( Button::Range, 	  true );
		ui.in_set_sensitive( Button::Info,	  	  true );
		break;

	    case BUTTONS_ALL_OFF:
		ui.in_set_sensitive( Button::Restart, 	  false );
		ui.in_set_sensitive( Button::Rewind, 	  false );
		ui.in_set_sensitive( Button::Backwards, 	  false );
		ui.in_set_sensitive( Button::Pause, 	  false );
		ui.in_set_sensitive( Button::Forward, 	  false );
		ui.in_set_sensitive( Button::Fastforward, 	  false );
		ui.in_set_sensitive( Button::ColormapSelect, false );
		ui.in_set_sensitive( Button::InvertPhysical, false );
		ui.in_set_sensitive( Button::InvertColormap, false );
		ui.in_set_sensitive( Button::Transform, 	  false );
		ui.in_set_sensitive( Button::Blowup, 	  false );
		ui.in_set_sensitive( Button::Print, 	  false );
		ui.in_set_sensitive( Button::Dimset, 	  false );
		ui.in_set_sensitive( Button::Range, 	  false );
		ui.in_set_sensitive( Button::BlowupType,	  false );
		ui.in_set_sensitive( Button::Edit,	  	  false );
		ui.in_set_sensitive( Button::Info,	  	  false );
		break;

	default:
		fprintf( stderr, "ncview: set_buttons: unknown to_state: %d\n",
			to_state );
		break;
	}
}

/**************************************************************************************/
	void
View::reDetermineScanAxes( NCVar *new_var, View *old_view )
{
	View *new_view = this;
	NCVar	*old_var;
	int	old_n_scannable_dims, i, dim_index;
	NCDim	*old_dim;

	old_var = old_view->variable;
	old_n_scannable_dims = stringlist_len(
		old_var->files.front()->file->scannableDims( const_cast<char *>(old_var->name.c_str())) );

	for( i=0; i<old_n_scannable_dims; i++ ) {
		old_dim = old_var->dim[i].get();

		/* the dim is set to NULL if it is not scannable */
		if( old_dim != NULL ) {

			/* dim_index is the index in the *new* variable of
		 	 * the *old* dimension
		 	 */
			dim_index = new_var->files.front()->file->dimNameToId(
					const_cast<char *>(new_var->name.c_str()), const_cast<char *>(old_dim->name.c_str()) );
			if( dim_index != -1 ) {
				/* This dimension is in the new variable. 
				 * Set to be the same dimension as it used to
				 * be.
				 */

				if( i == old_view->x_axis_id )
					new_view->x_axis_id = dim_index;

				else if(  i == old_view->y_axis_id )
					new_view->y_axis_id = dim_index;

				else if(  i == old_view->scan_axis_id )
					new_view->scan_axis_id = dim_index;
				}
			}
		}
}
					
/***************************************************************************
 * Set the current place for the variable, i.e., the index into the
 * scan dimensions at which we want to view it.  It is assumed if 
 * old_view is not NULL then old_view is the view used previously to
 * the current one; in that case, try to set the new view to the same
 * place as the old view, if possible.
 */
	void
View::setScanPlace( NCVar *var, View *old_view )
{
	View *new_view = this;

	/* Initially, always set to zero */
	initialSetScanPlace( var );

	/* If there is some additional information based on the
	 * old view, use that.
	 */
	if( old_view != NULL )
		reSetScanPlace( var, old_view );

	/* All place information for the displayed axes MUST be
	 * set to zero!!
	 */
	new_view->var_place[new_view->x_axis_id] = 0L;
	new_view->var_place[new_view->y_axis_id] = 0L;
}

/**************************************************************************************/
	void
View::initialSetScanPlace( NCVar *var )
{
	View *view = this;
	int	i;

	for( i=0; i<var->n_dims; i++ )
		view->var_place[i] = 0L;

}

/**************************************************************************************/
	void
View::reSetScanPlace( NCVar *new_var, View *old_view )
{
	View *new_view = this;
	int	i, dim_index;
	NCDim	*new_dim;
	NCVar	*old_var;
	size_t	old_place;

	old_var = old_view->variable;
	
	for( i=0; i<new_var->n_dims; i++ ) {
		/* dim_index is the dimension ID of the NEW dimension
		 * in the OLD variable.  -1 if the new dimension does
		 * not exist in the old variable.
		 */
		new_dim   = new_var->dim[i].get();
		if( new_dim != NULL ) {
			dim_index = old_var->files.front()->file->dimNameToId(
					const_cast<char *>(old_var->name.c_str()), const_cast<char *>(new_dim->name.c_str()) );
			if( dim_index != -1 ) {
				old_place = old_view->var_place[dim_index];
				if( old_place < new_var->size[i] )
					new_view->var_place[i] = old_place;
				}
			}
		}
}

/**************************************************************************************/
/* If 'val_to_set_to' is -99999, then the blowup is calculated automatically,
 * otherwise the blowup is set to val_to_set_to
 */
	void
View::calculateBlowup( NCVar *var, int val_to_set_to )
{
	View *view = this;
	size_t	x_size, y_size;
	float	fbx, fby, f_x_size, f_y_size, f_blowup;
	int	ifbx, view_var_is_valid;

	if( options.small )
		return;

	view_var_is_valid = 0;

	if( val_to_set_to != -99999 ) {
		while( options.blowup > val_to_set_to ) {
			view->changeBlowup( -1, false, view_var_is_valid );			
			}
		while( options.blowup < val_to_set_to ) {
			view->changeBlowup( 1, false, view_var_is_valid );			
			}
		return;
		}

	/* If the picture is too small, start out by blowing it up some */
	x_size = var->size[view->x_axis_id];
	y_size = var->size[view->y_axis_id];
	while( (options.blowup*x_size < static_cast<size_t>(options.blowup_default_size)) &&
	       (options.blowup*y_size < static_cast<size_t>(options.blowup_default_size)) ) {
		view->changeBlowup( 1, false, view_var_is_valid );
		}

	/* If picture is too big, reduce it some */
	f_x_size = (float)x_size;
	f_y_size = (float)y_size;
	f_blowup = (float)options.blowup;
	fbx = f_blowup * f_x_size / (double)options.blowup_default_size;
	fby = f_blowup * f_y_size / (double)options.blowup_default_size;
	fbx = (fbx > fby) ? fbx : fby;
	if( fbx > 3 ) {
		ifbx = -(int)fbx;
		view->changeBlowup(ifbx,false, view_var_is_valid);
		}
}

/**************************************************************************************/
	static void
draw_file_info( NCVar *var, ViewerSession &session, ViewerUi &ui )
{
	std::string title, units, var_long_name;
	char	range_label[256], temp_label[600];
	std::unique_ptr<ViewState> &view = session.activeView();

	title = var->files.front()->file->title();
	if( title.empty() )
		ui.in_set_label( Label::Title, PROGRAM_ID );
	else
		ui.in_set_label( Label::Title, title.c_str() );

	units = var->files.front()->file->varUnits( var->name );
	if( units.empty() ) {
		if( (var->global_min != var->user_min) || (var->global_max != var->user_max))
			snprintf( range_label, 255, "%g to %g (%g to %g shown)",
					var->global_min,
					var->global_max,
					var->user_min,
					var->user_max );
		else
			snprintf( range_label, 255, "%g to %g",
					var->global_min,
					var->global_max );
		}
	else
		{
		if( (var->global_min != var->user_min) || (var->global_max != var->user_max))
			snprintf( range_label, 255, "%g to %g %s (%g to %g shown)",
					var->global_min,
					var->global_max,
					limit_string(units).c_str(),
					var->user_min,
					var->user_max );
		else
			snprintf( range_label, 255, "%g to %g %s",
					var->global_min,
					var->global_max,
					limit_string(units).c_str() );
		}
	snprintf( temp_label, 599, "displayed range: %s", range_label );
	ui.in_set_label( Label::DataExtrema, temp_label );

	var_long_name = view->variable->files.front()->file->longVarName(
					view->variable->name );
	if( var_long_name.empty() ) {
		snprintf( temp_label, 255, "variable=%s", limit_string(view->variable->name).c_str() );
		ui.in_set_label( Label::ScanvarName, temp_label );
		if( options.want_extra_info ) {
			snprintf( temp_label, 599, "%s (%s)", limit_string(view->variable->name).c_str(),
								range_label );
			ui.in_set_label( Label::CcInfo1, temp_label );
			}
		}
	else
		{
		snprintf( temp_label, 599, "displaying %s", limit_string(var_long_name).c_str() );
		ui.in_set_label( Label::ScanvarName, temp_label );
		if( options.want_extra_info ) {
			snprintf( temp_label, 599, "%s (%s)",  limit_string(var_long_name).c_str(), range_label );
			ui.in_set_label( Label::CcInfo1, temp_label );
			}
		}
}

/**************************************************************************************/
	void
View::redrawDimensionInfo()
{
	View *view = this;
	ViewerUi	&ui = *g_app.ui;
	int	i, please_flip;
	NCDim	*d, *y_dim;
	Stringlist *dimlist;
	NCVar	*var;
	char	*cur_y_name;

	/* Phase 13b: var->dim[y_axis_id] below is a std::vector of
	 * unique_ptr<NCDim>; with y_axis_id == -1 that reads a bogus pointer
	 * out of bounds and dereferences it for ->name. There is no Y axis to
	 * label in that state. */
	if( ! view->has2dAxes() )
		return;

	var     = view->variable;
	dimlist = var->files.front()->file->scannableDims( const_cast<char *>(var->name.c_str()) );

	y_dim      = var->dim[view->y_axis_id].get();
	cur_y_name = const_cast<char *>(y_dim->name.c_str());

	ui.x_init_dim_info( dimlist );

	for( i=0; i<var->n_dims; i++ )
		if( (d = var->dim[i].get()) != NULL ) {
			please_flip = ((d->name == cur_y_name) &&
						options.invert_physical);
			ui.in_fill_dim_info( d, please_flip );
			}

	view->showCurrentDimValues();
	view->labelDimensions();
}

/**************************************************************************************/
	void
View::showCurrentDimValues()
{
	View *view = this;
	int	dimid, has_bounds;
	NCVar	*var;
	Stringlist *scannable_dims;
	size_t	place;
	double	new_dimval, bound_min, bound_max;
	char	temp_string[1024], *dim_name;
	nc_type	type;

	var = view->variable;
	scannable_dims   = var->files.front()->file->scannableDims( const_cast<char *>(var->name.c_str()) );

	if( scannable_dims != NULL )
	for( auto &e : *scannable_dims ) {
		dim_name   = (char *)e.string.c_str();
		dimid      = var->files.front()->file->dimNameToId(
					const_cast<char *>(var->name.c_str()),
					dim_name );
		/* Phase 12e: dimid can legitimately be -1 if this dim, though
		 * scannable in general, doesn't resolve for this particular
		 * variable -- var_place[-1] would be var_place[SIZE_MAX], an
		 * out-of-bounds read (std::vector<size_t>). This function runs
		 * on every redraw of the dimension-info labels, so skip the one
		 * dim silently rather than pop an in_error() dialog on every
		 * frame -- matches ViewerSession::curDimIndex()'s existing
		 * "benign no-op on an unresolved dim" precedent (viewer_session.cc). */
		if( dimid < 0 )
			continue;

		place = view->var_place[dimid];

		type  = g_app.session.dataset().dimValue( view->variable, dimid, place, &new_dimval, temp_string,
			&has_bounds, &bound_min, &bound_max, view->var_place.data() );
		if( type == NC_DOUBLE )
			snprintf( temp_string, 1023, "%lg", new_dimval );
		g_app.ui->in_set_cur_dim_value( dim_name, temp_string );
		}
}

/**************************************************************************************/
	void
View::labelDimensions()
{
	View *view = this;
	ViewerUi	&ui = *g_app.ui;
	NCDim	*dim;
	char	*dim_name;

	if( view->x_axis_id != -1 ) {
		dim      = view->variable->dim[view->x_axis_id].get();
		dim_name = const_cast<char *>(dim->name.c_str());
		ui.in_indicate_active_dim( Dimension::X, dim_name );
		ui.in_set_cur_dim_value  ( dim_name, "-X-" );
		}

	if( view->y_axis_id != -1 ) {
		dim      = view->variable->dim[view->y_axis_id].get();
		dim_name = const_cast<char *>(dim->name.c_str());
		ui.in_indicate_active_dim( Dimension::Y, dim_name );
		ui.in_set_cur_dim_value  ( dim_name, "-Y-" );
		}

	if( view->scan_axis_id != -1 ) {
		dim      = view->variable->dim[view->scan_axis_id].get();
		dim_name = const_cast<char *>(dim->name.c_str());
		ui.in_indicate_active_dim( Dimension::Scan, dim_name );
		}
}

/**************************************************************************************/
	void
View::flipIfInverted()
{
	View *view = this;
	NCDim	*y_dim;

	if( options.no_autoflip )
		return;

	if( view->y_axis_id == -1 )
		return;

	y_dim = view->variable->dim[view->y_axis_id].get();
	if( y_dim->min > y_dim->max )
		options.invert_physical = true;
	else
		options.invert_physical = false;

	g_app.ui->x_force_set_invert_state( options.invert_physical );
}

/**************************************************************************************
 * Phase 2 postscript: moved from the file-local static free function
 * view_construct_scalar_coord_str() onto View. Its `view == NULL` check
 * looked like the same session guard the 12 functions above carry, but
 * both its call sites (View::setScanButtons(), View::scanToPlace() above)
 * are View methods, so the global `view` they read is always exactly the
 * `this` whose method is currently running -- never actually null here.
 * The check is preserved verbatim anyway (`View *view = this;` makes it
 * unreachable rather than removing it, matching this codebase's
 * move-don't-split rule -- see also View::setScanDims()'s similarly-dead
 * cancel check).
 */
	void
View::constructScalarCoordStr( char *str, int slen )
{
	View *view = this;
	size_t	ts;
	FDBlist	*fdb;
	int	isc, nsc, fdb_index, space_avail;
	NCDim_map_info *sdmi;
	float	fval, fmin, fmax;
	char	*funits, tstr[1024], v1[100], v2[100];
	int	displaying_along_time_dim, nfiles;

	str[0] = '\0';

	if( (view == NULL) || (view->variable == NULL) || (view->variable->scalar_dim_map_info.empty())
				|| (view->var_place.empty())) {
		return;
		}

	/* Get the time step (value of first dim, which must be the only
	 * dim that is virtually concatenated along different files).
	 */
	ts = view->var_place[0];

	/* The file associated with this time step */
	fdb = view->variable->timestep_2_fdb[ts];

	/* The integer position of this FDB on the var's list of FDBs. 
	 * We need this because the scalar data is cached by FDB, *not*
	 * by the variable's timestep
	 */
	fdb_index = fdb->index;

	/* See if we are displaying ALONG the time dim. If we are, then we 
	 * check to see if the scalar coords change along the time
	 * dim. If they do, we just print <varies>. Otherwise, we print
	 * the value as per usual 
	 */
	displaying_along_time_dim = ((view->x_axis_id == 0) || (view->y_axis_id == 0));

	/* Get all scalar dims associated with this var and this file */
	nsc = (int)view->variable->scalar_dim_map_info.size();
	for( isc=0; isc<nsc; isc++ ) {
		sdmi   = view->variable->scalar_dim_map_info[isc].get();
		fval   = sdmi->data_cache[fdb_index];	/* NOTE! this is NOT [timtestep], it's [fdb_number] */
		snprintf( v1, 95, "%f", fval );
		strip_trailing_zeros( v1 );
		funits = const_cast<char *>(sdmi->coord_var_units.c_str());

		if( displaying_along_time_dim && (! sdmi->scalar_all_same)) {
			nfiles = view->variable->files.back()->index + 1;	/* number of files this var lives in */
			fmin = view_calc_minval_float( sdmi->data_cache.data(), nfiles );
			fmax = view_calc_maxval_float( sdmi->data_cache.data(), nfiles );
			snprintf( v1, 95, "%f", fmin );
			strip_trailing_zeros( v1 );
			snprintf( v2, 95, "%f", fmax );
			strip_trailing_zeros( v2 );
			snprintf( tstr, 1020, "%s=%s -> %s %s", sdmi->coord_var_name.c_str(), v1, v2, funits );
			}
		else if( sdmi->timelike ) {
			/* A CF scalar coordinate whose own units parse as a UDUNITS
			 * time (e.g. WRF's "XTIME", "minutes since ..."). Format it
			 * as a calendar date the same way a real time dimension's
			 * current value is (View::scanToPlace(), above) instead of
			 * showing the raw "<value> <units>" string -- built via a
			 * throwaway NCDim carrying just what fmt_time()/udu_fmt_time()
			 * actually read (name/units/calendar/timelike/time_std); no
			 * real NCDim exists for a scalar coordinate variable.
			 */
			NCDim time_dim;
			time_dim.name = sdmi->coord_var_name;
			time_dim.units = sdmi->coord_var_units;
			time_dim.calendar = sdmi->calendar;
			time_dim.timelike = 1;
			time_dim.time_std = TimeStandard::Udunits;
			char date_str[128];
			fmt_time( date_str, sizeof(date_str), (double)fval, &time_dim, 0 );
			snprintf( tstr, 1020, "%s=%s", sdmi->coord_var_name.c_str(), date_str );
			}
		else
			snprintf( tstr, 1020, "%s=%s %s", sdmi->coord_var_name.c_str(), v1, funits );

		if( isc != (nsc-1)) 
			strncat( tstr, "; ", sizeof(tstr) - strlen(tstr) - 1 );

		/* Only add on this new string if there is room for it */
		space_avail = (slen-2) - strlen(str);
		if( space_avail <= 5 ) return;
		if( strlen(tstr) < static_cast<size_t>(space_avail) )
			snprintf( str + strlen(str), (size_t)slen - strlen(str), "%s", tstr );
		else
			{
			tstr[ space_avail ] = '\0';
			snprintf( str + strlen(str), (size_t)slen - strlen(str), "%s", tstr );
			return;
			}
		}
}

/**************************************************************************************/
/* This reports the mouse location in the popup XY graph (the SciPlot widget) */
	void
view_report_position_vals( float xval, float yval, int plot_index )
{
	char	current_value_label[200], temp[80];
	NCDim	*dim;

	dim = plot_XY_dim[plot_index];

	/* If the X dimension is timelike, consider formatting that value */
	if( (dim != NULL) && dim->timelike && options.t_conv ) {
		fmt_time( temp, 79, xval, dim, 1 );
		snprintf( current_value_label, 199, "Current: x=%s, y=%g",
		                temp, yval );
		}
	else
		snprintf( current_value_label, 79, "Current: x=%g, y=%g", 
				xval, yval );
	in_set_label( Label::DataValue, current_value_label );
}

/**************************************************************************************/
	void
View::setDataeditPlace()
{
	View *view = this;
	size_t	data_x, data_y, orig_data_y, x_size, y_size;
	int	x, y;
	size_t	index;

	/* Phase 13b: reached from a middle-button press/drag on the 2-D pane
	 * (ui/src/main_window.cc), which this port left clickable even while
	 * a 1-D variable is selected. Indexes size[] by both display axes and
	 * then reads view->data. Silent: mouse drags fire continuously. */
	if( ! view->has2dImage() )
		return;

	if( view->data_status == ViewDataStatus::Invalid ) {
		view->fillViewData();
		view->data_status = ViewDataStatus::Valid;
		}

	g_app.ui->in_query_pointer_position( &x, &y );
	if( (x < 0) || (y < 0) )
		return;

	mouse_xy_to_data_xy( x, y, options.blowup, &data_x, &data_y );

	x_size = view->variable->size[view->x_axis_id];
	y_size = view->variable->size[view->y_axis_id];

	/* Make sure we don't go outside the limits */
	data_x = ( (data_x >= x_size ) ? x_size-1 : data_x );
	data_y = ( (data_y >= y_size ) ? y_size-1 : data_y );

	orig_data_y = data_y;

	/* Invert Y because the reporting counts from the
	 * UPPER LEFT, not the lower left like we want
	 * it to.  If the *picture* is inverted, don't flip
	 * y!
	 */
	if( !options.invert_physical )
		data_y = y_size - data_y - 1;
	
	index =  data_x + orig_data_y*x_size;
	g_app.ui->in_set_edit_place( index, data_x, orig_data_y, x_size, y_size );
}

/**************************************************************************************/
	void
View::dataEdit()
{
	View *view = this;
	size_t	i, j;
	int	j2;
	size_t	x_size, y_size;
	size_t	index, n_entries;
	float	val;
	char	buf[32];

	/* Phase 13b: the "Edit" button stays enabled while a 1-D variable is
	 * selected, and the size[] reads below are then out of bounds --
	 * n_entries comes out of whatever heap bytes precede the vector, and
	 * the loop reads view->data (empty on that path) that many times.
	 * There is no 2-D grid of cells to edit. */
	if( ! view->has2dImage() ) {
		in_error( "There is no 2-D data field to edit for this variable." );
		return;
		}

	x_size = view->variable->size[view->x_axis_id];
	y_size = view->variable->size[view->y_axis_id];

	n_entries  = x_size * y_size;
	/* Phase 12b: was a manual double-malloc'd char** (one alloc for the
	 * pointer array, one per cell) that FltkViewerUi::x_dataedit never
	 * freed -- a real leak on every data-edit dialog open in the actual
	 * application. A std::vector<std::string> owns its own storage, so
	 * there's no allocation to leak and no separate NULL-terminator slot
	 * to size correctly (the vector's own size() is the count). */
	std::vector<std::string> cells( n_entries );

	index = 0L;
	for( j=0; j<y_size; j++)
	for( i=0; i<x_size; i++) {
		if( options.invert_physical )
			j2 = j;
		else
			j2 = y_size - j - 1;
		val = view->data[i + j2*x_size];
		snprintf( buf, sizeof(buf), "%-10.5g", val );
		cells[index] = buf;
		index++;
		}

	g_app.ui->x_dataedit( cells, x_size );
}

/**************************************************************************************/
	void
View::changeDat( size_t index, float new_val )
{
	View *view = this;
	size_t	x_size, y_size, scaled_x_size, scaled_y_size, x, y;

	/* Phase 13b: same precondition as View::dataEdit(), which is the only
	 * thing that can put a cell on screen for this to be called back
	 * with. Silent: the grid should not exist at all in that state, so
	 * there is nobody to report to. */
	if( ! view->has2dImage() )
		return;

	view->data_status = ViewDataStatus::Edited;

	x_size = view->variable->size[view->x_axis_id];
	y_size = view->variable->size[view->y_axis_id];
	view_get_scaled_size( options.blowup, x_size, y_size, &scaled_x_size, &scaled_y_size );

	y = index / x_size;
	x = index - (y*x_size);
	if( !options.invert_physical )
		y = y_size - y - 1;

	printf( "changed (%3zu,%3zu) from %9f to %9f\n", x, y,
		view->data[x + (x_size)*y], new_val );

	view->data[x + (x_size)*y] = new_val;
	view->initSaveframes();
	{
	LockoutViewChangesGuard lockout_guard;
	if( view->dataToPixels() < 0 ) {
		g_app.ui->in_timer_clear();
		if( view->variable->global_min == view->variable->global_max )
			invalidate_variable( view->variable, g_app.session, *g_app.ui );
		return;
		}
	}
	g_app.ui->in_set_2d_size  ( scaled_x_size, scaled_y_size );
	g_app.ui->in_draw_2d_field( view->pixels.data(), scaled_x_size, scaled_y_size, 0 );
}

/**************************************************************************************/
	void
View::dataEditDump()
{
	View *view = this;
	char	filename[1024], *dim_name, *var_name;
	int	ncid, dims[2];
	Message	message;
	size_t	x_size, y_size, start[2], count[2];
	int	x_dimid, y_dimid, varid, err;

	/* Phase 13b: indexes size[] and dim[] by both display axes, and writes
	 * x_size*y_size floats out of view->data. Reached from
	 * view_data_edit_warn(), i.e. from allocStorage() on the *next*
	 * variable switch, so the View here is not necessarily the one the
	 * edits were made on. */
	if( ! view->has2dImage() ) {
		in_error( "There is no 2-D data field to save for this variable." );
		return;
		}

	if( view->data_status != ViewDataStatus::Edited ) {
		fprintf( stderr, "Warning!  Data is NOT CHANGED!\n" );
		}

	message = g_app.ui->in_choose_save_file( "Dump data to netCDF file", "dump.data", filename, sizeof(filename) );
	if( message == Message::OK ) {
		ncid = nccreate( filename, NC_CLOBBER );

		x_size = view->variable->size[view->x_axis_id];
		y_size = view->variable->size[view->y_axis_id];

		dim_name = const_cast<char *>(view->variable->dim[view->x_axis_id]->name.c_str());
		x_dimid = ncdimdef( ncid, dim_name, x_size );
		dim_name = const_cast<char *>(view->variable->dim[view->y_axis_id]->name.c_str());
		y_dimid = ncdimdef( ncid, dim_name, y_size );

		var_name = const_cast<char *>(view->variable->name.c_str());
		dims[0] = y_dimid;
		dims[1] = x_dimid;
		varid = ncvardef( ncid, var_name, NC_FLOAT, 2, dims );

		ncattput( ncid, varid, "missing_value", NC_FLOAT, 1, 
					&view->variable->fill_value );
		ncendef ( ncid );

		start[0] = 0L;
		start[1] = 0L;
		count[0] = y_size;
		count[1] = x_size;
		err = nc_put_vara_float( ncid, varid, start, count, view->data.data() );
		if( err != NC_NOERR ) {
			fprintf( stderr, "Error writing data to new netcdf file!!\n" );
			fprintf( stderr, "%s\n", nc_strerror(err) );
			}
		ncclose( ncid );
		}
}

/**************************************************************************************/
	void
view_data_edit_warn( ViewerSession &session, ViewerUi &ui )
{
	Message	message;
	std::unique_ptr<ViewState> &view = session.activeView();

	/* Phase 13b: the only caller (View::allocStorage()) runs with a View
	 * in hand, so this is defence in depth rather than a reproduced
	 * crash -- but it is the one path here that dereferences `view`
	 * without asking, and a dialog sits between the check and the use. */
	if( view == NULL )
		return;

	message = ui.in_dialog( "Warning!  Data edits will be lost unless you save them now.\nSave them now?", true );
	if( message == Message::Cancel )
		return;

	view->dataEditDump();
}

/**************************************************************************************
 * Set the axis along which to plot to the passed name.
 */
	void
View::setXYPlotAxis( char *label )
{
	View *view = this;
	int		dim_to_plot, i, j;
	char		message[1024];

	if( options.debug )
		fprintf( stderr, "entering view_set_XY_plot_axis with dim=%s\n", label );

	dim_to_plot = -1;
	for( i=0; i<view->variable->n_dims; i++ )
		if( (view->variable->dim[i] != nullptr) &&
		    (view->variable->dim[i]->name == label) )
			dim_to_plot = i;

	if( dim_to_plot == -1 ) {
		snprintf( message, 1023, "Error: I can't find dimension %s in variable %s!\n",
			label, view->variable->name.c_str() );
		in_error( message );
		return;
		}

	if( options.debug )
		fprintf( stderr, "view_set_XY_plot_axis: found dim to plot: %d\n", dim_to_plot );

	view->plot_XY_axis = dim_to_plot;

	std::vector<size_t> start( view->variable->n_dims );
	std::vector<size_t> count( view->variable->n_dims );

	g_app.ui->unlock_plot();

	for( i=0; i<view->plot_XY_nlines; i++ ) {
		/* Change start and count to reflect new axis selection */
		for( j=0; j<view->variable->n_dims; j++ ) {
			start[j] = view->plot_XY_position[i][j];
			count[j] = 1L;
			}
		start[view->plot_XY_axis] = 0L;
		count[view->plot_XY_axis] = view->variable->size[view->plot_XY_axis];
		if( options.debug )
			fprintf( stderr, "view_set_XY_plot_axis: about to call plot_XY_sc\n" );
		view->plotXYSc( start.data(), count.data() );
		}
}

/**************************************************************************************
 * Plot all the data along the specified start and count
 */
	void
View::plotXYSc( size_t *start, size_t *count )
{
	View *view = this;
	ViewerUi	&ui = *g_app.ui;
	size_t	i_size;
	int	n_misplace=30, n_missing_eliminated;
	long	i, j, k, n, misplace_index[30];
	float	t_xval, t_yval, tol, *tmp_yvals;
	double	y_min, y_max, temp_double, bound_min, bound_max;
	char	x_axis_title[132], y_axis_title[132], temp2_string[128], legend[512];
	char	title[512], temp_string[1024], *dim_name;
	std::string units, long_name, file_title;
	char	message[512];
	int	has_bounds, type, all_same, have_done_one, dim_to_plot, plot_index;
	Stringlist *dimlist;
	size_t	virt_cursor_place[MAX_NC_DIMS];

	if( options.debug ) {
		fprintf( stderr, "plot_XY_sc: entering\n" );
		for( i=0; i<view->variable->n_dims; i++ )
			fprintf( stderr, "i=%ld start=%zu count=%zu\n",
				i, *(start+i), *(count+i) );
		}

	if( options.show_sel ) {
		printf( "-var %s -start \\(", view->variable->name.c_str() );
		for( i=view->variable->n_dims-1; i >= 0; i-- ) {
			printf( "%1zu", 1 + (*(start+i)) );
			if( i != 0 )
				printf( "," );
			}
		printf( "\\) -count \\(" );
		for( i=view->variable->n_dims-1; i >= 0; i-- ) {
			printf( "%1zu", *(count+i) );
			if( i != 0 )
				printf( "," );
			}
		printf( "\\) %s\n", view->variable->files.front()->filename.c_str() );
		}

	/* The axis we want to plot must be the one with more
	 * than one count.
	 */
	dim_to_plot = -1;
	for( i=0; i<view->variable->n_dims; i++ )
		if( *(count+i) > 1 ) {
			if( dim_to_plot != -1 ) {
				in_error( "Error!  I found more than one dimension to plot!\n" );
				return;
				}
			dim_to_plot = i;
			}
	if( dim_to_plot == -1 ) {
		in_error( "Error!  I found no dimension to plot!\n" );
		return;
		}
	dim_name = const_cast<char *>(view->variable->dim[dim_to_plot]->name.c_str());
		
        n = view->variable->size[dim_to_plot];

	if( options.debug )
		fprintf( stderr, "about to malloc %ld floats (x vals)\n", n );
	plot_XY_xvals.resize( n );

	if( options.debug )
		fprintf( stderr, "about to malloc %ld floats (y vals)\n", n );
	plot_XY_yvals.resize( n );

	std::vector<float> tmp_yvals_buf( n );
	tmp_yvals = tmp_yvals_buf.data();

	ui.in_set_cursor_busy();

	/* Get the X values for the plot. */
	for(i_size=0L; i_size<static_cast<size_t>(n); i_size++) {

		for( i=0; i<view->variable->n_dims; i++ )
			virt_cursor_place[i] = view->var_place[i];
		virt_cursor_place[dim_to_plot] = i_size;

		type = g_app.session.dataset().dimValue( view->variable, dim_to_plot, i_size, &temp_double, 
				temp_string, &has_bounds, &bound_min, &bound_max, virt_cursor_place );
		if( type == NC_DOUBLE )
			plot_XY_xvals[i_size] = temp_double;
		else
			plot_XY_xvals[i_size] = (double)i_size;
		}
	/* If there is a range of the axis of 0, commonly because
	 * the dimvar has only fill values, then the plotting widget
	 * crashes.  Hack to avoid this problem.
	 */
	if( plot_XY_xvals[0] == plot_XY_xvals[n-1] )
		for(i=0; i<n; i++ )
			plot_XY_xvals[i] = (double)i;

	/* Get the y values (values to be plotted) */
	g_app.session.dataset().getData( view->variable, start, count, tmp_yvals );

	/* Eliminate the missing values */
	j = 0;
	n_missing_eliminated = 0;
	tol = fabs(view->variable->fill_value * 1.e-5);
	for( i=0; i<n; i++ ) {
		t_xval = plot_XY_xvals[i];
		t_yval = *(tmp_yvals+i);
		if( (fabs((double)(t_yval - view->variable->fill_value)) > tol) &&
		    (t_yval < 9.e36)) {
			plot_XY_xvals[j] = t_xval;
			plot_XY_yvals[j] = (double)t_yval;
			j++;
			}
		else
			{
			n_missing_eliminated++;
			if( n_missing_eliminated < n_misplace )
				misplace_index[n_missing_eliminated-1] = i;
			}
		}
	if( n != j ) {
		printf( "Note: %ld missing values were eliminated along axis \"%s\"; index= ", 
							n-j, dim_name );
		for( k=0; k<(n_missing_eliminated<n_misplace?n_missing_eliminated:n_misplace); k++ )
			printf( " %ld", misplace_index[k]+1 );
		if( n_missing_eliminated > n_misplace )
			printf( "...\n" );
		else
			printf( "\n" );
		n = j;
		}
	if( n == 0 ) {
		in_error( "All values are missing!\n" );
		return;
		}

	/* Get the X axis title */
	snprintf( x_axis_title, sizeof(x_axis_title), "%s", view->variable->dim[dim_to_plot]->name.c_str() );
	units    = view->variable->files.front()->file->dimUnits( dim_name );
	if( !units.empty() ) {
		strncat( x_axis_title, " (", sizeof(x_axis_title) - strlen(x_axis_title) - 1 );
		strncat( x_axis_title, units.c_str(), sizeof(x_axis_title) - strlen(x_axis_title) - 1 );
		strncat( x_axis_title, ")", sizeof(x_axis_title) - strlen(x_axis_title) - 1 );
		}

	/* Another hack to fix the plotter widget...it barfs if all
	 * the Y values are the same thing.  Fix this case.
	 */
	all_same = true;
	y_min    = 1.e35;
	y_max    = -1.e35;
	for( i=1; i<n; i++ ) {
		if( plot_XY_yvals[i] != plot_XY_yvals[0] )
			all_same = false;
		if( plot_XY_yvals[i] < y_min )
			y_min = plot_XY_yvals[i];
		if( plot_XY_yvals[i] > y_max )
			y_max = plot_XY_yvals[i];
		}
	if( all_same ) {
		ui.in_set_cursor_normal();
		snprintf( message, 511, "All values are identical: %f\n", plot_XY_yvals[0] );
		in_error( message );
		return;
		}

	/* Get the Y (which is the active variable) axis title */
	snprintf( y_axis_title, sizeof(y_axis_title), "%s", view->variable->name.c_str() );
	units = view->variable->files.front()->file->varUnits( view->variable->name );
	if( !units.empty() ) {
		strncat( y_axis_title, " (", sizeof(y_axis_title) - strlen(y_axis_title) - 1 );
		strncat( y_axis_title, units.c_str(), sizeof(y_axis_title) - strlen(y_axis_title) - 1 );
		strncat( y_axis_title, ")", sizeof(y_axis_title) - strlen(y_axis_title) - 1 );
		}

	/* Get the overall plot title */
	long_name = view->variable->files.front()->file->longVarName(
				view->variable->name );
	if( !long_name.empty() )
		snprintf( title, sizeof(title), "%s", long_name.c_str() );
	else
		snprintf( title, sizeof(title), "%s", view->variable->name.c_str() );
	file_title = view->variable->files.front()->file->title();
	if( !file_title.empty() ) {
		strncat( title, " from ", sizeof(title) - strlen(title) - 1 );
		strncat( title, file_title.c_str(), sizeof(title) - strlen(title) - 1 );
		}

	/* Make the legend */
	legend[0] = '(';
	legend[1] = '\0';
	have_done_one = false;
	for( i=0; i<view->variable->n_dims; i++ ) 
		/* if( (i != dim_to_plot) && ((*(start+i) != 0) || (*(count+i) != 1))) { */
		if( (i != dim_to_plot) && (view->variable->dim[i].get() != NULL)) {
			if( have_done_one )
				strncat( legend, ", ", sizeof(legend) - strlen(legend) - 1 );
			strncat( legend, view->variable->dim[i]->name.c_str(), sizeof(legend) - strlen(legend) - 1 );
			have_done_one = true;
			}
	strncat( legend, ") = (", sizeof(legend) - strlen(legend) - 1 );
	have_done_one = false;
	for( i=0; i<view->variable->n_dims; i++ ) 
		if( (i != dim_to_plot) && (view->variable->dim[i].get() != NULL)) {
			if( have_done_one )
				strncat( legend, ", ", sizeof(legend) - strlen(legend) - 1 );
			type = g_app.session.dataset().dimValue( view->variable, i, *(start+i), &temp_double, temp_string,
					&has_bounds, &bound_min, &bound_max, view->var_place.data() );
			if( type == NC_DOUBLE ) {
				snprintf( temp2_string, 127, "%lg", temp_double );
				strncat( legend, temp2_string, sizeof(legend) - strlen(legend) - 1 );
				}
			else	{
				/* Phase 12h follow-up: temp_string is 1024 bytes (widened
				 * in Phase 12g to satisfy Dataset::dimValue()'s ≥1024-byte
				 * contract) while legend is only 512 -- GCC's
				 * -Wstringop-truncation can prove this strncat's source
				 * may be longer than the remaining space and flags it as
				 * -Werror, even though truncating here is intentional and
				 * safe. Same fix as colormap_library.cc's -- make the
				 * truncation explicit via memcpy/strnlen so nothing is
				 * left for the heuristic to warn about. */
				size_t used = strlen(legend);
				size_t remaining = sizeof(legend) - used - 1;
				size_t copy_len = strnlen( temp_string, remaining );
				memcpy( legend+used, temp_string, copy_len );
				legend[used+copy_len] = '\0';
				}
			have_done_one = true;
			}
	strncat( legend, ")", sizeof(legend) - strlen(legend) - 1 );

	/* Get list of scannable dimensions, which are possible axes
	 * that we could plot along.  This will be displayed in the
	 * pulldown menu of possible dimensions to plot along, so arrange
	 * it so that the current dimension we are plotting along is first.
	 */
	dimlist = NULL;
	/* Put the current dim first on the list */
	stringlist_add_string( &dimlist, view->variable->dim[dim_to_plot]->name.c_str() );
	for( i=0; i<view->variable->n_dims; i++ )
		/* We are using here the fact that view->variable->dim was initialized
		 * so that non-scannable dims were set to NULL.  However, this can still
		 * fail in the case of the 'time' dimension, which is always included
		 * even if there is a count of only 1 along it.  Get rid of that case
		 * by making sure the size > 1.
		 */
		if( (view->variable->dim[i] != nullptr) && (i != dim_to_plot)
		    && (view->variable->size[i] > 1))
			stringlist_add_string( &dimlist, view->variable->dim[i]->name.c_str() );

	if( options.debug ) {
		fprintf( stderr, "about to call in_popup_XY_graph...\n" );
		fprintf( stderr, "     n     =%ld\n", n );
		fprintf( stderr, "     dim   =%i\n", dim_to_plot );
		fprintf( stderr, "     xtitle=%s\n", x_axis_title );
		fprintf( stderr, "     ytitle=%s\n", y_axis_title );
		fprintf( stderr, "     title =%s\n", title );
		}
	plot_index = ui.in_popup_XY_graph( n, dim_to_plot, plot_XY_xvals.data(),
			plot_XY_yvals.data(), x_axis_title, y_axis_title,
			title, legend, dimlist );

	/* Save the X dimension for this plot, so that we can later format
	 * that dim's time values if requested (while the mouse is inside
	 * that plot's window).
	 */
	if( plot_index != -1 )
		plot_XY_dim[plot_index] = view->variable->dim[dim_to_plot].get();

	ui.in_set_cursor_normal();
	if( options.debug ) 
		fprintf( stderr, "plot_XY_sc: leaving\n" );
}

/**************************************************************************************/
	void
View::plotXYFmtXVal( float val, int dimindex, char *s, size_t s_len )
{
	View *view = this;
	NCDim	*dim;

	dim = view->variable->dim[dimindex].get();
	if( dim->timelike && options.t_conv )
		fmt_time( s, s_len-1, val, dim, 1 );
	else
		snprintf( s, s_len-1, "%g", val );
}

/**************************************************************************************/
	void
View::information()
{
	g_app.ui->in_display_stuff( variable->files.front().get()->file->attString(
						variable->name ).c_str(),
			variable->name.c_str() );
}

/**************************************************************************************/
	void
invalidate_variable( NCVar *var, ViewerSession &session, ViewerUi &ui )
{
	std::unique_ptr<ViewState> &view = session.activeView();

	ui.x_set_var_sensitivity( const_cast<char *>(view->variable->name.c_str()), false );
	set_buttons( BUTTONS_ALL_OFF, ui );
	view.reset();	/* deletes the View this used to just orphan */
	options.blowup = 1;
}

/**************************************************************************************/
	void
view_get_scaled_size( int blowup, size_t old_nx, size_t old_ny, size_t *new_nx, size_t *new_ny )
{
	double d_new_nx, d_new_ny, d_old_nx, d_old_ny, d_blowup, epsilon;

	if( blowup > 0 ) {
		*new_nx = blowup * old_nx;
		*new_ny = blowup * old_ny;
		return;
		}

	/* Now we know blowup < 0 */
	d_old_nx = (double)old_nx;
	d_old_ny = (double)old_ny;
	d_blowup = (double)(-blowup);
	
	d_new_nx = ceil( d_old_nx/d_blowup );
	d_new_ny = ceil( d_old_ny/d_blowup );

	epsilon = .00001;
	*new_nx = (size_t)(d_new_nx + epsilon);
	*new_ny = (size_t)(d_new_ny + epsilon);
}

/**************************************************************************************/
	void
mouse_xy_to_data_xy( int mouse_x, int mouse_y, int blowup, size_t *data_x, size_t *data_y )
{
	int b;

	if( blowup > 0 ) {
		*data_x = mouse_x / options.blowup;
		*data_y = mouse_y / options.blowup;
		return;
		}

	b = -blowup;
	*data_x = mouse_x * b + (int)((double)b/2.0);
	*data_y = mouse_y * b + (int)((double)b/2.0);
}

/*======================================================================================
 * Return true if there is *any* missing data in the current view, and false otherwise
 */
	bool
View::hasMissingData() const
{
	const View *v = this;
	size_t 	nx, ny, i;
	float	dat;

	if( v->variable == NULL )
		return(true);

	/* Phase 13b: the x_axis_id guard below was already right, but `data`
	 * being *sized* is a separate precondition -- set_scan_variable()'s
	 * 1-D path never calls allocStorage(), so a View can reach here with
	 * a perfectly good x_axis_id and an empty buffer, and the read loop
	 * then walks off the end of nothing (a real SIGSEGV, not a quiet
	 * overread). "No data loaded" answers this question the same way
	 * "no variable" does. */
	if( v->data.empty() )
		return(true);

	if( v->x_axis_id < 0 )
		return(true);
	nx = v->variable->size[v->x_axis_id];

	if( v->y_axis_id < 0 ) 
		ny = 1;
	else
		ny = v->variable->size[v->y_axis_id];

	for( i=0; i<nx*ny; i++ ) {
		dat = v->data[i];
		if( close_enough( dat, v->variable->fill_value) || (dat == FILL_FLOAT)) 
			return(true);
		}

	return(false);
}

/***************************************************************************
 * Change the current data transformation
 */
	void
view_change_transform( int delta, ViewerUi &ui )
{
	int	transform_int = static_cast<int>(options.transform) + delta;
	if( transform_int > N_TRANSFORMS )
		transform_int = 1;
	if( transform_int < 1 )
		transform_int = N_TRANSFORMS;
	options.transform = static_cast<Transform>(transform_int);

	switch( options.transform ) {
		case Transform::None   : ui.in_set_label( Label::Transform, "Linear" ); break;
		case Transform::Low    : ui.in_set_label( Label::Transform, "Low"    ); break;
		case Transform::Hi     : ui.in_set_label( Label::Transform, "Hi"     ); break;
		case Transform::Center : ui.in_set_label( Label::Transform, "Center" ); break;
		default:
			fprintf( stderr, "ncview: change_transform: unknown transform %d\n",
				transform_int );
			exit( -1 );
		}

	g_app.controller.draw( true, false );
	g_app.controller.recomputeColorbar();
}

/***************************************************************************/
	static float
view_calc_minval_float( float *arr, size_t n )
{
	float	retval=1.e30;
	size_t	i;
	
	for( i=0L; i<n; i++ )
		if( arr[i] < retval )
			retval = arr[i];
	return( retval );
}

/***************************************************************************/
	static float
view_calc_maxval_float( float *arr, size_t n )
{
	float	retval=-1.e30;
	size_t	i;
	
	for( i=0L; i<n; i++ )
		if( arr[i] > retval )
			retval = arr[i];
	return( retval );
}

/***************************************************************************/
	static void
strip_trailing_zeros( char *s )
{
	int	i;

	i = strlen(s) - 1;
	while( (i > 0 ) && (s[i] == '0' ) && (s[i-1] != '.')) 
		s[i--] = '\0';
}

