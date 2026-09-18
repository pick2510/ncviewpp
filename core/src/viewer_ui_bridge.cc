/*
 * core/src/viewer_ui_bridge.cc
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * See ncview/viewer_ui.h. OOP_redesign plan, Step 9b. Defines every free
 * function ncview/interface.h declares (core's toolkit seam) as a one-line
 * forwarder onto the currently-installed ViewerUi (g_app.ui). This is
 * what lets every existing core call site (view.cc, util.cc, do_print.cc,
 * overlay.cc, ncview.cc, viewer_controller.cc) keep calling these
 * functions by their original free-function names, unchanged, while the
 * actual implementation now lives behind a swappable virtual interface
 * (FltkViewerUi for the real app, a recording fake for tests) instead of
 * requiring a second, parallel set of ~48 free-function definitions per
 * binary.
 *
 * "Refine the architecture" plan, Phase 11a/11b threaded a ViewerUi&
 * (or g_app.ui directly) through most call sites, so most of the seam is
 * now reached by method call (ui.in_x(...) / g_app.ui->in_x(...)) rather
 * than through one of these forwarders. Phase 11c re-verified, line by
 * line and comment-aware (a naive grep overcounts: many "hits" below were
 * comments or FltkViewerUi's own qualified method *definitions*, not
 * calls), exactly which of the original 48 forwarders still have a real
 * bare free-function caller anywhere in core/, ui/, app/, or tests/, and
 * deleted the 35 that had none -- see ncview/interface.h for the same
 * accounting against its declarations. What's left below still has at
 * least one genuine bare caller:
 *   - in_set_label: view.cc's view_report_position_vals(), one of
 *     Phase 11a's deliberately-deferred free functions.
 *   - in_create_colormap: ncview.cc's create_default_colormap() (dead
 *     code, zero callers of *it*, left alone per 11a) and
 *     colormap_library.cc's init_cmap_from_file()/init_cmap_from_data()
 *     (also deferred by 11a -- fixed test call-site signatures).
 *   - in_set_cursor_busy/in_set_cursor_normal/in_print/printer_options:
 *     do_print.cc's do_print()/build_print_info(), deferred by 11a for
 *     the same reason (large test/UI call-site blast radius).
 *   - x_seen_colormap_name: colormap_library.cc, same colormap deferral.
 *   - x_error: overlay.cc's do_overlay() (deferred by 11a) and
 *     ui/src/plot_window.cc (ui/-side code, outside this plan's
 *     core-focused scope).
 *   - in_dialog: not called bare from core/ui/tests directly, but
 *     in_error() (below, in this same file) calls it as a bare free
 *     function -- keeping in_dialog's forwarder is what lets in_error()
 *     stay a one-line free function instead of needing its own access to
 *     g_app.ui.
 *   - in_flush/in_timer_clear/pix_to_rgb: each has exactly one bare
 *     caller, and it's ui/src/interface_fltk.cc calling itself (i.e.
 *     FltkViewerUi's own implementation reaching another of its own
 *     methods through the free-function seam instead of `this->`) --
 *     harmless, ui/-internal, and outside this phase's core-focused scope.
 */
#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/app_context.h"

void 	in_set_label		( Label label_id, const char *string )
{
	g_app.ui->in_set_label( label_id, string );
}

void	in_create_colormap	( const char *name, const ncv_pixel r[256], const ncv_pixel g[256], const ncv_pixel b[256] )
{
	g_app.ui->in_create_colormap( name, r, g, b );
}

Message	in_dialog		( const char *message, int want_cancel_button )
{
	return g_app.ui->in_dialog( message, want_cancel_button );
}

void 	in_set_cursor_busy	( void )
{
	g_app.ui->in_set_cursor_busy();
}

void 	in_set_cursor_normal	( void )
{
	g_app.ui->in_set_cursor_normal();
}

void 	in_flush		( void )
{
	g_app.ui->in_flush();
}

void 	in_timer_clear		( void )
{
	g_app.ui->in_timer_clear();
}

void	in_print		( const PrintInfo &info, const PrintOptions &po )
{
	g_app.ui->in_print( info, po );
}

Message	printer_options		( PrintOptions *po )
{
	return g_app.ui->printer_options( po );
}

int	x_seen_colormap_name( const char *name )
{
	return g_app.ui->x_seen_colormap_name( name );
}

void	x_error( const char *message )
{
	g_app.ui->x_error( message );
}

void	pix_to_rgb( ncv_pixel pix, int *r, int *g, int *b )
{
	g_app.ui->pix_to_rgb( pix, r, g, b );
}

/* Formerly util.cc's in_error() (Phase 4b, "refine the architecture"
 * plan): indicates an error condition which can be continued from.
 * Routed through in_dialog() (this same seam) rather than being one
 * itself -- kept here since every other UI-seam function lives in this
 * file, even though it forwards to another free function rather than
 * directly to g_app.ui. */
void
in_error( const char *message )
{
	in_dialog( message, false );
}
