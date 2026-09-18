/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 1993-2024 David W. Pierce
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
 * core/include/ncview/protos.h
 *
 * Prototypes for the UI-free ncview_core library. This is upstream's
 * ncview.protos.h with every UI-implementation-only prototype (x_interface.c,
 * range.c, set_options.c, filesel.c, plot_xy.c, plot_range.c, x_dataedit.c,
 * x_display_info.c, printer_options.c, interface/cbar.c, interface/make_tc_data.c,
 * interface/colormap_funcs.c) removed, EXCEPT for the small subset of those
 * functions core actually calls directly by name (not through the in_*
 * contract) -- those are declared below in "interface.h" as part of the seam
 * ncview_ui must implement. See docs/PORTING.md.
 */

#pragma once

#include <memory>
#include <vector>

#include "ncview/stringlist.h"
#include "ncview/interface.h"
#include "ncview/app_context.h"

class NetCDFFile;

/* Global state, defined in ncview.cc. Upstream had every .c file that
 * needed these declare its own local `extern`; this is the one canonical
 * declaration ncview_ui can use too. */
extern Options options;

/* g_app (ncview/app_context.h) is the single composition-root global:
 * it owns the ViewerSession (Dataset, active ViewState, FrameCache,
 * pixel_transform, Options's field storage), the ViewerController, and a
 * non-owning pointer to whichever ViewerUi is installed. `g_dataset`,
 * `view`, `framestore`, and `pixel_transform` are migration bridges --
 * references onto g_app.session's own members -- so the many existing
 * callsites across core/, ui/, and tests/ that read these globals by
 * their original names keep compiling and behaving identically. All are
 * defined together in ncview.cc, except `view`, which is defined in
 * view.cc (its sole owner before this step). */
extern NcviewApp g_app;
extern Dataset &g_dataset;
extern std::vector<std::unique_ptr<NCVar>> &variables;
extern std::unique_ptr<ViewState> &view;
extern FrameCache &framestore;
extern std::vector<ncv_pixel> &pixel_transform;

/******************************************************************************
 * in ncview.c
 */
/* Upstream's main(); renamed so ncview_ui's app/main.cc can be the real
 * process entry point (and so this library never defines `main` itself).
 * Phase 11a ("refine the architecture" plan, Part IV) added the ViewerUi&
 * parameter: app/main.cc already constructs the real FltkViewerUi before
 * calling this, so passing it explicitly (in addition to still setting
 * g_app.ui, which other not-yet-threaded code still reads) removes this
 * function's own internal reach into that global. */
int	ncview_main		    ( int argc, char **argv, ViewerUi &ui );
void	initialize_misc		    ( void );
/* The options-fields-and-framestore-defaulting part of initialize_misc(), split
 * out so it can be re-run without repeating udu_utinit(NULL) -- see its
 * definition in ncview.cc and tests/support/session_fixture.h. */
void	reset_session_defaults	    ( void );
Stringlist *parse_options           ( int argc,  char *argv[] );
void 	initialize_file_interface   ( Stringlist *input_files );
/* Phase 11a threaded ViewerUi& through this and process_user_input()
 * below (2 and 1 internal seam calls respectively, both single-caller
 * chains from ncview_main()); initialize_colormaps()/init_cmap_from_file()
 * stay untouched -- the latter has fixed-signature test call sites
 * (tests/test_colormaps.cc) this phase didn't want to disturb. */
void	initialize_display_interface( ViewerUi &ui );
void	initialize_colormaps	    ( void );
void	init_cmap_from_file	    ( const char *dir_name, const char *file_name, int n_suffix );
void	process_user_input          ( ViewerUi &ui );
void	quit_app		    ( void );
void	create_default_colormap     ( void );
int	check			    ( int value, int min, int max );
void	print_disclaimer	    ( void );
void	print_no_warranty	    ( void );
void	print_copying	    	    ( void );
void	useage			    ( void );

/******************************************************************************
 * in file.c
 *
 * The 13 single-file fi_*() forwarders that used to live here (fi_list_vars,
 * fi_n_dims, fi_var_size, fi_scannable_dims, fi_title, fi_long_var_name,
 * fi_var_units, fi_dim_units, fi_dim_longname, fi_dim_id_to_name,
 * fi_dim_name_to_id, fi_fill_aux_data, fi_recdim_id) were collapsed onto
 * NetCDFFile methods (Phase 6 of the "refine the architecture" plan) --
 * see core/include/ncview/dataset.h.
 */
int 	fi_initialize    ( char *name );
void 	fi_close         ( int fileid );
void	determine_file_type( Stringlist *input_files );
std::string fi_dim_calendar ( int fileid, std::string_view dim_name );

/******************************************************************************
 * in file_netcdf.c, netcdf specific routines
 */
std::string netcdf_att_string       ( int fileid, std::string_view var_name );
int 	netcdf_fi_confirm	( char *name );
int 	netcdf_fi_initialize	( char *name );
Stringlist *netcdf_fi_list_vars	( int fileid );
int	netcdf_fi_n_dims	( int fileid, char *var_name );
size_t	*netcdf_fi_var_size	( int fileid, char *var_name );
void 	netcdf_fi_get_data	( int fileid, char *var_name, size_t *start_pos,
						size_t *count, float *data, NetCDFOptions *aux_data );
void	netcdf_fi_close		( int fileid );
int 	netcdf_n_dims 		( int cdfid, char *varname );
Stringlist *netcdf_scannable_dims( int fileid, char *var_name );
std::string netcdf_title           ( int fileid );
std::string netcdf_long_var_name   ( int fileid, std::string_view var_name );
std::string netcdf_get_char_att( int fileid, std::string_view var_name, std::string_view att_name );
std::string netcdf_var_units       ( int fileid, std::string_view var_name );
std::string netcdf_dim_units       ( int fileid, std::string_view dim_name );
int 	netcdf_has_dim_values   ( int fileid, char *dim_name );
std::string netcdf_dim_longname 	( int fileid, std::string_view dim_name );
nc_type	netcdf_dim_value     	( int fileid, char *dim_name, size_t place, double *ret_val_double, char *ret_val_char,
				  size_t virt_place, int *has_bounds, double *return_bounds_min, double *return_bounds_max  );
std::string netcdf_dim_id_to_name  ( int fileid, std::string_view var_name, int dim_id );
int 	netcdf_dim_name_to_id   ( int fileid, char *var_name, char *dim_name );
size_t 	netcdf_n_dim_entries    ( int fileid, char *dim_name );
void 	netcdf_fill_aux_data    ( int id, char *var_name, FDBlist *fdb );
int	netcdf_min_max_option_set( NCVar *var, float *ret_min, float *ret_max );
int	netcdf_min_option_set	( NCVar *var, float *ret_min );
int	netcdf_max_option_set	( NCVar *var, float *ret_max );
void 	netcdf_fill_value	( int file_id, char *var_name, float *v, NetCDFOptions *opts );
int 	netcdf_fi_recdim_id     ( int fileid );
std::string netcdf_dim_calendar( int fileid, std::string_view dim_name );
int 	safe_ncvarid( int fileid, char *varname );

/******************************************************************************
 * util.cc was dissolved in Phase 4b of the "refine the architecture" plan
 * -- these declarations remain, but the definitions they refer to now
 * live in the files each comment below names.
 */
/* data_to_pixels()/expand_data() moved onto View (View::dataToPixels(),
 * private View::expandData()) -- see core/src/render_pipeline.cc.
 * close_enough/clip_f have no natural View to attach to and stay free
 * functions, also now defined in render_pipeline.cc. new_netcdf() lost its
 * external linkage entirely: it moved into an anonymous namespace in
 * dataset.cc, its only caller. */
int 	close_enough	   ( float data, float fill );
void	clip_f		   ( float *val, float min, float max );
/* Only called by Dataset::addVariable() (ncview/dataset.h) -- fills in
 * fields of an already-allocated NCVar* from netCDF metadata without
 * touching the variable list itself, so it stayed a free function here
 * rather than moving onto Dataset with the functions that do. Now defined
 * in var_metadata.cc (Phase 4b), same reasoning, new file. */
void 	fill_dim_structs   ( NCVar *v );
/* Ditto -- also only called by Dataset::addVariable(). */
void	handle_dim_mapping ( NCVar *v );
std::string limit_string   ( std::string_view s );
std::vector<int> gen_overlay       ( View *v, char *overlay_fname );
void 	fmt_time	   ( char *temp_string, size_t temp_string_len, double new_dimval, NCDim *dim, int include_granularity );
int	n_vars_in_list	   ( const std::vector<std::unique_ptr<NCVar>> &v );
/* Phase 11a threaded ViewerUi& through this instead of reaching g_app.ui
 * internally via in_set_label()'s free-function seam; its two
 * ViewerController::blowupType() call sites don't hold a ViewerUi of
 * their own yet (that's 11c), so they pass g_app.ui explicitly for now. */
void 	set_blowup_type	   ( BlowupType new_type, ViewerUi &ui );
int 	strncmp_nocase     ( const char *s1, const char *s2, size_t n );
void 	virt_to_actual_place( NCVar *var, size_t *virt_pl, size_t *act_pl, FDBlist **file );
int     is_scannable        ( NCVar *v, int i );
int 	unpack_groupname( const char *varname, int ig, char *groupname );
int 	count_nslashes	    ( const char *s );
void 	varname_no_groups   ( const char *varname, char *varname_sans_groups, char *groupname );

/******************************************************************************
 * in do_buttons.c
 *
 * The 21 do_*() action functions that used to live here (do_range,
 * do_pause, ..., do_blowup_type) are gone -- "refine the architecture"
 * plan, Phase 1. Each had become a two-line forward onto
 * g_app.controller (ncview/app_context.h) after the OOP_redesign plan's
 * Step 7, with no logic of its own left; every call site now calls the
 * corresponding ViewerController method directly. which_button_pressed()
 * stays a free function (it's part of the interface.h-adjacent seam some
 * UI code queries directly, not just an internal forwarder).
 */
Button	which_button_pressed( void );

/******************************************************************************
 * in view.c
 *
 * "Refine the architecture" plan, Phase 2 moved the 12 entry points that
 * used to live here -- change_view, view_draw, view_change_cur_dim,
 * view_set_cur_dim_index, view_get_cur_dim_index, view_report_position,
 * plot_XY, set_min_from_curdata, set_max_from_curdata,
 * invalidate_all_saveframes, view_recompute_colorbar, view_current_nt --
 * onto ViewerSession/ViewerController (ncview/app_context.h:
 * g_app.session.currentNt()/curDimIndex()/invalidateAllSaveframes(),
 * g_app.controller.stepView()/draw()/changeCurDim()/setCurDimIndex()/
 * reportPosition()/plotXY()/setMinFromCurdata()/setMaxFromCurdata()/
 * recomputeColorbar()). Each carried its own `view == NULL` guard that
 * was really a session fact ("no variable selected yet"), not a
 * genuine-anywhere possibility for a `View` method's `this`. See
 * docs/PORTING.md's Phase 2 entry.
 */
/* Phase 11a threaded ViewerSession&/ViewerUi& through this instead of
 * reading the global `view` alias and the in_x()/x_x() free-function seam
 * internally; its sole caller, in_variable_selected() (the fixed
 * UI-triggered seam entry point just above), passes g_app.session/
 * g_app.ui explicitly. */
int 	set_scan_variable    ( NCVar *var, ViewerSession &session, ViewerUi &ui );
/* Formerly also declared here: view_forward()/view_backward() (never
 * defined anywhere, never called -- dead upstream declarations, removed
 * in the same Phase 1 cleanup) and redraw_ccontour() (a one-line wrapper
 * around view_draw() with zero callers, removed along with its
 * definition in view.cc). */
void 	view_report_position_vals( float xval, float yval, int plot_index );
void	view_get_scaled_size ( int blowup, size_t old_nx, size_t old_ny, size_t *new_nx, size_t *new_ny );
/* Phase 11a threaded ViewerUi& through this instead of reaching g_app.ui
 * via in_set_label()'s seam internally; its two
 * ViewerController::transform() call sites pass g_app.ui explicitly,
 * matching set_blowup_type()'s treatment above. */
void 	view_change_transform( int delta, ViewerUi &ui );

/******************************************************************************
 * in overlay.c
 */
void 	do_overlay		( int n, char *custom_filename, int suppress_screen_changes );
const char 	**overlay_names		( void );
int 	overlay_current		( void );
int 	overlay_n_overlays	( void );
void 	determine_overlay_base_dir( char *overlay_base_dir, size_t n );
int 	overlay_custom_n	( void );
void	overlay_init		( void );

/******************************************************************************
 * in udu.c
 */
void 	udu_utinit( char *path );
int 	udu_utistime( char *dimname, char *units );
TimeGranularity 	udu_calc_tgran( int fileid, NCVar *v, int dimid );
void 	udu_fmt_time( char *temp_string, size_t temp_string_len, double new_dimval, NCDim *dim, int include_granularity );

/******************************************************************************
 * in epic_time.c
 */
void epic_fmt_time( char *temp_string, size_t temp_string_len, double new_dimval, NCDim *dim );
int  epic_istime0( int fileid, NCVar *v, NCDim *d );
TimeGranularity  epic_calc_tgran( int fileid, NCDim *d );
/* The TimeStandard dispatch layer (formerly util.cc, moved here Phase 4b
 * of the "refine the architecture" plan): handle_time_dim() is called from
 * var_metadata.cc's fill_dim_structs(), across a TU boundary, so it needs
 * external linkage here -- unlike months_calc_tgran(), which stays a
 * private helper called only from within this file. */
void	handle_time_dim	   ( NetCDFFile *file, NCVar *v, int dimid );

/******************************************************************************
 * in do_print.c
 */
void 	print_init	( void );
void 	do_print	( void );

/******************************************************************************
 * in handle_rc_file.c
 */
int 	write_state_to_file( Stringlist *state_to_save );
int 	read_state_from_file( Stringlist **state );
/* Phase 11a threaded ViewerUi& through this instead of reaching g_app.ui
 * to call get_persistent_X_state() (itself a seam function) internally.
 * Sole caller is ncview_main(), which now holds a ViewerUi& of its own. */
Stringlist *get_persistent_state( ViewerUi &ui );

/******************************************************************************
 * Toolkit-agnostic logic factored out of upstream's src/interface/interface.c
 * because core itself calls these (not just the UI). in_variable_selected
 * lives in view.cc next to set_scan_variable(); in_button_pressed and
 * in_colormap_selected live in do_buttons.cc next to the do_*() functions
 * they dispatch to; in_error lives in viewer_ui_bridge.cc (moved there
 * from util.cc in Phase 4b of the "refine the architecture" plan, since
 * every other UI-seam function already lives in that file).
 */
void	in_variable_selected	( const char *var_name );
void	in_colormap_selected	( const char *name );
void	in_button_pressed	( Button button_id, Modifier modifier );
void	in_error		( const char *message );
