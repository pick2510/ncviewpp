/*
 * core/src/dataset.cc
 *
 * Copyright (C) 1993 through 2024 David W. Pierce
 * Modifications Copyright (C) 2026 Dominik Strebel
 *
 * See ncview/dataset.h. OOP_redesign plan, Step 5.
 */
#include "ncview/dataset.h"
#include "ncview/viewer_ui.h"

#include "ncview/includes.h"
#include "ncview/protos.h"	/* netcdf_fi_close(), netcdf_fi_get_data(), netcdf_dim_value(),
				 * netcdf_fill_value(), virt_to_actual_place() */

#ifdef HAVE_UDUNITS2
#include "udunits2.h"
#include "ncview/utCalendar2_cal.h"	/* utCalendar2_cal(), utInvCalendar2_cal() -- dimValueConvert() */
extern ut_system *unitsys;
#endif

extern Options options;

void NetCDFFile::close()
{
	/* netcdf_fi_close(), not fi_close(): NetCDFFile is already
	 * irrevocably the netCDF backend (it's in the name), so routing its
	 * own close through file.cc's file_type dispatch buys nothing and
	 * costs a real invariant -- fi_close() exit(-1)s unless
	 * determine_file_type() has already run in this process, which
	 * every existing production caller happens to guarantee (fi_close()
	 * was previously reachable only via Dataset::trackFile(), itself
	 * only reachable via fi_initialize(), which is only ever called
	 * after determine_file_type()). NetCDFFile::open() (Phase 6) can
	 * construct a NetCDFFile without that ever having run -- caught by
	 * a shuffled-order ctest run exit(-1)ing outright the first time a
	 * NetCDFFile opened via open() destructed before any other test in
	 * the process had called determine_file_type(). */
	if( fileid_ >= 0 ) {
		netcdf_fi_close( fileid_ );
		fileid_ = -1;
		}
}

NetCDFFile::~NetCDFFile()
{
	close();
}

std::optional<NetCDFFile> NetCDFFile::open( const std::string &path, int *nc_errcode )
{
	int fileid, ierr;

	ierr = nc_open( path.c_str(), NC_NOWRITE, &fileid );
	if( nc_errcode != nullptr )
		*nc_errcode = ierr;
	if( ierr != NC_NOERR )
		return std::nullopt;

	return NetCDFFile( fileid );
}

/* The 13 single-file fi_*() forwarders, collapsed onto NetCDFFile -- see
 * dataset.h's comment above the declarations for why these bodies are
 * exactly the netcdf_*() call file.cc's forwarders used to make. */
Stringlist *NetCDFFile::listVars() const { return netcdf_fi_list_vars( fileid_ ); }
std::string NetCDFFile::title() const { return netcdf_title( fileid_ ); }
std::string NetCDFFile::longVarName( std::string_view var_name ) const { return netcdf_long_var_name( fileid_, var_name ); }
std::string NetCDFFile::varUnits( std::string_view var_name ) const { return netcdf_var_units( fileid_, var_name ); }
std::string NetCDFFile::dimUnits( std::string_view dim_name ) const { return netcdf_dim_units( fileid_, dim_name ); }
int NetCDFFile::nDims( char *var_name ) const { return netcdf_fi_n_dims( fileid_, var_name ); }
Stringlist *NetCDFFile::scannableDims( char *var_name ) const { return netcdf_scannable_dims( fileid_, var_name ); }
size_t *NetCDFFile::varSize( char *var_name ) const { return netcdf_fi_var_size( fileid_, var_name ); }
std::string NetCDFFile::dimIdToName( std::string_view var_name, int dim_id ) const { return netcdf_dim_id_to_name( fileid_, var_name, dim_id ); }
int NetCDFFile::dimNameToId( char *var_name, char *dim_name ) const { return netcdf_dim_name_to_id( fileid_, var_name, dim_name ); }
std::string NetCDFFile::dimLongname( std::string_view dim_name ) const { return netcdf_dim_longname( fileid_, dim_name ); }
int NetCDFFile::recdimId() const { return netcdf_fi_recdim_id( fileid_ ); }
void NetCDFFile::fillAuxData( char *var_name, FDBlist *fdb ) const { netcdf_fill_aux_data( fileid_, var_name, fdb ); }

/* Four more single-file forwarders, added in Phase 7b to close out the
 * residue Phase 6 left behind -- see the comment above their declarations
 * in dataset.h. */
std::string NetCDFFile::charAtt( std::string_view var_name, std::string_view att_name ) const { return netcdf_get_char_att( fileid_, var_name, att_name ); }
std::string NetCDFFile::attString( std::string_view var_name ) const { return netcdf_att_string( fileid_, var_name ); }
nc_type NetCDFFile::dimValue( char *dim_name, size_t place, double *ret_val_double, char *ret_val_char,
	size_t virt_place, int *return_has_bounds, double *return_bounds_min, double *return_bounds_max ) const
{
	return netcdf_dim_value( fileid_, dim_name, place, ret_val_double, ret_val_char, virt_place,
		return_has_bounds, return_bounds_min, return_bounds_max );
}
void NetCDFFile::getData( char *var_name, size_t *start_pos, size_t *count, float *data ) const { netcdf_fi_get_data( fileid_, var_name, start_pos, count, data, NULL ); }

NetCDFFile *Dataset::trackFile( int fileid )
{
	for( auto &f : files_ )
		if( f->id() == fileid )
			return f.get();

	files_.push_back( std::make_unique<NetCDFFile>( fileid ) );
	return files_.back().get();
}

int FDBlist::id() const
{
	return file ? file->id() : 0;
}

namespace {

/* Formerly util.cc's new_netcdf(): allocate and default-initialize a new
 * NetCDFOptions. `new`, not malloc(): the caller (new_fdblist(), directly
 * below) immediately hands the result to a std::unique_ptr<NetCDFOptions>,
 * whose default deleter calls `delete`, not `free()`. This was a real
 * malloc/delete mismatch -- caught by ASan's alloc-dealloc-mismatch check
 * only once something actually destroyed a Dataset mid-process (Phase 0a
 * of the "refine the architecture" plan's SessionFixture). Moved here
 * (Phase 4b, dissolving util.cc) since new_fdblist() is its only caller;
 * no longer needs external linkage. */
void new_netcdf( NetCDFOptions **n )
{
	(*n) = new NetCDFOptions();
	(*n)->valid_range_set  = false;
	(*n)->valid_min_set    = false;
	(*n)->valid_max_set    = false;
	(*n)->scale_factor_set = false;
	(*n)->add_offset_set   = false;

	(*n)->valid_range[0] = 0.0;
	(*n)->valid_range[1] = 0.0;
	(*n)->valid_min      = 0.0;
	(*n)->valid_max      = 0.0;
	(*n)->scale_factor   = 1.0;
	(*n)->add_offset     = 0.0;
}

/* Formerly util.cc's new_fdblist(): allocate a new FDBlist element with
 * its NetCDFOptions aux_data slot initialized. Only used by
 * Dataset::addVariable(). */
std::unique_ptr<FDBlist> new_fdblist()
{
	auto el = std::make_unique<FDBlist>();
	NetCDFOptions *new_netcdf_options;

	el->filename = "UNINITIALIZED";

#ifdef HAVE_UDUNITS2
	el->ut_unit_ptr = NULL;
#endif

	new_netcdf( &new_netcdf_options );
	el->aux_data.reset( new_netcdf_options );

	return( el );
}

/* Formerly util.cc's equivalent_FDBs(): true if v1 and v2 live in exactly
 * the same sequence of files. Only used by
 * Dataset::copyInfoToIdenticalDims(). */
int equivalent_FDBs( NCVar *v1, NCVar *v2 )
{
	if( v1->files.size() != v2->files.size() )
		return(0); /* files differ */

	for( size_t i=0; i<v1->files.size(); i++ )
		if( v1->files[i]->id() != v2->files[i]->id() )
			return(0); /* files differ */

	return(1);
}

/* Formerly file.cc's fi_dim_value_convert(): reconciles a dimension value
 * read from one file in a multi-file series against the time units the
 * FIRST file uses, when the files disagree (e.g. each file's own
 * "days since <its own start date>"). Only called from dimValue() below,
 * both before and after this moved out of file.cc, so it stays a free
 * function rather than a Dataset method -- it doesn't touch Dataset
 * state, only its FDBlist/NCVar/NCDim arguments. May ALTER *dimval. */
void dimValueConvert( double *dimval, FDBlist *file, NCVar *var, NCDim *d )
{
#ifdef HAVE_UDUNITS2
	double converted_dimval;
	int	year0, month0, hour0, min0, day0, err;
	double	sec0;

	FDBlist *first_file = var->files.front().get();
	if( (file->recdim_units.empty()) ||
	    (first_file->recdim_units.empty()) ||
	    (first_file->ut_unit_ptr  == NULL) ||
	    (file->ut_unit_ptr 		   == NULL) ||
	    (! d->timelike )                        ||
	    (file->recdim_units == first_file->recdim_units) )
	    	return;

	/* Convert the dim value to a date using the units given
	 * in the file that this dim value came from
	 */
	err = utCalendar2_cal( *dimval, file->recdim_units.c_str(),
		&year0, &month0, &day0, &hour0, &min0, &sec0, d->calendar.c_str() );
	if( err == 0 ) {
		err = utInvCalendar2_cal( year0, month0, day0, hour0, min0, sec0,
			first_file->recdim_units.c_str(), &converted_dimval,
			d->calendar.c_str() );
		if( err == 0 )
			*dimval = converted_dimval;
		}
#endif
}

} // namespace

void Dataset::getData( NCVar *var, size_t *virt_start_pos, size_t *count, void *data )
{
	FDBlist	*file;

	/* Check to see if we should loop over the timelike indices
	 */
	if( (var->is_virtual == true) && (count[0] > 1) ) {
		getDataIterate( var, virt_start_pos, count, data );
		return;
		}

	std::vector<size_t> act_start_pos_buf( var->n_dims );
	size_t *act_start_pos = act_start_pos_buf.data();
	virt_to_actual_place( var, virt_start_pos, act_start_pos, &file );

	/* Always netCDF (file.cc's file_type dispatch this used to go
	 * through only ever held FILE_TYPE_NETCDF -- confirmed dead in
	 * Phase 6's step 1; calling netcdf_fi_get_data() directly here
	 * instead is part of the same collapse, not a new simplification
	 * invented for this move). */
	netcdf_fi_get_data( file->id(), const_cast<char *>(var->name.c_str()), act_start_pos,
		  count, (float *)data, file->aux_data.get() );
}

/*****************************************************************************
 * This is called when a variable lives in multiple files AND we
 * want data from more than one file.  We must iterate over the files.
 */
void Dataset::getDataIterate( NCVar *var, size_t *virt_start_pos, size_t *count, void *data )
{
	size_t	it, start2[MAX_NC_DIMS], count2[MAX_NC_DIMS], prod_lower_dims;
	FDBlist	*file;
	int	i;

	std::vector<size_t> act_start_pos_buf( var->n_dims );
	size_t *act_start_pos = act_start_pos_buf.data();

	prod_lower_dims = 1L;
	for( i=1; i<var->n_dims; i++ ) {
		start2[i] = virt_start_pos[i];
		count2[i] = count[i];
		prod_lower_dims *= count[i];
		}

	count2[0] = 1L;
	for( it=virt_start_pos[0]; it<(virt_start_pos[0]+count[0]); it++ ) {
		start2[0] = it;
		virt_to_actual_place( var, start2, act_start_pos, &file );
		netcdf_fi_get_data( file->id(), const_cast<char *>(var->name.c_str()), act_start_pos,
			  count2, ((float *)data)+(it-virt_start_pos[0])*prod_lower_dims,
			  	file->aux_data.get() );
		}
}

/*************************************************************************************
 * Return the value of a dimension at a specific point.  Returns the type
 * of the dimension value, which is either NC_DOUBLE or NC_CHAR.  Make sure
 * to allocate space for at least a 1024 character string in the return_value!
 * It will never be larger than that.  Takes a virtual place, and converts
 * it to an actual place before determining the value.
 *
 * Phase 12h: this never aborts the process either -- netcdf_dim_value()
 * degrades to a virt_place-derived NC_DOUBLE (or an unbounded real value)
 * on any read/shape failure it can't make sense of, rather than exiting.
 */
nc_type Dataset::dimValue( NCVar *var, int dim_id, size_t virt_place, double *return_val_double,
	char *return_val_char, int *return_has_bounds, double *return_bounds_min,
	double *return_bounds_max, size_t *complete_ndim_virt_place )
{
	size_t	actual_place;
	FDBlist	*file;
	int	i;
	std::string	dim_name;
	nc_type	ret_val;
	NCDim	*d;
	size_t	idx_map;
	NCDim_map_info	*dmi;

	/* See if this dim value is actually 2-d mapped */
	dmi = var->dim_map_info[dim_id].get();
	if( dmi != NULL ) {
		/* It IS 2-d mapped, calculate entry in data cache where val is */
		idx_map = 0L;
		for( i=0; i<var->n_dims; i++ ) {
			idx_map += complete_ndim_virt_place[i] * dmi->index_place_factor[i];
			}
		*return_val_double = dmi->data_cache[idx_map];
		/* Phase 12f: this early return used to leave *return_has_bounds
		 * (and the two bounds values) uninitialized -- a 2-D-mapped
		 * dim has no bounds concept, so say so explicitly rather than
		 * leaving the caller to read garbage off the stack. */
		*return_has_bounds = 0;
		*return_bounds_min = 0.0;
		*return_bounds_max = 0.0;
		return( NC_DOUBLE );
		}

	std::vector<size_t> act_start_pos_buf( var->n_dims );
	size_t *act_start_pos = act_start_pos_buf.data();
	std::vector<size_t> virt_start_pos_buf( var->n_dims );
	size_t *virt_start_pos = virt_start_pos_buf.data();

	for( i=0; i<var->n_dims; i++ )
		*(virt_start_pos+i) = 0L;
	*(virt_start_pos+dim_id) = virt_place;

	virt_to_actual_place( var, virt_start_pos, act_start_pos, &file );

	actual_place = *(act_start_pos+dim_id);

	d = (var->dim[dim_id].get());
	dim_name  = d->name;
	/* Always netCDF -- see getData()'s comment above for why the old
	 * file_type dispatch is gone rather than carried over. */
	ret_val = netcdf_dim_value( file->id(), const_cast<char *>(dim_name.c_str()), actual_place,
			return_val_double, return_val_char, virt_place,
			return_has_bounds, return_bounds_min, return_bounds_max );

#ifdef HAVE_UDUNITS2
	/* Now we have to figure out if we need to change units on the
	 * returned value...This will happen with timelike dimensions that
	 * have a different units string in each file.
	 */
	if( ret_val != NC_CHAR) {
		dimValueConvert( return_val_double, file, var, d );
		/* Phase 12f: only convert the bounds values when
		 * netcdf_dim_value() actually reported bounds -- otherwise
		 * *return_bounds_min / *return_bounds_max are the "no bounds"
		 * default (0.0) and running them through unit conversion is
		 * meaningless at best. Before this fix these two out-params
		 * were, on the has-bounds-only write path, exactly the two
		 * that could still be uninitialized stack memory (see
		 * netcdf_dim_value()'s own Phase 12f fix). */
		if( *return_has_bounds ) {
			dimValueConvert( return_bounds_min, file, var, d );
			dimValueConvert( return_bounds_max, file, var, d );
			}
		}
#endif

	return( ret_val );
}

/*******************************************************************************
 * If the file format we are currently using defines a "fill value" (i.e.,
 * a special data value which indicates out-of-domain or never-written data)
 * then set the value to that fill value.  Otherwise, don't change it.
 */
void Dataset::fillValue( NCVar *var, float *fill_value )
{
	/* Always netCDF -- see getData()'s comment above. */
	netcdf_fill_value( var->files.front()->id(), const_cast<char *>(var->name.c_str()),
			fill_value, var->files.front()->aux_data.get() );
}

NCVar *Dataset::findVariable( const char *var_name )
{
	for( auto &vptr : variables_ )
		if( vptr->name == var_name )
			return( vptr.get() );

	return( NULL );
}

void Dataset::addVariables( Stringlist *var_list, int id, const char *filename )
{
	if( options.debug )
		printf( "add_vars_to_list: entering, adding vars to list for file %s\n", filename );
	if( var_list != NULL )
	for( auto &e : *var_list ) {
		if( options.debug )
			printf( "adding variable %s to list\n", e.string.c_str() );
		addVariable( e.string.c_str(), id, filename );
		}
	if( options.debug )
		printf( "done adding vars for file %s\n", filename );
}

void Dataset::addVariable( const char *var_name, int file_id, const char *filename )
{
	NCVar	*var;
	int	n_dims;

	/* make a new file description entry for this var/file combo */
	auto new_fdb_owner = new_fdblist();
	FDBlist *new_fdb = new_fdb_owner.get();
	new_fdb->file     = trackFile( file_id );
	{
	size_t *raw_size = new_fdb->file->varSize( const_cast<char *>(var_name) );
	int raw_n_dims = new_fdb->file->nDims( const_cast<char *>(var_name) );
	new_fdb->var_size.assign( raw_size, raw_size + raw_n_dims );
	free( raw_size );
	}
	if( strlen(filename) > (MAX_FILE_NAME_LEN-1)) {
		fprintf( stderr, "Error, input file name is too long; longest I can handle is %d\nError occurred on file %s\n",
			MAX_FILE_NAME_LEN, filename );
		exit(-1);
		}
	new_fdb->filename = filename;

	/* fill out auxiliary (data-file format dependent) information
	 * for the new fdb.
	 */
	new_fdb->file->fillAuxData( const_cast<char *>(var_name), new_fdb );
#ifdef HAVE_UDUNITS2
	new_fdb->ut_unit_ptr = ut_parse( unitsys, new_fdb->recdim_units.c_str(), UT_ASCII ); /* Will be NULL if there was an error */
#endif

	/* Does this variable already have an entry on the list? */
	var = findVariable( var_name );
	if( var == NULL ) {	/* NO -- make a new NCVar structure */
		auto new_var_owner = std::make_unique<NCVar>();
		NCVar *new_var = new_var_owner.get();
		new_var->name       = var_name;
		n_dims              = new_fdb->file->nDims( const_cast<char *>(var_name) );
		new_var->n_dims     = n_dims;
		if( options.debug )
			printf( "adding variable %s with %d dimensions\n",
				var_name, n_dims );
		new_var->global_min = 0.0;
		new_var->global_max = 0.0;
		new_var->user_min   = 0.0;
		new_var->user_max   = 0.0;
		new_var->user_set_blowup   = -99999;
		new_var->auto_set_no_range = 0;
		new_var->have_set_range    = false;
		{
		size_t *raw_size = new_fdb->file->varSize( const_cast<char *>(var_name) );
		new_var->size.assign( raw_size, raw_size + n_dims );
		free( raw_size );
		}
		new_fdb->index      = 0;	/* Since this is the FIRST fdb for this var */
		new_var->files.push_back( std::move( new_fdb_owner ) );
		new_var->fill_value = DEFAULT_FILL_VALUE;
		fillValue( new_var, &(new_var->fill_value) );

		/* Init the dim mapping info -- scalar_dim_map_info starts empty
		 * and grows (up to MAX_SCALAR_COORDS) as scalar coords are
		 * discovered in handle_dim_mapping_scalar(). */
		handle_dim_mapping( new_var );	/* needs to be before fill_dim_structs cuz latter access fi_dim_info */

		fill_dim_structs( new_var );
		new_var->is_virtual = false;
		variables_.push_back( std::move( new_var_owner ) );

		}
	else	/* YES -- just add the FDB to the list of files in which
		 * this variable appears, and accumulate the variable's size.
		 */
		{
		/* Go to the end of the file list and add it there */
		if( options.debug )
			printf( "adding another file with variable %s in it\n",
				var_name );
		if( var->files.empty() ) {
			fprintf( stderr, "ncview: add_var_to_list: internal ");
			fprintf( stderr, "inconsistency; var has no last_file\n" );
			exit( -1 );
			}
		new_fdb->index    = var->files.back()->index + 1;	/* so index for this fdb is 1 more than index for prev one */
		var->size[0]      += new_fdb->var_size[0];	/* this works b/c you can only concatenate across first (timelike) dim */
		/* var->dim[0] (if scannable) is the NCDim fill_dim_structs() built
		 * from the FIRST file alone, back when this var was created above --
		 * its ->size field is a separate copy of what var->size[0] was at
		 * that time, not a view onto it, so it silently goes stale here
		 * unless kept in sync too. Left stale, this is invisible almost
		 * everywhere else (view_change_cur_dim()'s prev/next stepping and
		 * set_scan_view()'s "frame N/M" label both read var->size[0]
		 * directly), but MainWindow::fillDimInfo() sets the scan-axis
		 * slider's range from exactly this ->size -- so for a multi-file
		 * (per-timestep-per-file, e.g. WRF-style) virtual variable, the
		 * slider silently gets stuck at whatever range the first file
		 * alone had (bounds(0,0), i.e. no visible range, if each file only
		 * contributes one timestep) while every other UI element already
		 * reflects the full concatenated size.
		 */
		if( var->dim[0] != nullptr )
			var->dim[0]->size = var->size[0];
		var->files.push_back( std::move( new_fdb_owner ) );
		var->is_virtual   = true;
		}
}

void Dataset::cacheScalarCoordInfo()
{
	NCDim_map_info	*dmi;
	float		fval;
	size_t		zeros[MAX_NC_DIMS], ones[MAX_NC_DIMS], n_ts, ii, i_cursor, n_ts_this_file;

	if( options.debug ) printf( "cache_scalar_coord_info: entering\n" );

	/* Allocate space for the timestep_2_fdb array. This points
	 * to the file (FDBlist) associated with EACH TIMESTEP of
	 * the variable
	 */
	for( const auto &vptr : variables_ ) {
		NCVar *v = vptr.get();
		n_ts = v->size[0];	/* total number of timesteps across ALL files */
		if( n_ts > 0 ) {
			if( options.debug )
				printf( "Constructing timestep_2_fdb array for var %s, which has %zu timesteps\n", v->name.c_str(), n_ts );
			/* One FDBlist pointer for each timestep of the var */
			v->timestep_2_fdb.resize( n_ts );

			i_cursor = 0L;
			for( auto &tfile_owner : v->files ) {
				FDBlist *tfile = tfile_owner.get();
				/* Set all FDBpointers for the timesteps in THIS file
				 * to point to this file
				 */
				n_ts_this_file = tfile->var_size[0];
				if( options.debug )
					printf( "%zu timesteps of var %s are in file %s\n", n_ts_this_file, v->name.c_str(), tfile->filename.c_str() );
				for( ii=0; ii<n_ts_this_file; ii++ )
					v->timestep_2_fdb[i_cursor++] = tfile;
				}
			if( i_cursor != n_ts ) {
				fprintf( stderr, "Internal error: in routine cache_scalar_coord_info, got a total length of the unlimited dim in var %s to be %zu, but when setting pointers to the files, there seemd to be only %zu entries\n",
					v->name.c_str(), n_ts, i_cursor );
				exit(-1);
				}
			}
		}

	/* These will be used as the start (zeros) and count (ones)
	 * to get the scalar data
	 */
	for( int isc=0; isc<MAX_NC_DIMS; isc++ ) {
		zeros[isc] = 0L;
		ones[isc]  = 1L;
		}

	for( const auto &vptr : variables_ ) {
		NCVar *v = vptr.get();
		int nsc = (int)v->scalar_dim_map_info.size();
		if( nsc > 0 ) {
			/* How many files does this var live in? */
			int nfiles = (int)v->files.size();
			if( options.debug )
				printf( "Making cache for the %d SCALAR coordinates of variable %s, which lives in %d files\n", nsc, v->name.c_str(), nfiles );

			/* We hold the values of the scalar coords in the data_cache */
			for( int isc=0; isc<nsc; isc++ ) {
				dmi = v->scalar_dim_map_info[isc].get();
				dmi->data_cache.resize( nfiles ); /* one val per FILE (not timestep) */
				}

			/* Go through each file and read in the vals of all the scalar coords */
			for( int ifile=0; ifile<nfiles; ifile++ ) {
				FDBlist *tfile = v->files[ifile].get();
				for( int isc=0; isc<nsc; isc++ ) {
					dmi = v->scalar_dim_map_info[isc].get();
					if( dmi == NULL ) {
						fprintf( stderr, "Coding error, uninitialized pointer to a scalar dim info struct is being used\n" );
						exit(-1);
						}
					netcdf_fi_get_data( tfile->id(), const_cast<char *>(dmi->coord_var_name.c_str()), zeros, ones, &fval, NULL );
					if( options.debug ) printf( "In file %d/%d, value of scalar coord \"%s\" is %f %s\n",
						ifile, nfiles, dmi->coord_var_name.c_str(), fval, dmi->coord_var_units.c_str() );
					dmi->data_cache[ifile] = fval;
					}
				}

			/* Now see if all the scalar values are the same */
			for( int isc=0; isc<nsc; isc++ ) {
				dmi = v->scalar_dim_map_info[isc].get();
				dmi->scalar_all_same = 1;
				if( nfiles > 1 ) {
					for( int ifile=1; ifile<nfiles; ifile++ ) {
						if( dmi->data_cache[ifile] != dmi->data_cache[0] )
							dmi->scalar_all_same = 0;
						}
					}
				}
			}
		}

	if( options.debug ) printf( "cache_scalar_coord_info: finished\n" );
}

void Dataset::copyInfoToIdenticalDims( NCVar *vsrc, NCDim *dsrc, size_t dim_len )
{
	int	i, dims_are_same;
	NCDim	*d;

	for( auto &vptr : variables_ ) {
		NCVar *v = vptr.get();
		for( i=0; i<v->n_dims; i++ ) {
			d = v->dim[i].get();
			if( (d != NULL) && (d->have_calc_minmax == 0)) {
				/* See if this dim is same as passed dim */
				dims_are_same = (dsrc->name == d->name) &&
						equivalent_FDBs( vsrc, v );
				if( dims_are_same ) {
					if( options.debug ) {
						printf( "Dim %s (%d) is same as dim %s (%d), copying min&max from former to latter... min=%f max=%f\n",
							dsrc->name.c_str(), dsrc->global_id, d->name.c_str(), d->global_id,
							dsrc->min, dsrc->max );
						}
					d->min = dsrc->min;
					d->max = dsrc->max;
					d->have_calc_minmax = 1;
					d->values.assign( dsrc->values.begin(), dsrc->values.begin() + dim_len );
					d->is_lat = dsrc->is_lat;
					d->is_lon = dsrc->is_lon;
					}
				}
			}
		}
}

void Dataset::calcDimMinmaxes()
{
	int	i, j;
	NCDim	*d;
	char	temp_str[1024];
	nc_type	type;
	double	temp_double, bounds_max, bounds_min;
	int	has_bounds, name_lat, name_lon, units_lat, units_lon;
	size_t	dim_len;
	size_t	cursor_place[MAX_NC_DIMS];

	for( auto &vptr : variables_ ) {
		NCVar *v = vptr.get();
		for( i=0; i<v->n_dims; i++ ) {
			d = v->dim[i].get();
			if( (d != NULL) && (d->have_calc_minmax == 0)) {
				if( options.debug )
					printf( "%s %d ...min & maxes for dim d->name=>%s< (d->global_id=%d)...\n",
						__FILE__, __LINE__, d->name.c_str(), d->global_id );
				dim_len = v->size[i];
				d->values.resize( dim_len );

				for( j=0; j<v->n_dims; j++ )
					cursor_place[j] = (int)(v->size[j]/2.0);	/* take middle in case 2-d mapped dims apply */

				type = dimValue( v, i, 0L, &temp_double, temp_str, &has_bounds, &bounds_min,
								&bounds_max, cursor_place );	/* used to get type ONLY */
				if( type == NC_DOUBLE ) {
					for( j=0; j<(int)dim_len; j++ ) {
						cursor_place[i] = j;
						type = dimValue( v, i, j, &temp_double, temp_str, &has_bounds, &bounds_min, &bounds_max, cursor_place );
						d->values[j] = (float)temp_double;
						}
					d->min  = d->values[0];
					d->max  = d->values[dim_len - 1];
					}
				else
					{
					if( options.debug )
						printf( "**Note: non-float dim found; i=%d\n", i );
					d->min  = 1.0;
					d->max  = (float)dim_len;
					for( j=0; j<(int)dim_len; j++ )
						d->values[j] = (float)j;
					}
				d->have_calc_minmax = 1;

				/* Try to see if the dim is a lat or lon.  Not an exact science by a long shot */
				name_lat  = strncmp_nocase(d->name.c_str(),  "lat",    3)==0;
				units_lat = strncmp_nocase(d->units.c_str(), "degree", 6) == 0;
				name_lon  = strncmp_nocase(d->name.c_str(),  "lon",    3)==0;
				units_lon = strncmp_nocase(d->units.c_str(), "degree", 6) == 0;
				d->is_lat = ((name_lat || units_lat) && (d->max <  90.01) && (d->min > -90.01));
				d->is_lon = ((name_lon || units_lon) && (d->max < 360.01) && (d->min > -180.01));

				/* There is a funny thing we need to do at this point.  Think about the following case.
				 * We want to look at 3 different files, and they all have a dim named 'lon' in them,
				 * and each is different.  Because this might happen, we can't use the name as an
				 * indication of a unique dimension.  On the other hand, it is very slow to repeatedly
				 * reprocess the same dim over and over, especially if it's the time dim in a series
				 * of virtually concatenated input files.  For that reason, we copy the min and max
				 * values we just found to all identical dims.
				 */
				copyInfoToIdenticalDims( v, d, dim_len );
				}
			}
		}
}

void Dataset::getMinMaxOnestep( NCVar *var, size_t n_other, size_t tstep, float *data,
                                 float *min, float *max, int verbose )
{
	std::vector<size_t> start_v, count_v;
	size_t	n_time;
	size_t	j;
	int	i;
	float	dat, fill_v;

	count_v.resize( var->n_dims );
	start_v.resize( var->n_dims );
	size_t	*start = start_v.data();
	size_t	*count = count_v.data();
	fill_v = var->fill_value;

	n_time = var->size[0];
	if( tstep > (n_time-1) )
		tstep = n_time-1;

	*(count) = 1L;
	*(start) = tstep;
	for( i=1; i<var->n_dims; i++ ) {
		*(start+i) = 0L;
		*(count+i) = var->size[i];
		}

	if( verbose ) {
		printf( "." );
		fflush( stdout );
		}

	getData( var, start, count, data );

	for( j=0; j<n_other; j++ ) {
		dat = *(data+j);
		if( dat != dat )
			dat = fill_v;
		if( (! close_enough(dat, fill_v)) && (dat != FILL_FLOAT) )
			{
			if( dat > *max )
				*max = dat;
			if( dat < *min )
				*min = dat;
			}
		}
}

void Dataset::checkRanges( NCVar *var, ViewerUi &ui )
{
	float	min, max;
	Message	message;
	char	temp_string[ 1024 ];

	if( netcdf_min_max_option_set( var, &min, &max ) ) {
		if( var->global_min < min ) {
			snprintf( temp_string, 1022, "Calculated minimum (%g) is less than\nvalid_range minimum (%g).  Reset\nminimum to valid_range minimum?", var->global_min, min );
			message = ui.in_dialog( temp_string, true );
			if( message == Message::OK )
				var->global_min = min;
			}
		if( var->global_max > max ) {
			snprintf( temp_string, 1022, "Calculated maximum (%g) is greater\nthan valid_range maximum (%g). Reset\nmaximum to valid_range maximum?", var->global_max, max );
			message = ui.in_dialog( temp_string, true );
			if( message == Message::OK )
				var->global_max = max;
			}
		}

	if( netcdf_min_option_set( var, &min ) ) {
		if( var->global_min < min ) {
			snprintf( temp_string, 1022, "Calculated minimum (%g) is less than\nvalid_min minimum (%g).  Reset\nminimum to valid_min value?", var->global_min, min );
			message = ui.in_dialog( temp_string, true );
			if( message == Message::OK )
				var->global_min = min;
			}
		}

	if( netcdf_max_option_set( var, &max ) ) {
		if( var->global_max > max ) {
			snprintf( temp_string, 1022, "Calculated maximum (%g) is greater than\nvalid_max maximum (%g).  Reset\nmaximum to valid_max value?", var->global_max, max );
			message = ui.in_dialog( temp_string, true );
			if( message == Message::OK )
				var->global_max = max;
			}
		}

	var->user_min = var->global_min;
	var->user_max = var->global_max;
	var->have_set_range = true;
}

void Dataset::initMinMax( NCVar *var, ViewerUi &ui )
{
	long	n_other, i, step;
	size_t	n_timesteps;
	float	init_min, init_max;
	std::vector<float> data;
	int	verbose;

	init_min =  9.9e30;
	init_max = -9.9e30;
	var->global_min = init_min;
	var->global_max = init_max;

	printf( "calculating min and maxes for %s", var->name.c_str() );

	/* n_other is the number of elements in a single timeslice of the data array */
	n_timesteps = var->size[0];
	n_other     = 1L;
	for( i=1; i<var->n_dims; i++ )
		n_other *= var->size[i];

	data.resize( n_other );

	/* We always get the min and max of the first, middle, and last time
	 * entries if they are distinct.
	 */
	verbose = true;
	step    = 0L;
	getMinMaxOnestep( var, n_other, step, data.data(),
			&(var->global_min), &(var->global_max), verbose );
	if( n_timesteps == 1 ) {
		if( verbose )
			printf( "\n" );
		checkRanges( var, ui );
		return;
		}

	step = n_timesteps-1L;
	getMinMaxOnestep( var, n_other, step, data.data(),
			&(var->global_min), &(var->global_max), verbose );
	if( n_timesteps == 2 ) {
		if( verbose )
			printf( "\n" );
		checkRanges( var, ui );
		return;
		}

	step = (n_timesteps-1L)/2L;
	getMinMaxOnestep( var, n_other, step, data.data(),
			&(var->global_min), &(var->global_max), verbose );
	if( n_timesteps == 3 ) {
		if( verbose )
			printf( "\n" );
		checkRanges( var, ui );
		return;
		}

	switch( options.min_max_method ) {
		case MinMaxMethod::Fast:
			if( verbose )
				printf( "\n" );
			break;

		case MinMaxMethod::Med:
			verbose = true;
			step = (n_timesteps-1L)/4L;
			getMinMaxOnestep( var, n_other, step, data.data(),
				&(var->global_min), &(var->global_max), verbose );
			step = (3L*(n_timesteps-1L))/4L;
			getMinMaxOnestep( var, n_other, step, data.data(),
				&(var->global_min), &(var->global_max), verbose );
			if( verbose )
				printf( "\n" );
			break;

		case MinMaxMethod::Slow:
			verbose = true;
			for( i=2; i<=9; i++ ) {
				printf( "." );
				step = (i*(n_timesteps-1L))/10L;
				getMinMaxOnestep( var, n_other, step, data.data(),
					&(var->global_min), &(var->global_max), verbose );
				}
			if( verbose )
				printf( "\n" );
			break;

		case MinMaxMethod::Exhaust:
			verbose = true;
			for( i=1; i<(long)(n_timesteps-2L); i++ ) {
				step = i;
				getMinMaxOnestep( var, n_other, step, data.data(),
					&(var->global_min), &(var->global_max), verbose );
				}
			if( verbose )
				printf( "\n" );
			break;
		}

	if( (var->global_min == init_min) && (var->global_max == init_max) ) {
		var->global_min = 0.0;
		var->global_max = 0.0;
		}

	checkRanges( var, ui );
}
