/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 1993 through 2024 David W. Pierce
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
 * David W. Pierce
 * davidwilliampierce@gmail.com
 */

/*****************************************************************************
 *
 *	The netcdf file interface to ncview.  I.e., routines to interface
 *	netCDF format files to the ncview program.  For descriptions, see
 *	the generalized interface routines in 'file.c'.
 *
 *****************************************************************************/

#include <array>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

extern  Options options;

void 	warn_about_char_dims();
int 	safe_ncdimid( int fileid, char *dim_name1 );
int 	netcdf_dimvar_id( int fileid, char *dim_name, int *dimvar_gid );
int 	netcdf_get_att_util( int id, int varid, const char *var_name, const char *att_name, int expected_len, void *value );
int 	nc_inq_varid_grp( int ncid, char *varname, int *varid, int *groupid );
char 	*ncview_groupname( int gid );
char 	*ncview_varname( int gid, int varid );
int 	nc_root_id_from_group_id( int gid );

const char *nc_type_to_string( nc_type type );

/* These three used to be declared in protos.h with external linkage but,
 * per Phase 6 of the "refine the architecture" plan, were confirmed to
 * have zero callers anywhere outside this translation unit (the earlier
 * "zero callers project-wide" survey that flagged them was read as
 * "unused" but they are each called from within this same file --
 * verified directly rather than deleted on the strength of that survey).
 * Declared static here, matching this file's other internal-only
 * helpers above. */
static char *netcdf_varindex_to_name( int cdfid, int index );
static std::string netcdf_global_att_string( int fileid );
static int netcdf_dimvar_bounds_id( int fileid, char *dim_name, int *nvertices );

/*******************************************************************************************/
void safe_strcat( char *dest, size_t dest_len, const char *src )
{
	size_t	nfree;

	if( strlen(dest) >= (dest_len-1) ) {
		dest[ dest_len-1 ] = '\0';
		return;
		}

	nfree = (dest_len-1) - strlen(dest);	/* Must be >= 1 */
	strncat( dest, src, nfree );

	dest[ dest_len-1 ] = '\0';
}

/*******************************************************************************************/
int netcdf_fi_confirm( char *name )
{
	int	ierr, fd;

	ierr = nc_open( name, NC_NOWRITE, &fd );
	if( ierr != NC_NOERR )
		return( false );

	ierr = nc_close( fd );
	return ( true );
}

/*******************************************************************************************/
int netcdf_fi_initialize( char *name )
{
	int	cdfid, ierr;

	ierr = nc_open( name, NC_NOWRITE, &cdfid );
	if( ierr != NC_NOERR ) {
		fprintf( stderr, "fi_initialize: can't properly open file %s\n",
			name );
		exit( -1 );
		}

	return( cdfid );
}
		
/*******************************************************************************************/
int netcdf_fi_recdim_id( int fileid )
{
	int	n_vars, err, n_dims, n_gatts, rec_dim;

	err = nc_inq( fileid, &n_dims, &n_vars, &n_gatts, &rec_dim );
	if( err != NC_NOERR ) {
		fprintf( stderr, "netcdf_fi_recdim_id: error on nc_inq, cdfid=%d\n", fileid );
		exit( -1 );
		}
	return( rec_dim );
}

/*******************************************************************************************
 * NOTE that netcdf returns names starting with slashes, while I do not. So, I strip 
 * the leading slash from returned names.
 */
void ncdf_fi_name_of_group( int ncid, char **name, int full_path ) 
{
        int     ierr;
	size_t  nchar, dummy;

	ierr = nc_inq_grpname_len( ncid, &nchar );	/* According to docs, ALWAYS returns len of full path */
	if( ierr != NC_NOERR ) {
		fprintf( stderr, "Error getting grpname length from file for ncid=%d: %s\n", 
			ncid, nc_strerror(ierr) );
		exit(-1);
		}

	*name = (char *)malloc( sizeof(char) * (nchar+2) );     /* add space for trailing NULL */

	if( full_path == 0 )
		ierr = nc_inq_grpname( ncid, *name );
	else
		ierr = nc_inq_grpname_full( ncid, &dummy, *name );

	/* Get rid of leading slash. Shift left in place (rather than just
	 * advancing *name past it, as this used to) so *name still points
	 * at the actual malloc()'d block -- the caller frees *name, and
	 * free()ing a pointer that's been moved off its allocation's start
	 * is undefined behavior, which is exactly why every caller of this
	 * used to just leak it instead. */
	if( (*name)[0] == '/' )
		memmove( *name, (*name)+1, strlen(*name) );

	if( ierr != NC_NOERR ) {
		fprintf( stderr, "Error getting grpname from file for ncid=%d: %s\n", 
			ncid, nc_strerror(ierr) );
		exit(-1);
		}
}

/*******************************************************************************************
 * Returns list of ONLY displayable vars in the passed group. The list of displayable vars
 * is appended to ret_val, which might already have some displayable vars from different
 * groups in it.
 */
void netcdf_fi_list_vars_inner( Stringlist **ret_val, int gid, char *groupname )
{
	int	n_vars, err, i, jj, kk, n_dims, n_var_dims, eff_ndims;
	char	*var_name, *grp_var_name;
	Stringlist *dimlist;
	int	n_gatts, rec_dim;
	size_t	*size, total_size;

	err = nc_inq( gid, &n_dims, &n_vars, &n_gatts, &rec_dim );
	if( err != NC_NOERR ) {
		fprintf( stderr, "netcdf_fi_list_vars: error on ncinqire, cdfid=%d\n", gid );
		exit( -1 );
		}

	/* Here is where we set the requirements for a variable to appear
	 * as a displayable variable.  At present, we require: 1) that the
	 * variable have at least 1 scannable dimensions. 2) It shouldn't
	 * be a "dimension variable"; i.e., there should be no dimension
	 * with the same name as this variable. 3) Its total size should
	 * be > 1.
	 */
	for( i=0; i<n_vars; i++ ) {
		var_name = netcdf_varindex_to_name( gid, i );

		/* Prepend group name */
		size_t	grp_var_name_size = sizeof(char) * (strlen(var_name) + strlen(groupname) + 10);
		std::vector<char> grp_var_name_buf( grp_var_name_size );
		grp_var_name = grp_var_name_buf.data();
		grp_var_name[0] = '\0';
		if( (strlen(groupname) == 0) || ((strlen(groupname) == 1) && (groupname[0] == '/' )))
			snprintf( grp_var_name, grp_var_name_size, "%s", var_name );
		else
			snprintf( grp_var_name, grp_var_name_size, "%s/%s", groupname, var_name );

		if( options.debug ) printf( "netcdf_fi_list_vars_inner: checking to see if a displayable var: >%s<\n", 
			grp_var_name );

		if( netcdf_dim_name_to_id( gid, var_name, var_name ) == -1 ){
			/* then it's NOT a dimension variable */
			size = netcdf_fi_var_size( gid, var_name );
			n_var_dims = netcdf_fi_n_dims( gid, var_name );
			total_size = 1L;
			eff_ndims  = 0;
			for(jj=0; jj<n_var_dims; jj++ ) {
				total_size *= *(size+jj);
				if( *(size+jj) > 1 ) 
					eff_ndims++;
				}
			/* netcdf_scannable_dims(), not fi_scannable_dims(): this file
			 * IS the netCDF backend fi_scannable_dims() would dispatch to,
			 * so calling back through file.cc's dispatch layer here was a
			 * circular dependency with no purpose (Phase 6, "refine the
			 * architecture" plan) -- broken by calling the primitive
			 * directly, as every other call in this file already does. */
			dimlist  = netcdf_scannable_dims( gid, var_name );
			/* Phase 12j: exclude non-numeric variables from the
			 * displayable list -- nc_get_vara_float() (this function's
			 * eventual data-read primitive, in netcdf_fi_get_data())
			 * cannot read NC_CHAR/NC_STRING or any netCDF-4 user-defined
			 * type (NC_VLEN/NC_OPAQUE/NC_ENUM/NC_COMPOUND, all with
			 * nc_type >= NC_FIRSTUSERTYPEID). The loop variable i IS the
			 * netCDF varid within group gid (varids are 0..n_vars-1 per
			 * group), so this costs one cheap call with no re-resolution.
			 * If the type lookup itself fails, treat as non-displayable
			 * rather than risk offering something we can't read. */
			nc_type vtype;
			int	vtype_err = nc_inq_vartype( gid, i, &vtype );
			int	is_numeric_type = (vtype_err == NC_NOERR) &&
				(vtype != NC_CHAR) && (vtype != NC_STRING) &&
				(vtype < NC_FIRSTUSERTYPEID);
			if( is_numeric_type && (total_size > 1L) && (stringlist_len( dimlist ) >= 1)) {
				/* Hack to make version 1.70+ emulate older versions
				 * that did not display 1-d vars.
				 */
				if( ! (options.no_1d_vars && (eff_ndims == 1) )) {
					if( options.debug ) {
						printf( "netcdf_fi_list_vars_inner: YES, is a displayable var: >%s< ndims=%d sizes=", 
							grp_var_name, n_var_dims );
						for( kk=0; kk<n_var_dims; kk++ ) 
							printf( "%zu ", size[kk] );
						printf( "\n" );
						}
					stringlist_add_string( ret_val, grp_var_name );
					}
				}
			else
				if( options.debug ) printf( "netcdf_fi_list_vars_inner: NO, is size 1 so not displayable: >%s<\n", 
					grp_var_name );
				
			}
		else
			if( options.debug ) printf( "netcdf_fi_list_vars_inner: NO, is a dim so not displayable: >%s<\n", 
				grp_var_name );
		}
}

/*******************************************************************************************/
void netcdf_fi_list_vars_v4( Stringlist **retval, int fileid )
{
	char 	*groupname;
	int	i, err, n_groups, full_path;


	/* Get name of this group
	 */
	full_path = 1;
	ncdf_fi_name_of_group( fileid, &groupname, full_path );

	netcdf_fi_list_vars_inner( retval, fileid, groupname );
	free( groupname );

	/* Get number of groups in this group
	 */
	err = nc_inq_grps( fileid, &n_groups, NULL);
	if( err != NC_NOERR ) {
		fprintf( stderr, "netcdf_fi_list_vars: error on nc_inq_grps, cdfid=%d: %s\n",
			fileid, nc_strerror(err) );
		exit( -1 );
		}

	/* Get group IDs
	 */
	std::vector<int> grp_id( n_groups );
	err = nc_inq_grps( fileid, &n_groups, grp_id.data() );
	for( i=0; i<n_groups; i++ ) {
		netcdf_fi_list_vars_v4( retval, grp_id[i] );
		}

}

/*******************************************************************************************
 * This provides the same interface for the requirements of groups introduced
 * in netcdf library version 4
 */
Stringlist *netcdf_fi_list_vars( int fileid )
{
	Stringlist	*retval = NULL;

	if( options.debug ) printf( "netcdf_fi_list_vars: entering for file %d\n", fileid );

	netcdf_fi_list_vars_v4( &retval, fileid );

	if( options.debug ) {
		printf( "netcdf_fi_list_vars: exiting with list of DISPLAYABLE vars in this file:\n" );
		stringlist_dump( retval );
		}

	return( retval );
}

/*******************************************************************************************/
Stringlist *netcdf_scannable_dims( int fileid, char *var_name )
{
	int	var_id, n_dims, i, err, gid;
	size_t	dim_size;
	Stringlist *dimlist = NULL;
	int	n_atts, dim[MAX_VAR_DIMS];
	nc_type	var_type;
	char	var_name_ng[MAX_NC_NAME];
	std::array<char, MAX_NC_NAME> dim_name_buf; /* defined in netcdf.h */
	char	*dim_name = dim_name_buf.data();

	err = nc_inq_varid_grp( fileid, var_name, &var_id, &gid );
	if( err != NC_NOERR ) {
		fprintf( stderr, "Error in netcdf_scannable_dims: could not find var named \"%s\" in file!\n",
			var_name );
		exit(-1);
		}

	varname_no_groups( var_name, var_name_ng, NULL );

	err = nc_inq_var( gid, var_id, var_name_ng, &var_type, &n_dims, dim, &n_atts );
	if( err != NC_NOERR ) {
		fprintf( stderr, "netcdf_scannable_dims: Error on nc_inq_var call for var %s\n", var_name );
		exit(-1);
		}

	for( i=0; i<n_dims; i++ ) {
		err = nc_inq_dim( gid, *(dim+i), dim_name, &dim_size );
		if( err < 0 ) {
			fprintf( stderr, "ncview++: netcdf_scannable_dims: ");
			fprintf( stderr, "error on nc_inq_dim call\n" );
			fprintf( stderr, "fileid=%d, variable name=%s\n",
					fileid, var_name );
			exit( -1 );
			}
		/* Here is where we set the requirements for a "scannable"
		 * dimension.  For a netcdf file, it makes sense to pick
		 * the first dimension (which is, by convention in netCDF files,
		 * the 'time' dimension if it exists) and otherwise pick
		 * dimensions which are greater in size than some cutoff.  Here,
		 * the cutoff is just one so that 'layer' can be picked up
		 * in layer models -- typically just 1 to 2 in that case.
		 */
		if( (i == 0) || (dim_size > 1) )
			stringlist_add_string( &dimlist, dim_name );
		}

	return( dimlist );
}

/*******************************************************************************************
 * On input, var_name might have prepended group names of the form "group0/group1/varname"
 */
int netcdf_fi_n_dims( int fileid, char *var_name )
{
	int	n_dims, err, varid, groupid;
	int	n_atts, dim[MAX_VAR_DIMS];
	char	var_name_nogroups[MAX_NC_NAME];
	nc_type	var_type;

	err = nc_inq_varid_grp( fileid, var_name, &varid, &groupid );
	if( err != NC_NOERR ) {
		fprintf( stderr, "Error in netcdf_fi_n_dims: could not find var named \"%s\" in file!\n",
			var_name );
		exit(-1);
		}

	/* Strip off leading group names */
	varname_no_groups( var_name, var_name_nogroups, NULL );

	err = nc_inq_var( groupid, varid, var_name_nogroups, &var_type, &n_dims, dim, &n_atts );
	if( err != NC_NOERR ) {
		fprintf( stderr, "netcdf_fi_n_dims: error on nc_inq_var\n" );
		fprintf( stderr, "netcdfid=%d, var_name=%s\n",
			fileid, var_name );
		exit( -1 );
		}
	return( n_dims );
}

/*******************************************************************************************/
size_t netcdf_dim_size( int fileid, int dimid )
{
	size_t	ret_val;
	int	err;

	err = nc_inq_dimlen( fileid, dimid, &ret_val );
	if( err != NC_NOERR ) {
		fprintf( stderr, "netcdf_dim_size: failed on nc_inq_dimlen call!\n" );
		exit(-1);
		}
	return( ret_val );
}

/*******************************************************************************************
 * On input, var_name might have prepended group names of the form "group0/group1/varname"
 */
size_t * netcdf_fi_var_size( int fileid, char *var_name )
{
	int	n_dims, varid, err, i, groupid;
	size_t	*ret_val, dim_size;
	int	n_atts, dim[MAX_VAR_DIMS], debug;
	char	var_name_nogroups[MAX_NC_NAME];
	nc_type var_type;

	debug = 0;

	if( debug==1 ) printf( "netcdf_fi_var_size: entering for fileid=%d varname=>%s<\n", fileid, var_name );

	n_dims  = netcdf_fi_n_dims( fileid, var_name );
	ret_val = (size_t *)malloc( n_dims * sizeof(size_t) );

	err = nc_inq_varid_grp( fileid, var_name, &varid, &groupid );
	if( err != NC_NOERR ) {
		fprintf( stderr, "Error in netcdf_fi_var_size: could not find var named \"%s\" in file!\n",
			var_name );
		exit(-1);
		}

	/* Strip off leading group names */
	varname_no_groups( var_name, var_name_nogroups, NULL );

	err = nc_inq_var( groupid, varid, var_name_nogroups, &var_type, &n_dims, dim, &n_atts );
	if( err != NC_NOERR ) {
		fprintf( stderr, "netcdf_fi_var_size: error on nc_inq_var\n" );
		fprintf( stderr, "netcdfid=%d, var_name=%s\n",
			fileid, var_name );
		exit( -1 );
		}

	if( debug==1 ) printf( "netcdf_fi_var_size: here are dim sizes:\n" );
	for( i=0; i<n_dims; i++ ) {
		err = nc_inq_dimlen( groupid, *(dim+i), &dim_size );
		*(ret_val+i) = dim_size;
		if( debug==1 ) printf( "dim=%d size=%zu\n", i, dim_size );
		}

	return( ret_val );
}

/*******************************************************************************************
 * for the given variable, which has N dims, return the fully qualified name of the 
 * "dim_id"'th dim (dim_id is an index from 0 to 1-NDIMS(var_name))
 */
std::string netcdf_dim_id_to_name( int fileid, std::string_view var_name, int dim_id )
{
	int	netcdf_dim_id, netcdf_var_id, gid;
	int	n_dims, err, n_atts;
	char	dim_name[MAX_NC_NAME], var_name_ng[MAX_NC_NAME], groupname[MAX_NC_NAME];
	nc_type	var_type;
	std::string var_name_s( var_name );

	/* see notes under "netcdf_dim_name_to_id".  "dim_id" is NOT
	 * the netCDF dimension ID, it is the entry into the size array
	 * for the passed variable.
	 */
	err = nc_inq_varid_grp( fileid, var_name_s.data(), &netcdf_var_id, &gid );
	if( err != NC_NOERR ) {
		fprintf( stderr, "Error in netcdf_dim_id_to_name: could not find var named \"%s\" in file!\n",
			var_name_s.c_str() );
		exit(-1);
		}

	/* At this point fully qualified var name "var_name" lives in group "gid" with varid "netcdf_var_id" */

	varname_no_groups( var_name_s.data(), var_name_ng, groupname );
	/*
	printf( "VVVV %s %d netcdf_dim_id_to_name for dim var_name: >%s< var_name_ng: >%s< groupname: >%s<\n",
		__FILE__, __LINE__,
		var_name_s.c_str(), var_name_ng, groupname );
	*/


	/* netcdf_fi_n_dims(), not fi_n_dims(): breaking the same circular
	 * call-back into file.cc's dispatch layer as above (Phase 6). */
	n_dims = netcdf_fi_n_dims( gid, var_name_ng );
	std::vector<int> dim( n_dims );
	err    = nc_inq_var( gid, netcdf_var_id, var_name_ng, &var_type,
				&n_dims, dim.data(), &n_atts );
	if( err != NC_NOERR ) {
		fprintf( stderr, "ncview++: netcdf_dim_id_to_name: error on ");
		fprintf( stderr, "nc_inq_var call.  Variable=%s\n", var_name_s.c_str() );
		exit( -1 );
		}

	netcdf_dim_id = dim[dim_id];
	err      = nc_inq_dimname( gid, netcdf_dim_id, dim_name );
	if( err != NC_NOERR ) {
		fprintf( stderr, "ncview++: netcdf_dim_id_to_name: error on ");
		fprintf( stderr, "nc_inq_dimname call.  Variable=%s\n", var_name_s.c_str() );
		exit( -1 );
		}
	/*
	printf( "VVVV %s %d netcdf_dim_id_to_name for netcdf_dim_id=%d here is dim_name:>%s<\n",
		__FILE__, __LINE__,
		netcdf_dim_id, dim_name );
	*/

	/* 2024-11-05: return fully qualified dim name, not short version */
	/* return( dim_name ); */

	std::string fq_dim_name;
	if( groupname[0] == '\0' )
		fq_dim_name = dim_name;
	else
		fq_dim_name = std::string( groupname ) + "/" + dim_name;
	/* Preserve the exact truncation semantics of the old
	 * snprintf( fq_dim_name, MAX_NC_NAME, ... ) buffer this replaced. */
	if( fq_dim_name.size() > MAX_NC_NAME - 1 )
		fq_dim_name.resize( MAX_NC_NAME - 1 );

	/*
	printf( "VVVV %s %d netcdf_dim_id_to_name for dim >%s< here is full varname, varname_ng: >%s< >%s< FULLY QUAL DIM NAME: >%s<\n",
		__FILE__, __LINE__,
		dim_name, var_name_s.c_str(), var_name_ng, fq_dim_name.c_str() );
	*/

	return( fq_dim_name );
}

/*******************************************************************************************
 * On entry var_name could be something like "group0/group1/varname"
 */
int netcdf_dim_name_to_id( int fileid, char *var_name, char *dim_name )
{
	int	netcdf_dim_id, netcdf_var_id, n_dims, err, i, n_atts, gid, debug;
	nc_type	var_type;
	char	var_name_ng[MAX_NC_NAME];

	debug = 0;

	if( debug == 1 ) printf( "netcdf_dim_name_to_id: entering with fileid=%d var_name=%s dim_name=%s\n",
		fileid, var_name, dim_name );

	/* It is important to note that this routine does NOT return
	 * the dimension ID of the passed dimension.  That concept is
	 * too netCDF specific.  It returns the dimension's index into 
	 * the size and count arrays, for this particular variable. 
	 * Thus calling this routine with the same dimension name, but
	 * for different variables, will in general give different
	 * return values.  Returns -1 if the dimension is not found
	 * in the requested variable.
	 */

	err = nc_inq_varid_grp( fileid, var_name, &netcdf_var_id, &gid );
	if( err != NC_NOERR ) {
		/* Phase 12e: was exit(-1) -- every caller of this function's
		 * wrapper chain (NetCDFFile::dimNameToId()) now checks for -1
		 * before indexing anything with the result (see view.cc's
		 * View::setAxis()/showCurrentDimValues() and
		 * viewer_controller.cc's changeCurDim()/setCurDimIndex()), so
		 * returning -1 here instead of exiting is safe. */
		fprintf( stderr, "Error in netcdf_dim_name_to_id: could not find var named \"%s\" in file!\n",
			var_name );
		in_error( "The requested variable was not found in this file." );
		return(-1);
		}
	if( debug == 1 ) {
		printf( "netcdf_dim_name_to_id: nc_inq_varid_grp reported that var >%s< of gid=%d (%s)",
			var_name, 
			fileid,
			ncview_groupname(fileid) ); 
		printf( " is varid %d of gid=%d (%s), which ACTUALLY has name >%s<\n", 
			netcdf_var_id, 
			gid,
			ncview_groupname(gid), 
			ncview_varname(gid, netcdf_var_id) );
		}

	varname_no_groups( var_name, var_name_ng, NULL );

	if( debug == 1 ) printf( "netcdf_dim_name_to_id: group_id=%d var_name_no_groups=%s\n", 
		gid, var_name_ng );

	netcdf_dim_id = safe_ncdimid( gid, dim_name );
	if( debug == 1 ) printf( "netcdf_dim_name_to_id: netcdf_dim_id=%d\n", netcdf_dim_id );
	if( netcdf_dim_id == -1 )
		return( -1 );

	/* netcdf_fi_n_dims(), not fi_n_dims(): breaking the same circular
	 * call-back into file.cc's dispatch layer as above (Phase 6). */
	n_dims = netcdf_fi_n_dims( gid, var_name_ng );
	std::vector<int> dim( n_dims );
	err    = nc_inq_var( gid, netcdf_var_id, var_name_ng, &var_type,
				&n_dims, dim.data(), &n_atts );
	if( err != NC_NOERR ) {
		/* Phase 12e: was exit(-1) -- see the identical reasoning above,
		 * at this function's other exit() site. */
		fprintf( stderr, "ncview++: netcdf_dim_name_to_id: error on ");
		fprintf( stderr, "nc_inq_var call.  Variable %s, Dimension %s\n",
					var_name, dim_name );
		in_error( "Failed to query dimension information for this variable." );
		return(-1);
		}

	for( i=0; i<n_dims; i++ )
		if( dim[i] == netcdf_dim_id )
			return( i );

	return( -1 );
}

/*******************************************************************************************
 * On entry var_name could be something like "group0/group1/varname"
 */
void netcdf_fi_get_data( int fileid, char *var_name, size_t *start_pos, 
		size_t *count, float *data, NetCDFOptions *aux_data )
{
	int	err, varid, gid, debug, do_scale, do_offset;
	char	var_name_ng[MAX_NC_NAME];
	size_t	i, tot_size, n_dims;
	float	missval, eps;

	debug = 0;

	if( debug==1 ) printf( "netcdf_fi_get_data: entering for fileid=%d var_name=%s\n",
		fileid, var_name );

	err = nc_inq_varid_grp( fileid, var_name, &varid, &gid );
	if( err != NC_NOERR ) {
		/* Phase 12j: degrade rather than abort. Unlike the read-failure
		 * branch below, tot_size can't safely be computed here -- n_dims
		 * is only knowable via a successful lookup of this same variable,
		 * and every helper that could supply it (netcdf_fi_n_dims(),
		 * etc.) would re-run the identical failing lookup and exit()
		 * itself, undoing the point of this fix. This lookup failing at
		 * all is effectively unreachable in practice (every caller sizes
		 * start_pos/count from a variable already resolved once at
		 * addVariable() time), so simply reporting and returning without
		 * touching data -- rather than guessing at a fill extent -- is
		 * the honest choice: the caller's buffer is left exactly as it
		 * was on entry. */
		fprintf( stderr, "Error in netcdf_fi_get_data: could not find var named \"%s\" in file!\n",
			var_name );
		return;
		}

	varname_no_groups( var_name, var_name_ng, NULL );

	tot_size = 1L;
	n_dims = netcdf_fi_n_dims( gid, var_name_ng );
	if( debug==1 ) printf( "netcdf_fi_get_data: ndims=%zu\n", n_dims );
	for( i=0; i<n_dims; i++ ) {
		tot_size *= *(count+i);
		if( debug==1 ) printf( "start[%zu]=%zu count[%zu]=%zu\n", i, start_pos[i], i, count[i] );
		}


	if( options.debug ) {
		fprintf( stderr, "About to call nc_get_vara_float on variable %s\n",
				var_name );
		fprintf( stderr, "Index, start, count:\n" );
		for( i=0; i<(size_t)netcdf_fi_n_dims(fileid, var_name); i++ )
			fprintf( stderr, "[%zu]: %zu %zu\n", i, *(start_pos+i), *(count+i) );
		}

	err = nc_get_vara_float( gid, varid, start_pos, count, data );
	if( err != NC_NOERR ) {
		/* Phase 12j: degrade rather than abort -- this is the single
		 * most reachable exit() site in this file (any displayable
		 * variable that fails a data read, e.g. a non-numeric type that
		 * slipped past netcdf_fi_list_vars_inner()'s type filter via a
		 * coordinates attribute rather than the displayable-variable
		 * list). tot_size is already known at this point, so fill the
		 * whole buffer with FILL_FLOAT (this function's own "bad value"
		 * sentinel, already used a few lines below for NaN results from
		 * a *successful* read) and return immediately -- critically,
		 * before the NaN-elimination loop and the scale_factor/add_offset
		 * block below, so the sentinel isn't multiplied into something
		 * else. */
		fprintf( stderr, "netcdf_fi_get_data: error on nc_get_vara_float call\n" );
		fprintf( stderr, "cdfid=%d   variable=%s\n", fileid, var_name );
		fprintf( stderr, "start, count:\n" );
		for( i=0; i<(size_t)netcdf_fi_n_dims(fileid, var_name); i++ )
			fprintf( stderr, "[%zu]: %zu  %zu\n",
				i, *(start_pos+i), *(count+i) );
		fprintf( stderr, "%s\n", nc_strerror(err) );
		for( i=0L; i<tot_size; i++ )
			data[i] = FILL_FLOAT;
		return;
		}

	/* Eliminate nans */
        for( i=0L; i<tot_size; i++ ) {
		if( isnan(data[i]))
			data[i] = FILL_FLOAT;
		}

#ifdef ELIM_DENORMS
        /* Eliminate denormalized numbers and NaNs */
	n_nans = 0L;
        for( i=0L; i<tot_size; i++ ) {
                c = (unsigned char *)&(data[i]);
                if((*(c+3)==0) && ((*(c+0)!=0)||(*(c+1)!=0)||(*(c+2)!=0))) {
                        fprintf( stderr,
                          "Denormalized number in position %ld: Setting to zero!\n",
                          i, data[i] );
                        data[i] = 0.0;
                        }
		/* this reports bogus nans, for example on file
		 * /cirrus06/users/pierce/hcm_t3p0/sst_even_anoms.HOPE.NMC.65-02.nc
                if((*(c+0)==255) && ((*(c+1)==255)||(*(c+2)==103)||(*(c+3)==63))) {
			n_nans++;
                        data[i] = 1.0e30;
                        }
		*/
                }
	if( n_nans > 0 ) 
		fprintf( stderr, "Found %ld NaNs: Set to 1.e30!\n", n_nans );
        /*
	  for( i=0L; i<tot_size; i++ ) {
                c = (unsigned char *)&(data[i]);
		i0 = *(c+0);
		i1 = *(c+1);
		i2 = *(c+2);
		i3 = *(c+3);
		printf( "%x %x %x %x %f\n", i0, i1, i2, i3, data[i] );
		}
	*/
#endif

	/* Implement the "add_offset" and "scale_factor" attributes */
	if( aux_data != NULL ) {
		if( aux_data->add_offset_set && aux_data->scale_factor_set )
			for( i=0; i<tot_size; i++ )
				*(data+i) = *(data+i) * aux_data->scale_factor 
						+ aux_data->add_offset;
		else if( aux_data->add_offset_set )
			for( i=0; i<tot_size; i++ )
				*(data+i) = *(data+i) + aux_data->add_offset;
		else if( aux_data->scale_factor_set ) 
			for( i=0; i<tot_size; i++ )
				*(data+i) = *(data+i) * aux_data->scale_factor;
		}

	/* Implement the USERS scale and offset, used for changing units of displayed data */
	/* Note: this is NOT the netcdf file add_offset and scale_factor!!! */
	do_scale  = ( options.scale  < 0.9e30 );
	do_offset = ( options.offset < 0.9e30 );
	if( do_scale || do_offset ) {
		netcdf_fill_value( fileid, var_name, &missval, aux_data );
		eps = fabsf( missval ) * 1.e-5;
		}
	if( do_scale ) {
		for( i=0; i<tot_size; i++ ) {
			if( fabsf( *(data+i) - missval ) > eps )
				*(data+i) = *(data+i) * options.scale;
			}
		}
	if( do_offset ) {
		for( i=0; i<tot_size; i++ ) {
			if( fabsf( *(data+i) - missval ) > eps )
				*(data+i) = *(data+i) + options.offset;
			}
		}

	if( options.debug ) 
		fprintf( stderr, "returning from netcdf_fi_get_data\n" );
}

/*******************************************************************************************/
void netcdf_fi_close( int fileid )
{
	int	err;

	err = nc_close( fileid );
	if( err != NC_NOERR ) {
		fprintf( stderr, "netcdf_fi_close: error on nc_close\n" );
		exit( -1 );
		}
}

/****************************************************************************************/
/* netCDF utility routines.  Analogs are not required for each data file format.	*/
/****************************************************************************************/

/*******************************************************************************************
 * A version of 'nc_inq_varid' that has been enhanced to return a groupid/varid pair
 * given a var name of form "groupname/varname" (NOTE: *NO* leading slash!!)
 */
int nc_inq_varid_grp( int ncid, char *varname, int *varid, int *groupid )
{
	int	ns, ig, gid, ierr, group_depth, cur_gid, debug, retval, ncid2use;
	char	groupname[MAX_NC_NAME], varname_sans_groups[MAX_NC_NAME], cur_gid_groupname[MAX_NC_NAME];

	debug = 0;

	if( debug ) printf( "nc_inq_varid_grp: entering with ncid=%d (%s) varname=>%s<\n", 
		ncid, ncview_groupname(ncid), varname );

	/* Since we are generally called with a fully qualified varname, we need
	 * to start at the root ID, not the group id, which can be passed in 'ncid'
	 */
	/* ncid2use = nc_root_id_from_group_id( ncid ); */
	ncid2use = ncid;

	if( debug ) printf( "nc_inq_varid_grp: original ncid=%d (%s) ncid2use=%d (%s)\n", 
		ncid, ncview_groupname(ncid), ncid2use, ncview_groupname(ncid2use) );

	if( varname[0] == '/' ) {
		fprintf( stderr, "Internal error, called nc_inq_varid_grp with a varname that starts with a slash: >%s<\n",
			varname );
		exit(-1);
		}

	ns = count_nslashes( varname );
	if( debug ) printf( "nc_inq_varid_grp: number of slashes in varname: %d\n", ns );

	if( ns > 0 ) {
		cur_gid = ncid2use;
		group_depth = ns;

		/* Traverse to the LAST group in the chain of groups, that's where
		 * we should find this var.
		 */
		if( debug ) printf( "nc_inq_varid_grp: traversing to the LAST group of var >%s<\n", varname );
		for( ig=0; ig<group_depth; ig++ ) {

			ierr = nc_inq_grpname( cur_gid, cur_gid_groupname );
			if( debug ) printf( "nc_inq_varid_grp: traversing group, cur depth=%d cur root name=>%s<\n", 
				ig, cur_gid_groupname );

			ierr = unpack_groupname( varname, ig, groupname );	/* if ig==0, returns first groupname, etc */

			if( debug ) printf( "nc_inq_varid_grp: looking for subgroup >%s< in root group >%s<\n",
				groupname, cur_gid_groupname );

			if( strcmp( groupname, cur_gid_groupname ) == 0 ) {
				/* It is possible for this routine to be called with a groupname ID
				 * instead of a root file id. In that case, we might have the case
				 * that the groupname is ALREADY the current gid groupname, and
				 * we don't need to proceed further
				 */
				break;
				}

			ierr = nc_inq_ncid( cur_gid, groupname, &gid );
			if( ierr != NC_NOERR ) {
				fprintf( stderr, "%s %d nc_inq_varid_grp: Error, did not find group named >%s< in base group >%s<\n",
					__FILE__, __LINE__, 
					groupname, cur_gid_groupname );
				fprintf( stderr, "nc_inq_varid_grp was called with id=%d (%s) ncid2use=%d varname=>%s<\n", 
					ncid, cur_gid_groupname, ncid2use, varname );
				exit(-1);
				return(-1);
				}

			if( debug ) printf( "nc_inq_varid_grp: group >%s< has groupid %d\n", groupname, gid );

			cur_gid = gid;
			}

		*groupid = cur_gid;
		if( debug ) printf( "nc_inq_varid_grp: should now be on the LAST group, here is groupname: >%s<\n", ncview_groupname( cur_gid ));

		varname_no_groups( varname, varname_sans_groups, NULL );
		if( debug ) printf( "nc_inq_varid_grp: calling regular nc_inq_varid with group %d (%s) and varname_sans_groups >%s<\n",
			cur_gid, ncview_groupname(cur_gid), varname_sans_groups );
		retval = nc_inq_varid( cur_gid, varname_sans_groups, varid );

		if( debug ) printf( "nc_inq_varid_grp: final returned gid=%d (%s) varid=%d (which is a var named >%s<)\n",
			*groupid, ncview_groupname(*groupid), *varid, ncview_varname( *groupid, *varid ) );
		return( retval );
		}
	else
		{
		*groupid = ncid2use;
		return( nc_inq_varid( ncid2use, varname, varid ));
		}
}

/*******************************************************************************************/
/* How many dimensions does this variable have? 
*/
int netcdf_n_dims( int cdfid, char *varname )
{
	int	varid, err, n_dims;
	char 	var_name[MAX_NC_NAME];	
	nc_type	var_type;
	int	n_atts, dim[MAX_VAR_DIMS];

	err = nc_inq_varid( cdfid, varname, &varid );
	if( err != NC_NOERR ) {
		fprintf( stderr, "Error in netcdf_n_dims: could not find var named \"%s\" in file!\n",
			varname );
		exit(-1);
		}

	err = nc_inq_var( cdfid, varid, var_name, &var_type, &n_dims, dim, &n_atts );
	if( err != NC_NOERR ) {
		fprintf( stderr, "netcdf_n_dims: error calling nc_inq_var for cdfid=%d, ", 
					cdfid);
		fprintf( stderr, "varname=%s\n", varname );
		fprintf( stderr, "Reason: %s\n", nc_strerror( err ));
		exit( -1 );
		}

	return( n_dims );
}

/*******************************************************************************************/
/* Given the variable INDEX, what is the variable's name?
*/
static char *netcdf_varindex_to_name( int cdfid, int index )
{
	char	*var_name;
	int	err;
	nc_type	var_type;
	int	n_dims, n_atts, dim[MAX_VAR_DIMS];

	if( (var_name = (char *)malloc( MAX_NC_NAME )) == NULL ) {
		fprintf( stderr, "netcdf_varindex_to_name: couldn't allocate %d bytes\n",
			MAX_NC_NAME );
		exit( -1 );
		}

	err = nc_inq_var( cdfid, index, var_name, &var_type, &n_dims, dim, &n_atts );
	if( err != NC_NOERR ) {
		fprintf( stderr, "netcdf_varindex_to_name: error on nc_inq_var call\n" );
		exit( -1 );
		}

	return( var_name );
}

/*******************************************************************************************/
/* A 'safe' way to turn an attribute name into an attribute number.
 * If it returns -1, no attribute of that name exists for the given
 * variable (which might be NC_GLOBAL).
 */
int netcdf_att_id( int fileid, int varid, const char *name )
{
	int	err, n_atts, i;
	char	att_name[MAX_NC_NAME], var_name[MAX_NC_NAME];
	nc_type	var_type;
	int	n_vars, n_dims, rec_dim, dim[MAX_VAR_DIMS];

	if( varid == NC_GLOBAL ) {
		err = nc_inq( fileid, &n_dims, &n_vars, &n_atts, &rec_dim );
		if( err != NC_NOERR ) {
			fprintf( stderr, "netcdf_att_id: Error on nc_inq call for varid=%d attname=%s\n", 
				varid, name );
			exit(-1);
			}
		}
	else
		{
		err = nc_inq_var( fileid, varid, var_name, &var_type, &n_dims, dim, &n_atts );
		if( err != NC_NOERR ) {
			fprintf( stderr, "netcdf_att_id: Error on nc_inq_var call for varid=%d attname=%s\n", 
				varid, name );
			exit(-1);
			}
		}

	for( i=0; i<n_atts; i++ ) {
		err = nc_inq_attname( fileid, varid, i, att_name );
		if( err != NC_NOERR ) {
			fprintf( stderr, "netcdf_att_id: failed on ncattname call!\n" );
			exit(-1);
			}
		if( strcmp( att_name, name ) == 0 )
			return( i );
		}
	return( -1 );
}

/*******************************************************************************************/
std::string netcdf_title( int fileid )
{
	int	err, attid;
	nc_type	type;
	size_t	title_len;

	attid = netcdf_att_id( fileid, NC_GLOBAL, "title" );
	if( attid < 0 )
		return( std::string() );

	err = nc_inq_att( fileid, NC_GLOBAL, "title", &type, &title_len );
	if( type != NC_CHAR ) {
		fprintf( stderr, "ncview++: netcdf_title: internal error in the " );
		fprintf( stderr, "format of the netCDF input file; title\n" );
		fprintf( stderr, "not in character format!  Setting title to NULL.\n" );
		return( std::string() );
		}
	if( err != NC_NOERR )
		return( std::string() );

	/* title_len+1 to leave room for the guaranteed-NUL byte the malloc'd
	 * buffer used to have past the raw attribute bytes nc_get_att_text
	 * writes; std::string's own implicit trailing NUL plays that role. */
	std::string ret_val( title_len, '\0' );
	err = nc_get_att_text( fileid, NC_GLOBAL, "title", ret_val.data() );
	if( err != NC_NOERR )
		return( std::string() );

	/* Get rid of trailing spaces / blanks. This is necessary if
	 * title is too long, which seems to give X a core dump
	 */
	for( int kk=(int)title_len-1; kk>0; kk-- ) {
		if( ret_val[kk] == ' ' )
			ret_val[kk] = '\0';
		else
			break;
		}
	ret_val.resize( strlen( ret_val.c_str() ));

	/* Protection from X windows crash if title is too long */
	int max_title_len = 100;
	if( (int)ret_val.size() > max_title_len )
		ret_val.resize( max_title_len-1 );

	return( ret_val );
}

/*******************************************************************************************/
/* Returns NULL if there was no att of this name for the passed var.
 * Otherwise, returns a pointer to storage allocated in this routine;
 * the calling routine must deallocate the storage when it is done
 * with the attribute.
 * As a special case, if the attribute exited, but the length of the
 * attribute was zero, then it returns a pointer to a char string 
 * that is a single NULL.
 *
 * On input, var_name might be of the form "group0/group1/varname"
 *
 */
std::string netcdf_get_char_att( int fileid, std::string_view var_name, std::string_view att_name )
{
	int	varid, err, gid;
	size_t	name_length;
	nc_type	type;
	char	var_name_ng[MAX_NC_NAME];
	std::string var_name_s( var_name ), att_name_s( att_name );

	err = nc_inq_varid_grp( fileid, var_name_s.data(), &varid, &gid );
	if( err != NC_NOERR ) {
		fprintf( stderr, "Error in netcdf_get_char_att: could not find var named \"%s\" in file!\n",
			var_name_s.c_str() );
		exit(-1);
		}

	varname_no_groups( var_name_s.data(), var_name_ng, NULL );

	if( netcdf_att_id( gid, varid, att_name_s.data() ) < 0 )
		return( std::string() );

	err = nc_inq_att( gid, varid, att_name_s.data(), &type, &name_length );
	if( (err != NC_NOERR) || (type != NC_CHAR))
		return( std::string() );

	/* Special case: attribute exists but is zero-length -- indistinguishable
	 * from "not found" to every caller of this function, same as before. */
	if( name_length == 0 )
		return( std::string() );

	std::string ret_val( name_length, '\0' );
	err = nc_get_att_text( gid, varid, att_name_s.data(), ret_val.data() );
	if( err != NC_NOERR )
		return( std::string() );

	ret_val.resize( strlen( ret_val.c_str() ));
	return( ret_val );
}

/*******************************************************************************************/
std::string netcdf_long_var_name( int fileid, std::string_view var_name )
{
	return( netcdf_get_char_att( fileid, var_name, "long_name" ));
}

/*******************************************************************************************/
std::string netcdf_var_units( int fileid, std::string_view var_name )
{
	return( netcdf_get_char_att( fileid, var_name, "units" ));
}

/*******************************************************************************************/
std::string netcdf_dim_calendar( int fileid, std::string_view dim_name )
{
	int	dimvar_id, dimvar_gid;
	std::string dim_name_s( dim_name );

	dimvar_id = netcdf_dimvar_id( fileid, dim_name_s.data(), &dimvar_gid );

	if( dimvar_id < 0 )
		return( std::string() );

	return( netcdf_get_char_att( dimvar_gid, dim_name, "calendar" ));
}

/*******************************************************************************************/
std::string netcdf_dim_units( int fileid, std::string_view dim_name )
{
	int	dimvar_id, dimvar_gid;
	std::string dim_name_s( dim_name );

	dimvar_id = netcdf_dimvar_id( fileid, dim_name_s.data(), &dimvar_gid );

	if( dimvar_id < 0 )
		return( std::string() );

	return( netcdf_var_units( dimvar_gid, dim_name ));
}

/*******************************************************************************************
 * Given the (fully qualified) name of a dim, such as "group_obs_fine/time", returns
 * the id of the associated dimvar, or -1 if no associted dimvar is found
 *
 * Group update: this tends to be called with the root fileid, but sometimes the
 * dimvar is in a group (i.e., NOT the root fileid). If returned parameter
 * dimvar_gid == -1, then this is not an issue and just go ahead and use the
 * passed fileid. If dimvar_gid is NOT equal to -1, you must access the dimvar
 * using the returned dimvar_gid, NOT the originally passed fileid!
 */
int netcdf_dimvar_id( int fileid, char *dim_name, int *dimvar_gid )
{
	int	i, err, n_dims, n_vars, rec_dim, gid, fileid2use;
	char	var_name[256], dim_name_ng[MAX_NC_NAME], groupname[MAX_NC_NAME];
	char	dim_name_2use[ MAX_NC_NAME ];
	nc_type	var_type;
	int	n_atts, dim[MAX_VAR_DIMS];

	*dimvar_gid	= fileid;
	fileid2use 	= fileid;
	snprintf( dim_name_2use, sizeof(dim_name_2use), "%s", dim_name );

	/* If we enter with a fully qualified dim name, such as group_obs_fine/time,
	 * make sure we proceed with the fileid corresponding to that group. Note that
	 * the netcdf library preceeds all group names with a slash
	 */
	 /* First see if there is a slash in dim_name; if so, it is fully qualified */
	 if( count_nslashes( dim_name_2use ) > 0 ) {

	 	/* Get group name */
		varname_no_groups( dim_name_2use, dim_name_ng, groupname );

		/* I have to admit I don't understand the netcdf library
		 * at this point. Since it returns group names with a 
		 * leading slash it only makes sense it should accept/require
		 * group names with a leading slash. Yet it seems to require
		 * NO leading slashes to find the group!
		 */
		/*strcpy( gn_slash, "/" );*/
		/*strcat( gn_slash, groupname );*/
		/* err = nc_inq_grp_ncid( fileid, gn_slash, &gid ); */

		err = nc_inq_grp_ncid( fileid, groupname, &gid );

		/* If we were called with a group ID to begin with, try agian
		 * with the root ID
		 */
		/* Suspected == vs = typo, preserved verbatim -- see docs/modernization.md's
		 * "Follow-up commits" section. This always assigns NC_ENOGRP (truthy)
		 * rather than comparing err to it, so the fallback below always runs. */
		if( (err = NC_ENOGRP) )
			err = nc_inq_grp_ncid( nc_root_id_from_group_id(fileid), groupname, &gid );

		if( err != NC_NOERR ) {
			/* Phase 12i: this fires on any ordinary netCDF-4 file with
			 * a variable nested >=2 groups deep -- varname_no_groups()
			 * splits a fully-qualified dim name at the LAST slash, so
			 * a variable at grp1/grp2/x hands nc_inq_grp_ncid() the
			 * two-level path "grp1/grp2", which it can't resolve
			 * (nc_inq_grp_ncid() only accepts a simple, one-level
			 * group name -- nc_inq_grp_full_ncid() is the path-taking
			 * variant). Degrade like every other "no dimvar" path in
			 * this function rather than aborting: every caller
			 * already treats a negative return as "no associated
			 * dimvar" and falls back to the bare dim name/id. */
			fprintf( stderr, "%s line %d : Error: nc_inq_grp_ncid failed in routine netcdf_dimvar_id:\n",
				__FILE__, __LINE__ );
			fprintf( stderr, "%s\n", nc_strerror( err ) );
			return( -1 );
			}

		/* Found a group ID to use instead of the passed fileid */
		fileid2use = gid;

		/* For the rest of the code, the dim name to use is the
		 * UNqualifed dim name
		 */
		snprintf( dim_name_2use, sizeof(dim_name_2use), "%s", dim_name_ng );
	 	}

	err = nc_inq( fileid2use, &n_dims, &n_vars, &n_atts, &rec_dim );
	if( err != NC_NOERR )
		return( -1 );

	for( i=0; i<n_vars; i++ ) {
		err = nc_inq_var( fileid2use, i, var_name, &var_type, &n_dims, dim, &n_atts );
		if( err != NC_NOERR )
			return( -1 );
		if( strcmp( dim_name_2use, var_name ) == 0 ) {
			if( (var_type == NC_CHAR) && (options.no_char_dims) )
				return( -1 );
			else
				{
				*dimvar_gid = fileid2use;
				return( i );
				}
			}
		}

	return( -1 );
}

/*******************************************************************************************/
std::string netcdf_dim_longname( int fileid, std::string_view dim_name )
{
	int	dimvar_id, err, dimvar_gid;
	size_t	len;
	nc_type	att_type;
	std::string dim_name_s( dim_name );

	dimvar_id = netcdf_dimvar_id( fileid, dim_name_s.data(), &dimvar_gid );
	if( dimvar_id < 0 )
		return( dim_name_s );

	if( netcdf_att_id( dimvar_gid, dimvar_id, "long_name" ) < 0 )
		return( dim_name_s );

	err = nc_inq_att( dimvar_gid, dimvar_id, "long_name", &att_type, &len );
	if( (err != NC_NOERR) || (att_type != NC_CHAR))
		return( dim_name_s );

	std::string dim_longname( len, '\0' );
	err = nc_get_att_text( dimvar_gid, dimvar_id, "long_name", dim_longname.data() );
	if( err < 0 )
		return( dim_name_s );
	else
		return( dim_longname );
}

/*******************************************************************************************/
/* A netCDF file has dim values if it has a *variable* with the same
 * name as the *dimension*.  This is a netCDF convention...which not
 * all datafiles might follow, of course.
 */
int netcdf_has_dim_values( int fileid, char *dim_name )
{
	int	dimvar_id, dimvar_gid;

	dimvar_id = netcdf_dimvar_id( fileid, dim_name, &dimvar_gid );

	if( dimvar_id < 0 )
		return( false );
	else
		return( true );
}

/*******************************************************************************************/
/* Only one of the two possible returns, ret_val_double and ret_val_char, will
 * be filled out.  If the return value of the call is NC_CHAR, then ret_val_char
 * will have been filled out.  If the return value of the call is NC_DOUBLE, then
 * ret_val_double will have been filled out.
 *
 * Phase 12h: this function always returns NC_DOUBLE or NC_CHAR and never
 * aborts the process. When it cannot read or make sense of the requested
 * dimension value (an unusable file shape, a failed netCDF read, an
 * oversized bounds variable, ...) it degrades: it warns to stderr and
 * falls back to virt_place as a synthetic NC_DOUBLE coordinate (or, for
 * an unusable bounds variable specifically, to the plain unbounded
 * coordinate read with *return_has_bounds set to 0), the same "index as
 * coordinate" presentation ncview already uses for any dimension with no
 * coordinate variable at all.
 */
nc_type netcdf_dim_value( int fileid, char *dim_name, size_t place, 
		double *ret_val_double, char *ret_val_char, size_t virt_place, 
		int *return_has_bounds, double *return_bounds_min, double *return_bounds_max )
{
	int	err, dimvar_id, nvertices, dimvar_gid, use_bounds;
	char	var_name[MAX_NC_NAME];
	nc_type type, ret_type;
	size_t	limit;
	long	i;
	size_t	char_place[2], bstart[2], bcount[2];
	int	n_dims, n_atts, dim[MAX_VAR_DIMS], dimvar_bounds_id, debug;
	double	boundvals[50], boundvals_min, boundvals_max;

	debug = 0;

	if( debug ) printf( "netcdf_dim_value: entering with dim_name=>%s< place=%zu\n", dim_name, place );

	if( ! netcdf_has_dim_values( fileid, dim_name ) ) {
		*ret_val_double = (double)virt_place;
		*return_has_bounds = 0;
		return( NC_DOUBLE );
		}

	dimvar_id = netcdf_dimvar_id( fileid, dim_name, &dimvar_gid );
	if( dimvar_id < 0 ) {
		*ret_val_double = (double)virt_place;
		*return_has_bounds = 0;
		return( NC_DOUBLE );
		}

	err = nc_inq_var( dimvar_gid, dimvar_id, var_name, &type, &n_dims, dim, &n_atts );
	if( err != NC_NOERR ) {
		/* Phase 12h: dimvar_id/dimvar_gid were just handed back by a
		 * successful lookup two lines above, so this is effectively
		 * unreachable outside a corrupt handle or a netCDF-library-
		 * internal failure. Degrade anyway rather than abort, matching
		 * the rest of this function -- this runs before the
		 * has_bounds/bounds_min/bounds_max pre-zeroing block below, so
		 * it must set all four out-params itself, the same shape the
		 * two early returns above it use. */
		fprintf( stderr, "netcdf_dim_value: failed on nc_inq_var call for dim %s; using virtual place\n",
			dim_name );
		*ret_val_double = (double)virt_place;
		*return_has_bounds = 0;
		*return_bounds_min = 0.0;
		*return_bounds_max = 0.0;
		return( NC_DOUBLE );
		}

	/* Phase 12f: initialize on every path below, including the
	 * default (unhandled-datatype) and NC_CHAR cases, which never
	 * used to write these -- callers that check *return_has_bounds
	 * were reading uninitialized stack memory on those paths. */
	*return_has_bounds = 0;
	*return_bounds_min = 0.0;
	*return_bounds_max = 0.0;

	switch( type ) {
		case NC_CHAR:
			/* this one is really complicated because the netCDF standard
			 * doesn't have the concept of "strings".  So, each individual
			 * is left to decide for theirself how to handle it.  Some 
			 * reasonable ways of doing it are either as null-terminated
			 * C strings or as non-terminated assemblages of fortran 
			 * characters.  You could specify the length of fortran strings
			 * by an additional dimension to the dimvar.
			 */
			warn_about_char_dims();
			ret_type = NC_CHAR;
			if( n_dims == 2 ) {
				/* Fixed-length string over a second "characters"
				 * dimension: read char-by-char at [place, i]
				 * until a NUL or the dimension's length. */
				limit = netcdf_dim_size( dimvar_gid, dim[1] );
				i = 0L;
				char_place[0] = place;
				do	{
					char_place[1] = i;
					/* Phase 12g: nc_get_var1_uchar() cannot read
					 * NC_CHAR data at all -- confirmed empirically,
					 * it fails every time with NC_ECHAR ("Attempt to
					 * convert between text & numbers"), in both
					 * classic and netCDF-4 files. Before Phase 12f
					 * checked this call's error code, that failure
					 * was silently ignored and this buffer was left
					 * whatever it started as; Phase 12f's new check
					 * turned that into a hard exit() on every single
					 * 2-D NC_CHAR dimvar read, not just a rare one.
					 * nc_get_var1_text() is the function that
					 * actually reads NC_CHAR data. */
					err = nc_get_var1_text( dimvar_gid, dimvar_id, char_place, ret_val_char+i );
					if( err != NC_NOERR ) {
						/* Phase 12h: degrade rather than abort -- break
						 * out of the read loop and fall back to
						 * virt_place, overwriting the NC_CHAR assigned
						 * above. ret_val_char is only NUL-terminated
						 * below when ret_type is still NC_CHAR, so an
						 * error on the very first character (i==0,
						 * where i-1 would underflow) is handled safely. */
						fprintf( stderr, "netcdf_dim_value: failed reading character %ld of dim %s; using virtual place\n",
							i, dim_name );
						fprintf( stderr, "%s\n", nc_strerror( err ) );
						*ret_val_double = (double)virt_place;
						ret_type = NC_DOUBLE;
						break;
						}
					i++;
					}
				while
					(((size_t)i < limit) &&
						(*(ret_val_char+i-1) != '\0'));
				if( (ret_type == NC_CHAR) && (*(ret_val_char+i-1) != '\0') )
					*(ret_val_char+i-1) = '\0';
				}
			else if( n_dims == 1 ) {
				/* 1-D NC_CHAR coordinate variable: one character
				 * per coordinate position, no second dimension to
				 * index over. */
				size_t place1[1];
				place1[0] = place;
				/* Phase 12g: see the n_dims==2 case's comment above --
				 * nc_get_var1_uchar() cannot read NC_CHAR data. */
				err = nc_get_var1_text( dimvar_gid, dimvar_id, place1, ret_val_char );
				if( err != NC_NOERR ) {
					/* Phase 12h: degrade rather than abort. */
					fprintf( stderr, "netcdf_dim_value: failed reading dim %s at place %zu; using virtual place\n",
						dim_name, place );
					fprintf( stderr, "%s\n", nc_strerror( err ) );
					*ret_val_double = (double)virt_place;
					ret_type = NC_DOUBLE;
					}
				else
					ret_val_char[1] = '\0';
				}
			else	{
				/* Phase 12g: neither of the two shapes this function
				 * knows how to handle -- reading via either fixed-size
				 * index array (place1[1] or char_place[2]) below a
				 * higher-rank NC_CHAR dimvar would read past the end
				 * of that array. netcdf_dimvar_id() matches purely by
				 * name, so this is reachable on an ordinary file (a
				 * scalar or higher-rank variable whose name happens to
				 * collide with a dimension name), not just a corrupt
				 * one -- degrade like the default: case below rather
				 * than aborting the whole process over it.
				 */
				fprintf( stderr, "ncview++: netcdf_dim_value: unsupported rank (%d) for character dimension variable %s; using virtual place\n",
					n_dims, dim_name );
				*ret_val_double = (double)virt_place;
				ret_type = NC_DOUBLE;
				}
			break;

		case NC_BYTE:
		case NC_SHORT:
		case NC_LONG:
		case NC_FLOAT:
		case NC_DOUBLE:
		case NC_INT64:

			/* If we have a 'bounds' attribute for the dimvar, returned the value
			 * centered between the boundaries.  Some files have the dim value NOT
			 * centered between the boundaries, which isn't so useful.
			 */
			if( n_dims != 1 ) {
				/* Phase 12g: a numeric dimvar is expected to be
				 * 1-D. &place below is a single size_t used as the
				 * index array for nc_get_var1_double() -- if the
				 * dimvar were actually higher rank, the netCDF
				 * library would read past the end of it.
				 * netcdf_dimvar_id() matches purely by name, so this
				 * is reachable on an ordinary file (e.g. a 2-D
				 * curvilinear coordinate variable whose name happens
				 * to collide with a dimension name), not just a
				 * corrupt one -- degrade like the default: case below
				 * rather than aborting the whole process over it.
				 */
				fprintf( stderr, "ncview++: netcdf_dim_value: unsupported rank (%d) for numeric dimension variable %s; using virtual place\n",
					n_dims, dim_name );
				*ret_val_double = (double)virt_place;
				ret_type = NC_DOUBLE;
				break;
				}

			dimvar_bounds_id = netcdf_dimvar_bounds_id( dimvar_gid, dim_name, &nvertices );
			/* Phase 12h: "usable bounds" now covers both "no bounds
			 * attribute at all" and "bounds attribute present but this
			 * function can't handle it (too many vertices)" -- both
			 * converge on the same plain, bounds-less coordinate read
			 * below rather than each having their own copy of it. */
			use_bounds = (dimvar_bounds_id >= 0) && (nvertices <= 50);

			if( use_bounds ) {
				*return_has_bounds = nvertices;
				/* OK, have a usable bounds dimvar here, read it and
				 * compute the mean to get the value to return.
				 */
				bstart[0] = place;
				bstart[1] = 0L;
				bcount[0] = 1L;
				bcount[1] = nvertices;
				err = nc_get_vara_double( dimvar_gid, dimvar_bounds_id, bstart, bcount, boundvals );
				if( err != NC_NOERR ) {
					/* Phase 12h: degrade to the plain, bounds-less read
					 * below rather than aborting -- the coordinate
					 * variable itself is fine, only its bounds
					 * variable's read failed. */
					fprintf( stderr, "Error reading boundary dim values for dim %s from file; using unbounded coordinate instead\n",
						dim_name );
					fprintf( stderr, "%s\n", nc_strerror( err ) );
					use_bounds = 0;
					*return_has_bounds = 0;
					}
				else	{
					*ret_val_double = 0.0;
					boundvals_min = 1.e35;
					boundvals_max = -1.e35;
					for( i=0; i<nvertices; i++ ) {
#ifdef ELIM_DENORMS
						/* Eliminate denormalized numbers */
						c = (unsigned char *)boundvals[i];
						if((*(c+7)==0) && ((*(c+0)!=0)||(*(c+1)!=0)||(*(c+2)!=0)||(*(c+3)!=0)||(*(c+4)!=0)||(*(c+5)!=0)||(*(c+6)!=0))) {
							fprintf( stderr,
							  "Denormalized number in dimvar %s, position %ld: Setting to zero!\n",
							  var_name, place );
							boundvals[i] = 0.0;
							}
#endif
						*ret_val_double += boundvals[i];
						boundvals_min = (boundvals[i] < boundvals_min) ? boundvals[i] : boundvals_min;
						boundvals_max = (boundvals[i] > boundvals_max) ? boundvals[i] : boundvals_max;
						}
					*ret_val_double /= (double)nvertices;
					*return_bounds_min = boundvals_min;
					*return_bounds_max = boundvals_max;
					}
				}

			if( !use_bounds ) {
				/* Phase 12h: was `else`, i.e. dimvar_bounds_id < 0 --
				 * now also reached when a bounds variable exists but
				 * couldn't be used (too many vertices, or its own read
				 * failed above). Plain, bounds-less coordinate read. */
				if( (dimvar_bounds_id >= 0) && (nvertices > 50) )
					fprintf( stderr, "Error, compiled with max number of vertices for bounds var of 50!  But found a var with n=%d for dim %s; using unbounded coordinate instead\n",
						nvertices, dim_name );

				*return_has_bounds = 0;
				err = nc_get_var1_double( dimvar_gid, dimvar_id, &place, ret_val_double );
				if( err != NC_NOERR ) {
					/* Phase 12h: degrade rather than abort. */
					fprintf( stderr, "netcdf_dim_value: failed reading dim %s at place %zu; using virtual place\n",
						dim_name, place );
					fprintf( stderr, "%s\n", nc_strerror( err ) );
					*ret_val_double = (double)virt_place;
					}
#ifdef ELIM_DENORMS
				else	{
					/* Eliminate denormalized numbers */
					c = (unsigned char *)ret_val_double;
					if((*(c+7)==0) && ((*(c+0)!=0)||(*(c+1)!=0)||(*(c+2)!=0)||(*(c+3)!=0)||(*(c+4)!=0)||(*(c+5)!=0)||(*(c+6)!=0))) {
						fprintf( stderr,
						  "Denormalized number in dimvar %s, position %ld: Setting to zero!\n",
						  var_name, place );
						*ret_val_double = 0.0;
						}
					}
#endif
				}
			ret_type = NC_DOUBLE;
			break;

		default:
			fprintf( stderr, "ncview++: netcdf_dim_value: " );
			fprintf( stderr, "unknown data type (%d) for\n", type );
			fprintf( stderr, "dimension %s\n", dim_name );
			*ret_val_double = (double)virt_place;
			ret_type = NC_DOUBLE;
			break;
		}
	return( ret_type );
}

/*******************************************************************************************
 * On entry, var_name can be something like "group0/group1/varname"
 */
void netcdf_fill_aux_data( int id, char *var_name, FDBlist *fdb )
{
	int	err, varid, n_dims, dim[MAX_NC_DIMS], n_atts, unlimdimvar_id, recdim_id, gid;
	char	dummy_var_name[ MAX_NC_NAME ], var_name_ng[MAX_NC_NAME], unlimdim_name[MAX_NC_NAME];
	nc_type	type;
	NetCDFOptions *netcdf;

	netcdf = fdb->aux_data.get();

	err = nc_inq_varid_grp( id, var_name, &varid, &gid );
	if( err != NC_NOERR ) {
		fprintf( stderr, "Error in netcdf_fill_aux_data: could not find var named \"%s\" in file!\n",
			var_name );
		exit(-1);
		}

	varname_no_groups( var_name, var_name_ng, NULL );

	/* Record the recdim units in this file
	 */
	recdim_id = netcdf_fi_recdim_id( gid );
	if( recdim_id == -1 ) {
		fdb->recdim_units.clear();
		}
	else
		{
		/* Get NAME of the record dimension */
		err = nc_inq_dimname( gid, recdim_id, unlimdim_name );
		if( err != 0 ) {
			fprintf( stderr, "Error in netcdf_fill_aux_data: could not get recdim name\n%s\n",
				nc_strerror( err ));
			exit(-1);
			}
		/* See if there is a variable with the same name */
		err = nc_inq_varid( gid, unlimdim_name, &unlimdimvar_id );
		if( err != 0 )
			fdb->recdim_units.clear();
		else
			{
			/* Get the units for the dimvar. Empty means "no units
			 * attribute" -- callers now test fdb->recdim_units.empty()
			 * rather than comparing to NULL (see dataset.cc's
			 * dimValueConvert(), formerly file.cc's fi_dim_value_convert()
			 * before Phase 6 moved it). */
			fdb->recdim_units = netcdf_var_units( gid, unlimdim_name );
			}
		}

	err = nc_inq_var( gid, varid, dummy_var_name, &type, &n_dims, dim, &n_atts );
	if( err != NC_NOERR ) {
		fprintf( stderr, "netcdf_fill_aux_data: failed on nc_inq_var call!\n" );
		exit(-1);
		}

	if( n_atts == 0 )
		return;

	/* netcdf was fetched from fdb->aux_data.get() above, before the
	 * recdim_units work -- that's independent of it and still runs
	 * regardless. Every real caller (Dataset::addVariable(), via
	 * new_fdblist()) pre-allocates aux_data before reaching here, but
	 * nothing enforced that invariant in this function: Phase 5a found
	 * this via a real SIGSEGV while constructing a bare FDBlist without
	 * it (see tests/test_file_layer.cc), pinned it rather than fixing it
	 * (5a was tests-only), and flagged it for Phase 6. Guarding here,
	 * rather than dereferencing blindly, matches this file's existing
	 * style of checking preconditions instead of assuming them. */
	if( netcdf == NULL )
		return;

	netcdf->valid_range_set =
	    netcdf_get_att_util( gid, varid, var_name_ng, "valid_range",  2, netcdf->valid_range );
	netcdf->valid_min_set = 
	    netcdf_get_att_util( gid, varid, var_name_ng, "valid_min",    1, &(netcdf->valid_min) );
	netcdf->valid_max_set = 
	    netcdf_get_att_util( gid, varid, var_name_ng, "valid_max",    1, &(netcdf->valid_max) );
	netcdf->add_offset_set = 
	    netcdf_get_att_util( gid, varid, var_name_ng, "add_offset",   1, &(netcdf->add_offset) );
	netcdf->scale_factor_set = 
	    netcdf_get_att_util( gid, varid, var_name_ng, "scale_factor", 1, &(netcdf->scale_factor) );

	/* Special case: if we have add_offset and scale_factor attributes,
	 * then assume they apply to the valid range also.  Q: is this
	 * always true?  The netCDF specification doesn't really say. 
	 */
	if( netcdf->add_offset_set && netcdf->scale_factor_set ) {
		if( netcdf->valid_range_set ) {
			netcdf->valid_range[0] = netcdf->valid_range[0] * netcdf->scale_factor
					+ netcdf->add_offset;
			netcdf->valid_range[1] = netcdf->valid_range[1] * netcdf->scale_factor
					+ netcdf->add_offset;
			}
		else 
			{
			if( netcdf->valid_min_set ) {
				netcdf->valid_min = netcdf->valid_min * netcdf->scale_factor
					+ netcdf->add_offset;
				}
			if( netcdf->valid_max_set ) {
				netcdf->valid_max = netcdf->valid_max * netcdf->scale_factor
					+ netcdf->add_offset;
				}
			}
		}
	else if( netcdf->add_offset_set ) {
		if( netcdf->valid_range_set ) {
			netcdf->valid_range[0] = netcdf->valid_range[0] + netcdf->add_offset;
			netcdf->valid_range[1] = netcdf->valid_range[1] + netcdf->add_offset;
			}
		else 
			{
			if( netcdf->valid_min_set ) {
				netcdf->valid_min = netcdf->valid_min + netcdf->add_offset;
				}
			if( netcdf->valid_max_set ) {
				netcdf->valid_max = netcdf->valid_max + netcdf->add_offset;
				}
			}
		}
	else if( netcdf->scale_factor_set ) {
		if( netcdf->valid_range_set ) {
			netcdf->valid_range[0] = netcdf->valid_range[0] * netcdf->scale_factor;
			netcdf->valid_range[1] = netcdf->valid_range[1] * netcdf->scale_factor;
			}
		else 
			{
			if( netcdf->valid_min_set ) {
				netcdf->valid_min = netcdf->valid_min * netcdf->scale_factor;
				}
			if( netcdf->valid_max_set ) {
				netcdf->valid_max = netcdf->valid_max * netcdf->scale_factor;
				}
			}
		}
}

/*******************************************************************************************/
/* return true if found and set the value, and false otherwise */
int netcdf_get_att_util( int id, int varid, const char *var_name, const char *att_name, int expected_len, void *value )
{
	size_t	i;
	int	err;
	size_t	len;
	nc_type	type;
	short	short_1;
	double	double_1;
	long	long_1;

	if( netcdf_att_id( id, varid, att_name ) >= 0 ) {
		err = nc_inq_att( id, varid, att_name, &type, &len );
		if( err != NC_NOERR )
			return( false );
		if( type != NC_FLOAT ) {
			switch( type ) {
				case NC_CHAR:
					{
					std::vector<char> char_att( len+1 );
					err = nc_get_att_text( id, varid, att_name, char_att.data() );
					if( err != NC_NOERR )
						return( false );
					/* A char-typed numeric attribute (e.g. a
					 * malformed valid_range="abc", or a
					 * legitimate one with more than one
					 * space-separated value) -- parse up to
					 * expected_len values in sequence, and
					 * treat the whole attribute as absent
					 * (rather than reporting success with
					 * unset/garbage values) if any of them
					 * fails to parse. */
					const char *p = char_att.data();
					for( i=0; i<(size_t)expected_len; i++ ) {
						int n_consumed = 0;
						if( sscanf( p, "%f%n", (float *)value + i, &n_consumed ) != 1 ) {
							fprintf( stderr, "netcdf_get_att_util: error, could not parse a numeric value out of char attribute \"%s\" for variable %s (value: \"%s\")\n",
								att_name, var_name, char_att.data() );
							return( false );
							}
						p += n_consumed;
						}
					}
					break;

				case NC_BYTE:
				case NC_SHORT:
					{
					std::vector<short> short_att( len );
					err = nc_get_att_short( id, varid, att_name, short_att.data() );
					if( err != NC_NOERR )
						return( false );
					for( i=0; i<len; i++ ) {
						short_1 = short_att[i];
						*((float *)value + i) = (float)short_1;
						}
					}
					break;

				case NC_DOUBLE:
					{
					std::vector<double> double_att( len );
					err = nc_get_att_double( id, varid, att_name, double_att.data() );
					if( err != NC_NOERR )
						return( false );
					for( i=0; i<len; i++ ) {
						double_1 = double_att[i];
						*((float *)value + i) = (float)double_1;
						}
					}
					break;

				case NC_LONG:
					{
					std::vector<long> long_att( len );
					err = nc_get_att_long( id, varid, att_name, long_att.data() );
					if( err != NC_NOERR )
						return( false );
					for( i=0; i<len; i++ ) {
						long_1 = long_att[i];
						*((float *)value + i) = (float)long_1;
						}
					}
					break;
				default:	
					fprintf( stderr, "can't handle conversions from %s to FLOAT yet\n", nc_type_to_string( type ) );
				}
			return( true );
			}

		else if( len != (size_t)expected_len ) {
			fprintf( stderr, "error in specification of \"%s\" attribute for\n", att_name);
			fprintf( stderr, "variable %s: %zu values specified (should be %d)\n",
				var_name, len, expected_len );
			return( false );
			}

		else
			{
			/* If we get here, type is a NC_FLOAT */
			err = nc_get_att_float( id, varid, att_name, (float *)value );
			if( err != NC_NOERR )
				return( false );
			return( true );
			}
		}
	else
		return( false );
}

/*******************************************************************************************/
int netcdf_min_max_option_set( NCVar *var, float *ret_min, float *ret_max )
{
	NetCDFOptions 	*netcdf;
	int		range_set = false;
	float		min, max, t_min, t_max;

	min =  9.9e30;
	max = -9.9e30;

	for( auto &f : var->files ) {
		netcdf = f->aux_data.get();
		if( netcdf->valid_range_set ) {
			range_set = true;
			if( netcdf->valid_range[0] <  netcdf->valid_range[1] ) {
				t_min = netcdf->valid_range[0];
				t_max = netcdf->valid_range[1];
				}
			else
				{
				t_min = netcdf->valid_range[1];
				t_max = netcdf->valid_range[0];
				}
			min = (t_min < min) ? t_min : min;
			max = (t_max > max) ? t_max : max;
			}
		}

	if( range_set ) {
		*ret_min = min;
		*ret_max = max;
		}

	return( range_set );
}

/*******************************************************************************************/
int netcdf_min_option_set( NCVar *var, float *ret_min )
{
	NetCDFOptions 	*netcdf;
	int		min_set = false;
	float		min, t_min;

	min =  9.9e30;

	for( auto &f : var->files ) {
		netcdf = f->aux_data.get();
		if( netcdf->valid_min_set ) {
			min_set = true;
			t_min   = netcdf->valid_min;
			min     = (t_min < min) ? t_min : min;
			}
		}

	if( min_set )
		*ret_min = min;

	return( min_set );
}

/*******************************************************************************************/
int netcdf_max_option_set( NCVar *var, float *ret_max )
{
	NetCDFOptions 	*netcdf;
	int		max_set = false;
	float		max, t_max;

	max =  -9.9e30;

	for( auto &f : var->files ) {
		netcdf = f->aux_data.get();
		if( netcdf->valid_max_set ) {
			max_set = true;
			t_max   = netcdf->valid_max;
			max     = (t_max > max) ? t_max : max;
			}
		}

	if( max_set )
		*ret_max = max;

	return( max_set );
}

/*******************************************************************************************
 * On entry var_name might be something like "group0/group1/varname"
 */
void netcdf_fill_value( int file_id, char *var_name, float *v, NetCDFOptions *aux_data )
{
	int	err, varid, foundit, gid;
	char	var_name_ng[MAX_NC_NAME];
	nc_type	vartype;

	if( options.debug ) 
		printf( "Checking %s for a missing value...\n", var_name );

	foundit = false;
	err = nc_inq_varid_grp( file_id, var_name, &varid, &gid );
	if( err != NC_NOERR ) {
		fprintf( stderr, "Error in netcdf_fill_value: could not find var named \"%s\" in file!\n",
			var_name );
		exit(-1);
		}

	varname_no_groups( var_name, var_name_ng, NULL );

	if( netcdf_get_att_util( gid, varid, var_name_ng, "missing_value", 1, v ) ) {
		if( options.debug )
			printf( "found a \"missing_value\" attribute=%g\n",
				*v );
		foundit = true;
		}

	if( netcdf_get_att_util( gid, varid, var_name_ng, "_FillValue", 1, v ) ) {
		if( options.debug )
			printf( "found a \"_FillValue\" attribute=%g\n",
				*v );
		foundit = true;
		}

	/* Is there a global missing value? */
	if( netcdf_get_att_util( gid, NC_GLOBAL, var_name_ng, "missing_value", 1, v ) ) {
		if( options.debug )
			printf( "found a \"missing_value\" attribute=%g\n",
				*v );
		foundit = true;
		}

#ifdef ELIM_DENORMS
        c = (unsigned char *)v;
	if((*(c+0)==255) && ((*(c+1)==255)||(*(c+2)==103)||(*(c+3)==63))) {
		fprintf( stderr, "Missing value is a NaN! Setting to 1.e30\n" );
		*v = 1.e30;
		}
#endif

	if( foundit ) {
		/* Implement the "add_offset" and "scale_factor" attributes.
		 * aux_data is NULL for coordinate-variable reads (util.cc's
		 * fill_dim_structs()/Dataset::cacheScalarCoordInfo() both pass NULL
		 * here), which have no scale/offset attributes to apply. */
		if( aux_data != NULL && aux_data->add_offset_set && aux_data->scale_factor_set )
			*v = *v * aux_data->scale_factor
					+ aux_data->add_offset;
		else if( aux_data != NULL && aux_data->add_offset_set )
			*v = *v + aux_data->add_offset;
		else if( aux_data != NULL && aux_data->scale_factor_set )
			*v = *v * aux_data->scale_factor;

		/* Turn nan's into a more useful value */
		if( isnan(*v)) {
			*v = FILL_FLOAT;
			if( options.debug )
				fprintf( stderr, "fillvalue is nan; resetting to default=%g\n",
					*v );
			}
		return;
		}

	/* default behavior, if no specified "_FillValue" attribute.
	 * Thanks to Heiko Klein <Heiko.Klein@met.no> for the suggestion & code.
	 *
	 * Phase 12j: this used to pass file_id (the root id) here instead of
	 * gid, the group id nc_inq_varid_grp() resolved varid against a few
	 * lines up -- the same wrong-id bug class Phase 12i fixed in
	 * netcdf_att_string(). For a grouped variable this either resolves
	 * the wrong variable's type or fails outright, and on failure *v is
	 * left untouched -- reaching cacheScalarCoordInfo() and this
	 * function's own caller as an uninitialized stack float.
	*/
	if ( nc_inq_vartype( gid, varid, &vartype) == NC_NOERR ) {
		switch (vartype) {
			case NC_BYTE:   *v = (float) NC_FILL_BYTE; break;
			case NC_SHORT:  *v = (float) NC_FILL_SHORT; break;
			case NC_INT:    *v = (float) NC_FILL_INT; break;
			case NC_FLOAT:  *v = NC_FILL_FLOAT; break;
			case NC_DOUBLE: *v = (float) NC_FILL_DOUBLE; break;
			default: 	*v = NC_FILL_FLOAT;
			}
		}

	if( options.debug )
		printf( "setting fillvalue to default for var type=%g\n", *v );
}

/*******************************************************************************************/
/* This is a "safe" version of the standard ncvarid routine, in
 * that it returns -1 if there is no variable of that name in
 * the file, and the varid otherwise.
 */
int safe_ncvarid( int fileid, char *varname )
{
	int	err, varid;

	err = nc_inq_varid( fileid, varname, &varid );
	if( err != NC_NOERR )
		return( -1 );

	return( varid );
}

/*******************************************************************************************/
/* This is a "safe" version of the standard ncdimid routine, in
 * that it returns -1 if there is no dimension of that name in
 * the file, EVEN IF the netcdf library is compiled to barf on 
 * errors of this type (rather than always returning -1, as the
 * documentation says it should!!!)
 */
int safe_ncdimid( int fileid, char *dim_name1 )
{
	int	err, i, n_dims, include_parents;
	char	dim_name2[MAX_NC_NAME];
	int	debug;
	size_t	dim_size;

	debug = 0;

	if( debug == 1 ) printf( "safe_ncdimid: entering with fileid=%d (group %s) dim_name=%s\n", 
		fileid, ncview_groupname(fileid), dim_name1 );

	/* Find how many dims there are available, make space
	 * for their dimids, read them in 
	 */
	include_parents = 1;
	err = nc_inq_dimids( fileid, &n_dims, NULL, include_parents );
	if( err != NC_NOERR ) {
		fprintf( stderr, "safe_ncdimid: error on call to nc_inq_dimids (1): %s\n", 
			nc_strerror(err) );
		exit(-1);
		}
	if( debug == 1 ) printf( "safe_ncdimid: n_dims=%d\n", n_dims );

	std::vector<int> dimids( n_dims );
	err = nc_inq_dimids( fileid, &n_dims, dimids.data(), include_parents );
	if( err != NC_NOERR ) {
		fprintf( stderr, "safe_ncdimid: error on call to nc_inq_dimids (2): %s\n", 
			nc_strerror(err) );
		exit(-1);
		}

	for( i=0; i<n_dims; i++ ) {
		err = nc_inq_dim( fileid, dimids[i], dim_name2, &dim_size );
		if( err != 0 ) {
			fprintf( stderr, "safe_ncdimid: Error, call to nc_inq_dim returned: %s\n", 
				nc_strerror( err ));
			fprintf( stderr, "Called with ncid=%d dimid=%d\n", fileid, i );
			exit(-1);
			}
		if( debug == 1 ) printf( "safe_ncdimid: dim #%d (dimid %d) is named >%s<\n", 
			i, dimids[i], dim_name2 );
		if( strcmp( dim_name1, dim_name2 ) == 0 ) {
			if( debug==1 ) printf( "safe_ncdimid found match in dim %d: returning that value\n", dimids[i] );
			return( dimids[i] );
			}
		}

	if( debug==1 ) printf( "safe_ncdimid: no matches, returning -1\n" ); 
	return( -1 );
}	

/*******************************************************************************************/
const char *nc_type_to_string( nc_type type )
{
	switch( type ) {
		
		case NC_BYTE:	return( "BYTE" );

		case NC_CHAR:	return( "CHAR" );

		case NC_SHORT:	return( "SHORT" );

		case NC_LONG:	return( "LONG" );

		case NC_FLOAT:	return( "FLOAT" );

		case NC_DOUBLE:	return( "DOUBLE" );

		default: 	return( "UNKNOWN" );
		}
}

/*******************************************************************************************/
std::string netcdf_att_string( int fileid, std::string_view var_name )
{
	int	iatt, varid, groupid, size_to_use, n_dims,
		dim[50], n_atts, err;
	size_t	i;
	nc_type	datatype, type;
	char	att_name[MAX_NC_NAME], dummy_var_name[MAX_NC_NAME];
	char	line[2000];
	size_t	len, retval_len=10000;
	std::string var_name_s( var_name );

	/* retval_len caps the total length exactly as the old malloc'd buffer
	 * did (via safe_strcat's truncate-in-place behavior below) -- a
	 * std::vector<char> here, not an unbounded std::string, preserves
	 * that byte-for-byte, including truncation of pathologically
	 * attribute-heavy files. */
	std::vector<char> ret_string( retval_len );
	snprintf( ret_string.data(), retval_len, "Attributes for variable %s:\n------------------------------\n", var_name_s.c_str() );
	ret_string[retval_len-1] = '\0';

	/* Phase 12i: this used to look the variable up with a plain
	 * nc_inq_varid(), which only ever searches the root group -- every
	 * other function in this file uses the group-aware
	 * nc_inq_varid_grp() (see e.g. netcdf_fi_n_dims() above) because
	 * caller-supplied variable names are always fully group-qualified
	 * (netcdf_fi_list_vars_inner() prefixes them). So "Info" on any
	 * variable inside any group crashed the process; this was a real
	 * correctness bug, not just a missing safety check, since the plain
	 * lookup could never have found the variable in the first place.
	 * groupid (not fileid) is used for every subsequent call below,
	 * matching that same established pattern. */
	err = nc_inq_varid_grp( fileid, var_name_s.data(), &varid, &groupid );
	if( err != NC_NOERR ) {
		fprintf( stderr, "Error in netcdf_att_string: could not find var named \"%s\" in file!\n",
			var_name_s.c_str() );
		exit(-1);
		}

	err = nc_inq_var( groupid, varid, dummy_var_name, &type, &n_dims, dim, &n_atts );
	if( err != NC_NOERR ) {
		fprintf( stderr, "netcdf_att_string: failed on nc_inq_var call!\n" );
		exit(-1);
		}

	/* Phase 12d: reported once per call, not once per unhandled attribute,
	 * so a file with several modern-typed attributes doesn't pop up a
	 * dialog per attribute. */
	bool warned_unhandled_type = false;

	for( iatt=0; iatt<n_atts; iatt++ ) {

		err = nc_inq_attname( groupid, varid, iatt, att_name );
		if( err != NC_NOERR ) {
			fprintf( stderr, "netcdf_att_string: failed on nc_inq_attname call!\n" );
			exit(-1);
			}
		err = nc_inq_att(  groupid, varid, att_name, &datatype, &len );
		if( err != NC_NOERR ) {
			fprintf( stderr, "netcdf_att_string: failed on nc_inq_att call!\n" );
			exit(-1);
			}
		size_to_use = -1;
		switch( datatype ) {
			case NC_BYTE:   size_to_use = sizeof(char);   break;
			case NC_CHAR:   size_to_use = sizeof(char);   break;
			case NC_SHORT:  size_to_use = sizeof(short);  break;
			case NC_LONG:   size_to_use = sizeof(nclong); break;
			case NC_FLOAT:  size_to_use = sizeof(float);  break;
			case NC_DOUBLE: size_to_use = sizeof(double); break;
			case NC_NAT:    fprintf( stderr, "Error, can't handle attribute of type NC_NAT, ignoring\n" ); size_to_use = sizeof(double); break;
			default:
				{
				/* Phase 12d: was exit(-1) here -- every netCDF-4 type added
				 * since this switch was written in 1993 (NC_UINT, NC_INT64,
				 * NC_STRING, any user-defined/compound type) fell into this
				 * branch, so a perfectly valid netCDF-4 file with a
				 * modern-typed attribute crashed ncview the instant its
				 * value was displayed. Skip just this one attribute (note
				 * it in the returned text) and keep processing the rest,
				 * the same "degrade, don't abort" shape NC_NAT already
				 * uses one case above -- an unhandled type on one
				 * attribute shouldn't blank the whole variable-info
				 * display. */
				char note[512];
				snprintf( note, sizeof(note),
					"(attribute \"%s\": unhandled netCDF datatype %d, skipped)\n",
					att_name, (int)datatype );
				fprintf( stderr, "Error, unhandled netcdf data type: %d\n", datatype );
				if( ! warned_unhandled_type ) {
					in_error( "This file has one or more attributes of a netCDF datatype ncview doesn't display (see the terminal for details); they'll be skipped." );
					warned_unhandled_type = true;
					}
				safe_strcat( ret_string.data(), retval_len, note );
				continue;
				}
			}

		std::vector<char> data( size_to_use*len );

		ncattget( groupid, varid, att_name, data.data() );

		safe_strcat( ret_string.data(), retval_len, att_name );
		safe_strcat( ret_string.data(), retval_len, ": "     );

		for(i=0; i<len; i++) {
			switch( datatype ) {
				case NC_BYTE:   snprintf( line, 1999, "%d ",  *((char   *)data.data()+i) ); break;
				case NC_CHAR:   snprintf( line, 1999, "%c",   *((char   *)data.data()+i) ); break;
				case NC_SHORT:  snprintf( line, 1999, "%d ",  *((short  *)data.data()+i) ); break;
				case NC_LONG:   snprintf( line, 1999, "%ld ", (long)(*((nclong *)data.data()+i)) ); break;
				case NC_FLOAT:  snprintf( line, 1999, "%f ",  *((float  *)data.data()+i) ); break;
				case NC_DOUBLE: snprintf( line, 1999, "%lf ", *((double *)data.data()+i) ); break;
				case NC_NAT:    snprintf( line, 1999, "(NC_NAT) ");                  break;
				}
			safe_strcat( ret_string.data(), retval_len, line );
			}
		safe_strcat( ret_string.data(), retval_len, "\n" );
		}

	safe_strcat( ret_string.data(), retval_len, netcdf_global_att_string( fileid ).c_str() );
	return( std::string( ret_string.data() ));
}

/*******************************************************************************************/
static std::string netcdf_global_att_string( int fileid )
{
	int	iatt, len, size_to_use, i, n_atts, err;
	nc_type	datatype;
	char	att_name[MAX_NC_NAME];
	char	line[2000];
	size_t	retval_len=10000;

	err = nc_inq_natts( fileid, &n_atts );
	if( err == -1 ) {
		fprintf( stderr, "netcdf_gobal_att_string: failed on nc_inq_natts call!\n" );
		exit(-1);
		}

	if( n_atts == 0 )
		return( std::string() );

	/* retval_len caps the total length exactly as the old malloc'd buffer
	 * did (via safe_strcat's truncate-in-place behavior below). */
	std::vector<char> ret_string( retval_len );
	snprintf( ret_string.data(), retval_len-1, "\nGlobal attributes:\n--------------------------\n" );
	ret_string[retval_len-1] = '\0';

	/* Phase 12d: reported once per call, not once per unhandled attribute
	 * -- see netcdf_att_string()'s identical guard above for the reason. */
	bool warned_unhandled_type = false;

	for( iatt=0; iatt<n_atts; iatt++ ) {

		ncattname( fileid, NC_GLOBAL, iatt, att_name );
		ncattinq(  fileid, NC_GLOBAL, att_name, &datatype, &len );
		size_to_use = -1;
		switch( datatype ) {
			case NC_BYTE:   size_to_use = sizeof(char);   break;
			case NC_CHAR:   size_to_use = sizeof(char);   break;
			case NC_SHORT:  size_to_use = sizeof(short);  break;
			case NC_LONG:   size_to_use = sizeof(nclong); break;
			case NC_FLOAT:  size_to_use = sizeof(float);  break;
			case NC_DOUBLE: size_to_use = sizeof(double); break;
			case NC_NAT:    fprintf(stderr,"Error, cannot handle attributes of type NC_NAT; ignoring\n" ); break;
			default:
				{
				/* Phase 12d: was exit(-1) -- see netcdf_att_string()'s
				 * identical fix above for the full reasoning (a valid
				 * netCDF-4 file with a modern-typed global attribute
				 * crashed ncview on display). Skip just this attribute,
				 * note it, keep going. */
				char note[512];
				snprintf( note, sizeof(note),
					"(global attribute \"%s\": unhandled netCDF datatype %d, skipped)\n",
					att_name, (int)datatype );
				fprintf( stderr, "Error, unhandled netcdf data type: %d\n", datatype );
				if( ! warned_unhandled_type ) {
					in_error( "This file has one or more global attributes of a netCDF datatype ncview doesn't display (see the terminal for details); they'll be skipped." );
					warned_unhandled_type = true;
					}
				safe_strcat( ret_string.data(), retval_len, note );
				continue;
				}
			}

		std::vector<char> data( size_to_use*len );

		ncattget( fileid, NC_GLOBAL, att_name, data.data() );

		safe_strcat( ret_string.data(), retval_len, att_name);
		safe_strcat( ret_string.data(), retval_len, ": "    );

		for(i=0; i<len; i++) {
			switch( datatype ) {
				case NC_BYTE:   snprintf( line, 1999, "%d ",  *((char   *)data.data()+i) ); break;
				case NC_CHAR:   snprintf( line, 1999, "%c",   *((char   *)data.data()+i) ); break;
				case NC_SHORT:  snprintf( line, 1999, "%d ",  *((short  *)data.data()+i) ); break;
				case NC_LONG:   snprintf( line, 1999, "%ld ", (long)(*((nclong *)data.data()+i)) ); break;
				case NC_FLOAT:  snprintf( line, 1999, "%f ",  *((float  *)data.data()+i) ); break;
				case NC_DOUBLE: snprintf( line, 1999, "%lf ", *((double *)data.data()+i) ); break;
				case NC_NAT:    snprintf( line, 1999, "(NC_NAT) "); break;
				}
			safe_strcat( ret_string.data(), retval_len, line );
			}
		safe_strcat( ret_string.data(), retval_len, "\n" );
		}

	return( std::string( ret_string.data() ));
}

/*******************************************************************************************/
void warn_about_char_dims()
{
	static int	have_done_it = false;

	if( ! have_done_it ) {
		fprintf( stderr, "******************************************************\n" );
		fprintf( stderr, "Warning: you are using character-type dimensions.\n" );
		fprintf( stderr, "Unfortunately, the netCDF standard version 2 does not\n" );
		fprintf( stderr, "include string types.  Therefore, I may not be able to\n" );
		fprintf( stderr, "intuit what you have done.  If the program crashes,\n" );
		fprintf( stderr, "try rerunning with the -no_char_dims option.\n" ); 
		fprintf( stderr, "******************************************************\n" );
		have_done_it = true;
		}
}

/********************************************************************************************/
/* Returns -1 if the dim has NO bounds dimvar.  If the dim DOES have a bounds dimvar,
 * this returns the dimvarid of the bounds dimvar, and sets nvertices to the number
 * of vertices the bounds var has 
 */
static int netcdf_dimvar_bounds_id( int fileid, char *dim_name, int *nvertices )
{
	int	reg_dimvar_id, bounds_dimvar_id, dimvar_ndims, err, name_length, debug,
		dimvar_gid;
	const char	*attname = "bounds";
	nc_type	type;
	size_t	st_nvertices;
	int dimids[MAX_NC_DIMS];

	debug = 0;

	if( debug ) printf( "netcdf_dimvar_bounds_id: checking if dim %s have a bounds_var\n", dim_name );

	/* First get the regular dimvar for this dim, then see if that dimvar
	 * has an attribute named "bounds".
	 */
	reg_dimvar_id = netcdf_dimvar_id( fileid, dim_name, &dimvar_gid );
	if( reg_dimvar_id < 0 ) {
		if( debug ) printf( "netcdf_dimvar_bounds_id: dim %s does NOT have a regular dimvar, returning -1\n", dim_name );
		return( -1 );
		}

	if( netcdf_att_id( dimvar_gid, reg_dimvar_id, attname ) < 0 )
		return( -1 );

	err = ncattinq( dimvar_gid, reg_dimvar_id, attname, &type, &name_length );
	if( (err < 0) || (type != NC_CHAR))
		return( -1 );

	std::vector<char> bounds_dimvarname_buf( name_length+1 );
	char *bounds_dimvarname = bounds_dimvarname_buf.data();
	err = ncattget( dimvar_gid, reg_dimvar_id, attname, bounds_dimvarname );
	if( err < 0 )
		return( -1 );

	if( *(bounds_dimvarname+name_length-1) != '\0' )
		*(bounds_dimvarname + name_length) = '\0';

	err = nc_inq_varid( dimvar_gid, bounds_dimvarname, &bounds_dimvar_id );
	if( err != 0 )
		return( -1 );

	/* Currently only know how to handle 2-d bounds variables */
	err = nc_inq_varndims( dimvar_gid, bounds_dimvar_id, &dimvar_ndims );
	if( (err != NC_NOERR) || (dimvar_ndims != 2)) {
		fprintf( stderr, "Currently can only handle bounds dims with ndims=2; bounds var %s has ndims=%d.  Ignoring!\n",
				bounds_dimvarname, dimvar_ndims );
		return( -1 );
		}

	/* Get the dim ids of the bounds var so we can get the length of the trailing
	 * one, which is the number of vertices
	 */
	err = nc_inq_vardimid( dimvar_gid, bounds_dimvar_id, dimids );
	if( err != NC_NOERR ) {
		fprintf( stderr, "Error reading bounds info for boundary variable %s.  Ignoring!\n", bounds_dimvarname );
		return( -1 );
		}
	err = nc_inq_dimlen( dimvar_gid, dimids[1], &st_nvertices );
	if( err != NC_NOERR ) {
		fprintf( stderr, "Error reading nvertices info for boundary variable %s.  Ignoring!\n", bounds_dimvarname );
		return( -1 );
		}
	*nvertices = (int)st_nvertices;

	return( bounds_dimvar_id );
}

/*****************************************************************************************************
 * Returns a pointer to a static buffer with the group name; useful for debugging & info printouts
 */
char *ncview_groupname( int gid )
{
	static char 	buffer[MAX_NC_NAME];
	size_t	tlen;

	nc_inq_grpname_full( gid, &tlen, buffer );
	return( &(buffer[0]) );
}

/*****************************************************************************************************
 * Returns a pointer to a static buffer with the var name; useful for debugging & info printouts
 */
char *ncview_varname( int gid, int varid )
{
	static char 	buffer[MAX_NC_NAME];

	nc_inq_varname( gid, varid, buffer );
	return( &(buffer[0]) );
}

/*****************************************************************************************************
 * Given a fileid, which may be a root ID or a group ID, returns the root group ID
 */
int nc_root_id_from_group_id( int gid ) 
{
	int	err, cursor, parentid;

	cursor = gid;

	/* It is usually the case that the passed gid is the root id, so short circuit */
	if( nc_inq_grp_parent( gid, &parentid ) == NC_ENOGRP )
		return( gid );

	cursor = gid;
	while( (err = nc_inq_grp_parent( cursor, &parentid )) == 0 ) 
		cursor = parentid;
		

	if( err == NC_ENOGRP ) 
		return( cursor );	/* at the root of the chain */

	/* Phase 12c: was exit(0) -- a real netCDF error reported to stderr
	 * but exiting with SUCCESS status, so a caller checking the exit
	 * code (a script, CI) couldn't tell this ever failed. Every other
	 * error exit in this file uses -1; matched here for consistency. */
	fprintf( stderr, "%s line %d : nc_root_id_from_group_id failed with error %d : %s\n",
		__FILE__, __LINE__,
		err, nc_strerror(err) );
	exit(-1);
}

