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

/*
	This provides an interface to the udunits package, version 2
	(http://www.unidata.ucar.edu/packages/udunits/index.html)
	that can be bypassed if desired.
*/

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "math.h"

static int		valid_udunits_pkg;
static int		is_unique( char *units_name );
static std::vector<std::string> uniq;

/******************************************************************************/
#ifdef HAVE_UDUNITS2

ut_system	*unitsys;

#include "udunits2.h"
#include "ncview/utCalendar2_cal.h"

/******************************************************************************/
void udu_utinit( char *path )
{
	/* Turn annoying "override" errors off */
	ut_set_error_message_handler( ut_ignore );

	/* ut_read_xml() checks $UDUNITS2_XML_PATH itself and then falls back
	 * to the path this build's UDUNITS-2 was configured with (an
	 * installed location under CMAKE_INSTALL_PREFIX). As a last resort
	 * -- e.g. running straight out of an uninstalled build tree -- point
	 * $UDUNITS2_XML_PATH itself at the vendored submodule's copy of
	 * udunits2.xml, so long as the caller hasn't set it already. This is
	 * done via the environment (not just here in udu_utinit) because
	 * utCalendar2_cal.cc has its own, independent ut_read_xml(NULL) call
	 * that needs the exact same fallback.
	 */
#ifdef NCVIEW_BUILD_TREE_UDUNITS2_XML
	if( getenv( "UDUNITS2_XML_PATH" ) == NULL )
		/* setenv() is POSIX-only -- MinGW-w64's Windows CRT provides
		 * _putenv_s() instead, with the value passed as one combined
		 * "NAME=value" string rather than separate arguments. */
#ifdef _WIN32
		_putenv_s( "UDUNITS2_XML_PATH", NCVIEW_BUILD_TREE_UDUNITS2_XML );
#else
		setenv( "UDUNITS2_XML_PATH", NCVIEW_BUILD_TREE_UDUNITS2_XML, 0 );
#endif
#endif
	unitsys = ut_read_xml( path );

	/* Turn errors back on */
	ut_set_error_message_handler( ut_write_to_stderr );

	if( unitsys != NULL )
		valid_udunits_pkg = 1;
	else
		{
		valid_udunits_pkg = 0;
		fprintf( stderr, "Note: Udunits-2 library could not be initialized; no units conversion will be attmpted.\n" );
		fprintf( stderr, "(To fix, put the path of the units file into environmental variable UDUNITS2_XML_PATH)\n");
		}
}

/******************************************************************************/
int udu_utistime( char *dimname, char *unitstr )
{
	int		ierr;
	static int	have_initted=0;
	ut_unit		*unit;
	static ut_unit	*time_unit_with_origin;

	if( (unitstr == NULL) || (!valid_udunits_pkg))
		return(0);

	if( (unit = ut_parse( unitsys, unitstr, UT_ASCII)) == NULL ) {
		/* can't parse unit spec */
		if( is_unique(unitstr) ) 
			fprintf( stderr, "Note: udunits: unknown units for %s: \"%s\"\n",
				dimname, unitstr );
		return( 0 );
		}

	if(!have_initted) {
		if( (time_unit_with_origin = ut_parse( unitsys, "seconds since 1901-01-01", UT_ASCII)) == NULL ) {
			fprintf( stderr, "Internal error: could not ut_parse time origin test string\n" );
			valid_udunits_pkg = 0;
			return(0);
			}
		have_initted = 1;
		}

	ierr = ut_are_convertible( unit, time_unit_with_origin );
	ut_free( unit );

	return( ierr != 0 );   /* Oddly, returns a non-zero number if ARE convertible */
}

/******************************************************************************/
TimeGranularity udu_calc_tgran( int fileid, NCVar *v, int dimid )
{
	ut_unit		*unit, *seconds, *seconds_since_epoch = NULL;
	int		ii;
	TimeGranularity	retval;
	int		verbose, has_bounds;
	double		tval0_user, tval0_sec, tval1_user, tval1_sec, delta_sec, bound_min, bound_max;
	char		cval0[1024], cval1[1024];
	size_t		cursor_place[MAX_NC_DIMS];
	cv_converter	*convert_units_to_sec;

	NCDim *d;
	d = v->dim[dimid].get();

	/* Not enough data to analyze */
	if( d->size < 3 )
		return(TimeGranularity::Sec);

	verbose = 0;

	if( ! valid_udunits_pkg )
		/* Preserved verbatim: upstream returned a bare 0 here, not
		 * one of the TGRAN_* values. */
		return( static_cast<TimeGranularity>(0) );

	if( (unit = ut_parse( unitsys, d->units.c_str(), UT_ASCII )) == NULL ) {
		fprintf( stderr, "internal error: udu_calc_tgran with invalid unit string: %s\n",
			d->units.c_str() );
		exit( -1 );
		}

	if( (seconds = ut_parse( unitsys, "seconds", UT_ASCII )) == NULL ) {
		fprintf( stderr, "internal error: udu_calc_tgran can't parse seconds unit string!\n" );
		exit( -1 );
		}

	/* Get converter to convert from "units" to "seconds". A bare interval
	 * unit ("days") is directly convertible to bare "seconds", but a
	 * CF-convention "units since <reference-date>" unit -- what every real
	 * netCDF time axis actually uses -- is UDUNITS-2 "encoded time", which
	 * ut_are_convertible() reports as NOT convertible to a plain,
	 * unreferenced "seconds". Encoded-time units convert fine to *another*
	 * encoded-time unit, though (the reference date doesn't need to match;
	 * it only shifts the offset, and we only ever look at a difference
	 * between two converted values below), so fall back to comparing
	 * against a "seconds since <epoch>" unit before giving up.
	 */
	if( ut_are_convertible( unit, seconds ) == 0 ) {
		if( (seconds_since_epoch = ut_parse( unitsys, "seconds since 1970-01-01 00:00:00", UT_ASCII )) == NULL ) {
			fprintf( stderr, "internal error: udu_calc_tgran can't parse epoch seconds unit string!\n" );
			exit( -1 );
			}
		if( ut_are_convertible( unit, seconds_since_epoch ) == 0 ) {
			/* Units are genuinely not convertible to any notion of seconds */
			ut_free( unit );
			ut_free( seconds );
			ut_free( seconds_since_epoch );
			return( TimeGranularity::Sec );
			}
		ut_free( seconds );
		seconds = seconds_since_epoch;
		}
	if( (convert_units_to_sec = ut_get_converter( unit, seconds )) == NULL ) {
		/* This shouldn't happen */
		ut_free( unit );
		ut_free( seconds );
		return( TimeGranularity::Sec );
		}

	/* Get a delta time to analyze */
	for( ii=0L; ii<v->n_dims; ii++ )
		cursor_place[ii] = (int)((v->size[ii])/2.0);
	g_app.session.dataset().dimValue( v, dimid, 1L, &tval0_user, cval0, &has_bounds, &bound_min, &bound_max, cursor_place );
	g_app.session.dataset().dimValue( v, dimid, 2L, &tval1_user, cval1, &has_bounds, &bound_min, &bound_max, cursor_place );

	/* Convert time vals from user units to seconds */
	tval0_sec = cv_convert_double( convert_units_to_sec, tval0_user );
	tval1_sec = cv_convert_double( convert_units_to_sec, tval1_user );

	/* Free our units converter storage */
	cv_free( convert_units_to_sec );

	delta_sec = fabs(tval1_sec - tval0_sec);
	
	if( verbose )
		printf( "units: %s  t1: %lf t2: %lf delta-sec: %lf\n", d->units.c_str(), tval0_user, tval1_user, delta_sec );

	if( delta_sec < 57. ) {
		if(verbose)
			printf("data is TGRAN_SEC\n");
		retval = TimeGranularity::Sec;
		}
	else if( delta_sec < 3590. ) {
		if(verbose)
			printf("data is TGRAN_MIN\n");
		retval = TimeGranularity::Min;
		}
	else if( delta_sec < 86300. ) {
		if(verbose)
			printf("data is TGRAN_HOUR\n");
		retval = TimeGranularity::Hour;
		}
	else if( delta_sec < 86395.*28. ) {
		if(verbose)
			printf("data is TGRAN_DAY\n");
		retval = TimeGranularity::Day;
		}
	else if(  delta_sec < 86395.*365. ) {
		if(verbose)
			printf("data is TGRAN_MONTH\n");
		retval = TimeGranularity::Month;
		}
	else
		{
		if(verbose)
			printf("data is TGRAN_YEAR\n");
		retval = TimeGranularity::Year;
		}

	ut_free( unit );
	ut_free( seconds );

	return( retval );
}

/******************************************************************************/
void udu_fmt_time( char *temp_string, size_t temp_string_len, double new_dimval, NCDim *dim, int include_granularity )
{
	static  ut_unit	*dataunits=NULL;
	int	year, month, day, hour, minute, debug;
	double	second;
	static  char last_units[1024];
	static	char months[12][4] = { "Jan", "Feb", "Mar", "Apr",
				       "May", "Jun", "Jul", "Aug",
				       "Sep", "Oct", "Nov", "Dec"};

	debug = 0;
	if( debug ) fprintf( stderr, "udu_fmt_time: entering with dim=%s, units=%s, value=%f\n",
		dim->name.c_str(), dim->units.c_str(), new_dimval );

	if( ! valid_udunits_pkg ) {
		snprintf( temp_string, temp_string_len-1, "%lg", new_dimval );
		return;
		}

	if( (dim->units.size() > 1023) || strcmp(dim->units.c_str(),last_units) != 0 ) {
		if( dataunits != NULL )	/* see if left over from previous invocation */
			ut_free( dataunits );
		if( (dataunits = ut_parse( unitsys, dim->units.c_str(), UT_ASCII )) == NULL ) {
			fprintf( stderr, "internal error: udu_fmt_time can't parse data unit string!\n" );
			fprintf( stderr, "problematic units: \"%s\"\n", dim->units.c_str() );
			fprintf( stderr, "dim->name: %s  dim->timelike: %d\n", dim->name.c_str(), dim->timelike );
			exit( -1 );
			}
		strncpy( last_units, dim->units.c_str(), 1023 );
		}

	if( utCalendar2_cal( new_dimval, dim->units.c_str(), &year, &month, &day, &hour,
				&minute, &second, dim->calendar.c_str() ) != 0 ) {
		fprintf( stderr, "internal error: udu_fmt_time can't convert to calendar value!\n");
		fprintf( stderr, "units: >%s<\n", dim->units.c_str() );
		exit( -1 );
		}

	if( debug ) {
		fprintf( stderr, "udu_fmt_time: dimval=%lf units=%s calendar=%s utCalendar2_cal returns: year=%d month=%d day=%d hour=%d minute=%d second=%lf\n",
			new_dimval, dim->units.c_str(), dim->calendar.c_str(), year, month, day, hour, minute, second );
		}
	
	if( include_granularity ) {
		switch( dim->tgran ) {
			
			case TimeGranularity::Year:
			case TimeGranularity::Month:
			case TimeGranularity::Day:
				snprintf( temp_string, temp_string_len-1, "%1d-%s-%04d", day, months[month-1], year );
				break;

			case TimeGranularity::Hour:
			case TimeGranularity::Min:
				snprintf( temp_string, temp_string_len-1, "%1d-%s-%04d %02d:%02d", day, 
						months[month-1], year, hour, minute );
				break;

			default:
				snprintf( temp_string, temp_string_len-1, "%1d-%s-%04d %02d:%02d:%02.0f",
					day, months[month-1], year, hour, minute, second );
			}
		}
	else
		{
		snprintf( temp_string, temp_string_len-1, "%1d-%s-%04d %02d:%02d:%02.0f",
				day, months[month-1], year, hour, minute, second );
		}
}

/******************************************************************************/
	static int 
is_unique( char *units )
{
	for( const auto &name : uniq )
		if( name == units )
			return( false );

	uniq.emplace_back( units );
	return( true );
}

/* No `#else` stub branch here: core/CMakeLists.txt unconditionally
 * defines HAVE_UDUNITS2 and always builds the vendored UDUNITS-2, so a
 * no-udunits build is not a supported configuration today (see
 * PORTING.md's "refine the architecture" plan, Baseline section) -- the
 * stub implementations of udu_utinit/udu_utistime/udu_calc_tgran/
 * udu_fmt_time that used to live in an `#else` here could never actually
 * be compiled, and were deleted as dead code (Phase 3d) rather than kept
 * as a fallback for a configuration axis that doesn't exist. */
#endif
