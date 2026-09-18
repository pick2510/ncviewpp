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
 * 	colormap_library.cc
 *
 *	Colormap loading, moved out of ncview.cc (Phase 8, "refine the
 *	architecture" plan) as pure file motion -- byte-identical bodies, no
 *	behavior change. initialize_colormaps() and init_cmap_from_file() are
 *	the two externally-declared entry points (ncview/protos.h);
 *	ncview_cmap_suffix()/get_cmaps_from_dir()/init_cmaps_from_data()/
 *	init_cmap_from_data() stay `static` to this file, exactly as they
 *	were static to ncview.cc before the move -- nothing outside this file
 *	called them directly. tests/test_colormaps.cc characterizes the two
 *	public entry points against the unmodified functions before this
 *	move; it deliberately does not (and cannot) call the four static
 *	helpers directly, matching this plan's practice of testing through a
 *	file's public entry points rather than its private implementation.
 */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

/* These hold data for our colormaps */

/* A. Shchepetkin: new colormaps are added here */
#include "ncview/colormaps_bright.h"
#include "ncview/colormaps_banded.h"
#include "ncview/colormaps_rainbow.h"
#include "ncview/colormaps_jaisnb.h"
#include "ncview/colormaps_jaisnc.h"
#include "ncview/colormaps_jaisnd.h"
#include "ncview/colormaps_blu_red.h"
#include "ncview/colormaps_manga.h"
#include "ncview/colormaps_jet.h"
#include "ncview/colormaps_wheel.h"

/* Post-port addition: matplotlib's perceptually-uniform colormap family,
 * added here alongside the other contributed colormaps rather than with
 * the M0-ported originals below. */
#include "ncview/colormaps_viridis.h"
#include "ncview/colormaps_plasma.h"
#include "ncview/colormaps_inferno.h"
#include "ncview/colormaps_magma.h"
#include "ncview/colormaps_cividis.h"

/* the following are original colormaps from ncview */
#include "ncview/colormaps_3gauss.h"
#include "ncview/colormaps_3saw.h"
#include "ncview/colormaps_bw.h"
#include "ncview/colormaps_default.h"
#include "ncview/colormaps_detail.h"
#include "ncview/colormaps_extrema.h"
#include "ncview/colormaps_helix.h"
#include "ncview/colormaps_helix2.h"
#include "ncview/colormaps_hotres.h"
#include "ncview/colormaps_ssec.h"

static void init_cmaps_from_data();
static void init_cmap_from_data( const char *colormap_name, int *data );
static int get_cmaps_from_dir( const char *dir_name );
static int ncview_cmap_suffix( const char *s, int *n_suffix );

/***********************************************************************************************/
	void
initialize_colormaps()
{
	char	*ncview_base_dir;

	/* ncview has a useful set of built-in colormaps.  The set of built-in
	 * colormaps can be augmented by user-specified colormaps that are contained
	 * in simple ASCII files with 256 lines, where each line has 3 entries
	 * (separated by spaces), which indicate the R, B, and G values.  Each
	 * value must be an integer between 0 and 255, inclusive.  User-specified
	 * colormaps are contained in files with the extension of ".ncmap",
	 * and can live in the following places:
	 *  1) NCVIEW_LIB_DIR, which is determined at installation time.
	 *     	A reasonable choice is "/usr/local/lib/ncview".
	 *  2) In a directory named by the environmental variable
	 *	"NCVIEWBASE".
	 *  3) If there is no environmental variable "NCVIEWBASE", then
	 *     	in $HOME.
	 *  4) In the current working directory.
	 */

	/* Get built-in colormaps */
	init_cmaps_from_data();

	/* Get user-specified colormaps, if any */
#ifdef NCVIEW_LIB_DIR
	get_cmaps_from_dir( NCVIEW_LIB_DIR );
#endif
	ncview_base_dir = (char *)getenv( "NCVIEWBASE" );
	if( ncview_base_dir == NULL )
		ncview_base_dir = (char *)getenv( "HOME" );

	if( ncview_base_dir != NULL )
		get_cmaps_from_dir( ncview_base_dir );

	get_cmaps_from_dir( "." );
}

/***********************************************************************************************/
/* Returns 0 if the passed char string "s" has a valid ncview cmap file suffix, and 1 otherwise.
 * If a 0 is returned, then n_suffix is also set to the suffix length, including the period.
 */
	int
ncview_cmap_suffix( const char *s, int *n_suffix )
{
	int		nc;

	if( s == NULL )
		return( 1 );

	nc = strlen( s );
	if( nc < 5 )
		return( 1 );

	if( strncasecmp( (s+nc-4), ".ncm", 4 ) == 0 ) {
		*n_suffix = 4;
		return( 0 );
		}

	if( nc < 7 )
		return( 1 );

	if( strncasecmp( (s+nc-6), ".ncmap", 6 ) == 0 ) {
		*n_suffix = 6;
		return( 0 );
		}

	return( 1 );
}

/***********************************************************************************************/
	int
get_cmaps_from_dir( const char *dir_name )
{
	DIR		*ncdir = NULL;
	struct dirent	*dir_entry;
	int		n_colormaps = 0, n_suffix;

	if( options.debug )
		printf( "Getting colormaps from dir >%s<\n", dir_name );

	ncdir     = opendir( dir_name );
	if( ncdir == NULL )
		return( 0 );
	dir_entry = readdir( ncdir    );
	while( dir_entry != NULL ) {
		/* Additional allowed filenames as per suggestion by
		 * Arlindo da Silva for his Windows port
		 */
		if( ncview_cmap_suffix( dir_entry->d_name, &n_suffix ) == 0) {
			init_cmap_from_file( dir_name, dir_entry->d_name, n_suffix );
			n_colormaps++;
			}
		dir_entry = readdir( ncdir );
		}
	closedir( ncdir );

	return( n_colormaps );
}

/***********************************************************************************************/
	void
init_cmaps_from_data()
{
/* the following are original colormaps from ncview */

	init_cmap_from_data( "3gauss",  cmap_3gauss  );
	init_cmap_from_data( "detail",  cmap_detail  );
	init_cmap_from_data( "ssec",    cmap_ssec    );

/* A. Shchepetkin: new colormaps are added here */

        init_cmap_from_data( "bright",  cmap_bright  );
        init_cmap_from_data( "banded",  cmap_banded  );
        init_cmap_from_data( "rainbow", cmap_rainbow );
        init_cmap_from_data( "jaisnb",  cmap_jaisnb  );
        init_cmap_from_data( "jaisnc",  cmap_jaisnc  );
        init_cmap_from_data( "jaisnd",  cmap_jaisnd  );
        init_cmap_from_data( "blu_red", cmap_blu_red );
        init_cmap_from_data( "manga",   cmap_manga   );
        init_cmap_from_data( "jet",     cmap_jet     );
        init_cmap_from_data( "wheel",   cmap_wheel   );
        init_cmap_from_data( "viridis", cmap_viridis );
        init_cmap_from_data( "plasma",  cmap_plasma  );
        init_cmap_from_data( "inferno", cmap_inferno );
        init_cmap_from_data( "magma",   cmap_magma   );
        init_cmap_from_data( "cividis", cmap_cividis );

/* the following are the rest of the original colormaps from ncview */

	init_cmap_from_data( "3saw",    cmap_3saw    );
	init_cmap_from_data( "bw",      cmap_bw      );
	init_cmap_from_data( "default", cmap_default );
	init_cmap_from_data( "extrema", cmap_extrema );
	init_cmap_from_data( "helix",   cmap_helix   );
	init_cmap_from_data( "helix2",  cmap_helix2  );
	init_cmap_from_data( "hotres",  cmap_hotres  );
}

/***********************************************************************************************/
	void
init_cmap_from_data( const char *colormap_name, int *data )
{
	int	i;
	unsigned char r[256], g[256], b[256];

	if( options.debug )
		printf( "    ... initting cmap >%s< from supplied data\n", colormap_name );

	for( i=0; i<256; i++ ) {
		r[i] = (unsigned char)data[i*3+0];
		g[i] = (unsigned char)data[i*3+1];
		b[i] = (unsigned char)data[i*3+2];
		}

	in_create_colormap( colormap_name, r, g, b );
}

/***********************************************************************************************/
	void
init_cmap_from_file( const char *dir_name, const char *file_name, int n_suffix )
{
	char 	*colormap_name;
	FILE	*cmap_file;
	int	i, nentries, r_entry, g_entry, b_entry;
	char	line[ 128 ], *long_file_name;
	unsigned char r[256], g[256], b[256];
	size_t	slen;

	if( options.debug )
		printf( "    ... initting cmap >%s<\n", file_name );

	/* Colormap name is the file name without the '.ncmap' or '.ncm' extension */
	std::vector<char> colormap_name_buf( strlen(file_name)-(n_suffix-1) );
	colormap_name = colormap_name_buf.data();
	/* colormap_name_buf is sized to exactly fit this copy plus the NUL
	 * written just below, so this can't truncate; memcpy (rather than
	 * strncpy) avoids -Wstringop-truncation's "bound depends on the
	 * length of the source" heuristic, which can't see that. */
	memcpy( colormap_name, file_name, strlen(file_name)-n_suffix );
	*(colormap_name + strlen(file_name)-n_suffix) = '\0';

	/* Make sure this colormap name isn't already known */
	if( x_seen_colormap_name( colormap_name )) {
		if( options.debug )
			printf( "Already have a colormap named >%s<, not NOT inittig colormap from file >%s<\n",
				colormap_name, file_name);
		return;
		}

	/* Read in the r, g, b values */
	slen = strlen(file_name) + strlen(dir_name) + 5;  /* add space for intermediate slash and trailing NULL */
	std::vector<char> long_file_name_buf( slen );  /* add space for intermediate slash and trailing NULL */
	long_file_name = long_file_name_buf.data();
	snprintf( long_file_name, slen, "%s/%s", dir_name, file_name );
	if( (cmap_file = fopen( long_file_name, "r" )) == NULL ) {
		fprintf( stderr, "ncview++: init_cmap_from_file: error " );
		fprintf( stderr, "opening file %s\n", long_file_name );
		return;
		}
	for( i=0; i<256; i++ ) {
		if( fgets( line, 128, cmap_file ) == NULL ) {
			fprintf( stderr, "ncview++: init_cmap_from_file: file %s finished ",
					long_file_name );
			fprintf( stderr, "on line %d, but should have 256 lines: not a valid ncview cmap file\n", i+1 );
			return;
			}

		nentries = sscanf( line, "%d %d %d", &r_entry, &g_entry, &b_entry );
		if( nentries != 3 ) {
			fprintf( stderr, "ncview++: init_cmap_from_file: incorrect number " );
			fprintf( stderr, "of entries on the line.  Should be 3\n" );
			fprintf( stderr, "file %s, line %d\n", long_file_name, i+1 );
			return;
			}

		if( check( r_entry, 0, 255 ) < 0 ) {
			fprintf( stderr, "ncview++: init_cmap_from_file: first entry (red) " );
			fprintf( stderr, "is outside valid limits of 0 to 255.\n" );
			fprintf( stderr, "file %s, line %d\n", long_file_name, i+1 );
			return;
			}

		if( check( g_entry, 0, 255 ) < 0 ) {
			fprintf( stderr, "ncview++: init_cmap_from_file: second entry (green) " );
			fprintf( stderr, "is outside valid limits of 0 to 255.\n" );
			fprintf( stderr, "file %s, line %d\n", long_file_name, i+1 );
			return;
			}

		if( check( b_entry, 0, 255 ) < 0 ) {
			fprintf( stderr, "ncview++: init_cmap_from_file: third entry (blue) " );
			fprintf( stderr, "is outside valid limits of 0 to 255.\n" );
			fprintf( stderr, "file %s, line %d\n", long_file_name, i+1 );
			return;
			}
		r[i] = (unsigned char)r_entry;
		g[i] = (unsigned char)g_entry;
		b[i] = (unsigned char)b_entry;
		}

	in_create_colormap( colormap_name, r, g, b );
}
