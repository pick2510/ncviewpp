/*
 * core/src/viewer_controller.cc
 *
 * Copyright (C) 1993 through 2024 David W. Pierce
 * Modifications Copyright (C) 2026 Dominik Strebel
 *
 * See ncview/viewer_controller.h. Bodies moved verbatim from do_buttons.cc's
 * do_*() functions (OOP_redesign plan, Step 7) -- cur_button is now a
 * private member (cur_button_) instead of a file-static, and the two
 * timer-rearming lambdas (rewind, fastforward) capture [this] instead of
 * recursing through a free function.
 *
 * "Refine the architecture" plan, Phase 3e added the 9 methods below
 * range()/dimset()/etc. (down through recomputeColorbar()) -- bodies
 * moved verbatim from view.cc, where Phase 2 had left them (it moved the
 * *names* onto ViewerController but the *bodies* stayed put). Pure file
 * motion: no signature or logic change. See view_internal.h for the
 * handful of view.cc-local helpers these still call.
 */
#include "ncview/viewer_controller.h"
#include "ncview/viewer_ui.h"

#include "ncview/includes.h"
#include "ncview/protos.h"
#include "view_internal.h"

#define DELAY_DELTA	350.0
#define DELAY_OFFSET	10L

extern Options options;

/* Sole caller is stepView()'s stop_on_restart branch, below -- static,
 * not declared in protos.h (moved here from view.cc in Phase 3e; it was
 * already static-with-no-header-declaration since Phase 3d). */
	static void
beep()
{
	fprintf( stderr, "\a" );
	fflush(  stderr );
}

/*===========================================================================================*/
void
ViewerController::dispatch( Button button_id, Modifier modifier )
{
	switch( button_id ) {
		case Button::Range:		range( modifier );		break;
		case Button::Dimset:		dimset( modifier );		break;
		case Button::Transform:		transform( modifier );		break;
		case Button::Blowup:		blowup( modifier );		break;
		case Button::Quit:		quit( modifier );		break;
		case Button::Restart:		restart( modifier );		break;
		case Button::Rewind:		rewind( modifier );		break;
		case Button::Backwards:		backwards( modifier );		break;
		case Button::Pause:		pause( modifier );		break;
		case Button::Forward:		forward( modifier );		break;
		case Button::Fastforward:	fastforward( modifier );	break;
		case Button::ColormapSelect:	colormapSelect( modifier );	break;
		case Button::InvertPhysical:	invertPhysical( modifier );	break;
		case Button::InvertColormap:	invertColormap( modifier );	break;
		case Button::Minimum:		setMinimum( modifier );	break;
		case Button::Maximum:		setMaximum( modifier );	break;
		case Button::BlowupType:	blowupType( modifier );	break;
		case Button::Edit:		dataEdit( modifier );		break;
		case Button::Info:		info( modifier );		break;
		case Button::Print:		print();			break;
		case Button::Options:		optionsDialog( modifier );	break;

		default:
			fprintf( stderr, "ViewerController::dispatch: unknown " );
			fprintf( stderr, "button id: %d\n", static_cast<int>(button_id) );
			exit( -1 );
		}
}

/*===========================================================================================*/
void
ViewerController::range( Modifier modifier )
{
	view->initSaveframes();
	if( modifier == Modifier::M3 )
		view->setRangeFrame();
	else
		view->setRange();
}

/*===========================================================================================*/
void
ViewerController::dimset( Modifier modifier )
{
	/* Phase 13a: the only dispatch() handler reaching straight into view->
	 * with no "is a variable selected yet?" check -- the same session fact
	 * changeCurDim()/setCurDimIndex() below already guard, and with the
	 * same message. Button::Dimset is insensitive until the first variable
	 * is selected, so this is defence in depth rather than a reproduced
	 * crash, but it costs nothing and removes the odd one out. */
	if( view == NULL ) {
		in_error( "Please select a variable first" );
		return;
		}

	view->setScanDims();
}

/*===========================================================================================*/
void
ViewerController::restart( Modifier modifier )
{
	cur_button_ = Button::Pause;

	g_app.ui->in_timer_clear();

	view->scanToPlace( 0 );
	draw    ( true, false );

	g_app.ui->in_timer_clear();
}

/*===========================================================================================*/
void
ViewerController::rewind( Modifier modifier )
{
	unsigned long delay_millisec;
	size_t	size;
	double	d_delta;
	int	i_delta;

	cur_button_ = Button::Rewind;

	delay_millisec = (long)(DELAY_DELTA * options.frame_delay) + DELAY_OFFSET;

	g_app.ui->in_timer_clear();

	if( modifier == Modifier::M2 ) {
		size = session_.currentNt();
		d_delta = (double)size / 1000.0;
		if( d_delta < 10.0 )
			i_delta = -10;
		else
			i_delta = -d_delta;
		stepView( i_delta, FRAMES );
		g_app.ui->in_timer_set( [this](){ rewind(Modifier::M2); }, delay_millisec );
		}
	else
		{
		stepView( -1, FRAMES );
		g_app.ui->in_timer_set( [this](){ rewind(Modifier::M1); }, delay_millisec );
		}
}

/*===========================================================================================*/
void
ViewerController::quit( Modifier modifier )
{
	quit_app();
}

/*===========================================================================================*/
void
ViewerController::backwards( Modifier modifier )
{
	size_t	size;

	g_app.ui->in_timer_clear();

	if( modifier == Modifier::M2 ) {
		size = session_.currentNt();
		if( size < 500 )
			stepView( -10, PERCENT );
		else if( size < 5000 )
			stepView(  -5, PERCENT );
		else if( size < 50000 )
			stepView(  -2, PERCENT );
		else
			stepView(  -1, PERCENT );
		}
	else
		stepView( -1, FRAMES );

	cur_button_ = Button::Pause;
}

/*===========================================================================================*/
void
ViewerController::pause( Modifier modifier )
{
	cur_button_ = Button::Pause;
	g_app.ui->in_timer_clear();
}

/*===========================================================================================*/
void
ViewerController::forward( Modifier modifier )
{
	size_t	size;

	cur_button_ = Button::Pause;
	g_app.ui->in_timer_clear();

	if( modifier == Modifier::M2 ) {
		size = session_.currentNt();
		if( size < 500 )
			stepView( 10, PERCENT );
		else if( size < 5000 )
			stepView(  5, PERCENT );
		else if( size < 50000 )
			stepView(  2, PERCENT );
		else
			stepView(  1, PERCENT );
		}
	else
		stepView( 1, FRAMES );
}

/*===========================================================================================*/
void
ViewerController::fastforward( Modifier modifier )
{
	unsigned long	delay_millisec;
	size_t	size;
	double	d_delta;
	int	i_delta;

	cur_button_ = Button::Fastforward;

	g_app.ui->in_timer_clear();

	delay_millisec = (long)(DELAY_DELTA * options.frame_delay) + DELAY_OFFSET;

	if( modifier == Modifier::M2 ) {
		size = session_.currentNt();
		d_delta = (double)size / 1000.0;
		if( d_delta < 10.0 )
			i_delta = 10;
		else
			i_delta = d_delta;
		if( stepView( i_delta, FRAMES ) == 0 )
			g_app.ui->in_timer_set( [this](){ fastforward(Modifier::M2); }, delay_millisec );
		}
	else
		{
		if( stepView( 1, FRAMES ) == 0 )
			g_app.ui->in_timer_set( [this](){ fastforward(Modifier::M1); }, delay_millisec );
		}
}

/*===========================================================================================*/
void
ViewerController::colormapSelect( Modifier modifier )
{
	if( modifier == Modifier::M3 )
		g_app.ui->in_install_prev_colormap( true );
	else
		g_app.ui->in_install_next_colormap( true );
	draw( true, false );
	recomputeColorbar();
}

/*===========================================================================================*/
void
ViewerController::colormapSelectByName( const char *name )
{
	g_app.ui->in_install_colormap_by_name( name, true );
	draw( true, false );
	recomputeColorbar();
}

/*===========================================================================================*/
void
ViewerController::invertPhysical( Modifier modifier )
{
	view->initSaveframes();
	if( options.invert_physical )
		options.invert_physical = false;
	else
		options.invert_physical = true;
	draw( true, false );
	view->redrawDimensionInfo();
}

/*===========================================================================================*/
void
ViewerController::dataEdit( Modifier modifier )
{
	view->dataEdit();
}

/*===========================================================================================*/
void
ViewerController::invertColormap( Modifier modifier )
{
	view->initSaveframes();
	if( options.invert_colors )
		options.invert_colors = false;
	else
		options.invert_colors = true;
	draw( true, false );
	recomputeColorbar();
}

/*===========================================================================================*/
void
ViewerController::setMinimum( Modifier modifier )
{
}

/*===========================================================================================*/
void
ViewerController::setMaximum( Modifier modifier )
{
}

/*===========================================================================================*/
void
ViewerController::blowup( Modifier modifier )
{
	int view_var_is_valid = true;

	if( modifier == Modifier::M3 )
		view->changeBlowup( -1, true, view_var_is_valid );

	else if( modifier == Modifier::M2 ) {
		/* Double the current blowup -- make image BIGGER */
		if( options.blowup > 0 )
			view->changeBlowup( options.blowup, true, view_var_is_valid );
		else
			view->changeBlowup( -(options.blowup)/2, true, view_var_is_valid );
		}

	else if( modifier == Modifier::M4 ) {
		/* Halve the current blowup -- make image SMALLER */
		if( options.blowup > 0 )
			view->changeBlowup( -(options.blowup/2), true, view_var_is_valid );
		else
			view->changeBlowup( options.blowup, true, view_var_is_valid );
		}

	else
		view->changeBlowup( 1, true, view_var_is_valid );

	/* If we are shrinking magnification, then try re-saving
	 * the frames because now there might be enough room.
	 */
	view->initSaveframes();
	if( modifier == Modifier::M3 )
		options.save_frames = true;
}

/*===========================================================================================*/
void
ViewerController::transform( Modifier modifier )
{
	view->initSaveframes();
	if( modifier == Modifier::M3 )
		view_change_transform( -1, *g_app.ui );
	else
		view_change_transform( 1, *g_app.ui );
}

/*===========================================================================================*/
void
ViewerController::blowupType( Modifier modifier )
{
	view->initSaveframes();
	if( options.blowup_type == BlowupType::Replicate )
		set_blowup_type( BlowupType::Bilinear, *g_app.ui );
	else
		set_blowup_type( BlowupType::Replicate, *g_app.ui );
	draw( true, false );
}

/*===========================================================================================*/
void
ViewerController::info( Modifier modifier )
{
	view->information();
}

/*===========================================================================================*/
void
ViewerController::optionsDialog( Modifier modifier )
{
	g_app.ui->set_options();
}

/*===========================================================================================*/
void
ViewerController::print()
{
	do_print();
}

/********************************************************************************
 * Change the view we currently have on the data; i.e., scan along
 * the scan-axis.  'interpretation' can be either FRAMES or PERCENT, and
 * indicates how in interpret the passed delta value.
 *
 * Phase 2: moved from the free function change_view() onto
 * ViewerController -- its `view == NULL` guard was standing in for "no
 * variable selected yet", a session fact.
 */
	int
ViewerController::stepView( int delta, int interpretation )
{
	std::unique_ptr<ViewState> &view = session_.activeView();
	size_t	size;
	long	place;
	float	provisional_delta;

	if( view == NULL )	/* This happens because this routine is called    */
		return(0);	/* when Expose events are generated, and one is   */
				/* generated before the view has been initialized */

	if( delta != 0 ) {
		if( view->data_status == ViewDataStatus::Edited ) {
			fprintf( stderr, "warning! flushing changes!\n" );
			}
		view->data_status = ViewDataStatus::Invalid;
		}

	/* Apply the skip */
	if( interpretation == FRAMES )
		delta *= view->skip;

	if(view->scan_axis_id == -1) {
		if( delta == 0 ) {
			draw( false, false );
			return(0);
			}
		else
			{
			fprintf( stderr,
				"called change_view with no scan_axis\n" );
			exit( -1 );
			}
		}

	if( interpretation == PERCENT ) {
		/* Delta is in percent of total size */
		size              = view->variable->size[view->scan_axis_id];
		provisional_delta = (float)size * (float)delta/100.0;
		if( (int)provisional_delta == 0 ) {
			if( delta < 0 )
				delta = -1;
			else
				delta = 1;
			}
		else
			delta = (int)provisional_delta;
		}

	place = view->var_place[view->scan_axis_id] + delta;
	size  = view->variable->size[view->scan_axis_id];

	/* Have we incremented past the maximum allowed value?
	 * If we have, then reset to ZERO, not just (size modulo
	 * step), so that when stepping with a skip > 1, we can
	 * save frames and come back to the same frames in the
	 * framestore.
	 */
	if( place >= (long)size ) {
		place = 0L;
		if( options.beep_on_restart )
			beep();
		if( options.stop_on_restart ) {
			g_app.controller.pause( Modifier::M1 );
			return(0);
			}
		}

	/* Have we decremented below the minimum allowed value? */
	if( place < 0L )
		place = size - 1L;

	view->scanToPlace( place );
	return( draw( true, false ) );
}

/********************************************************************************
 * draw the current view onto the display
 *
 * Phase 2: moved from the free function view_draw() onto ViewerController
 * -- its `view == NULL` guard was standing in for "no variable selected
 * yet", a session fact.
 */
	int
ViewerController::draw( int allow_framestore_usage, int force_range_to_frame )
{
	std::unique_ptr<ViewState> &view = session_.activeView();
	FrameCache &framestore = session_.frameCache();
	LastFrameSize &last_frame_size = session_.lastFrameSize();
	size_t		i;
	size_t		x_size, y_size, scan_size, scaled_x_size, scaled_y_size, framesize, frameno;
	int		must_recalc_range;
	float		min, max, dat;

	/* The reason why we have to lockout the possiblity that this
	 * routine is called WHILE it is executing is tricky.  The 
	 * 'data_to_pixels' call, below and elsewhere, can result in a modal
	 * dialog being popped up, but the ccontour window can still
	 * get 'expose' events.  In that case multiple modal dialogs would
	 * be popped up, to conflict at random, unless there were some
	 * way of locking out entry to this subroutine while it is actively
	 * being executed or the other modal dialogs are popped up.
	 */
	if( lockout_view_changes )
		return(0);
	lockout_view_changes = true;

	/* These can happen because this routine is called when the ccontour
	 * window gets 'expose' events, which happens on program startup,
	 * before the view has been initialized.
	 */
	if( (view == NULL) || (view->data.empty())) {
		lockout_view_changes = false;
		return(0);
		}

	x_size = view->variable->size[view->x_axis_id];
	y_size = view->variable->size[view->y_axis_id];

	must_recalc_range = force_range_to_frame || options.autoscale;

	view_get_scaled_size( options.blowup, x_size, y_size, &scaled_x_size, &scaled_y_size );

	framesize = scaled_x_size * scaled_y_size;
	if( view->scan_axis_id == -1 )
		frameno = 0;
	else
		frameno = view->var_place[view->scan_axis_id];

	if( options.debug ) {
		fprintf( stderr, "in view_draw:\n" );
		fprintf( stderr, "	x_size, y_size:%zu %zu\n",
						x_size, y_size );
		fprintf( stderr, "	scan_axis_id:%d\n",
						view->scan_axis_id);
		fprintf( stderr, "	scan_place:%zu\n",
						frameno );
		}

	/* Is this frame stored in the framestore? Never true under
	 * -autoscale: upstream relied on its recalc-and-invalidate block
	 * running *before* this check (invalidating whatever's cached here
	 * on every autoscale draw, so this check would always miss while
	 * autoscale is on). This port moved that block below, after
	 * View::fillViewData(), so it recomputes from the freshly-loaded frame
	 * instead of upstream's stale-until-next-frame data -- but that
	 * leaves a window where a still-cached, currently-displayed frame
	 * (e.g. redrawn right after toggling autoscale on in the Options
	 * dialog) gets served from the cache before the invalidate below
	 * ever runs. Excluding autoscale here closes that window the same
	 * way upstream's ordering did.
	 */
	if( allow_framestore_usage && !options.autoscale ) {
		const ncv_pixel *cached = framestore.lookup( frameno );
		if( cached != nullptr ) {
			if( options.debug )
				printf( "drawing from framestore...\n" );
			g_app.ui->in_draw_2d_field( cached, scaled_x_size, scaled_y_size, frameno );
			lockout_view_changes = false;

			if( view->scan_axis_id != -1 ) {
				scan_size  = view->variable->size[view->scan_axis_id];
				if( (frameno == (scan_size-1)) && (which_button_pressed() == Button::Pause)) {
					g_app.ui->in_timer_set( [](){ ::view->checkNewData(0); }, 1000L );
					}
				}
			return(0);
			}
		}

	if( view->data_status == ViewDataStatus::Invalid ) {
		if( options.debug )
			printf( "Reading data to contour...\n" );
		view->fillViewData();
		}
	else
		{
		if( options.debug )
			printf( "NOT reading data to contour, since data is valid (%d)\n", static_cast<int>(view->data_status) );
		}

	/* If we need to adjust the range to the current frame, then do so.
	 * Must run after View::fillViewData() above, so it sees the just-loaded
	 * slice rather than whatever the previous frame left in view->data.
	 */
	if( must_recalc_range ) {
		min = 1.0e35;
		max = -min;

		for( i=0; i<x_size*y_size; i++ ) {
			dat = view->data[i];
			if( dat != dat )
				dat = view->variable->fill_value;
			if( ! close_enough( dat, view->variable->fill_value) && (dat != FILL_FLOAT)) {
				if( dat > max )
					max = dat;
				if( dat < min )
					min = dat;
				}
			}

		view->variable->user_min = min;
		view->variable->user_max = max;
		view->setRangeLabels( min, max );
		view->data_status = ViewDataStatus::Invalid;
		session_.invalidateAllSaveframes();	/* note we invalidate all frames, so even if allow_framestore_useage is true, it won't happen */
		recomputeColorbar();
		}

	if( options.debug )
		printf( "Calling data_to_pixels...\n" );
	if( view->dataToPixels() < 0 ) {
		g_app.ui->in_timer_clear();
		if( view->variable->global_min == view->variable->global_max )
			invalidate_variable( view->variable, session_, *g_app.ui );
		lockout_view_changes = false;
		return( -1 );
		}

	if( (last_frame_size.width != scaled_x_size) ||
	    (last_frame_size.height != scaled_y_size)) {
		last_frame_size.width = scaled_x_size;
		last_frame_size.height = scaled_y_size;
		g_app.ui->in_set_2d_size( scaled_x_size, scaled_y_size );
		}

	if( options.debug )
		printf( "Calling draw_2d_field...\n" );
	g_app.ui->in_draw_2d_field( view->pixels.data(), scaled_x_size, scaled_y_size, frameno );

	if( framestore.valid() )
		framestore.store( frameno, view->pixels.data(), framesize );

	/* If we just drew the last time entry for this var, then
	 * set up a callback that waits 1 second and checks for
	 * the var having new data in it.
	 */
	if( view->scan_axis_id != -1 ) {
		scan_size  = view->variable->size[view->scan_axis_id];
		if( (frameno == (scan_size-1)) && (which_button_pressed() == Button::Pause)) {
			g_app.ui->in_timer_set( [](){ ::view->checkNewData(0); }, 1000L );
			}
		}

	lockout_view_changes = false;
	return( 0 );
}

	void
ViewerController::changeCurDim( char *dim_name, Modifier modifier )
{
	std::unique_ptr<ViewState> &view = session_.activeView();
	int	dimid;
	size_t	place, size;
	long	delta, prov_place;
	NCDim	*dim;

	if( view == NULL ) {
		in_error( "Please select a variable first" );
		return;
		}

	if( view->data_status == ViewDataStatus::Edited )
		view_data_edit_warn( session_, *g_app.ui );

	dimid  = view->variable->files.front()->file->dimNameToId(
				const_cast<char *>(view->variable->name.c_str()), dim_name );
	/* Phase 12e: dimid can legitimately be -1 (dim_name not found for
	 * this variable) -- the x_axis_id/y_axis_id check below only catches
	 * this by accident, when one of those axis ids also happens to be -1
	 * (no scan axis assigned). Check dimid itself first: dim[-1]/
	 * var_place[-1] below would be operator[](SIZE_MAX), an out-of-bounds
	 * read on both std::vector<std::unique_ptr<NCDim>> and
	 * std::vector<size_t>. */
	if( dimid < 0 ) {
		in_error( "The requested dimension was not found for this variable." );
		return;
		}
	if( (dimid == view->x_axis_id) ||
	    (dimid == view->y_axis_id) )
		return;

	dim = view->variable->dim[dimid].get();

	/* Modifier 1 is the standard action */
	if( modifier == Modifier::M1 ) {
		view->var_place[dimid] = view->var_place[dimid]+1L;
		if( view->var_place[dimid] > view->variable->size[dimid]-1L )
			view->var_place[dimid] = 0L;
		}
	else if( modifier == Modifier::M2 ) {
		/* Modifier 2 means "do it faster" */
		size  = view->variable->size[dimid];
		delta = (int)(0.1*(float)size);
		view->var_place[dimid] = view->var_place[dimid]+(long)delta;
		if( view->var_place[dimid] > view->variable->size[dimid]-1L )
			view->var_place[dimid] = 0L;
		}
	else
		{
		/* Modifier 3 means to go backwards */
		prov_place = view->var_place[dimid]-1L;
		if( prov_place < 0L )
			view->var_place[dimid] = view->variable->size[dimid] -1L;
		else
			view->var_place[dimid] = prov_place;
		}

	place = view->var_place[dimid];
	view->applyCurDimPlace( dimid, dim, place );
}

/**********************************************************************
 * Jump a non-scan dimension directly to an absolute index -- the
 * counterpart to view_change_cur_dim()'s relative +1/-1/+10% stepping,
 * for a UI control (a slider) that lets the user pick a place directly
 * instead of clicking through it one step at a time.
 */
	void
ViewerController::setCurDimIndex( const char *dim_name, long place )
{
	std::unique_ptr<ViewState> &view = session_.activeView();
	int	dimid;
	NCDim	*dim;

	if( view == NULL ) {
		in_error( "Please select a variable first" );
		return;
		}

	if( view->data_status == ViewDataStatus::Edited )
		view_data_edit_warn( session_, *g_app.ui );

	dimid  = view->variable->files.front()->file->dimNameToId(
				const_cast<char *>(view->variable->name.c_str()),
				const_cast<char *>(dim_name) );
	/* Phase 12e: same guard as ViewerController::changeCurDim() above --
	 * dimid can legitimately be -1, and size[dimid]/dim[dimid] below
	 * would be an out-of-bounds read at operator[](SIZE_MAX) otherwise. */
	if( dimid < 0 ) {
		in_error( "The requested dimension was not found for this variable." );
		return;
		}
	if( (dimid == view->x_axis_id) ||
	    (dimid == view->y_axis_id) )
		return;

	if( place < 0 )
		place = 0;
	if( (size_t)place > view->variable->size[dimid]-1L )
		place = (long)(view->variable->size[dimid]-1L);

	dim = view->variable->dim[dimid].get();
	view->applyCurDimPlace( dimid, dim, (size_t)place );
}

/**************************************************************************************/
/* This reports the mouse location in the main (2-D color contour) window */
	void
ViewerController::reportPosition( int x, int y, unsigned int button_mask )
{
	std::unique_ptr<ViewState> &view = session_.activeView();
	size_t	data_x, data_y, x_size, y_size;
	int	type, has_bounds, i, x_is_mapped, y_is_mapped;
	float	val;
	double	new_dimval, bound_min, bound_max;
	char	current_value_label[500], temp_string[1024];
	std::string	xdim_str, ydim_str;
	NCDim	*xdim, *ydim;
	size_t	virt_cursor_pos[MAX_NC_DIMS];

	/* This can happen if you display a 2-d variable, then
	 * display a variable with no valid range (thereby setting
	 * the view to NULL), then roll back over the 2-d color
	 * window. Fix thanks to Matthew Bettencourt @ usm.edu */
	if( ! view )
		return;

	/* This can happen if we click on a 2-d variable, then click
	 * on a 1-d variable, then move the pointer back over the
	 * displayed colormap of the (old) 2-d variable.
	 */
	if( view->variable->effective_dimensionality == 1 )
		return;

	/* Phase 13b: the check above is upstream's proxy for "set_scan_variable()
	 * took its plot-and-return path, so there is no allocated 2-D field".
	 * has2dImage() is the condition the code below actually needs, and
	 * additionally covers the "couldn't determine the axes" degrade paths,
	 * where effective_dimensionality can be 3 while both display axis ids
	 * are -1. */
	if( ! view->has2dImage() )
		return;

	if( view->data_status == ViewDataStatus::Invalid ) {
		view->fillViewData();
		view->data_status = ViewDataStatus::Valid;
		}

	mouse_xy_to_data_xy( x, y, options.blowup, &data_x, &data_y );

	x_size = view->variable->size[view->x_axis_id];
	y_size = view->variable->size[view->y_axis_id];

	/* Make sure we don't go outside the limits */
	data_x = ( (data_x >= x_size ) ? x_size-1 : data_x );
	data_y = ( (data_y >= y_size ) ? y_size-1 : data_y );

	/* Invert Y because the reporting counts from the
	 * UPPER LEFT, not the lower left like we want
	 * it to.  If the *picture* is inverted, don't flip
	 * y!
	 */
	if( !options.invert_physical )
		data_y = y_size - data_y - 1;
	
	/* Get the value of the data field under the cursor */
	val = view->data[data_x + data_y*x_size];

	/* Get the values of the X and Y indices. 
	* 'type' is the data type of the dimension--can be float or character 
	*/
	xdim = view->variable->dim[view->x_axis_id].get();
	ydim = view->variable->dim[view->y_axis_id].get();

	x_is_mapped = (view->variable->dim_map_info[ view->x_axis_id ] != NULL);
	y_is_mapped = (view->variable->dim_map_info[ view->y_axis_id ] != NULL);
	if( 1 || x_is_mapped || y_is_mapped ) {
		/* Get virtual position in all dims for this mouse cursor point */
		for( i=0; i<view->variable->n_dims; i++ )
			virt_cursor_pos[i] = view->var_place[i];
		virt_cursor_pos[ view->x_axis_id ] = data_x;
		virt_cursor_pos[ view->y_axis_id ] = data_y;
		}

	type = g_app.session.dataset().dimValue( view->variable, view->x_axis_id, data_x, &new_dimval,
			temp_string, &has_bounds, &bound_min, &bound_max, virt_cursor_pos );
	if( type == NC_DOUBLE ) {
		char	dim_str_buf[80];
		if( (xdim != NULL) && xdim->timelike && options.t_conv )
			fmt_time( dim_str_buf, 79, new_dimval, xdim, 1 );
		else
			snprintf( dim_str_buf, 79, "%.7lg", new_dimval );
		xdim_str = dim_str_buf;
		}
	else
		xdim_str = std::string( temp_string, strnlen( temp_string, 79 ) );

	type = g_app.session.dataset().dimValue( view->variable, view->y_axis_id, data_y, &new_dimval,
			temp_string, &has_bounds, &bound_min, &bound_max, virt_cursor_pos );
	if( type == NC_DOUBLE ) {
		char	dim_str_buf[80];
		if( (ydim != NULL) && ydim->timelike && options.t_conv )
			fmt_time( dim_str_buf, 79, new_dimval, ydim, 1 );
		else
			snprintf( dim_str_buf, 79, "%.7lg", new_dimval );
		ydim_str = dim_str_buf;
		}
	else
		ydim_str = std::string( temp_string, strnlen( temp_string, 79 ) );

	snprintf( current_value_label, 499, "Current: (i=%1zu, j=%1zu) %g (x=%s, y=%s)\n",
				data_x, data_y, val, xdim_str.c_str(), ydim_str.c_str() );
	g_app.ui->in_set_label( Label::DataValue, current_value_label );
}

/**************************************************************************************/
	void
ViewerController::setMinFromCurdata()
{
	std::unique_ptr<ViewState> &view = session_.activeView();
	size_t	data_x, data_y, x_size, y_size;
	int	x, y;
	float	val;

	// See plotXY()'s comment: ImageView accepts Ctrl-click before any
	// variable is selected, unlike upstream's canvas widget.
	if( view == NULL )
		return;

	/* Phase 13b: the Ctrl-click twin of plotXY()'s guard. Indexes size[]
	 * by both display axes and samples view->data, neither of which
	 * exists while a 1-D variable is selected -- and fillViewData() just
	 * below would itself write through size[y_axis_id] first. */
	if( ! view->has2dImage() )
		return;

	if( view->data_status == ViewDataStatus::Invalid ) {
		view->fillViewData();
		view->data_status = ViewDataStatus::Valid;
		}

	g_app.ui->in_query_pointer_position( &x, &y );
	mouse_xy_to_data_xy( x, y, options.blowup, &data_x, &data_y );

	x_size = view->variable->size[view->x_axis_id];
	y_size = view->variable->size[view->y_axis_id];

	/* Make sure we don't go outside the limits */
	data_x = ( (data_x >= x_size ) ? x_size-1 : data_x );
	data_y = ( (data_y >= y_size ) ? y_size-1 : data_y );

	/* Invert Y because the reporting counts from the
	 * UPPER LEFT, not the lower left like we want
	 * it to.  If the *picture* is inverted, don't flip
	 * y!
	 */
	if( !options.invert_physical )
		data_y = y_size - data_y - 1;
	
	/* Get the value of the data field under the cursor */
	val = view->data[data_x + data_y*x_size];

	view->variable->user_min = val;
	view->setRangeLabels( val, view->variable->user_max );
	view->initSaveframes();
	draw( true, false ); /* 'true' because we just invalidated saveframes */

	recomputeColorbar();
}

/**************************************************************************************/
	void
ViewerController::setMaxFromCurdata()
{
	std::unique_ptr<ViewState> &view = session_.activeView();
	size_t	data_x, data_y, x_size, y_size;
	int	x, y;
	float	val;

	// See plotXY()'s comment: ImageView accepts Ctrl-click before any
	// variable is selected, unlike upstream's canvas widget.
	if( view == NULL )
		return;

	/* Phase 13b: the Ctrl-click twin of plotXY()'s guard. Indexes size[]
	 * by both display axes and samples view->data, neither of which
	 * exists while a 1-D variable is selected -- and fillViewData() just
	 * below would itself write through size[y_axis_id] first. */
	if( ! view->has2dImage() )
		return;

	if( view->data_status == ViewDataStatus::Invalid ) {
		view->fillViewData();
		view->data_status = ViewDataStatus::Valid;
		}

	g_app.ui->in_query_pointer_position( &x, &y );
	mouse_xy_to_data_xy( x, y, options.blowup, &data_x, &data_y );

	x_size = view->variable->size[view->x_axis_id];
	y_size = view->variable->size[view->y_axis_id];

	/* Make sure we don't go outside the limits */
	data_x = ( (data_x >= x_size ) ? x_size-1 : data_x );
	data_y = ( (data_y >= y_size ) ? y_size-1 : data_y );

	/* Invert Y because the reporting counts from the
	 * UPPER LEFT, not the lower left like we want
	 * it to.  If the *picture* is inverted, don't flip
	 * y!
	 */
	if( !options.invert_physical )
		data_y = y_size - data_y - 1;
	
	/* Get the value of the data field under the cursor */
	val = view->data[data_x + data_y*x_size];

	view->variable->user_max = val;
	view->setRangeLabels( val, view->variable->user_max );
	view->initSaveframes();
	draw( true, false ); /* 'true' because we just invalidated saveframes */

	recomputeColorbar();
}

/**************************************************************************************
 * Plot all the data along the plot_XY_axis for this (X,Y) position.  This is
 * the routine called when the user selects an (X,Y) position by clicking on
 * the 2-D color contour window.  
 */
	void
ViewerController::plotXY()
{
	std::unique_ptr<ViewState> &view = session_.activeView();
	int	X_axis, i, x_window, y_window;
	size_t	data_x, data_y, x_size, y_size, n;

	// Upstream's Xt canvas widget never has this wired up to a live click
	// until a variable is actually being displayed, so 'view' being NULL
	// here never came up there. Our ImageView is a plain Fl_Widget that
	// accepts clicks from the moment the window is shown -- clicking the
	// still-empty 2-D pane before selecting any variable reaches this
	// function with view == NULL and crashed (segfault dereferencing
	// view->plot_XY_axis) instead of upstream's implicit no-op.
	if( view == NULL )
		return;

	/* Phase 13b: reached from a plain left click on the 2-D pane
	 * (ui/src/main_window.cc). reportPosition() below already refuses the
	 * same situation for a mouse *hover*, with a comment naming it
	 * exactly -- "click on a 2-d variable, then click on a 1-d variable,
	 * then move the pointer back over the displayed colormap of the (old)
	 * 2-d variable" -- but the click path was never given the same guard,
	 * and start[view->y_axis_id] = data_y below is then an out-of-bounds
	 * heap WRITE, not just a bad read. Silent: a click on a pane that
	 * shows a stale picture is not an error the user needs told about. */
	if( ! view->has2dAxes() )
		return;

	X_axis = view->plot_XY_axis;
	if( X_axis == -1 ) {
		in_error( "Error! I have no valid axis to plot along!\n" );
		return;
		}

	if( options.debug )
		fprintf( stderr, "plot_XY: entering with axisid=%d\n", X_axis );

	g_app.ui->in_query_pointer_position( &x_window, &y_window );
	if( (x_window < 0) || (y_window < 0) ) {
		fprintf( stderr, "OUT OF WINDOW!!\n" );
		return;
		}

	mouse_xy_to_data_xy( x_window, y_window, options.blowup, &data_x, &data_y );

	x_size = view->variable->size[view->x_axis_id];
	y_size = view->variable->size[view->y_axis_id];

	/* Make sure we don't go outside the limits */
	data_x = ( (data_x >= x_size ) ? x_size-1 : data_x );
	data_y = ( (data_y >= y_size ) ? y_size-1 : data_y );

	/* Invert Y because the reporting counts from the
	 * UPPER LEFT, not the lower left like we want
	 * it to.  If the *picture* is inverted, don't flip
	 * y!
	 */
	if( !options.invert_physical )
		data_y = y_size - data_y - 1;

	std::vector<size_t> start( view->variable->n_dims );
	std::vector<size_t> count( view->variable->n_dims );

	/* Compute start and count arrays for data to plot.  Note that
	 * the ordering of the following lines is important.  We first
	 * set to the base variable place.  We then insert the place
	 * in the window that was clicked upon.  We then set the X
	 * axis to have a full count of all elements be plotted.
	 */
        n = view->variable->size[X_axis];
	for( i=0; i<view->variable->n_dims; i++ ) {
		start[i] = view->var_place[i];
		count[i] = 1L;
		}
	start[view->x_axis_id] = data_x;
	start[view->y_axis_id] = data_y;

	/* Handle the lines on the plot.
	 */
	view->plot_XY_nlines++;
	if( view->plot_XY_nlines > MAX_LINES_PER_PLOT )
		view->plot_XY_nlines = 1;

	/* Save position so we can later replot if X axis changes
	 */
	for( i=0; i<view->variable->n_dims; i++ ) {
		view->plot_XY_position[ view->plot_XY_nlines-1 ][i] = start[i];
		if( options.debug )
			fprintf( stderr, "Setting position for line %d, dim %d: %zu\n",
				view->plot_XY_nlines-1, i, start[i] );
		}

	start[X_axis] = 0L;
	count[X_axis] = n;

	if( options.debug )
		fprintf( stderr, "plot_XY: about to call plot_XY_sc\n" );
	view->plotXYSc( start.data(), count.data() );

	if( options.debug )
		fprintf( stderr, "plot_XY: exiting\n" );
}

/***************************************************************************/
void ViewerController::recomputeColorbar( void )
{
	std::unique_ptr<ViewState> &view = session_.activeView();
	/* The user might ask to rearrange colormaps before any
	 * variable is selected. In that event, return
	 * immediately
	 */
	if( (view == NULL) || (view->variable == NULL))
		return;

	if( options.debug ) {
		fprintf( stderr, "view_recompute_colorbar: entering\n" );
		fprintf( stderr, "view_recompute_colorbar: about to call x_create_colorbar with user_min=%f user_max=%f transform=%d\n",
				view->variable->user_min, view->variable->user_max, static_cast<int>(options.transform) );
		}

	g_app.ui->x_create_colorbar( view->variable->user_min, view->variable->user_max, options.transform );

	if( options.debug )
		fprintf( stderr, "view_recompute_colorbar: about to call x_draw_colorbar" );
	g_app.ui->x_draw_colorbar();

	if( options.debug )
		fprintf( stderr, "view_recompute_colorbar: exiting\n" );
}

/******************************************************************************
 * Set the style of blowup we want to do. Formerly util.cc's
 * set_blowup_type() (Phase 4b, "refine the architecture" plan): stays a
 * free function (no natural View/ViewerController "this" -- it's called
 * from both ViewerController::blowupType() below and view.cc's
 * set_scan_variable(), neither of which is the sole owner), moved here
 * since ViewerController::blowupType() is its primary caller.
 */
	void
set_blowup_type( BlowupType new_type, ViewerUi &ui )
{
	if( new_type == BlowupType::Replicate )
		ui.in_set_label( Label::BlowupType, "Repl"   );
	else
		ui.in_set_label( Label::BlowupType, "Bi-lin" );

	options.blowup_type = new_type;
}
