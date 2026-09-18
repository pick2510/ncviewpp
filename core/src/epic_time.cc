/*
	This first part is taken directly from the PMEL EPIC library, which has the 
	following contact information attached:

	--------------------------------------------------------------------------
	Willa Zhu                           Phone:    206-526-6208
	NOAA/PMEL/OCRD                      FAX:      206-526-6744
	7600 Sand Point Way NE              OMNET:    TAO.PMEL
	Seattle, WA 98115                   Internet: willa@noaapmel.gov

*/

#include "ncview/includes.h"
#include "ncview/dataset.h"	/* NetCDFFile -- handle_time_dim()/months_calc_tgran() (Phase 7b) */
#include "ncview/defines.h"
#include "ncview/protos.h"

#define JULGREG   2299161

/* handle_time_dim()/months_calc_tgran()/fmt_time() (formerly util.cc, moved
 * here Phase 4b of the "refine the architecture" plan, group 3) are the
 * TimeStandard dispatch layer shared across this file's Epic0 functions and
 * udu.cc's Udunits ones -- fmt_time() calls into both. Moved here rather
 * than into udu.cc since epic_time.cc is the smaller, less central of the
 * two calendar backends. */
static TimeGranularity  months_calc_tgran( NetCDFFile *file, NCDim *d );

/* Variables local to routines in this file */
static  const char *month_name[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

void
ep_time_to_mdyhms(long *time, int *mon, int *day, int *yr, int *hour, int *min, float *sec)
{
/*
 * convert eps time format to mdy hms
 */
  long ja, jalpha, jb, jc, jd, je;

  while(time[1] >= 86400000) { /* increament days if ms larger then one day */
    time[0]++;
    time[1] -= 86400000;
  }

  if(time[0] >= JULGREG) {
    jalpha=(long)(((double) (time[0]-1867216)-0.25)/36524.25);
    ja=time[0]+1+jalpha-(long)(0.25*jalpha);
  } else
    ja=time[0];

  jb=ja+1524;
  jc=(long)(6680.0+((double)(jb-2439870)-122.1)/365.25);
  jd=(long)(365*jc+(0.25*jc));
  je=(long)((jb-jd)/30.6001);
  *day=jb-jd-(int)(30.6001*je);
  *mon=je-1;
  if(*mon > 12) *mon -= 12;
  *yr=jc-4715;
  if(*mon > 2) --(*yr);

  if(*yr <=0) --(*yr);

  ja = time[1]/1000;
  *hour = ja/3600;
  *min = (ja - (*hour)*3600)/60;
  *sec = (float)(time[1] - ((*hour)*3600 + (*min)*60)*1000)/1000.0f;
}

/*************************************************************************/
	int
epic_istime0( int fileid, NCVar *v, NCDim *d )
{
	if( (!d->units.empty()) && strncmp( d->units.c_str(), "True Julian Day", 15 ) == 0 ) {
		return( 1 );
		}
	
	return( 0 );
}


/*************************************************************************/
	TimeGranularity
epic_calc_tgran( int fileid, NCDim *d )
{
	/* EPIC is strange because it can use TWO dimvars for time.
	 * don't know how to handle the millisecond part yet, so
	 * just fake it by indicating day-like granularity.  Will
	 * have to be fixed at some point.
	 */
	return( TimeGranularity::Day );	
}

/*************************************************************************/
	void
epic_fmt_time( char *temp_string, size_t temp_string_len, double new_dimval, NCDim *dim )
{
	long	epic_time[2];
	int	mon, day, yr, hour, min;
	float	sec;
	static  char months[12][4] = { 	"Jan", "Feb", "Mar", "Apr",
					"May", "Jun", "Jul", "Aug",
					"Sep", "Oct", "Nov", "Dec"};

	epic_time[0] = (long)new_dimval;
	epic_time[1] = 0L;
	ep_time_to_mdyhms(epic_time, &mon, &day, &yr, &hour, &min, &sec);

	/* ep_time_to_mdyhms()'s Julian-day math only clamps the upper end
	 * (mon>12 -> mon-=12); an out-of-range EPIC time value can still
	 * produce mon<=0 here, reading out of bounds on months[mon-1]. */
	if( mon < 1 ) mon = 1;
	if( mon > 12 ) mon = 12;

	snprintf( temp_string, temp_string_len, "%1d-%s-%04d %02d:%02d", day, months[mon-1],
		yr, hour, min );
	temp_string[ temp_string_len-1 ] = '\0';
}

/******************************************************************************/
	void
handle_time_dim( NetCDFFile *file, NCVar *v, int dimid )
{
	NCDim   *d;
	int	fileid = file->id();

	d = v->dim[dimid].get();

	if( udu_utistime( const_cast<char *>(d->name.c_str()), const_cast<char *>(d->units.c_str()) ) ) {
		d->timelike = 1;
		d->time_std = TimeStandard::Udunits;
		d->tgran    = udu_calc_tgran( fileid, v, dimid );
		}
	else if( epic_istime0( fileid, v, d )) {
		d->timelike = 1;
		d->time_std = TimeStandard::Epic0;
		d->tgran    = epic_calc_tgran( fileid, d );
		}
	else if( (!d->units.empty()) &&
		 (d->units.size() >= 5) &&
		 (strncasecmp( d->units.c_str(), "month", 5 ) == 0 ))  {
		d->timelike = 1;
		d->time_std = TimeStandard::Months;
		d->tgran    = months_calc_tgran( file, d );
		}
	else
		d->timelike = 0;
}

/******************************************************************************/
	static TimeGranularity
months_calc_tgran( NetCDFFile *file, NCDim *d )
{
	char	temp_string[1024];
	float	delta, v0, v1;
	int	type, has_bounds;
	double	temp_double, bounds_min, bounds_max;

	if( d->size < 2 ) {
		return( TimeGranularity::Day );
		}

	type = file->dimValue( const_cast<char *>(d->name.c_str()), 0L, &temp_double, temp_string, 0L, &has_bounds, &bounds_min, &bounds_max );
	if( type == NC_DOUBLE )
		v0 = (float)temp_double;
	else
		{
		fprintf( stderr, "Note: can't calculate time granularity, unrecognized timevar type (%d)\n",
			type );
		return( TimeGranularity::Day );
		}

	type = file->dimValue( const_cast<char *>(d->name.c_str()), 1L, &temp_double, temp_string, 1L, &has_bounds, &bounds_min, &bounds_max );
	if( type == NC_DOUBLE )
		v1 = (float)temp_double;
	else
		{
		fprintf( stderr, "Note: can't calculate time granularity, unrecognized timevar type (%d)\n",
			type );
		return( TimeGranularity::Day );
		}

	delta = v1 - v0;

	if( delta > 11.5 )
		return( TimeGranularity::Year );
	if( delta > .95 )
		return( TimeGranularity::Month );
	if( delta > .03 )
		return( TimeGranularity::Day );

	return( TimeGranularity::Min );
}

/******************************************************************************/
void fmt_time( char *temp_string, size_t temp_string_len, double new_dimval, NCDim *dim, int include_granularity )
{
	int 	year, month, day;

	if( ! dim->timelike ) {
		fprintf( stderr, "ncview: internal error: fmt_time called on non-timelike axis!\n");
		fprintf( stderr, "dim name: %s\n", dim->name.c_str() );
		exit( -1 );
		}

	if( dim->time_std == TimeStandard::Udunits )
		udu_fmt_time( temp_string, temp_string_len, new_dimval, dim, include_granularity );

	else if( dim->time_std == TimeStandard::Epic0 )
		epic_fmt_time( temp_string, temp_string_len, new_dimval, dim );

	else if( dim->time_std == TimeStandard::Months ) {
		/* Format for months standard */
		year  = (int)( (new_dimval-1.0) / 12.0 );
		month = (int)( (new_dimval-1.0) - year*12 + .01 );
		month = (month < 0) ? 0 : month;
		month = (month > 11) ? 11 : month;
		day   =
		   (int)( ((new_dimval-1.0) - year*12 - month) * 30.0) + 1;
		snprintf( temp_string, temp_string_len-1, "%s %2d %4d", month_name[month],
				day, year+1 );
		}

	else
		{
		fprintf( stderr, "Internal error: uncaught value of tim_std=%d\n", static_cast<int>(dim->time_std) );
		exit( -1 );
		}
}

