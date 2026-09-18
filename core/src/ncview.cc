/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 1993 through 2024 David W. Pierce
 * Modifications Copyright (C) 2026 Dominik Strebel
 *
 * This program  is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 3, as 
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

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/frame_cache.h"
#include "ncview/protos.h"

/* Program defaults in a easy-to-find place */
#define DEFAULT_INVERT_PHYSICAL	false
#define DEFAULT_INVERT_COLORS	false
#define DEFAULT_BLOWUP		1
#define DEFAULT_MIN_MAX_METHOD	MinMaxMethod::Fast
#define DEFAULT_N_COLORS	200
#define DEFAULT_PRIVATE_CMAP	false
#define DEFAULT_BLOWUP_TYPE	BlowupType::Bilinear
#define DEFAULT_SHRINK_METHOD	ShrinkMethod::Mean
#define DEFAULT_SAVEFRAMES	true
#define DEFAULT_NO_AUTOFLIP	false
#define DEFAULT_LISTSEL_MAX	40
#define DEFAULT_COLOR_BY_NDIMS	true
#define DEFAULT_AUTO_OVERLAY	true

NcviewApp g_app;
Options	  options( g_app.session );
Dataset   &g_dataset = g_app.session.dataset();
std::vector<std::unique_ptr<NCVar>> &variables = g_dataset.variablesMutable();
std::vector<ncv_pixel> &pixel_transform = g_app.session.pixelTransform();
FrameCache &framestore = g_app.session.frameCache();
Stringlist *read_in_state;

static int any_var_in_group( const std::vector<std::unique_ptr<NCVar>> &vars );

/***********************************************************************************************/
	int
ncview_main( int argc, char **argv, ViewerUi &ui )
{
	Stringlist *input_files, *state_to_save;
	int	   err, found_state_file;

	/* Initialize misc constants */
	initialize_misc();

	/* Read in our state file from a previous run of ncview
	 */
	read_in_state = NULL;	/* Note: a global var. Set to null to flag following routine to make a new stringlist */
	err = read_state_from_file( &read_in_state );
	if( err == 0 )
		found_state_file = true;
	else
		found_state_file = false;

	ui.in_parse_args             ( &argc, argv );
	input_files = parse_options ( argc,  argv );	/* This parses ALL the non-X11 command line options, not just the input files */

	/* No files given on the command line -- ask the user via a native
	 * file-open dialog instead of just erroring out below. This is what
	 * makes launching ncview with no arguments (double-click, dock icon,
	 * "Open with") usable instead of a dead end.
	 */
	if( stringlist_len( input_files ) == 0 )
		input_files = ui.in_choose_input_files();

	if( stringlist_len( input_files ) == 0 ) {
		fprintf( stderr, "ncview: no input files given; exiting.\n" );
		exit( 0 );
		}

	determine_file_type         ( input_files );

	options.blowup       = 1;

	/* this routine sets up the 'variables' structure */
	initialize_file_interface   ( input_files );

	if( n_vars_in_list( g_app.session.dataset().variablesMutable() ) == 0 ) {
		fprintf( stderr, "no displayable variables found!\n" );
		exit( -1 );
		}

	/* If any vars are in groups, we build the interface differently.
	 * I pass this information through the global "options" struct.
	 */
	if( any_var_in_group( g_app.session.dataset().variablesMutable() )) {
		options.enable_group_sel = true;
		options.varsel_style = VarselStyle::Menu;
		}
	else
		options.enable_group_sel = false;

	/* This initializes the colormaps, and then the X widows system */
	if( options.debug ) printf( "Initializing display interface...\n" );
	initialize_display_interface( ui );
	if( options.debug ) printf( "Initializing printing subsystem...\n" );
	print_init();
	if( options.debug ) printf( "Initializing overlays...\n" );
	overlay_init();

	/* If there is only one variable, make it the active one */
	if( n_vars_in_list( g_app.session.dataset().variablesMutable() ) == 1 ) {
		/* set_scan_variable     ( variables       ); */
		ui.in_indicate_active_var( const_cast<char *>(g_app.session.dataset().variablesMutable()[0]->name.c_str()) );
		}

	/* If we didn't find a state file (".ncviewrc") when we started up, then
	 * write a new one out now that we are all initialized
	 */
	if( found_state_file == false ) {
		state_to_save = get_persistent_state( ui );
		if( (err = write_state_to_file( state_to_save )) != 0 ) {
			fprintf( stderr, "Error %d while trying to save options file \"$HOME/.ncviewrc\".\n", err );
			}
		stringlist_delete_entire_list( state_to_save );
		}

	process_user_input( ui );

	return(0);
}

/***********************************************************************************************/
/* Every options field and framestore default this port sets before first use,
 * split out from initialize_misc() below so it can be re-run on its own
 * (see tests/support/session_fixture.h) without repeating
 * udu_utinit(NULL) -- test_udunits_helper.h documents why calling that a
 * second time silently breaks every already-cached ut_unit comparison.
 * Idempotent and side-effect-free otherwise (no I/O, no process-global
 * state besides options/framestore themselves), so safe to call as often
 * as needed.
 */
	void
reset_session_defaults()
{
	options.invert_physical  = DEFAULT_INVERT_PHYSICAL;
	options.invert_colors    = DEFAULT_INVERT_COLORS;
	options.blowup           = DEFAULT_BLOWUP;
	options.shrink_method    = DEFAULT_SHRINK_METHOD;
	options.min_max_method   = DEFAULT_MIN_MAX_METHOD;
	options.transform        = Transform::None;
	options.n_colors 	 = DEFAULT_N_COLORS;
	options.n_extra_colors 	 = 10;
	options.private_colormap = DEFAULT_PRIVATE_CMAP;
	options.debug		 = false;
	options.show_sel	 = false;
	options.want_extra_info  = false;
	options.beep_on_restart  = false;
	options.stop_on_restart  = false;
	options.small  		 = false;
	options.blowup_type      = DEFAULT_BLOWUP_TYPE;
	options.save_frames      = DEFAULT_SAVEFRAMES;
	options.no_autoflip      = DEFAULT_NO_AUTOFLIP;
	options.t_conv      	 = true;
	options.varsel_style	 = VarselStyle::List;
	options.dump_frames	 = false;
	options.listsel_max	 = DEFAULT_LISTSEL_MAX;
	options.color_by_ndims	 = DEFAULT_COLOR_BY_NDIMS;
	options.auto_overlay	 = DEFAULT_AUTO_OVERLAY;
	options.autoscale	 = false;
	options.scale		 = 1.e30;	/* This val means do NOT do any user scaling of data */
	options.offset		 = 1.e30;	/* This val means do NOT do any user offset of data */

	options.overlay          = std::make_unique<OverlayOptions>();
	options.overlay->doit    = false;

	options.maxsize_pct	 = 75;	/* maximum size of a window, in percent of screen, before switching to scrollbars */

	/* Set default color to use for missing data */
	options.missval_r 	= 255;
	options.missval_g 	= 255;
	options.missval_b 	= 255;

	g_app.session.frameCache() = FrameCache();

}

/***********************************************************************************************/
	void
initialize_misc()
{
	print_disclaimer();

	udu_utinit( NULL );
	reset_session_defaults();
}

/***********************************************************************************************/
	void
initialize_file_interface( Stringlist *input_files )
{
	int	idim, nvars;

	if( options.debug )
		printf( "Initializing file interface...\n" );

	if( input_files != NULL )
		for( auto &f : *input_files )
			fi_initialize( (char *)f.string.c_str() );
	if( options.debug )
		printf( "...calculating dim min & maxes...\n" );
	g_app.session.dataset().calcDimMinmaxes();

	/* Get the effective dimensionality of all the vars.
	 * Can't do this before we have read in all of the
	 * input file.
	 */
	nvars = 0;
	for( auto &var_owner : g_app.session.dataset().variablesMutable() ) {
		NCVar *var = var_owner.get();
		nvars++;
		var->effective_dimensionality = 0;
		for( idim=0; idim<var->n_dims; idim++ ) {
			if( var->size[idim] > 1 )
				var->effective_dimensionality++;
			if( options.debug ) {
				/* var->dim[idim] is null for a non-scannable
				 * (singleton) dimension -- see util.cc's
				 * fill_dim_structs(), "Indicate non-scannable
				 * dimensions by a null entry". A pre-existing
				 * upstream bug unconditionally dereferenced it
				 * here too. */
				if( var->dim[idim] != nullptr )
					printf( "var %s has %d dims, dim %d: >%s< len %zu\n",
						var->name.c_str(), var->n_dims, idim,
						var->dim[idim]->name.c_str(), var->dim[idim]->size );
				else
					printf( "var %s has %d dims, dim %d: (non-scannable, no dim struct)\n",
						var->name.c_str(), var->n_dims, idim );
				}
			}
		if( options.debug ) {
			printf( "variable %s had effective_dimensionality of %d\n",
				var->name.c_str(), var->effective_dimensionality );
			}
		}

	/* Now that we have read in all the files, we can
	 * gather any scalar coordinate information (which
	 * might possibly change in each file)
	 */
	g_app.session.dataset().cacheScalarCoordInfo();

	if( nvars > options.listsel_max )
		options.varsel_style = VarselStyle::Menu;

	if( options.debug ) 
		printf( "Done initializing file interface...\n" );
}

/***********************************************************************************************/
	void
initialize_display_interface( ViewerUi &ui )
{
	/* Upstream allocated and filled this identity/remap table in
	 * interface/colormap_funcs.c's x_create_colormap() -- the X11
	 * colorcell-allocation file this port intentionally doesn't carry over
	 * (FLTK expands a pixel index straight to RGB, the same as upstream's
	 * own TrueColor branch there: pixel_transform[i] = i). Without this,
	 * util.cc:data_to_pixels() unconditionally dereferences a NULL
	 * pixel_transform for the first missing/fill-value pixel it sees.
	 */
	g_app.session.pixelTransform().resize( options.n_colors+options.n_extra_colors );
	for( int i=0; i<options.n_colors+options.n_extra_colors; i++ )
		g_app.session.pixelTransform()[i] = (ncv_pixel)i;

	initialize_colormaps();

	/* Make the colormaps in the program congruent in order
	 * and "enabled-ness" with the read-in state
	 */
	ui.x_check_legal_colormap_loaded();

	if( options.debug ) printf( "...initializing X interface\n" );
	ui.in_initialize();
	if( options.debug ) printf( "...done with initializing X interface\n" );
}

/***********************************************************************************************/
	void
process_user_input( ViewerUi &ui )
{
	/* This call never returns, as it loops, handling user interface events */
	ui.in_process_user_input();
}

/***********************************************************************************************/
/* Only correct exit point for 'ncview' */
	void
quit_app()
{
	exit( 0 );
}

/***********************************************************************************************/
	int
check( int val, int min, int max )
{
	if( (val >= min) && (val <= max) )
		return( 0 );
	else
		return( -1 );
}

/***********************************************************************************************/
/* Make a quickie colormap in case none is found */
	void
create_default_colormap()
{
	ncv_pixel	r[256], g[256], b[256];
	int		i;

	for( i=0; i<256; i++ ) {
		r[i] = i;
		g[i] = 255 - abs(i-128);
		b[i] = 255-i;
		}

	in_create_colormap( "default", r, g, b );
}

/***********************************************************************************************/
int any_var_in_group( const std::vector<std::unique_ptr<NCVar>> &vars ) {

	for( const auto &cursor : vars )
		if( count_nslashes( cursor->name.c_str() ) > 0 )
			return( 1 );

	return( 0 );
}

/***********************************************************************************************/
	void
print_disclaimer()
{
fprintf( stderr, "%s\n", PROGRAM_ID );
fprintf( stderr, "https://cirrus.ucsd.edu/ncview/\n" );
fprintf( stderr, "Copyright (C) 1993 through 2024, David W. Pierce\n" );
fprintf( stderr, "This C++/FLTK port, Copyright (C) 2026 Dominik Strebel\n" );
fprintf( stderr, "Ncview comes with ABSOLUTELY NO WARRANTY; for details type `ncview -w'.\n" );
fprintf( stderr, "This is free software licensed under the Gnu General Public License version 3; type `ncview -c' for redistribution details.\n\n" );
}
