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
 *	The file interface to ncview.
 *
 *	All the routines in this file must be provided for whatever
 *	format data file you want to have.  Ideally, all the information
 *    	about the data file formats should be encapsulated here.
 *
 *	Phase 12c: this file used to dispatch every entry point on a
 *	`static int file_type` set by determine_file_type() -- but netCDF has
 *	been the only format ncview has ever opened, FILE_TYPE_NETCDF
 *	(defines.h) was the only constant of its kind, and file_type was
 *	assigned that value in exactly one place. The dispatch was real but
 *	permanently dead code (every `else` branch exit(-1)'d on a value
 *	that could never occur), simplified away rather than kept as
 *	indirection for a second backend that has never existed and isn't
 *	planned -- see the plan file's Part V "declined" items for the
 *	reasoning against building a real multi-backend interface instead.
 *
 *****************************************************************************/

#include "ncview/includes.h"
#include "ncview/dataset.h"	/* NetCDFFile -- fi_initialize()'s file->listVars() below */
#include "ncview/defines.h"
#include "ncview/protos.h"

extern Options options;

/************************************************************************************/
/* Do all file opening and initialization for the passed filename.
 * Return a unique integer ID by which this file will be indicated
 * in the future.
 */
	int
fi_initialize( char *name )
{
	int	id;
	Stringlist *var_list;
	NetCDFFile *file;

	if( options.debug )
		printf( "Initializing file %s\n", name );
	id = netcdf_fi_initialize( name );

	if( options.debug )
		printf( "Getting list of variables for file %s\n", name );
	/* trackFile() here (rather than leaving Dataset::addVariables() to
	 * discover this fileid on its own) is what lets listVars() below be a
	 * NetCDFFile method instead of the free-function fi_list_vars() this
	 * replaced (Phase 6) -- trackFile() is idempotent by fileid, so
	 * addVariables()'s own per-variable trackFile() calls just return the
	 * same object. */
	file = g_app.session.dataset().trackFile( id );
	var_list = file->listVars();
	g_app.session.dataset().addVariables( var_list, id, name );

	if( options.debug )
		printf( "Done initializing file %s\n", name );

	return( id );
}

/************************************************************************************
 * Return the 'calendar' attribution of a dimension, if appropriate.  Otherwise, return empty.
 *
 * Stayed a free function through Phase 6 of the "refine the architecture"
 * plan (unlike its 13 sibling forwarders, now NetCDFFile methods --
 * dataset.h/dataset.cc): this one adds real logic (the command-line
 * override below) beyond dispatch, so it isn't a pure forwarder.
 */
	std::string
fi_dim_calendar( int fileid, std::string_view dim_name )
{
	/* Command line specified calendar OVERRIDES info in the file */
	if( ! options.calendar.empty() )
		return options.calendar;

	return( netcdf_dim_calendar( fileid, dim_name ));
}

/************************************************************************************
 * Close the relevant file
 */
	void
fi_close( int fileid )
{
	netcdf_fi_close( fileid );
}

/*************************************************************************************
 * File utility routines; things below this line shouldn't have to be changed
 * for different data file formats.
 */

	void
determine_file_type( Stringlist *input_files )
{
	int		ierr;
	struct stat 	buf;

	if( input_files == NULL ) {
		fprintf( stderr, "ncview: takes at least one file name as argument\n" );
		useage();
		exit( -1 );
		}

	const char *first_file = (*input_files)[0].string.c_str();

	if( ! netcdf_fi_confirm( (char *)first_file ) )
		{
		ierr = stat( first_file, &buf );
		if( ierr == 0 ) {
			fprintf( stderr, "ncview: can't recognize format of input file %s\n",
				first_file );
			exit( -1 );
			}
		else
			{
			fprintf( stderr, "ncview: can't open file %s",
				first_file );
			perror(" ");
			exit( -1 );
			}
		}
}
