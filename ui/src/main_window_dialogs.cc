/*
 * ui/src/main_window_dialogs.cc
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * MainWindow's "M4 dialogs" -- setOptionsDialog()/rangeDialog()/
 * scanDimsDialog()/printerOptionsDialog() -- split out of main_window.cc
 * (Phase 9 of the "refine the architecture" plan). Unlike the rest of
 * MainWindow, which is tightly bound to shared widget members (win_,
 * image_, colorbar_, dim_rows_, button_bar_, ...), these four methods
 * touch none of that state: each builds its own small local Fl_Window,
 * runs a blocking modal Fl::wait() loop (the standard FLTK pattern), and
 * returns a result through its parameters. That's what makes them safely
 * separable while the rest of the class isn't -- a genuine cluster
 * boundary, not an arbitrary line count split.
 */
#include "ncview_ui/main_window.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Float_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Round_Button.H>
#include <FL/Fl_Window.H>

namespace ncview_ui {

/* ===================== M4 dialogs ===================== */
/* Small modal dialogs, run with their own Fl::wait() loop (the standard
 * FLTK pattern for a blocking modal window: show(), set_modal(), spin until
 * it's hidden by a button callback). Replaces upstream's Xt dialog/range.c
 * /set_options.c-family widgets one dialog at a time; see PORTING.md. */

namespace {
struct ModalResult { bool ok = false; };
struct OverlayBrowseData { MainWindow *self; Fl_Box *label_box; };

void modalOkCallback( Fl_Widget *w, void *data )
{
	static_cast<ModalResult*>(data)->ok = true;
	w->window()->hide();
}

void modalCancelCallback( Fl_Widget *w, void * )
{
	w->window()->hide();
}
} // namespace

void MainWindow::setOptionsDialog()
{
	// Upstream's set_options.c also has a "select which colormaps are
	// enabled for cycling" section, backed by interface/colormap_funcs.c
	// (X11 colorcell allocation, deliberately not ported -- see PORTING.md's
	// M6 notes); everything else there is reproduced here.
	const int kOverlayY = 135;
	int n_overlays = overlay_n_overlays();
	int overlay_bottom = kOverlayY + 20 + n_overlays * 24;

	Fl_Window win( 340, overlay_bottom + 80, "Options" );
	Fl_Check_Button autoscale( 10, 10, 300, 25, "Autoscale each frame" );
	autoscale.value( options.autoscale );
	Fl_Check_Button extra_info( 10, 40, 300, 25, "Show extra info" );
	extra_info.value( options.want_extra_info );
	Fl_Check_Button save_frames( 10, 70, 300, 25, "Save frames in memory" );
	save_frames.value( options.save_frames );
	Fl_Check_Button auto_overlay( 10, 100, 300, 25, "Automatic coastline overlay" );
	auto_overlay.value( options.auto_overlay );

	Fl_Box overlay_label( 10, kOverlayY, 300, 20, "Overlay:" );
	overlay_label.align( FL_ALIGN_LEFT | FL_ALIGN_INSIDE );
	overlay_label.labelfont( FL_HELVETICA_BOLD );

	const char **names = overlay_names();
	int current_overlay = overlay_current();
	int custom_idx = overlay_custom_n();
	std::vector<Fl_Round_Button *> overlay_btns;
	int y = kOverlayY + 20;
	for( int i = 0; i < n_overlays; i++ ) {
		auto *btn = new Fl_Round_Button( 10, y, 300, 22, names[i] );
		btn->type( FL_RADIO_BUTTON );
		if( i == current_overlay ) btn->setonly();
		overlay_btns.push_back( btn );
		y += 24;
	}

	// Upstream's equivalent (set_options.c's static overlay_filename) is
	// also a value that survives across dialog invocations, not reset
	// each time the dialog opens -- Phase 12c moved it onto this
	// singleton's own instance data (custom_overlay_filename_) instead of
	// a function-local static, same as set2DSize()'s last_2d_width_/
	// last_2d_height_.
	Fl_Box filename_box( 10, y, 230, 25 );
	filename_box.box( FL_DOWN_BOX );
	filename_box.align( FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP );
	filename_box.copy_label( custom_overlay_filename_.empty() ?
		"(no custom overlay file selected)" : custom_overlay_filename_.c_str() );
	Fl_Button browse_btn( 250, y, 80, 25, "Browse..." );
	OverlayBrowseData browse_data{ this, &filename_box };
	browse_btn.callback( []( Fl_Widget *, void *data ) {
		auto *bd = static_cast<OverlayBrowseData *>( data );
		Fl_Native_File_Chooser fc;
		fc.title( "Select custom overlay file" );
		char base_dir[1024];
		determine_overlay_base_dir( base_dir, sizeof(base_dir) );
		fc.directory( base_dir );
		if( fc.show() == 0 && fc.filename() != nullptr ) {
			bd->self->custom_overlay_filename_ = fc.filename();
			bd->label_box->copy_label( bd->self->custom_overlay_filename_.c_str() );
		}
	}, &browse_data );
	y += 35;

	ModalResult result;
	Fl_Return_Button ok( 100, y, 70, 30, "OK" );
	ok.callback( modalOkCallback, &result );
	Fl_Button cancel( 190, y, 70, 30, "Cancel" );
	cancel.callback( modalCancelCallback, nullptr );

	win.end();
	win.set_modal();
	win.show();
	while( win.shown() ) Fl::wait();

	if( result.ok ) {
		options.autoscale = autoscale.value();
		options.want_extra_info = extra_info.value();
		options.save_frames = save_frames.value();
		options.auto_overlay = auto_overlay.value();

		int new_overlay = current_overlay;
		for( int i = 0; i < n_overlays; i++ )
			if( overlay_btns[i]->value() ) { new_overlay = i; break; }
		// Re-apply if unchanged but "custom": matches upstream's own
		// set_options.c condition, letting a freshly Browse()'d filename
		// take effect even if "Custom" was already selected.
		if( new_overlay != current_overlay || new_overlay == custom_idx )
			do_overlay( new_overlay,
				new_overlay == custom_idx && !custom_overlay_filename_.empty() ?
					(char *)custom_overlay_filename_.c_str() : nullptr,
				false );

		g_app.controller.draw( true, false );
	}
}

Message MainWindow::rangeDialog( float old_min, float old_max, float global_min, float global_max,
		float *new_min, float *new_max, int *allvars )
{
	Fl_Window win( 320, 190, "Set Range" );
	char buf[64];

	Fl_Box global_box( 10, 10, 300, 20 );
	snprintf( buf, sizeof(buf), "Global range: %g to %g", global_min, global_max );
	global_box.copy_label( buf );

	Fl_Box min_label( 10, 40, 60, 25, "Min:" );
	Fl_Float_Input min_input( 80, 40, 220, 25 );
	snprintf( buf, sizeof(buf), "%g", old_min );
	min_input.value( buf );

	Fl_Box max_label( 10, 70, 60, 25, "Max:" );
	Fl_Float_Input max_input( 80, 70, 220, 25 );
	snprintf( buf, sizeof(buf), "%g", old_max );
	max_input.value( buf );

	Fl_Check_Button all_vars_cb( 10, 100, 300, 25, "Apply to all variables" );
	all_vars_cb.value( 0 );

	(void)min_label; (void)max_label;

	ModalResult result;
	Fl_Return_Button ok( 90, 145, 70, 30, "OK" );
	ok.callback( modalOkCallback, &result );
	Fl_Button cancel( 170, 145, 70, 30, "Cancel" );
	cancel.callback( modalCancelCallback, nullptr );

	win.end();
	win.set_modal();
	win.show();
	while( win.shown() ) Fl::wait();

	if( !result.ok ) return Message::Cancel;

	*new_min = (float)atof( min_input.value() );
	*new_max = (float)atof( max_input.value() );
	if( allvars ) *allvars = all_vars_cb.value();
	return Message::OK;
}

int MainWindow::scanDimsDialog( const Stringlist *dim_list, const char *x_axis_name, const char *y_axis_name,
		Stringlist **new_dim_list )
{
	std::vector<std::string> names;
	if( dim_list != nullptr )
		for( auto &e : *dim_list )
			names.push_back( e.string );
	if( names.empty() ) return 0;

	Fl_Window win( 320, 150, "Set Scan Dimensions" );
	Fl_Box x_label( 10, 15, 60, 25, "X axis:" );
	Fl_Choice x_choice( 90, 15, 210, 25 );
	Fl_Box y_label( 10, 50, 60, 25, "Y axis:" );
	Fl_Choice y_choice( 90, 50, 210, 25 );
	(void)x_label; (void)y_label;

	int x_default = 0, y_default = 0;
	for( size_t i = 0; i < names.size(); i++ ) {
		x_choice.add( names[i].c_str() );
		y_choice.add( names[i].c_str() );
		if( x_axis_name && names[i] == x_axis_name ) x_default = (int)i;
		if( y_axis_name && names[i] == y_axis_name ) y_default = (int)i;
	}
	x_choice.value( x_default );
	y_choice.value( names.size() > 1 ? (int)((y_default == x_default) ? (x_default+1)%names.size() : y_default) : 0 );

	ModalResult result;
	Fl_Return_Button ok( 90, 105, 70, 30, "OK" );
	ok.callback( modalOkCallback, &result );
	Fl_Button cancel( 170, 105, 70, 30, "Cancel" );
	cancel.callback( modalCancelCallback, nullptr );

	win.end();
	win.set_modal();
	win.show();
	while( win.shown() ) Fl::wait();

	if( !result.ok || new_dim_list == nullptr ) return 0;

	// Build the returned list Y-axis first, then X-axis (matching upstream's
	// in_set_scan_dims contract: "first the name of the Y dimension, then
	// the name of the X dimension").
	*new_dim_list = nullptr;
	stringlist_add_string( new_dim_list, names[y_choice.value()].c_str() );
	stringlist_add_string( new_dim_list, names[x_choice.value()].c_str() );
	return 1;
}

Message MainWindow::printerOptionsDialog( PrintOptions *po )
{
	// Where the output goes (printer vs file, which printer, paper,
	// orientation, copies) is the native print dialog's job now -- see
	// in_print(). This dialog only covers what that dialog can't:
	// page-layout settings for the ncview-drawn content of the page.
	static const char *kFontNames[] = { "Helvetica", "Courier", "Times" };

	Fl_Window win( 420, 235, "Print Layout" );
	char buf[64];

	Fl_Box margin_label( 10, 10, 90, 25, "Margins (in):" );
	Fl_Box xmar_label( 100, 10, 20, 25, "X" );
	Fl_Float_Input xmar_input( 120, 10, 50, 25 );
	snprintf( buf, sizeof(buf), "%g", po->page_x_margin ); xmar_input.value( buf );
	Fl_Box ytmar_label( 180, 10, 60, 25, "Y top" );
	Fl_Float_Input ytmar_input( 240, 10, 50, 25 );
	snprintf( buf, sizeof(buf), "%g", po->page_upper_y_margin ); ytmar_input.value( buf );
	Fl_Box ybmar_label( 300, 10, 60, 25, "Y bot" );
	Fl_Float_Input ybmar_input( 360, 10, 50, 25 );
	snprintf( buf, sizeof(buf), "%g", po->page_lower_y_margin ); ybmar_input.value( buf );

	Fl_Box font_label( 10, 45, 90, 25, "Font:" );
	Fl_Choice font_name_choice( 100, 45, 120, 25 );
	int font_index = 0;
	for( size_t i = 0; i < sizeof(kFontNames)/sizeof(kFontNames[0]); i++ ) {
		font_name_choice.add( kFontNames[i] );
		if( po->font_name == kFontNames[i] ) font_index = (int)i;
	}
	font_name_choice.value( font_index );
	Fl_Box fontsize_label( 230, 45, 40, 25, "Size" );
	Fl_Float_Input fontsize_input( 270, 45, 40, 25 );
	snprintf( buf, sizeof(buf), "%d", po->font_size ); fontsize_input.value( buf );
	Fl_Box headsize_label( 315, 45, 45, 25, "Head" );
	Fl_Float_Input headsize_input( 360, 45, 40, 25 );
	snprintf( buf, sizeof(buf), "%d", po->header_font_size ); headsize_input.value( buf );

	Fl_Check_Button include_title( 10, 80, 190, 25, "Title" );
	include_title.value( po->include_title );
	Fl_Check_Button include_axis( 10, 105, 190, 25, "Axis labels" );
	include_axis.value( po->include_axis_labels );
	Fl_Check_Button include_extra( 10, 130, 190, 25, "Extra info" );
	include_extra.value( po->include_extra_info );
	Fl_Check_Button include_outline( 210, 80, 190, 25, "Outline" );
	include_outline.value( po->include_outline );
	Fl_Check_Button include_id( 210, 105, 190, 25, "ID" );
	include_id.value( po->include_id );
	Fl_Check_Button test_only( 210, 130, 190, 25, "No image (test only)" );
	test_only.value( po->test_only );

	ModalResult result;
	Fl_Return_Button ok( 190, 185, 70, 30, "OK" );
	ok.callback( modalOkCallback, &result );
	Fl_Button cancel( 270, 185, 70, 30, "Cancel" );
	cancel.callback( modalCancelCallback, nullptr );

	win.end();
	win.set_modal();
	win.show();
	while( win.shown() ) Fl::wait();

	if( !result.ok ) return Message::Cancel;

	po->page_x_margin = (float)atof( xmar_input.value() );
	po->page_upper_y_margin = (float)atof( ytmar_input.value() );
	po->page_lower_y_margin = (float)atof( ybmar_input.value() );
	po->font_name = kFontNames[font_name_choice.value()];
	po->font_size = atoi( fontsize_input.value() );
	po->header_font_size = atoi( headsize_input.value() );
	po->include_title = include_title.value();
	po->include_axis_labels = include_axis.value();
	po->include_extra_info = include_extra.value();
	po->include_outline = include_outline.value();
	po->include_id = include_id.value();
	po->test_only = test_only.value();

	return Message::OK;
}

} // namespace ncview_ui
