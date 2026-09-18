/*
 * core/include/ncview/dataset.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * NetCDFFile: move-only RAII ownership of one nc_open'd file, closing it
 * exactly once on destruction via netcdf_fi_close() (file_netcdf.cc)
 * directly -- not via file.cc's file_type-dispatching fi_close(), since
 * NetCDFFile is already committed to being the netCDF backend and
 * routing through that dispatch layer bought nothing (Phase 6 of the
 * "refine the architecture" plan) while requiring determine_file_type()
 * to have already run in-process, which open() below doesn't guarantee.
 * Before this class existed, no file ncview opened was ever closed --
 * fi_close() had zero callers anywhere in core/ui.
 *
 * Dataset: owns every NCVar (replacing the global `variables`) and every
 * NetCDFFile a session has opened. A single physical file is opened once
 * (addVariable() below is called once per (file, variable name) pair, but
 * with the SAME netCDF fileid for every variable that file contains), so
 * trackFile() deduplicates by fileid: the first FDBlist for a given
 * fileid causes Dataset to take ownership of it; every subsequent FDBlist
 * for that same fileid gets a pointer to the already-tracked NetCDFFile
 * instead of a second, competing owner.
 *
 * OOP_redesign plan, Step 5 introduced the ownership and bridged the
 * legacy global `variables` onto Dataset's storage (see ncview.cc); a
 * later pass moved the functions that actually build/mutate that list
 * (formerly free functions in util.cc: add_var_to_list, add_vars_to_list,
 * get_var, cache_scalar_coord_info, calc_dim_minmaxes, init_min_max, plus
 * their file-private helpers check_ranges/get_min_max_onestep/
 * copy_info_to_identical_dims/equivalent_FDBs/new_fdblist) onto Dataset
 * as real methods, with every call site updated directly -- no
 * free-function forwarding shim kept for compatibility. Functions that
 * only fill in fields of an already-allocated NCVar* from netCDF metadata,
 * without touching the variable list itself (fill_dim_structs,
 * handle_dim_mapping, is_scannable, ...), stay free functions -- moved to
 * var_metadata.cc when util.cc was dissolved (Phase 4b of the "refine the
 * architecture" plan), still called from Dataset's methods the same way
 * anything else calls them.
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <netcdf.h>	/* nc_type, for dimValue() below */

#include "ncview/defines.h"
#include "ncview/stringlist.h"

class ViewerUi;

class NetCDFFile {
public:
	explicit NetCDFFile( int fileid ) : fileid_( fileid ) {}
	~NetCDFFile();

	NetCDFFile( const NetCDFFile & ) = delete;
	NetCDFFile &operator=( const NetCDFFile & ) = delete;

	NetCDFFile( NetCDFFile &&other ) noexcept : fileid_( other.fileid_ ) {
		other.fileid_ = -1;
	}
	NetCDFFile &operator=( NetCDFFile &&other ) noexcept {
		if( this != &other ) {
			close();
			fileid_ = other.fileid_;
			other.fileid_ = -1;
			}
		return *this;
	}

	int id() const { return fileid_; }

	/* Opens 'path' read-only via nc_open(), returning nullopt (instead of
	 * netcdf_fi_initialize()/fi_initialize()'s exit(-1)) for the
	 * nonexistent, unreadable, or not-a-netCDF-file cases -- Phase 6 of
	 * the "refine the architecture" plan added this as a testable
	 * open primitive; it is NOT wired into the production startup path
	 * (determine_file_type()/fi_initialize()), which keeps its existing
	 * exit()-on-failure behavior deliberately unchanged (a production
	 * error-handling change was out of scope for this phase). If
	 * nc_errcode is non-null, *nc_errcode is set to nc_open()'s return
	 * code (an netCDF NC_* constant; see <netcdf.h>/nc_strerror()) on
	 * both success (NC_NOERR) and failure, so a caller can report why. */
	static std::optional<NetCDFFile> open( const std::string &path, int *nc_errcode = nullptr );

	/* The 13 single-file fi_*() forwarders formerly in file.cc, collapsed
	 * onto this class (Phase 6 of the "refine the architecture" plan):
	 * every call site already held the owning FDBlist/NetCDFFile and only
	 * used ->id() to hand a bare fileid to a free-function dispatcher
	 * whose `file_type` switch (file.cc) only ever held FILE_TYPE_NETCDF
	 * anyway (confirmed dead, step 1 of this phase) -- so each method body
	 * below is the netcdf_*() call the old forwarder made, unchanged,
	 * just relocated with an implicit fileid_ instead of a passed-in one.
	 * fi_initialize()/fi_dim_calendar()/determine_file_type() stayed free
	 * functions in file.cc: the first two add real logic beyond dispatch,
	 * the third doesn't forward at all. */
	Stringlist *listVars() const;
	std::string title() const;
	std::string longVarName( std::string_view var_name ) const;
	std::string varUnits( std::string_view var_name ) const;
	std::string dimUnits( std::string_view dim_name ) const;
	int nDims( char *var_name ) const;
	Stringlist *scannableDims( char *var_name ) const;
	size_t *varSize( char *var_name ) const;
	std::string dimIdToName( std::string_view var_name, int dim_id ) const;
	int dimNameToId( char *var_name, char *dim_name ) const;
	std::string dimLongname( std::string_view dim_name ) const;
	int recdimId() const;
	void fillAuxData( char *var_name, FDBlist *fdb ) const;

	/* Four more single-file netcdf_*() primitives, given the same
	 * already-open-object treatment as the 13 above (Phase 7b of the
	 * "refine the architecture" plan, closing out Phase 6's residue) --
	 * these were never part of the fi_*()/file.cc dispatch layer Phase 6
	 * collapsed (no fi_get_char_att/fi_att_string/fi_dim_value/
	 * fi_get_data_single_file ever existed as such), so they weren't in
	 * that migration's list, but every call site already held the owning
	 * NetCDFFile* the same way the 13 did. dimValue() here is NOT a
	 * refactor of Dataset::dimValue() above -- that one takes a virtual
	 * place across a variable's whole file series and does unit
	 * conversion; this one is netcdf_dim_value()'s raw single-file,
	 * single-record read, used by epic_time.cc's months_calc_tgran()
	 * before a variable's multi-file structure is necessarily built. */
	std::string charAtt( std::string_view var_name, std::string_view att_name ) const;
	std::string attString( std::string_view var_name ) const;
	nc_type dimValue( char *dim_name, size_t place, double *ret_val_double, char *ret_val_char,
		size_t virt_place, int *return_has_bounds, double *return_bounds_min,
		double *return_bounds_max ) const;
	void getData( char *var_name, size_t *start_pos, size_t *count, float *data ) const;

private:
	void close();
	int fileid_;
};

class Dataset {
public:
	Dataset() = default;

	std::vector<std::unique_ptr<NCVar>> &variablesMutable() { return variables_; }
	const std::vector<std::unique_ptr<NCVar>> &variables() const { return variables_; }

	/* Returns the NetCDFFile that owns 'fileid', taking ownership of it
	 * (Dataset will close it on destruction) the first time this fileid
	 * is seen; returns the same NetCDFFile* on every later call with the
	 * same fileid. */
	NetCDFFile *trackFile( int fileid );

	/* Formerly util.cc's get_var(): a plain linear scan by name. */
	NCVar *findVariable( const char *var_name );

	/* Formerly util.cc's add_var_to_list()/add_vars_to_list(): fill out
	 * the FDBlist/NCVar structures for the given variable(s) and add them
	 * to (or extend an existing entry in) variables_. Neither of these
	 * originally took an `nfiles` parameter for "total number of files
	 * on the command line" -- one was added when Dataset took over from
	 * the free functions, threaded all the way from fi_initialize(), but
	 * was never actually read in either function body (confirmed by
	 * Phase 5a of the "refine the architecture" plan, tests/test_file_
	 * layer.cc); removed in Phase 6 rather than left as documented dead
	 * weight. */
	void addVariable( const char *var_name, int file_id, const char *filename );
	void addVariables( Stringlist *var_list, int id, const char *filename );

	/* Formerly util.cc's cache_scalar_coord_info(): builds timestep_2_fdb
	 * and the scalar-coordinate data cache for every variable currently
	 * on the list. Must run after all files have been added. */
	void cacheScalarCoordInfo();

	/* Formerly util.cc's calc_dim_minmaxes(): computes min/max (and a
	 * lat/lon guess) for every not-yet-processed NCDim referenced by any
	 * variable on the list. */
	void calcDimMinmaxes();

	/* Formerly util.cc's init_min_max(): samples a variable's data to
	 * establish its global min/max, then reconciles that against any
	 * valid_range/valid_min/valid_max attribute (possibly prompting via
	 * ui.in_dialog()). Phase 11b ("refine the architecture" plan, Part
	 * IV): takes ViewerUi& explicitly now, threaded down into
	 * checkRanges() below, instead of reaching the in_dialog() seam
	 * free function (which read the global g_app.ui internally). */
	void initMinMax( NCVar *var, ViewerUi &ui );

	/* Formerly util.cc's get_min_max_onestep(): reads one timestep's data
	 * and folds its extrema into min/max. Public: initMinMax() uses it
	 * internally to sample several timesteps, but view.cc's
	 * view_check_new_data() also calls it directly (to check a single
	 * newly-arrived timestep against an as-yet-unset range), so it's a
	 * genuinely shared operation, not a private implementation detail of
	 * initMinMax() alone. */
	void getMinMaxOnestep( NCVar *var, size_t n_other, size_t tstep, float *data,
	                        float *min, float *max, int verbose );

	/* Formerly file.cc's fi_get_data()/fi_get_data_iterate(): reads
	 * 'var's data at the given virtual start/count, transparently
	 * spanning file boundaries for a multi-file (is_virtual) variable.
	 * Moved onto Dataset (Phase 6 of the "refine the architecture"
	 * plan) rather than NetCDFFile since it operates across however many
	 * files 'var' actually lives in, not on one open file. */
	void getData( NCVar *var, size_t *virt_start_pos, size_t *count, void *data );

	/* Formerly file.cc's fi_dim_value(): the value of dimension 'dim_id'
	 * at virtual position 'virt_place' for 'var', translating virtual to
	 * actual place and reconciling cross-file time-unit differences
	 * (dimValueConvert(), dataset.cc) internally. Same file-spanning
	 * reasoning as getData() for why this is a Dataset method. */
	nc_type dimValue( NCVar *var, int dim_id, size_t virt_place, double *return_val_double,
	                   char *return_val_char, int *return_has_bounds, double *return_bounds_min,
	                   double *return_bounds_max, size_t *complete_ndim_virt_place );

	/* Formerly file.cc's fi_fill_value(): sets *fill_value from the
	 * first file 'var' lives in, if the file format defines one. */
	void fillValue( NCVar *var, float *fill_value );

private:
	/* getData()'s helper for is_virtual variables with count[0] > 1 --
	 * formerly file.cc's static fi_get_data_iterate(), moved along with
	 * getData() as an implementation detail (no other caller). */
	void getDataIterate( NCVar *var, size_t *virt_start_pos, size_t *count, void *data );

	/* Declared before variables_ so it's destroyed AFTER variables_ --
	 * member destruction runs in reverse declaration order, and nothing
	 * should still be holding an FDBlist::file pointer once these
	 * NetCDFFiles start closing. */
	std::vector<std::unique_ptr<NetCDFFile>> files_;
	std::vector<std::unique_ptr<NCVar>> variables_;

	/* initMinMax()'s only caller of this -- formerly util.cc's
	 * check_ranges(), with no other external callers, so it moved along
	 * with initMinMax() as an implementation detail rather than becoming
	 * a public Dataset method. Takes ViewerUi& (Phase 11b), passed down
	 * from initMinMax(). */
	void checkRanges( NCVar *var, ViewerUi &ui );

	/* calcDimMinmaxes()'s only caller of this -- formerly util.cc's
	 * copy_info_to_identical_dims(), which directly scanned the global
	 * variable list the same way calcDimMinmaxes() itself does. */
	void copyInfoToIdenticalDims( NCVar *vsrc, NCDim *dsrc, size_t dim_len );
};
