/*
 * core/include/ncview/viewer_ui.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * OOP_redesign plan, Step 9b. `ViewerUi` is the toolkit seam
 * (ncview/interface.h) turned into a virtual interface: every function
 * signature is unchanged (including the original in_ / x_ prefixed
 * free-function names, kept verbatim as method names -- a deliberate, purely mechanical
 * choice, to make the free-function -> method conversion exactly a rename
 * rather than a redesign, minimizing the chance of a transcription
 * mistake across ~48 signatures). Two concrete implementations exist:
 * `FltkViewerUi` (ui/include/ncview_ui/fltk_viewer_ui.h, delegating to the
 * MainWindow singleton exactly as interface_fltk.cc's free functions used
 * to) and a recording test fake (tests/stub_interface.cc's
 * `RecordingViewerUi`, replacing what used to be ~48 free-function stubs).
 *
 * `core/src/viewer_ui_bridge.cc` defines the *free functions* still
 * declared in ncview/interface.h (core calls them by those names at every
 * existing call site -- zero call-site churn) as one-line forwarders onto
 * `g_app.ui` (ncview/app_context.h), the currently-installed ViewerUi.
 * Whoever owns the process (app/main.cc for the real app, each test
 * binary's setup for tests) constructs a concrete ViewerUi and assigns it
 * to g_app.ui before any core code that might call through the seam runs.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ncview/defines.h"
#include "ncview/stringlist.h"

class ViewerUi {
public:
	virtual ~ViewerUi() = default;

	virtual void 	in_display_stuff	( const char *s, const char *var_name ) = 0;
	virtual void 	in_set_edit_place	( size_t index, int x, int y, int nx, int ny ) = 0;
	virtual void 	in_indicate_active_var  ( const char *var_name ) = 0;
	virtual void 	in_indicate_active_dim  ( Dimension dimension, const char *dim_name ) = 0;
	virtual void 	in_parse_args		( int *p_argc, char **argv ) = 0;
	virtual void 	in_initialize		( void ) = 0;
	virtual void 	in_set_label		( Label label_id, const char *string ) = 0;
	virtual void	in_process_user_input	( void ) = 0;
	virtual void	in_draw_2d_field 	( const unsigned char *data, size_t width, size_t height, size_t timestep ) = 0;
	virtual void	in_create_colormap	( const char *name, const ncv_pixel r[256], const ncv_pixel g[256], const ncv_pixel b[256] ) = 0;
	virtual char	*in_install_next_colormap( int do_widgets_flag ) = 0;
	virtual int	in_set_2d_size   	( size_t width, size_t height ) = 0;
	virtual void	in_set_sensitive	( Button button_id, int state ) = 0;
	virtual Message	in_dialog		( const char *message, int want_cancel_button ) = 0;
	virtual void 	in_fill_dim_info	( const NCDim *d, int please_flip ) = 0;
	virtual void	in_set_cur_dim_value	( const char *name, const char *string ) = 0;
	virtual void 	in_set_cursor_busy	( void ) = 0;
	virtual void 	in_set_cursor_normal	( void ) = 0;
	/* Returns 1 if the user accepted the dialog, 0 if they cancelled it (or
	 * if there was nothing to choose between). *new_dim_list is written
	 * ONLY when 1 is returned: a freshly built two-entry list, Y-axis
	 * name first and X-axis name second, which the caller then owns. On 0
	 * the pointer is left exactly as the caller passed it. Callers must
	 * check the return value before dereferencing the list -- see
	 * View::setScanDims() (Phase 13a), where not doing so was a live
	 * segfault on every Cancel. */
	virtual int 	in_set_scan_dims	( const Stringlist *dim_list, const char *x_axis, const char *y_axis, Stringlist **new_dim_list ) = 0;
	virtual void 	in_flush		( void ) = 0;
	virtual int	in_popup_XY_graph	( size_t n, int dimindex, double *xvals, double *yvals, const char *x_axis_title,
					const char *y_axis_title, const char *title, const char *legend,
					const Stringlist *scannable_dims ) = 0;
	virtual void 	in_query_pointer_position( int *x, int *y ) = 0;
	virtual void	in_popup_2d_window	( void ) = 0;
	virtual void	in_popdown_2d_window	( void ) = 0;
	virtual void 	in_timer_clear		( void ) = 0;
	virtual int	in_report_auto_overlay  ( void ) = 0;
	virtual void 	in_timer_set            ( std::function<void()> callback, unsigned long delay_millisec ) = 0;
	virtual char    *in_install_prev_colormap( int do_widgets ) = 0;
	virtual char	*in_install_colormap_by_name( const char *name, int do_widgets ) = 0;
	virtual Stringlist *in_choose_input_files( void ) = 0;
	virtual Message in_choose_save_file( const char *title, const char *default_name, char *ret_path, size_t ret_path_size ) = 0;
	virtual void	in_print		( const PrintInfo &info, const PrintOptions &po ) = 0;

	virtual void	set_options		( void ) = 0;
	virtual Message	printer_options		( PrintOptions *po ) = 0;
	virtual Message x_range( float old_min, float old_max, float global_min, float global_max,
			float *new_min, float *new_max, int *allvars ) = 0;
	/* Phase 12b: was `char **text` -- a raw double-malloc'd buffer whose
	 * ownership contract ("the UI frees it") was documented in prose but
	 * never actually honored by FltkViewerUi::x_dataedit (a real leak on
	 * every data-edit dialog open). std::vector<std::string> makes the
	 * ownership structural instead: View::dataEdit() builds it, hands a
	 * reference across the seam, and it's destroyed automatically when
	 * the caller's local goes out of scope -- there is no longer a
	 * "who frees this" question to get wrong. */
	virtual void	x_dataedit( std::vector<std::string> &cells, int nx ) = 0;
	virtual int	x_seen_colormap_name( const char *name ) = 0;
	virtual void	x_check_legal_colormap_loaded( void ) = 0;
	virtual void	x_create_colorbar( float user_min, float user_max, Transform transform ) = 0;
	virtual void	x_draw_colorbar( void ) = 0;
	virtual void	x_error( const char *message ) = 0;
	virtual void	x_force_set_invert_state( int state ) = 0;
	virtual void	x_init_dim_info( const Stringlist *dim_list ) = 0;
	virtual void	x_set_var_sensitivity( const char *varname, int sens ) = 0;
	virtual void	unlock_plot( void ) = 0;
	virtual Stringlist *get_persistent_X_state( void ) = 0;
	virtual void	pix_to_rgb( ncv_pixel pix, int *r, int *g, int *b ) = 0;
};
