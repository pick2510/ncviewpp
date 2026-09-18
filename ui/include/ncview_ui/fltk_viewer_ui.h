/*
 * ui/include/ncview_ui/fltk_viewer_ui.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * FltkViewerUi -- OOP_redesign plan, Step 9b. The FLTK implementation of
 * ncview/viewer_ui.h's ViewerUi interface, delegating to the MainWindow
 * singleton (main_window.h) exactly as ui/src/interface_fltk.cc's free
 * functions used to before this step. app/main.cc constructs one and
 * assigns it to g_app.ui before calling into ncview_core.
 */
#pragma once

#include "ncview/viewer_ui.h"

namespace ncview_ui {

class FltkViewerUi : public ViewerUi {
public:
	void 	in_display_stuff	( const char *s, const char *var_name ) override;
	void 	in_set_edit_place	( size_t index, int x, int y, int nx, int ny ) override;
	void 	in_indicate_active_var  ( const char *var_name ) override;
	void 	in_indicate_active_dim  ( Dimension dimension, const char *dim_name ) override;
	void 	in_parse_args		( int *p_argc, char **argv ) override;
	void 	in_initialize		( void ) override;
	void 	in_set_label		( Label label_id, const char *string ) override;
	void	in_process_user_input	( void ) override;
	void	in_draw_2d_field 	( const unsigned char *data, size_t width, size_t height, size_t timestep ) override;
	void	in_create_colormap	( const char *name, const ncv_pixel r[256], const ncv_pixel g[256], const ncv_pixel b[256] ) override;
	char	*in_install_next_colormap( int do_widgets_flag ) override;
	int	in_set_2d_size   	( size_t width, size_t height ) override;
	void	in_set_sensitive	( Button button_id, int state ) override;
	Message	in_dialog		( const char *message, int want_cancel_button ) override;
	void 	in_fill_dim_info	( const NCDim *d, int please_flip ) override;
	void	in_set_cur_dim_value	( const char *name, const char *string ) override;
	void 	in_set_cursor_busy	( void ) override;
	void 	in_set_cursor_normal	( void ) override;
	int 	in_set_scan_dims	( const Stringlist *dim_list, const char *x_axis, const char *y_axis, Stringlist **new_dim_list ) override;
	void 	in_flush		( void ) override;
	int	in_popup_XY_graph	( size_t n, int dimindex, double *xvals, double *yvals, const char *x_axis_title,
					const char *y_axis_title, const char *title, const char *legend,
					const Stringlist *scannable_dims ) override;
	void 	in_query_pointer_position( int *x, int *y ) override;
	void	in_popup_2d_window	( void ) override;
	void	in_popdown_2d_window	( void ) override;
	void 	in_timer_clear		( void ) override;
	int	in_report_auto_overlay  ( void ) override;
	void 	in_timer_set            ( std::function<void()> callback, unsigned long delay_millisec ) override;
	char    *in_install_prev_colormap( int do_widgets ) override;
	char	*in_install_colormap_by_name( const char *name, int do_widgets ) override;
	Stringlist *in_choose_input_files( void ) override;
	Message in_choose_save_file( const char *title, const char *default_name, char *ret_path, size_t ret_path_size ) override;
	void	in_print		( const PrintInfo &info, const PrintOptions &po ) override;

	void	set_options		( void ) override;
	Message	printer_options		( PrintOptions *po ) override;
	Message x_range( float old_min, float old_max, float global_min, float global_max,
			float *new_min, float *new_max, int *allvars ) override;
	void	x_dataedit( std::vector<std::string> &cells, int nx ) override;
	int	x_seen_colormap_name( const char *name ) override;
	void	x_check_legal_colormap_loaded( void ) override;
	void	x_create_colorbar( float user_min, float user_max, Transform transform ) override;
	void	x_draw_colorbar( void ) override;
	void	x_error( const char *message ) override;
	void	x_force_set_invert_state( int state ) override;
	void	x_init_dim_info( const Stringlist *dim_list ) override;
	void	x_set_var_sensitivity( const char *varname, int sens ) override;
	void	unlock_plot( void ) override;
	Stringlist *get_persistent_X_state( void ) override;
	void	pix_to_rgb( ncv_pixel pix, int *r, int *g, int *b ) override;
};

} // namespace ncview_ui
