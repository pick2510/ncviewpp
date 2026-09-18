/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 1993 through 2024  David W. Pierce
 * Modifications Copyright (C) 2026 Dominik Strebel
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
 * davidwilliampierce@gmail.com
 */

/*******************************************************************************
 * 	cli_options.cc
 *
 *	parse_options(): command-line argument parsing, moved out of
 *	ncview.cc (Phase 8, "refine the architecture" plan) as pure file
 *	motion -- byte-identical body, no behavior change. Still writes
 *	directly into the global `options` (ncview/defines.h's Options
 *	struct, whose fields are reference members bound to storage inside
 *	g_app.session -- see viewer_session.h's header comment), rather than
 *	returning a populated StartupSettings: `options.` is referenced 242
 *	more times across 10 other core/src files, so redesigning this
 *	function's output shape is a project-wide API change, not something
 *	a file-boundary move should absorb. tests/test_cli_options.cc
 *	characterizes this against the unmodified function before this move.
 */
#include <cstdio>
#include <cstring>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

/***********************************************************************************************/
	Stringlist *
parse_options( int argc, char *argv[] )
{
	int	i, n;
	Stringlist *file_list = NULL;
	char	bufr[1000], *comma_ptr;

	for( i=1; i<argc; i++ ) {
		if( argv[i][0] == '-' ) {
			/* found an entry that is in option syntax */
			if( strncmp( argv[i], "-min", 4 ) == 0 ) {

				if( i == (argc-1) ) {
					fprintf( stderr, "Error, -minmax argument must be followed by one of these: fast med slow all\n" );
					exit(-1);
					}

				if( strncmp( argv[i+1], "fast", 4 ) == 0 ) {
					options.min_max_method  = MinMaxMethod::Fast;
					i++;
					}
				else if( strncmp( argv[i+1], "med", 3 ) == 0 ) {
					options.min_max_method  = MinMaxMethod::Med;
					i++;
					}
				else if( strncmp( argv[i+1], "slow", 4 ) == 0 ) {
					options.min_max_method  = MinMaxMethod::Slow;
					i++;
					}
				else if( strncmp( argv[i+1], "exh", 3 ) == 0 ) {
					options.min_max_method  = MinMaxMethod::Exhaust;
					i++;
					}
				else if( strncmp( argv[i+1], "all", 3 ) == 0 ) {
					options.min_max_method  = MinMaxMethod::Exhaust;
					i++;
					}
				else
					{
					fprintf( stderr, "unrecognizied option: %s %s\n",
						argv[i], argv[i+1] );
					/* doesn't return */
					useage();
					}
				}

			else if( strncmp( argv[i], "-cal", 4 ) == 0 ) {
				if( i == (argc-1) ) {
					fprintf( stderr, "Error, -cal must be followed by a calendar name\n" );
					exit(-1);
					}
				options.calendar = argv[i+1];
				i++;
				}

			else if( strncmp( argv[i], "-w", 2 ) == 0 ) {
				print_no_warranty();
				exit( 0 );
				}

			else if( strncmp( argv[i], "-pri", 4 ) == 0 )
				options.private_colormap = true;

			else if( strncmp( argv[i], "-deb", 4 ) == 0 )
				options.debug = true;

			else if( strncmp( argv[i], "-beep", 5 ) == 0 )
				options.beep_on_restart = true;

			else if( strncmp( argv[i], "-pause_on_restart", 17 ) == 0 )
				options.stop_on_restart = true;

			else if( strncmp( argv[i], "-fra", 4 ) == 0 )
				options.dump_frames = true;

			else if( strncmp( argv[i], "-small", 6 ) == 0 )
				options.small = true;

			else if( strncmp( argv[i], "-ext", 4 ) == 0 )
				options.want_extra_info = true;

			else if( strncmp( argv[i], "-mti", 3 ) == 0 )
				/* -mtitle's argument was stored into options.window_title,
				 * a field that nothing ever read (removed as dead code) --
				 * this flag has been a documented no-op since at least the
				 * start of this port. Still consume its argument so later
				 * flags parse correctly. */
				i++;

			else if( strncmp( argv[i], "-noauto", 7 ) == 0 )
				options.no_autoflip = true;

			else if( strncmp( argv[i], "-no1d", 5 ) == 0 )
				options.no_1d_vars = true;

			else if( strncmp( argv[i], "-show_sel", 9 ) == 0 )
				options.show_sel = true;

			else if( strncmp( argv[i], "-no_char_dim", 12 ) == 0 )
				options.no_char_dims = true;

			else if( strncmp( argv[i], "-notconv", 8 ) == 0 )
				options.t_conv = false;

			else if( strncmp( argv[i], "-shrink_mode", 12) == 0 )
				options.shrink_method = ShrinkMethod::Mode;

			else if( strncmp( argv[i], "-repl", 5) == 0 )
				/* Pre-existing quirk, preserved: this sets options.blowup (the
				 * blowup magnitude), not options.blowup_type, even though the
				 * value 1 here is BlowupType::Replicate's numeric value -- see
				 * docs/modernization.md's Phase 1 follow-up notes. */
				options.blowup = 1;

			else if( strncmp( argv[i], "-c", 2 ) == 0 ) {
				print_copying();
				exit( 0 );
				}

			else if( strncmp( argv[i], "-no_color_ndims", 7 ) == 0 ) {
				options.color_by_ndims = false;
				}

			else if( strncmp( argv[i], "-no_auto_overlay", 7 ) == 0 ) {
				options.auto_overlay = false;
				}

			else if( strncmp( argv[i], "-autoscale", 9 ) == 0 ) {
				options.autoscale = true;
				}

			else if( strncmp( argv[i], "-scale", 6 ) == 0 ) {
				if( i == (argc-1) ) {
					fprintf( stderr, "Error, -scale must be followed by a number\n" );
					exit(-1);
					}
				sscanf( argv[i+1], "%f", &(options.scale) );
				i++;
				}

			else if( strncmp( argv[i], "-offset", 7 ) == 0 ) {
				if( i == (argc-1) ) {
					fprintf( stderr, "Error, -offset must be followed by a number\n" );
					exit(-1);
					}
				sscanf( argv[i+1], "%f", &(options.offset) );
				i++;
				}

			else if( strncmp( argv[i], "-listsel_max", 7 ) == 0 ) {
				if( i == (argc-1) ) {
					fprintf( stderr, "Error, -listsel_max must be followed by an integer\n" );
					exit(-1);
					}
				sscanf( argv[i+1], "%d", &(options.listsel_max) );
				i++;
				}

			else if( strncmp( argv[i], "-missvalrgb", 11 ) == 0 ) {
				if( i > (argc-4) ) {
					fprintf( stderr, "Error, -missvalrgb must be followed by three integers (r g b)\n" );
					exit(-1);
					}
				sscanf( argv[i+1], "%d", &(options.missval_r) );
				i++;
				sscanf( argv[i+1], "%d", &(options.missval_g) );
				i++;
				sscanf( argv[i+1], "%d", &(options.missval_b) );
				i++;
				}

			else if( strncmp( argv[i], "-nc", 3 ) == 0 ) {
				if( i == (argc-1) ) {
					fprintf( stderr, "Error, -nc must be followed by an integer\n" );
					exit(-1);
					}
				sscanf( argv[i+1], "%d", &(options.n_colors) );
				/* Data colors are stored at pixel indices
				 * [n_extra_colors, n_extra_colors+n_colors) in a
				 * 256-entry (ncv_pixel is a byte) colormap table --
				 * n_colors can be at most 255-n_extra_colors, not
				 * 255, or util.cc's data_to_pixels() computes an
				 * out-of-range index for the brightest data values. */
				if( options.n_colors > (255 - options.n_extra_colors) ) {
					fprintf( stderr, "maximum number of colors is currently %d\n",
						255 - options.n_extra_colors );
					exit( -1 );
					}
				i++;
				}

			else if( strncmp( argv[i], "-max", 4 ) == 0 ) {

				if( i == (argc-1) ) {
					fprintf( stderr, "Error, -maxsize argument must be followed by either a single integer (pct of screen) or two comma separated integers (max width,max height)\n" );
					exit(-1);
					}
				/* See if there is a comma in the following arg */
				i++;
				if( (comma_ptr = strstr( argv[i], "," )) == NULL ) {
					if( (sscanf( argv[i], "%d", &(options.maxsize_pct) ) != 1 ) ||
					    (options.maxsize_pct < 30) ||
					    (options.maxsize_pct > 100)) {
					    	fprintf( stderr, "Error, when the -maxsize arg is followed by a single number, it must be an integer between 30 and 100\n" );
						exit(-1);
						}
					}
				else
					{
					/* parse a width,height pair of ints separated by a comma */
					options.maxsize_pct = -1;	/* flag using width,height rather than pct */
					if( strlen(argv[i]) > 900 ) {
						fprintf( stderr, "Error, string specified in -maxsize too long!\n" );
						exit(-1);
						}
					n = comma_ptr - argv[i];
					strncpy( bufr, argv[i], n );
					bufr[n] = '\0';
					if( (sscanf( bufr, "%d", &(options.maxsize_width) ) != 1 ) ||
					    (options.maxsize_width < 30) ||
					    (options.maxsize_width > 99999)) {
					    	fprintf( stderr, "Error, specified -maxsize width must be an integer between 30 and 100\n" );
						exit(-1);
						}
					snprintf( bufr, sizeof(bufr), "%s", argv[i]+n+1 );
					if( (sscanf( bufr, "%d", &(options.maxsize_height) ) != 1 ) ||
					    (options.maxsize_height < 30) ||
					    (options.maxsize_height > 99999)) {
					    	fprintf( stderr, "Error, specified -maxsize height must be an integer between 30 and 100\n" );
						exit(-1);
						}
					}
				}

			else	/* put other options here */
				{
				fprintf( stderr, "unrecognizied option: %s\n", argv[i] );
				/* doesn't return */
				useage();
				}
			}
		else /* found an entry which is NOT in option syntax -- assume a filename */
			stringlist_add_string( &file_list, argv[i] );
		} /* end of i loop through argv's */

	return( file_list );
}
