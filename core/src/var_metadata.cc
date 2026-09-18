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
 * 	var_metadata.cc
 *
 *	Functions that fill in fields of an already-allocated NCVar* from
 *	netCDF metadata, without touching Dataset's variable list itself:
 *	virt_to_actual_place, handle_dim_mapping(_scalar/_2d), fill_dim_structs,
 *	is_scannable, determine_lat_lon. Moved out of util.cc (Phase 4b,
 *	"refine the architecture" plan) as free functions, NOT Dataset
 *	methods -- ncview/dataset.h's own header comment records a prior,
 *	deliberate design decision to keep exactly these functions free
 *	(called from Dataset::addVariable() the same way anything else calls
 *	them), specifically because they don't touch the variable list
 *	itself. This plan's original inventory called for "private Dataset
 *	methods" here; verifying against that header comment before moving
 *	anything (per this plan's established discipline) found that
 *	assumption wrong, so this file respects the existing decision instead.
 *
 *	Also corrects a second inventory claim: determine_lat_lon() classifies
 *	a coordinate variable's NAME (a lat/lon prefix or substring, falling
 *	back to a bare x/y first letter) -- not a units string, as the plan
 *	described. See tests/test_dim_mapping.cc.
 *******************************************************************************/

#include <vector>

#include "ncview/includes.h"
#include "ncview/dataset.h"	/* NetCDFFile -- file0->dimUnits()/etc. below (Phase 6) */
#include "ncview/defines.h"
#include "ncview/protos.h"

extern Options   options;

static void handle_dim_mapping_scalar( NCVar *v, char *coord_var_name, char *coord_att );
static void handle_dim_mapping_2d( NCVar *v, char *coord_var_name, char *coord_att,
	size_t *coord_var_eff_size, int coord_var_neff_dims, char *orig_coord_att,
	int ncid );
static int  determine_lat_lon( char *s_in, int *is_lat, int *is_lon );

/******************************************************************************
 * Turn a virtual variable 'place' array into a file/place pair.  Which is
 * to say, the virtual size of a variable spans the entries in all the files; 
 * the actual place where the entry for a particular virtual location can
 * be found is in a file/actual_place pair.  This routine does the conversion.
 * Note that this routine is assuming the netCDF convention that ONLY THE
 * FIRST index can be contiguous across files.  The first index is typically
 * the time index in netCDF files.  NOTE! that 'act_pl' must be allocated 
 * before calling this!
 */
	void
virt_to_actual_place( NCVar *var, size_t *virt_pl, size_t *act_pl, FDBlist **file )
{
	FDBlist	*f;
	size_t	v_place, cur_start, cur_end;
	size_t	file_idx;
	int	i, n_dims;

	f       = var->files.front().get();
	n_dims  = f->file->nDims( const_cast<char *>(var->name.c_str()) );
	v_place = *(virt_pl);

	if( v_place >= var->size[0] ) {
		fprintf( stderr, "ncview: virt_to_actual_place: error trying ");
		fprintf( stderr, "to convert the following virtual place to\n" );
		fprintf( stderr, "an actual place for variable %s:\n", var->name.c_str() );
		for( i=0; i<n_dims; i++ )
			fprintf( stderr, "[%1d]: %zu\n", i, *(virt_pl+i) );
		exit( -1 );
		}

	file_idx  = 0;
	cur_start = 0L;
	cur_end   = f->var_size[0] - 1L;
	while( v_place > cur_end ) {
		cur_start += f->var_size[0];
		file_idx++;
		f          = var->files[file_idx].get();
		cur_end   += f->var_size[0];
		}

	*file = f;
	*act_pl = v_place - cur_start;

	/* Copy the rest of the indices over */
	for( i=1; i<n_dims; i++ )
		*(act_pl+i) = *(virt_pl+i);
}

/******************************************************************************
 * Initialize the var->dim_map_info table
 */
	void
handle_dim_mapping( NCVar *v )
{
	int	i, varid, ncid, coord_var_ndims, coord_var_neff_dims;
	size_t	coord_var_eff_size[MAX_NC_DIMS];
	char	*s, orig_coord_att[1024];
	const 	char *delim = " \n\0\t";

	if( options.debug ) printf( "handle_dim_mapping: entering for var %s\n", v->name.c_str() );

	ncid = v->files.front()->id();

	/* dim_map_info itself is never empty of entries. If the var has no coordinate
	 * mappings, then every entry stays a null unique_ptr.
	 */
	v->dim_map_info.clear();
	v->dim_map_info.resize( v->n_dims );

	/* See if this var has a "coordinates" attribute */
	/* coord_att is strtok()'d in place below and its address (with tokens
	 * already split out) is threaded on into handle_dim_mapping_scalar()/
	 * _2d() -- both still take a raw char*, so bridge with strdup() the
	 * same way the old malloc'd netcdf_get_char_att() return did (still
	 * never freed either way, matching prior behavior). */
	std::string coord_att_s = v->files.front()->file->charAtt( v->name, "coordinates" );
	if( coord_att_s.empty() )
		return;
	char *coord_att = strdup( coord_att_s.c_str() );

	snprintf( orig_coord_att, sizeof(orig_coord_att), "%s", coord_att );
	if( options.debug ) printf( "var %s HAS a coordinates attribute: >%s<\n", v->name.c_str(), coord_att );

	/* Check for blank-delimited strings in the coordinates attribute
	 * that name other vars in the file
	 */
	s = strtok( coord_att, delim );
	while( s != NULL ) {

		/* See if this token "s", which came from the coordinates attribute,
		 * is the name of a variable in the file
		 */
		varid = safe_ncvarid( ncid, s );
		if( varid != -1 ) {	/* yes, the token "s" matches the name of a var in the file! */

			/* Right now, I'm only going to try to handle either scalar (0d)
			 * or 2-D mapping dims.  Scalar mapping dims just give an
			 * additional location where the data is valid; for example,
			 * the data might be a 2d field in (lon,lat) and have a
			 * "height" coordinate that tells the height the data is at.
			 * If the dim is more complicated than that, then we simply
			 * ignore the mapping.  In particular, the test WRF output file
			 * I have has a 3-D mapping dim with time as the first time.
			 * Does WRF move the mapping around over time?  Dunno.  In
			 * any event, we will allow it to handle 2 EFFECTIVE dims,
			 * but otherwise, if the mapping var has more than 2 effective
			 * dims, then forget it.
			 */
			/* netcdf_n_dims(), not netcdf_fi_n_dims()/->nDims() --
			 * genuinely different functions (this one doesn't resolve
			 * group-prefixed names), left as a direct call rather than
			 * migrated onto NetCDFFile in Phase 7b: routing it through
			 * ->nDims() would silently switch which lookup runs. */
			coord_var_ndims = netcdf_n_dims( ncid, s );
			size_t *coord_var_size_raw = v->files.front()->file->varSize( s );
			coord_var_neff_dims = 0;
			for( i=0; i<coord_var_ndims; i++ )
				if( coord_var_size_raw[i] > 1 ) {
					coord_var_eff_size[coord_var_neff_dims] = coord_var_size_raw[i];
					coord_var_neff_dims++;
					}
			free( coord_var_size_raw );

			/* These routines are where we do most of the work
			 */
			if( coord_var_neff_dims == 0 )
				handle_dim_mapping_scalar( v, s, coord_att );

			else if( coord_var_neff_dims == 2 )
				handle_dim_mapping_2d( v, s, coord_att, coord_var_eff_size, coord_var_neff_dims,
					orig_coord_att, ncid );

			else
				{
				printf( "Note: the coordinates attribute for variable %s is being ignored,\n", v->name.c_str() );
				printf( "since it specifies a variable (%s) that has %d effective dims (an effective dim has a size greater than 1)\n",
					s, coord_var_neff_dims );
				printf( "I am not set up to handle cases with coordinate mapping using anything other than 0 or 2 effective dims\n" );
				return;
				}
			}
		else
			{
			/* varid == -1, indicating no var of this name was found in the file */
			if( options.debug )
				printf( "Warning: token \"%s\" appears in a coordinates attribute yet is NOT a var in the file\n", s );
			}

		s = strtok( NULL, delim );
		}
}

/**********************************************************************************************/
	static void
handle_dim_mapping_scalar( NCVar *v, char *coord_var_name, char *coord_att )
{
	if( (int)v->scalar_dim_map_info.size() >= MAX_SCALAR_COORDS ) {
		printf( "Note: var %s has exceeded the allowable number of scalar coordinate dimensions, which is %d. Ignoring the rest\n",
			v->name.c_str(), MAX_SCALAR_COORDS);
		return;
		}

	/* Make a new SCALAR dim map info structure to
	 * hold our single value
	 */
	auto tmi_owner = std::make_unique<NCDim_map_info>();
	NCDim_map_info *tmi = tmi_owner.get();

	/* Copy info to the new scalar dim map structure
	 */
	tmi->var_i_map = v;
	tmi->coord_var_name = coord_var_name;
	/* tmi->coord_var_units is printed with a bare "%s" with no NULL guard
	 * in view.cc, so an absent-units result must stay empty here rather
	 * than something that would render as blank text either way -- the
	 * empty-string convention is unambiguous now that this is std::string. */
	tmi->coord_var_units = v->files.front()->file->varUnits( coord_var_name );
	tmi->scalar_all_same = 0;

	/* Same check handle_time_dim() runs for a real dimension's own units --
	 * lets a scalar coordinate like WRF's "XTIME" (units "minutes since
	 * ...") be printed as a calendar date later, instead of the raw
	 * "<value> <units>" string this used to always fall back to. */
	if( udu_utistime( coord_var_name, const_cast<char *>(tmi->coord_var_units.c_str()) ) ) {
		tmi->timelike = 1;
		tmi->calendar = fi_dim_calendar( v->files.front()->id(), coord_var_name );
		}

	/* Add this new scalar dim to the array */
	v->scalar_dim_map_info.push_back( std::move( tmi_owner ) );

	if( options.debug ) printf("Added a new scalar coord to var %s: name=%s count=%zu\n",
		v->name.c_str(), coord_var_name, v->scalar_dim_map_info.size() );

	/* Note that we CANNOT fill out the data values for this "scalar" coord var
	 * yet. The reason is because this var might live in multiple files, and
	 * each file could have a different value of the scalar variable. I.e.,
	 * the scalar coord var could be used to essentially add another unlimited
	 * dimension to the variable. To handle this, we must read in the
	 * scalar values from EACH FILE after we know what all the files this
	 * variable lives in are. That happens in the routine that calls this
	 * one, add_var_to_list
	 */
}

/****************************************************************************************************/
	static void
handle_dim_mapping_2d( NCVar *v, char *coord_var_name, char *coord_att, size_t *coord_var_eff_size,
		int coord_var_neff_dims, char *orig_coord_att, int ncid )
{
	size_t		totsize, start[MAX_NC_DIMS], count[MAX_NC_DIMS];
	int		i, n_matches, err, is_lat, is_lon, idx_lat_dim, idx_lon_dim,
			must_be_left_of, j;

	/* Make new, uninitialized dim_map_info structure. Ownership transfers into
	 * v->dim_map_info[i] below on every path except an early "abandon mapping"
	 * return, where the local unique_ptr cleans it up automatically. */
	auto map_info_owner = std::make_unique<NCDim_map_info>();
	NCDim_map_info *map_info = map_info_owner.get();

	/* Copy over the coordinate attribute for posterity */
	map_info->coord_att = coord_att;

	/* This is the "variable that I map" */
	map_info->var_i_map = v;

	/* Since "coord_var_name" matches a var name, it must be the coordinate variable name in particular.
	 * The coordinate variable is the var in the file that holds mapping info
	 */
	map_info->coord_var_name = coord_var_name;

	if( options.debug ) printf( "Coord var named >%s< is a NON-SCALAR coord used to map a dimension of var %s\n",
			coord_var_name, v->name.c_str() );

	/* See how many dims this coord var has -- netcdf_n_dims(), not
	 * netcdf_fi_n_dims()/->nDims(); see handle_dim_mapping()'s comment
	 * on the same distinction. */
	map_info->coord_var_ndims = netcdf_n_dims( ncid, coord_var_name );

	/* Get size of the coord var */
	{
	size_t *raw = v->files.front()->file->varSize( coord_var_name );
	map_info->coord_var_size.assign( raw, raw + map_info->coord_var_ndims );
	free( raw );
	}

	if( options.debug ) {
		printf( "non-scalar Coord var %s has %d dims, here are their sizes: ",
			coord_var_name, map_info->coord_var_ndims );
		for( i=0; i<map_info->coord_var_ndims; i++ )
			printf( "%zu ", map_info->coord_var_size[i] );
		printf( "\n" );
		}

	/* Get array of boolean indicating which dims in the
	 * base var match the shape of this coord var.  For
	 * instance, if we have a var of shape (10,20,180,360)
	 * and a coord var of shape (180,360) then this indicating
	 * array will be 0,0,1,1.
	 */
	map_info->matching_var_dims.assign( v->n_dims, 0 );
	map_info->index_place_factor.assign( v->n_dims, 0 );

	/* We could have a problem if the dim sizes are repeated instead of unique.
	 * For example, imagine a square data array of size [n,n].  Then we have
	 * a lon mapping array of size [n,n].  We don't want the boolean array
	 * 'matching_var_dims' to end up as [0,1], we want it to end up as [1,1].
	 * In other words, stop a dim in the coord var from matching the same dim
	 * in the original var twice, even if the dim size is repeated.  We do this
	 * by fist finding a match, then requiring the NEXT match to be to the
	 * left of (in the array of the var's dim sizes) the previous match.
	 */
	must_be_left_of = v->n_dims;	/* Start out by setting all the way to right edge */
	for( i=map_info->coord_var_ndims-1; i>=0; i--) {/* Want to find a dim in v that matches size of coord_var dim number i... */
		/*
		printf( "Searching for a dim in var %s that matches dim number %d in %s, which is of size %d\n",
			v->name, i, s, coord_var_eff_size[i] );
		printf( "the match must be to the left of %d\n", must_be_left_of );
		*/
		for( j=must_be_left_of-1; j>=0; j-- ) {	/* ...subject to constraint that match be left of (have lower numerical value then) j */
			if( coord_var_eff_size[i] == v->size[j] ) {
				map_info->matching_var_dims[j] = 1;
				must_be_left_of = j;	/* found a match at j, so NEXT match must be at a lower value of j than this */
				break;
				}
			}
		}

	n_matches = 0;
	for( i=0; i<v->n_dims; i++ )
		n_matches += map_info->matching_var_dims[i];
	if( n_matches != coord_var_neff_dims ) {
		fprintf( stderr, "Warning: did not correctly match mapped dims specified in the coordinates attribute to dims in the variable\n" );
		fprintf( stderr, "Problem encountered on variable \"%s\" which has shape (", v->name.c_str() );
		for( i=0; i<v->n_dims; i++ ) {
			fprintf( stderr, "%zu", v->size[i] );
			if( i < (v->n_dims-1))
				fprintf( stderr, "," );
			}
		fprintf( stderr, ")\n" );
		fprintf( stderr, "and has coordinates attribute \"%s\"\n", orig_coord_att );
		fprintf( stderr, "The problem is that coordinate var \"%s\" has shape (", coord_var_name );
		for( i=0; i<map_info->coord_var_ndims; i++ ) {
			fprintf( stderr, "%zu", map_info->coord_var_size[i] );
			if( i < (map_info->coord_var_ndims-1))
				fprintf( stderr, "," );
			}
		fprintf( stderr, "), which does not match dimensions in the variable being mapped!\n" );
		fprintf( stderr, "Abandoning coordinate mapping for this variable\n-------------\n" );
		for( i=0; i<v->n_dims; i++ )
			v->dim_map_info[i].reset();
		return;
		}
	if( (n_matches<1) || (n_matches>2)) {
		fprintf( stderr, "(Location B) Error, did not correctly match mapped dims specified in the coordinates attribute to dims in the variable\n" );
		fprintf( stderr, "(Location B) Please send email to dpierce@ucsd.edu letting me know what your coordinates attribute looks like so I can fix this problem.\n" );
		fprintf( stderr, "Problem encountered on variable \"%s\"\n", v->name.c_str() );
		fprintf( stderr, "which has coordinates attribute \"%s\"\n", orig_coord_att );
		fprintf( stderr, "Abandoning coordinate mapping for this variable\n" );
		for( i=0; i<v->n_dims; i++ )
			v->dim_map_info[i].reset();
		return;
		}

	/* Try to figure out if this dim is 'latitude' like
	 * or 'longitude' like....these are the only options
	 * for now.
	 */
	err = determine_lat_lon( const_cast<char *>(map_info->coord_var_name.c_str()), &is_lat, &is_lon );
	if( err != 0 ) {
		/* Abort this process */
		for( i=0; i<v->n_dims; i++ )
			v->dim_map_info[i].reset();
		return;
		}
	idx_lon_dim = -1;
	idx_lat_dim = -1;
	if( is_lon ) {
		if( options.debug ) printf( "Coord var was found to be a LONGITUDE\n" );
		/* Match this coord var to the last one on the right */
		for( i=v->n_dims-1; i>=0; i-- ) {
			if( map_info->matching_var_dims[i] == 1 ) {
				if( options.debug )
					printf( "In variable \"%s\", dimension \"%s\" is mapped by LONGITUDE-like %d-dimensional variable \"%s\"\n",
					v->name.c_str(), v->files.front()->file->dimIdToName( v->name, i).c_str(),
					map_info->coord_var_ndims, map_info->coord_var_name.c_str() );
				v->dim_map_info[i] = std::move( map_info_owner );
				idx_lon_dim = i;
				break;
				}
			}
		/* Now, since we've found the index of the lon dim, the
		 * index of the lat dim must be the other one
		 */
		for( i=0; i<v->n_dims; i++ )
			if( (map_info->matching_var_dims[i] == 1) && (i != idx_lon_dim))
				idx_lat_dim = i;
		}
	else if( is_lat ) {
		if( options.debug ) printf( "Coord var was found to be a LATITUDE\n" );
		/* Match this coord var to the first one on the left */
		for( i=0; i<v->n_dims; i++ ) {
			if( map_info->matching_var_dims[i] == 1 ) {
				idx_lat_dim = i;
				if( options.debug )
					printf( "In variable \"%s\", dimension \"%s\" is mapped by LATITUDE-like dimension %d-dimensional variable \"%s\"\n",
					v->name.c_str(), v->files.front()->file->dimIdToName( v->name, i).c_str(),
					map_info->coord_var_ndims, map_info->coord_var_name.c_str() );
				v->dim_map_info[i] = std::move( map_info_owner );
				break;
				}
			}
		/* Now, since we've found the index of the lat dim, the
		 * index of the lon dim must be the other one
		 */
		for( i=0; i<v->n_dims; i++ )
			if( (map_info->matching_var_dims[i] == 1) && (i != idx_lat_dim))
				idx_lon_dim = i;
		}
	else
		{
		fprintf( stderr, "(Location C)Error, did not correctly match mapped dims specified in the coordinates attribute to dims in the variable\n" );
		fprintf( stderr, "(Location C)Please send email to dpierce@ucsd.edu letting me know what your coordinates attribute looks like so I can fix this problem.\n" );
		exit( -1 );
		}

	/* Read in data from var, store it in cache */
	totsize = 1L;
	for( i=0; i<map_info->coord_var_ndims; i++ ) {
		totsize *= map_info->coord_var_size[i];
		start[i] = 0L;
		count[i] = map_info->coord_var_size[i];
		}
	map_info->data_cache.resize( totsize );
	v->files.front()->file->getData( const_cast<char *>(map_info->coord_var_name.c_str()), start, count, map_info->data_cache.data() );

	if( n_matches == 1 ) {
		if( idx_lon_dim == -1 )
			map_info->index_place_factor[idx_lat_dim] = 1L;
		else
			map_info->index_place_factor[idx_lon_dim] = 1L;
		}
	else if( n_matches == 2 ) {
		map_info->index_place_factor[idx_lon_dim] = 1L;
		map_info->index_place_factor[idx_lat_dim] = v->size[ idx_lon_dim ];
		}
	else
		{
		fprintf( stderr, "(Location D)Error, did not correctly match mapped dims specified in the coordinates attribute to dims in the variable\n" );
		fprintf( stderr, "(Location D)Please send email to dpierce@ucsd.edu letting me know what your coordinates attribute looks like so I can fix this problem.\n" );
		exit( -1 );
		}

	/*
	printf( "matching var dims: " );
	for( i=0; i<v->n_dims; i++ )
		printf( "%d ", map_info->matching_var_dims[i] );
	printf( "Index place factor: " );
	for( i=0; i<v->n_dims; i++ )
		printf( "%zu ", map_info->index_place_factor[i] );
	printf( "\n" );
	*/
}

/******************************************************************************
 * Initialize all the fields in the dim structure by reading from the data file
 */
	void
fill_dim_structs( NCVar *v )
{
	int	i, fileid, debug;
	NCDim	*d;
	std::string dim_name, tmp_units;
	static  int global_id = 0;

	debug = 0;

	if( debug == 1 ) printf( "fill_dim_structs: entering for var %s, which has %d dims\n", v->name.c_str(), v->n_dims );

	fileid = v->files.front()->id();
	NetCDFFile *file0 = v->files.front()->file;
	v->dim.clear();
	v->dim.resize( v->n_dims );
	for( i=0; i<v->n_dims; i++ ) {
		dim_name = file0->dimIdToName( v->name, i );
		if( debug == 1 ) printf( "fill_dim_structs: dim %d has name %s and length %zu\n", i, dim_name.c_str(), v->size[i] );
		if( is_scannable( v, i ) ) {
			v->dim[i] = std::make_unique<NCDim>();
			d            	= v->dim[i].get();
			d->name      	= dim_name;
			d->long_name 	= file0->dimLongname( dim_name );
			d->have_calc_minmax = 0;
			d->units = file0->dimUnits( dim_name );
			d->units_change = 0;
			d->size      	= v->size[i];
			d->calendar = fi_dim_calendar( fileid, dim_name );
			d->global_id 	= ++global_id;
			handle_time_dim( file0, v, i );
			if( options.debug )
				printf( "adding scannable dim to var %s: dimname: %s dimsize: %zu\n", v->name.c_str(), dim_name.c_str(), d->size );
			}
		else
			{
			/* Indicate non-scannable dimensions by a null entry */
			v->dim[i].reset();
			if( options.debug )
				printf( "adding non-scannable dim to var %s: dim name: %s size: %zu\n",
					v->name.c_str(), file0->dimIdToName( v->name, i).c_str(), v->size[i] );
			}
		}

	/* If this variable lives in more than one file, it might have
	 * different time units in each one.  Check for this.
	 */
	if( v->is_virtual && (v->dim[0] != nullptr) && (v->files.size() > 1) ) {
		/* The timelike dimension MUST be the first one! */
		d = v->dim[0].get();
		if( d->timelike ) {
			/* Go through each file and see if it has the same units
			 * as the first file, which is stored in d->units.
			 *
			 * Upstream walked this via cursor->next on a linked list
			 * without ever advancing cursor -- an infinite loop the
			 * moment it triggered, never hit in practice because it
			 * requires >1 file AND a timelike first dimension AND
			 * (for the printed warning) differing units, an unlikely
			 * combination that apparently went unnoticed upstream.
			 * v->files is a vector here, so just index it instead of
			 * carrying that bug forward. */
			for( size_t ifile = 1; ifile < v->files.size(); ifile++ ) {
				tmp_units = v->files[ifile]->file->dimUnits( d->name );
				if( d->units != tmp_units ) {
					printf( "** Warning: different time units found in different files.  Trying to compensate...\n" );
					d->units_change = 1;
					}
				}
			}
		}
}

/******************************************************************************
 * Is this a "scannable" dimension -- i.e., accessable by the taperecorder
 * style buttons? Is scannable if: 
 * 	> is unlimited
 *	> or, is size > 1
 */
	int
is_scannable( NCVar *v, int i )
{
	/* The unlimited record dimension is always scannable */
	if( i == 0 )
		return( true );

	if( v->size[i] > 1 )
		return( true );
	else
		return( false );
}


/**************************************************************************************************
 * Determine if the passed string names a lat or if the string names a lon.
 * If we figure out either lat or lon, returns 0 (success).
 * If we cannot figure either lat or lon, returns 1 (error).
 */
int determine_lat_lon( char *s_in, int *is_lat, int *is_lon )
{
	static  int have_given_warning = 0;
	size_t	n, i;

	/* Get lower case version of input name */
	n = strlen(s_in);
	std::vector<char> s_buf( n+2 );
	char *s = s_buf.data();

	for( i=0; i<n; i++ )
		s[i] = tolower( s_in[i] );

	*is_lat = 0;
	*is_lon = 0;

	if( strncasecmp( "lat", s, 3 ) == 0 ) {
		*is_lat = 1;
		return(0);
		}

	if( strncasecmp( "lon", s, 3 ) == 0 ) {
		*is_lon = 1;
		return(0);
		}

	if( strstr( s, "lat" ) != NULL ) {
		*is_lat = 1;
		return(0);
		}

	if( strstr( s, "lon" ) != NULL ) {
		*is_lon = 1;
		return(0);
		}

	if( (s[0] == 'x') || (s[0] == 'X') ) {
		*is_lon = 1;
		return(0);
		}

	if( (s[0] == 'y') || (s[0] == 'Y') ) {
		*is_lat = 1;
		return(0);
		}

	if( (have_given_warning == 0) && options.debug ) {
		have_given_warning = 1;
		fprintf( stderr, "Warning, cannot figure out whether coordinate variable \"%s\" is a latitude or a longitude, just based on its name\n", s_in );
		fprintf( stderr, "Please name it either Latitude or Longitude, as appropriate, or send email to dpierce@ucsd.edu if you have a case that does not fit this description so I can fix it.\n----------------\n" );
		}

	return(1);	/* error return */
}
