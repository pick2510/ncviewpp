/*
 * core/include/ncview/interface.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * The toolkit seam. ncview_core calls only the functions declared here to
 * talk to the UI. This is upstream's in_* contract (originally declared
 * inline in ncview.protos.h, implemented by src/interface/interface.c
 * delegating to x_interface.c), plus a handful of functions core calls
 * directly by name that are really UI dialogs/state (set_options,
 * printer_options, x_range, x_dataedit, x_seen_colormap_name,
 * x_check_legal_colormap_loaded, x_create_colorbar, x_draw_colorbar,
 * x_error, x_force_set_invert_state, x_init_dim_info,
 * x_set_var_sensitivity, get_persistent_X_state, unlock_plot) -- upstream
 * never routed those through in_*, but they are exactly as much a part of
 * the seam. See PORTING.md, "Why the port is tractable".
 *
 * OOP_redesign plan, Step 9b: every function declared below is implemented
 * once, in core/src/viewer_ui_bridge.cc, as a forwarder onto
 * ncview/viewer_ui.h's ViewerUi virtual interface -- see that header for
 * why (short version: lets ncview_ui's FltkViewerUi and tests'
 * RecordingViewerUi both implement one interface instead of each
 * providing a parallel set of ~48 free functions).
 *
 * "Refine the architecture" plan, Phase 11a/11b/11c: most call sites now
 * reach the ViewerUi interface directly by method call
 * (ui.in_x(...)/g_app.ui->in_x(...)) instead of through one of these free
 * functions. Phase 11c re-verified, project-wide, which of the original
 * 48 declarations still have a real caller and deleted the 35 that had
 * none, along with their viewer_ui_bridge.cc forwarders -- see that
 * file's own header comment for exactly which functions remain declared
 * below and why. The corresponding virtual methods all still exist on
 * ViewerUi (ncview/viewer_ui.h) and are called directly by name in most
 * places; deleting a forwarder here only removes the now-unused
 * free-function *spelling* of that call, not the capability itself.
 */
#pragma once

/* NCVar, NCDim, ncv_pixel, and PrintOptions come from ncview/defines.h,
 * which every translation unit that reaches this header includes first
 * (the upstream convention: includes.h, then defines.h, then protos.h,
 * which pulls in this file). Not forward-declared here: they are anonymous
 * struct typedefs in defines.h, so a "struct NCVar;" forward declaration
 * here would name an unrelated, incompatible type. */

/******************************************************************************
 * in_* : implemented by src/interface/interface.c upstream, now by ncview_ui.
 *
 * Not here (moved to ncview/protos.h + core/src/view.cc/do_buttons.cc/util.cc
 * instead): in_variable_selected, in_colormap_selected, in_button_pressed,
 * in_error -- core itself calls these, so they can't be things only
 * ncview_ui implements. Also not here
 * (upstream had them as trivial one-line forwards to a *_core* function;
 * ncview_ui just calls that core function directly instead): report_position
 * (-> view_report_position), in_change_dat (-> view_change_dat),
 * in_data_edit_dump (-> view_data_edit_dump), in_change_current
 * (-> view_change_cur_dim). Also not here: in_make_dim_buttons and
 * in_clear_dim_buttons -- declared upstream but never actually called from
 * any core file (dead prototypes, like clip_i()); the real dimension-panel
 * entry points core uses are x_init_dim_info() and in_fill_dim_info(),
 * both now reached only via the ViewerUi interface (ncview/viewer_ui.h),
 * not as free functions -- see Phase 11c above.
 */
void 	in_set_label		( Label label_id, const char *string );
Message	in_dialog		( const char *message, int want_cancel_button );
void 	in_set_cursor_busy	( void );
void 	in_set_cursor_normal	( void );
void 	in_flush		( void );
void 	in_timer_clear		( void );
/* Called by do_print() (core/src/do_print.cc) once it has gathered the
 * metadata/pixels to print (info) and the user has confirmed the
 * page-layout settings in the printer_options() dialog below (po). Pops
 * the platform's native print dialog (printer/paper/orientation/copies,
 * "print to file") -- ncview_ui owns rendering the page onto whatever
 * surface that dialog hands back. */
void	in_print		( const PrintInfo &info, const PrintOptions &po );

/******************************************************************************
 * Functions core calls directly (not via in_*) that are nonetheless UI
 * dialogs/state, implemented by ncview_ui.
 */
Message	printer_options		( PrintOptions *po );
int	x_seen_colormap_name( const char *name );
void	x_error( const char *message );
void	pix_to_rgb( ncv_pixel pix, int *r, int *g, int *b );
void	in_create_colormap	( const char *name, const ncv_pixel r[256], const ncv_pixel g[256], const ncv_pixel b[256] );
