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
 * 	varname_utils.cc
 *
 *	Name/string helpers: variable name and netCDF-4 group-path parsing,
 *	plus a couple of small string utilities used alongside them. Moved
 *	verbatim out of util.cc (Phase 4b, "refine the architecture" plan) --
 *	pure file motion, no behavior change.
 *******************************************************************************/

#include <vector>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

/******************************************************************************
 * If we allowed strings of arbitrary length, some of the widgets
 * would crash when trying to display them.
 */
	/* Was: char *limit_string(char *s), trimming trailing spaces and
	 * truncating by writing '\0' bytes into the CALLER's own buffer and
	 * returning that same pointer. Every call site (all in view.cc)
	 * only ever uses the return value inline in a snprintf(), never
	 * re-reads the original buffer afterward expecting it pre-trimmed --
	 * so this is now a pure function computing into a fresh std::string,
	 * with no more side effect on the caller's storage. */
	std::string
limit_string( std::string_view s )
{
	std::string ret( s );

	int	i = (int)ret.size() - 1;
	while( i >= 0 && ret[i] == ' ' )
		i--;
	ret.resize( i+1 );

	if( ret.size() > MAX_DISPLAYED_STRING_LENGTH )
		ret.resize( MAX_DISPLAYED_STRING_LENGTH );

	return( ret );
}

/*********************************************************************************************
 * like strncmp, but ignoring case
 */
	int
strncmp_nocase( const char *s1, const char *s2, size_t n )
{
	size_t	i;
	int	retval;

	if( (s1==NULL) || (s2==NULL))
		return(-1);

	std::vector<char> s1_lc_buf(strlen(s1)+1), s2_lc_buf(strlen(s2)+1);
	char *s1_lc = s1_lc_buf.data();
	char *s2_lc = s2_lc_buf.data();

	for( i=0; i<strlen(s1); i++ )
		s1_lc[i] = tolower(s1[i]);
	s1_lc[i] = '\0';
	for( i=0; i<strlen(s2); i++ )
		s2_lc[i] = tolower(s2[i]);
	s2_lc[i] = '\0';

	retval = strncmp( s1_lc, s2_lc, n );

	return(retval);
}

/*******************************************************************************************
 * Returns the number of forward slashes in a string
 */
int count_nslashes( const char *s )
{
	size_t	i;
	int	nslash;

	nslash = 0;
	for( i=0; i<strlen(s); i++ )
		if( s[i] == '/' )
			nslash++;

	return( nslash );
}

/*******************************************************************************************
 * Given a varname string of format: groupname0/groupname1/groupnameN/varname
 *
 * and an integer ig: 0...N this returns groupname correspoinding to the integer ig
 * (NOTE: counting starts at 0, so if ig==0 then the first group name is returned)
 *
 * If ig == -1, then the full groupname without the varname is returned:
 * I.e., "groupname0/groupname1/groupnameN". If the var does NOT have any
 * forward slashes, it lives in the root group, and "/" is returned.
 *
 * If ig == -2, then ONLY the varname is returned. I.e., "varname"
 *
 * groupname must already be allocated upon entry
 *
 * Returns 0 on success, -1 on error
 */
int unpack_groupname( const char *varname, int ig, char *groupname )
{
	size_t	i, len;
	int	i0, i1, idx_slash[MAX_NC_NAME], nslash;
	char	ts[MAX_NC_NAME];

	/* varname's group-path prefix comes from nc_inq_grpname_full()
	 * (netcdf_fi_list_vars_v4(), file_netcdf.cc), which has no length or
	 * depth limit of its own -- unlike a single netCDF name, which IS
	 * capped at MAX_NC_NAME by the library. Two fixed-size local buffers
	 * below assume varname fits in MAX_NC_NAME: idx_slash[] (indexed by
	 * slash count) and ts[] (a copy of varname, silently truncated by
	 * snprintf if longer -- after which idx_slash[]'s indices, computed
	 * against the ORIGINAL untruncated varname, point past ts's real
	 * content). A netCDF-4 file with enough nested groups violates that
	 * assumption and both were real, file-triggerable stack-buffer
	 * overflows/overreads here, confirmed under ASan (Phase 10's fuzzing
	 * pass). One length guard up front covers both: bail out the same
	 * way this function already does a few lines down for an
	 * otherwise-impossible 'ig' value, rather than silently truncating
	 * and returning a wrong (or memory-unsafe) groupname.
	 */
	len = strlen( varname );
	if( len >= (size_t)MAX_NC_NAME ) {
		fprintf( stderr, "Error in unpack_groupname: varname is %zu characters "
			"(>= MAX_NC_NAME=%d), can't process: >%.100s...<\n",
			len, MAX_NC_NAME, varname );
		exit(-1);
		}

	/* Get indices of the slashes */
	nslash = 0;
	for( i=0; i<len; i++ ) {
		if( varname[i] == '/' ) {
			idx_slash[nslash] = i;
			nslash++;
			}
		}

	if( nslash == 0 ) {
		if (ig == -2 ) {
			/* Asked for varname only */
			snprintf( groupname, MAX_NC_NAME, "%s", varname );
			return(0);
			}
		else
			{
			/* If no slashes in the var name, must live in root group */
			snprintf( groupname, MAX_NC_NAME, "%s", "/" );
			return( 0 );
			}
		}

	if( ig > (nslash+1) ) {
		fprintf( stderr, "Error in unpack_groupname: varname: >%s< group to find (starting at 0)=%d invalid group to find (not this many groups in the varname)\n",
			varname, ig );
		exit(-1);
		}

	snprintf( ts, sizeof(ts), "%s", varname );

	if( ig == -2 ) {
		snprintf( groupname, MAX_NC_NAME, "%s", ts+idx_slash[nslash-1]+1 );
		return( 0 );
		}

	if( ig == -1 ) {
		ts[ idx_slash[nslash-1] ] = '\0';
		snprintf( groupname, MAX_NC_NAME, "%s", ts );
		return( 0 );
		}

	if( ig == 0 )
		i0 = 0;
	else
		i0 = idx_slash[ig-1] + 1;
	i1 = idx_slash[ig];
	ts[i1] = '\0';

	snprintf( groupname, MAX_NC_NAME, "%s", ts+i0 );

	return( 0 );
}

/*******************************************************************************************
 * Given a varname string of format: groupname0/groupname1/groupnameN/varname
 * this returns ONLY the trailing varname in "varname_sans_groups", and ONLY the
 * groupname with no leading or trailing slash ( "root/groupa" ) in "groupname"
 */
void varname_no_groups( const char *varname, char *varname_sans_groups, char *groupname )
{
	size_t	i, len;
	int	idx_slash[MAX_NC_NAME], nslash;

	/* Same overflow guard as unpack_groupname() just above, and for the
	 * same reason -- see its comment. */
	len = strlen( varname );
	if( len >= (size_t)MAX_NC_NAME ) {
		fprintf( stderr, "Error in varname_no_groups: varname is %zu characters "
			"(>= MAX_NC_NAME=%d), can't process: >%.100s...<\n",
			len, MAX_NC_NAME, varname );
		exit(-1);
		}

	nslash = 0;
	for( i=0; i<len; i++ ) {
		if( varname[i] == '/' ) {
			idx_slash[nslash] = i;
			nslash++;
			}
		}

	if( nslash == 0 ) {
		snprintf( varname_sans_groups, MAX_NC_NAME, "%s", varname );
		if( groupname != NULL )
			groupname[0] = '\0';
		return;
		}

	snprintf( varname_sans_groups, MAX_NC_NAME, "%s", varname+idx_slash[nslash-1]+1 );
	if( groupname != NULL ) {
		strncpy( groupname, varname, idx_slash[nslash-1] );
		groupname[ idx_slash[nslash-1] ] = '\0';
		}

	/*
	printf( "UUUU varname_no_groups, varname: >%s< varname_sans_groups: >%s< groupname: >%s<\n",
		varname,
		varname_sans_groups,
		((groupname == NULL) ? "NULL" : groupname));
	*/
}

/******************************************************************************
 * Returns the number of entries in the NCVarlist
 */
	int
n_vars_in_list( const std::vector<std::unique_ptr<NCVar>> &v )
{
	return( (int)v.size() );
}
