/*
 * ui/src/interface_fltk.cc
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * FltkViewerUi (ncview_ui/fltk_viewer_ui.h): the FLTK implementation of
 * ncview/viewer_ui.h's ViewerUi interface, delegating to the MainWindow
 * singleton (ui/include/ncview_ui/main_window.h). This is the FLTK
 * replacement for upstream's src/interface/interface.c +
 * src/interface/x_interface.c.
 *
 * OOP_redesign plan, Step 9b: these were free functions (in_ / x_
 * prefixed) directly satisfying ncview/interface.h's declarations until this step;
 * they're now FltkViewerUi methods instead, with core/src/viewer_ui_bridge.cc
 * providing the free-function forwarders onto whichever ViewerUi is
 * currently installed. Bodies are unchanged from the free-function form
 * (a pure rename), including calls between them, which still resolve via
 * ordinary unqualified member lookup, since both are now members of the
 * same class.
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_PostScript.H>
#include <FL/Fl_Printer.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Table.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include "ncview_ui/fltk_viewer_ui.h"
#include "ncview_ui/main_window.h"
#include "ncview_ui/plot_window.h"

using ncview_ui::FltkViewerUi;
using ncview_ui::MainWindow;
using ncview_ui::instance;

/* ---- lifecycle / event loop ------------------------------------------- */

void FltkViewerUi::in_parse_args( int *p_argc, char **argv )
{
	// FLTK consumes its own -display/-geometry etc. via Fl::args() if
	// ever needed; ncview's own option parser (parse_options(), M4) runs
	// separately over what's left. Nothing FLTK-specific to strip yet.
	(void)p_argc; (void)argv;
}

void FltkViewerUi::in_initialize( void )
{
	// Upstream read this (and similar) from an X application-defaults
	// resource ("Ncview*blowupDefaultSize: 300", fallback_resources.h);
	// it's plain core state (view.cc:View::calculateBlowup() divides by it),
	// not something routed through a seam function, so ncview_ui just
	// sets it directly. Leaving it at its zero-initialized default is a
	// real bug, not a graceful default: View::calculateBlowup() divides by
	// it unconditionally, producing a divide-by-zero -> inf -> UB
	// float-to-int conversion (observed as options.blowup becoming
	// INT_MIN on this machine).
	options.blowup_default_size = 300;
	options.delta_step = 1; // unused anywhere in core today, set for fidelity

	// By this point ncview_main() has already run initialize_file_interface(),
	// so the global `variables` list is populated.
	instance()->populateVarList();
	instance()->window()->show();
	if( const char *sel = getenv( "NCVIEW_TEST_AUTOSELECT" ) ) {
		auto &vars = g_app.session.dataset().variablesMutable();
		NCVar *v = vars.empty() ? nullptr : vars[0].get();
		// A specific variable name may be given (besides "1", meaning "just
		// pick the first one"); useful for driving a chosen 2-D field in
		// headless/manual testing without a real mouse.
		if( std::strcmp( sel, "1" ) != 0 )
			for( auto &c : vars )
				if( c->name == sel ) { v = c.get(); break; }
		if( v != nullptr )
			in_variable_selected( v->name.c_str() );
	}
	if( const char *d = getenv( "NCVIEW_TEST_DIALOG" ) ) {
		// Same {name, action} table style as NCVIEW_TEST_BUTTON below --
		// wrapped in std::function since, unlike the button table's uniform
		// in_button_pressed(id, modifier) dispatch, these 8 actions have
		// genuinely different call shapes (some take a Modifier, some take
		// none, "print" defers to the next event-loop tick).
		static const struct { const char *name; std::function<void()> action; } kDialogs[] = {
			{ "range",    []{ g_app.controller.range( Modifier::M1 ); } },
			{ "options",  []{ g_app.controller.optionsDialog( Modifier::M1 ); } },
			{ "dimset",   []{ g_app.controller.dimset( Modifier::M1 ); } },
			{ "info",     []{ view->information(); } },
			{ "dataedit", []{ view->dataEdit(); } },
			{ "plot",     []{ g_app.controller.plotXY(); } },
			{ "overlay",  []{ do_overlay( OVERLAY_P8DEG, nullptr, false ); } },
			// do_print() reads the printopts defaults that ncview_main()
			// sets up via print_init() -- which runs *after* in_initialize()
			// returns (see ncview.cc). Defer to the first event-loop tick so
			// the manual "print" test hook sees the same state a real
			// button press would.
			{ "print",    []{ Fl::add_timeout( 0.0, []( void * ) { do_print(); } ); } },
		};
		for( const auto &e : kDialogs )
			if( std::strcmp( d, e.name ) == 0 ) { e.action(); break; }
	}
	if( const char *b = getenv( "NCVIEW_TEST_BUTTON" ) ) {
		// Drives any button through the exact same in_button_pressed() path
		// MainWindow::buttonCallback uses for a real click -- for
		// regression-checking buttons (blowup, transform, invert, ...)
		// under Xvfb without a real mouse. Deferred one tick for the same
		// reason "print" is: some of these (Button::Blowup, Button::Restart)
		// interact with state that in_initialize()'s caller (ncview_main())
		// only finishes setting up after in_initialize() returns.
		static const struct { const char *name; Button id; } kButtons[] = {
			{ "rewind", Button::Rewind }, { "backwards", Button::Backwards },
			{ "pause", Button::Pause }, { "forward", Button::Forward },
			{ "fastforward", Button::Fastforward }, { "colormap", Button::ColormapSelect },
			{ "invert_physical", Button::InvertPhysical }, { "invert_colormap", Button::InvertColormap },
			{ "minimum", Button::Minimum }, { "maximum", Button::Maximum },
			{ "blowup", Button::Blowup }, { "restart", Button::Restart },
			{ "transform", Button::Transform }, { "blowup_type", Button::BlowupType },
		};
		for( const auto &e : kButtons )
			if( std::strcmp( b, e.name ) == 0 ) {
				Button id = e.id;
				Fl::add_timeout( 0.0, [](void *data) { in_button_pressed( static_cast<Button>((intptr_t)data), Modifier::M1 ); },
					(void*)(intptr_t)id );
				break;
			}
	}
}

void FltkViewerUi::in_process_user_input( void )
{
	// Upstream's contract: never returns, loops handling UI events.
	Fl::run();
}

Stringlist *FltkViewerUi::in_choose_input_files( void )
{
	// One multi-select dialog covers both cases ncview_main() wants:
	// picking a single file, or picking a whole run's worth of
	// one-file-per-timestep output to open as a series -- the latter is
	// exactly the "virtual variable" multi-file merge core already does
	// for however many filenames it's handed (see Dataset::addVariable() in
	// util.cc), it's just normally spelled out on the command line.
	Fl_Native_File_Chooser chooser;
	chooser.title( "Open NetCDF File(s)" );
	chooser.type( Fl_Native_File_Chooser::BROWSE_MULTI_FILE );
	chooser.filter( "NetCDF Files\t*.{nc,cdf,nc4}\nAll Files\t*" );

	switch( chooser.show() ) {
		case -1: // error
			fl_alert( "Error choosing file(s): %s", chooser.errmsg() );
			return nullptr;
		case 1: // cancelled
			return nullptr;
		default:
			break;
		}

	Stringlist *file_list = nullptr;
	int n = chooser.count();
	for( int i = 0; i < n; i++ )
		stringlist_add_string( &file_list, chooser.filename(i) );
	return file_list;
}

void FltkViewerUi::in_flush( void )
{
	Fl::flush();
}

void FltkViewerUi::in_set_cursor_busy( void )   { instance()->setCursorBusy( true ); }
void FltkViewerUi::in_set_cursor_normal( void ) { instance()->setCursorBusy( false ); }

/* ---- timers ------------------------------------------------------------ */

namespace {
// ncview only ever has one outstanding animation tick at a time (rewind,
// fastforward, or the new-data-check timer), so a single slot is enough to
// let in_timer_clear() actually free a cancelled callback's closure instead
// of leaking it.
std::unique_ptr<std::function<void()>> g_pending_timer;

void timerTrampoline( void *data )
{
	auto *fn = static_cast<std::function<void()>*>( data );
	// Reclaim ownership before invoking, matching the pre-RAII code's
	// "unconditional delete" defensive fallback for the (never actually
	// exercised) case where fn isn't the current pending slot: any stale fn
	// would already have had its FLTK timeout cancelled by in_timer_clear(),
	// so this branch never runs in practice, but it must still free fn.
	std::unique_ptr<std::function<void()>> owner;
	if( g_pending_timer.get() == fn ) owner = std::move( g_pending_timer );
	else owner.reset( fn );
	(*fn)();
}
} // namespace

void FltkViewerUi::in_timer_clear( void )
{
	Fl::remove_timeout( timerTrampoline );
	g_pending_timer.reset();
}

void FltkViewerUi::in_timer_set( std::function<void()> callback, unsigned long delay_millisec )
{
	in_timer_clear();
	g_pending_timer = std::make_unique<std::function<void()>>( std::move( callback ) );
	Fl::add_timeout( delay_millisec / 1000.0, timerTrampoline, g_pending_timer.get() );
}

/* ---- labels / sensitivity / dim buttons -------------------------------- */

void FltkViewerUi::in_set_label( Label label_id, const char *string )
{
	if( string == nullptr ) return;
	instance()->setLabel( label_id, string );
}

void FltkViewerUi::in_set_sensitive( Button button_id, int state )
{
	instance()->setSensitive( button_id, state );
}

void FltkViewerUi::in_indicate_active_var( const char *var_name )
{
	instance()->indicateActiveVar( var_name );
}

void FltkViewerUi::in_indicate_active_dim( Dimension dimension, const char *dim_name )
{
	instance()->indicateActiveDim( dimension, dim_name );
}

void FltkViewerUi::in_fill_dim_info( const NCDim *d, int please_flip )
{
	instance()->fillDimInfo( d, please_flip );
}

void FltkViewerUi::in_set_cur_dim_value( const char *name, const char *string )
{
	instance()->setCurDimValue( name, string );
}

/* ---- 2-D field / colormap ------------------------------------------------ */

namespace {
// Shared by dumpFrameToPng() below and in_print() (further down): expands
// an index-buffer into tightly packed 8-bit RGB via pix_to_rgb()'s >>8
// contract (see MainWindow::pixelToRgb).
std::vector<unsigned char> expandPixelsToRgb( const ncv_pixel *data, size_t width, size_t height )
{
	std::vector<unsigned char> rgb( width * height * 3 );
	for( size_t i = 0; i < width * height; i++ ) {
		int r, g, b;
		pix_to_rgb( data[i], &r, &g, &b );
		rgb[i*3+0] = (unsigned char)(r >> 8);
		rgb[i*3+1] = (unsigned char)(g >> 8);
		rgb[i*3+2] = (unsigned char)(b >> 8);
	}
	return rgb;
}

// M5: "-frames" (options.dump_frames) dumps every displayed frame to a PNG,
// e.g. to assemble into a movie. Upstream's x_interface.c did this itself
// with a direct libpng call inside its x_draw_2d_field(); FLTK already
// bundles libpng for its own image support (fltk_images/fl_write_png.cxx),
// so this needs no new dependency.
void dumpFrameToPng( const unsigned char *data, size_t width, size_t height, size_t frameno )
{
	static bool error_state = false;
	if( error_state ) return;

	char filename[64];
	snprintf( filename, sizeof(filename), "frame.%05zu.png", frameno );

	std::vector<unsigned char> rgb = expandPixelsToRgb( data, width, height );
	if( fl_write_png( filename, rgb.data(), (int)width, (int)height, 3 ) != 0 ) {
		fprintf( stderr, "ncview: can't write PNG file %s\n", filename );
		error_state = true;
	}
}
} // namespace

void FltkViewerUi::in_draw_2d_field( const unsigned char *data, size_t width, size_t height, size_t timestep )
{
	if( options.dump_frames )
		dumpFrameToPng( data, width, height, timestep );
	instance()->draw2DField( data, width, height, timestep );
}

void FltkViewerUi::in_create_colormap( const char *name, const ncv_pixel r[256], const ncv_pixel g[256], const ncv_pixel b[256] )
{
	instance()->createColormap( name, r, g, b );
}

char *FltkViewerUi::in_install_next_colormap( int do_widgets )
{
	return instance()->installNextColormap( do_widgets );
}

char *FltkViewerUi::in_install_prev_colormap( int do_widgets )
{
	return instance()->installPrevColormap( do_widgets );
}

char *FltkViewerUi::in_install_colormap_by_name( const char *name, int do_widgets )
{
	return instance()->installColormapByName( name, do_widgets );
}

int FltkViewerUi::in_set_2d_size( size_t width, size_t height )
{
	int r = instance()->set2DSize( width, height );
	in_flush();
	// Upstream relies on growing the image widget generating an X11
	// "expose" event that its Xt event loop wires back to change_view()
	// (see view.cc:set_scan_variable, the comment above its
	// in_set_2d_size() call: "an expansion generates an expose event...
	// registered to call change_view"). FLTK has no equivalent wiring in
	// this port, so without this the very first frame of a newly
	// selected variable never actually gets drawn.
	if( r >= 1 ) g_app.controller.stepView( 0, FRAMES );
	return r;
}

/* ---- pointer / mouse ---------------------------------------------------- */

void FltkViewerUi::in_query_pointer_position( int *x, int *y )
{
	// Must return the same data-buffer-pixel coordinate space
	// view_report_position() gets (ImageView::screenToBuffer() undoes the
	// widget's position plus its zoom/pan/centering transform) -- core's
	// mouse_xy_to_data_xy() divides this straight through by options.blowup
	// with no further offset, so a raw Fl::event_x()/y() here (window-
	// relative, not widget-relative) would misplace every caller: plot_XY(),
	// set_min/max_from_curdata(), set_dataedit_place().
	instance()->queryPointerPosition( x, y );
}

/* ---- dialogs / errors ---------------------------------------------------- */

Message FltkViewerUi::in_dialog( const char *message, int want_cancel_button )
{
	if( want_cancel_button ) {
		int r = fl_choice( "%s", "Cancel", "OK", nullptr, message );
		return r == 1 ? Message::OK : Message::Cancel;
	}
	fl_alert( "%s", message );
	return Message::OK;
}

Message FltkViewerUi::in_choose_save_file( const char *title, const char *default_name, char *ret_path, size_t ret_path_size )
{
	Fl_Native_File_Chooser chooser;
	chooser.title( title );
	chooser.type( Fl_Native_File_Chooser::BROWSE_SAVE_FILE );
	chooser.options( Fl_Native_File_Chooser::SAVEAS_CONFIRM );
	if( default_name != nullptr ) chooser.preset_file( default_name );
	switch( chooser.show() ) {
		case -1: fl_alert( "Error choosing file: %s", chooser.errmsg() ); return Message::Cancel;
		case 1:  return Message::Cancel;
		default: break;
	}
	if( chooser.filename() == nullptr ) return Message::Cancel;
	std::strncpy( ret_path, chooser.filename(), ret_path_size - 1 );
	ret_path[ret_path_size-1] = '\0';
	return Message::OK;
}

void FltkViewerUi::x_error( const char *message )
{
	fl_alert( "%s", message ? message : "(unknown error)" );
}

/* ---- variable-info popup -------------------------------------------------- */

void FltkViewerUi::in_display_stuff( const char *s, const char *var_name )
{
	char window_title[132];
	snprintf( window_title, sizeof(window_title), "Attributes of \"%s\"", var_name ? var_name : "" );

	// Owns itself: deleted when the user hits Close. Upstream capped these
	// at MAX_DISPLAY_POPUPS live popups (interface/display_info.c); FLTK has
	// no widget-count pressure that made that limit necessary, so any
	// number may be open at once here.
	auto *win = new Fl_Double_Window( 520, 260, window_title );
	win->begin();
	auto *buf = new Fl_Text_Buffer();
	buf->text( s ? s : "" );
	auto *disp = new Fl_Text_Display( 10, 10, 500, 200 );
	disp->buffer( buf );
	disp->wrap_mode( Fl_Text_Display::WRAP_AT_BOUNDS, 0 );
	auto *close_btn = new Fl_Button( 220, 220, 80, 30, "Close" );
	close_btn->callback( []( Fl_Widget *w, void * ) {
		Fl_Double_Window *window = static_cast<Fl_Double_Window*>( w->window() );
		window->hide();
		Fl::delete_widget( window );
	} );
	win->resizable( disp );
	win->end();
	win->show();
}

/* ---- data-edit grid -------------------------------------------------------- */

namespace {

// One data-edit window can be open at a time (matches upstream: x_dataedit()
// runs its own blocking mini event loop, so only one is ever live). The
// table cells are backed by a reference to the std::vector<std::string>
// View::dataEdit() built and handed us (Phase 12b -- was a raw char**
// nobody freed); editing a cell rewrites that string in place, same as the
// old fixed-size buffer did.
class DataEditTable : public Fl_Table {
public:
	DataEditTable( int x, int y, int w, int h, int nx, int ny, std::vector<std::string> &cells )
		: Fl_Table( x, y, w, h ), nx_( nx ), cells_( cells )
	{
		rows( ny );
		cols( nx );
		row_header( 0 );
		col_header( 0 );
		row_height_all( 22 );
		col_width_all( 72 );
		end();
	}

	int nx() const { return nx_; }
	size_t cellCount() const { return cells_.size(); }
	std::string &cellAt( int index ) { return cells_[index]; }

protected:
	void draw_cell( TableContext context, int R, int C, int X, int Y, int W, int H ) override
	{
		if( context != CONTEXT_CELL ) return;
		int index = R * nx_ + C;
		fl_push_clip( X, Y, W, H );
		fl_color( FL_WHITE );
		fl_rectf( X, Y, W, H );
		fl_color( FL_BLACK );
		if( index >= 0 && (size_t)index < cells_.size() )
			fl_draw( cells_[index].c_str(), X + 3, Y, W - 6, H, FL_ALIGN_LEFT );
		fl_rect( X, Y, W, H );
		fl_pop_clip();
	}

private:
	int nx_;
	std::vector<std::string> &cells_;
};

DataEditTable *g_dataedit_table = nullptr;

void dataeditDoneCallback( Fl_Widget *w, void *data )
{
	*static_cast<bool*>( data ) = true;
	w->window()->hide();
}

void dataeditDumpCallback( Fl_Widget *, void * )
{
	view->dataEditDump();
}

} // namespace

void FltkViewerUi::x_dataedit( std::vector<std::string> &cells, int nx )
{
	int n = (int)cells.size();
	int ny = nx > 0 ? n / nx : 0;
	if( ny <= 0 ) return;

	Fl_Double_Window win( 520, 420, "Data Edit" );
	win.begin();
	DataEditTable table( 10, 10, 500, 350, nx, ny, cells );
	table.when( FL_WHEN_RELEASE );
	auto *dump_btn = new Fl_Button( 10, 370, 100, 30, "Dump Data" );
	dump_btn->callback( dataeditDumpCallback );
	bool done = false;
	auto *done_btn = new Fl_Button( 410, 370, 100, 30, "Done" );
	done_btn->callback( dataeditDoneCallback, &done );
	win.end();

	// Double-click (or single release; Fl_Table doesn't distinguish well
	// without extra bookkeeping) on a cell prompts for a new value, mirroring
	// upstream's list-widget click -> x_dialog() -> in_change_dat() flow.
	table.callback( []( Fl_Widget *w, void * ) {
		auto *t = static_cast<DataEditTable*>( w );
		if( t->callback_context() != Fl_Table::CONTEXT_CELL || Fl::event() != FL_RELEASE ) return;
		int row = t->callback_row(), col = t->callback_col();
		int index = row * t->nx() + col;
		if( index < 0 || (size_t)index >= t->cellCount() ) return;
		std::string &cell = t->cellAt( index );

		char line[132];
		strncpy( line, cell.c_str(), sizeof(line)-1 );
		line[sizeof(line)-1] = '\0';
		const char *result = fl_input( "Value:", line );
		if( result == nullptr ) return;

		float new_val, dummy;
		if( sscanf( result, "%f %f", &new_val, &dummy ) != 1 ) return;

		view->changeDat( (size_t)index, new_val );
		char buf[32];
		snprintf( buf, sizeof(buf), "%-10.5g", new_val );
		cell = buf;
		t->redraw();
	} );

	g_dataedit_table = &table;

	win.show();
	while( win.shown() ) Fl::wait();

	g_dataedit_table = nullptr;
}

void FltkViewerUi::in_set_edit_place( size_t index, int x, int y, int nx, int ny )
{
	(void)x; (void)y; (void)ny;
	if( g_dataedit_table == nullptr || nx <= 0 ) return;
	int row = (int)(index / (size_t)nx);
	int col = (int)(index % (size_t)nx);
	g_dataedit_table->set_selection( row, col, row, col );
	g_dataedit_table->row_position( row );
	g_dataedit_table->col_position( col );
	g_dataedit_table->redraw();
}

int FltkViewerUi::in_set_scan_dims( const Stringlist *dim_list, const char *x_axis_name, const char *y_axis_name, Stringlist **new_dim_list )
{
	return instance()->scanDimsDialog( dim_list, x_axis_name, y_axis_name, new_dim_list );
}

int FltkViewerUi::in_popup_XY_graph( size_t n, int dimindex, double *xvals, double *yvals, const char *x_axis_title,
	const char *y_axis_title, const char *title, const char *legend, const Stringlist *scannable_dims )
{
	if( scannable_dims == nullptr ) {
		in_error( "Internal error: got NULL scannable_dims in in_popup_XY_graph!" );
		return -1;
	}
	return ncview_ui::popupXYGraph( n, dimindex, xvals, yvals, x_axis_title, y_axis_title,
			title, legend, scannable_dims );
}

// Phase 13c: both were empty no-ops. Upstream really did pop its 2-D
// colour-contour window up and down as variables were selected; this port
// dropped that on the grounds that the pane is a fixed child widget rather
// than a separate window, and nothing appeared to depend on it. Something
// did: set_scan_variable()'s 1-D path calls in_popdown_2d_window() exactly
// so the *previous* variable's picture stops being on screen and, more to
// the point, stops being clickable. With these empty, a click on that
// stale picture ran ViewerController::plotXY() against a View with
// y_axis_id == -1 -- an out-of-bounds heap write. Phase 13b guards that
// from core's side; this stops it being reachable at all.
void FltkViewerUi::in_popup_2d_window( void )   { instance()->setImageVisible( true ); }
void FltkViewerUi::in_popdown_2d_window( void ) { instance()->setImageVisible( false ); }

int FltkViewerUi::in_report_auto_overlay( void )
{
	// Upstream's x_report_auto_overlay() returns an X application-resource
	// default ("Ncview*autoOverlay", app_data.auto_overlay) that's ANDed
	// with the live options.auto_overlay toggle (view.cc:set_scan_variable)
	// -- effectively a second, resource-file-only on/off switch. That
	// resource defaults to 1 (DEFAULT_AUTO_OVERLAY in x_interface.c) and
	// this port has no application-resource system to override it with, so
	// returning 0 here (the previous M3 stub) silently disabled automatic
	// overlays entirely, no matter what the Options dialog's "Automatic
	// coastline overlay" checkbox said. Return the upstream default instead.
	return 1;
}

/* ---- extra seam: real UI dialogs/state (M4 stubs for now) ---------------- */

void FltkViewerUi::set_options( void )
{
	instance()->setOptionsDialog();
}

Message FltkViewerUi::printer_options( PrintOptions *po )
{
	// do_print() calls this dialog before in_print() -- see the
	// NCVIEW_TEST_PRINT_FILE handling there. Skip this modal too under the
	// same env var, so the "print" ui_smoke.sh case can run do_print() to
	// completion headlessly, keeping print_init()'s layout defaults.
	if( getenv( "NCVIEW_TEST_PRINT_FILE" ) != nullptr )
		return Message::OK;
	return instance()->printerOptionsDialog( po );
}

namespace {

Fl_Font printFont( const std::string &name )
{
	if( name == "Courier" ) return FL_COURIER;
	if( name == "Times" )   return FL_TIMES;
	return FL_HELVETICA;
}

// Lays out one printed page -- image, title, axis labels, extra-info
// block, outline, and ID stamp -- from the metadata do_print.cc built
// (info) and the page-layout settings from the printer_options() dialog
// above (po). This is upstream's do_print.c print_header()/
// print_other_info() PostScript-writing logic, redone as fl_draw() calls;
// shared between the real Fl_Printer job in in_print() below and the
// NCVIEW_TEST_PRINT_FILE PostScript-to-file test hook, so both exercise
// the exact same layout code.
void renderPrintPage( Fl_Paged_Device &dev, const PrintInfo &info, const PrintOptions &po )
{
	int pw, ph;
	dev.printable_rect( &pw, &ph );

	int x_margin   = (int)(po.page_x_margin * 72.0f);
	int top_margin = (int)(po.page_upper_y_margin * 72.0f);
	int bot_margin = (int)(po.page_lower_y_margin * 72.0f);
	// Never let margins swallow the whole page -- upstream would happily
	// push the image off the page if they did.
	if( x_margin < 0 || x_margin * 2 >= pw ) x_margin = pw / 8;
	if( top_margin < 0 ) top_margin = 0;
	if( bot_margin < 0 || top_margin + bot_margin >= ph ) { top_margin = ph / 8; bot_margin = ph / 8; }

	int avail_w = std::max( 1, pw - 2 * x_margin );
	int avail_h = std::max( 1, ph - top_margin - bot_margin );

	Fl_Font font    = printFont( po.font_name );
	int font_size   = po.font_size > 0 ? po.font_size : 11;
	int header_size = po.header_font_size > 0 ? po.header_font_size : 16;
	int leading     = po.leading >= 0 ? po.leading : 3;
	const float id_font_scale = 0.7f;	/* how much smaller the ID stamp's font is */

	int img_x = x_margin, img_y = top_margin, img_w = avail_w, img_h = avail_h;

	if( !po.test_only && info.width > 0 && info.height > 0 && info.pixels != nullptr ) {
		std::vector<unsigned char> rgb = expandPixelsToRgb( info.pixels, info.width, info.height );
		Fl_RGB_Image image( rgb.data(), (int)info.width, (int)info.height, 3 );

		float scale_x = (float)avail_w / (float)info.width;
		float scale_y = (float)avail_h / (float)info.height;
		float scale   = scale_x < scale_y ? scale_x : scale_y;
		img_w = std::max( 1, (int)((float)info.width  * scale) );
		img_h = std::max( 1, (int)((float)info.height * scale) );
		img_x = x_margin + (avail_w - img_w) / 2;

		std::unique_ptr<Fl_Image> scaled( image.copy( img_w, img_h ) );
		scaled->draw( img_x, img_y );
	}

	if( po.include_outline || po.test_only ) {
		fl_color( FL_BLACK );
		fl_rect( img_x, img_y, img_w, img_h );
		if( po.test_only ) {
			fl_line( img_x, img_y, img_x + img_w, img_y + img_h );
			fl_line( img_x, img_y + img_h, img_x + img_w, img_y );
		}
	}

	int center_x        = img_x + img_w / 2;
	int bottom_of_image = img_y + img_h;

	if( !info.title.empty() ) {
		fl_font( font, header_size );
		fl_draw( info.title.c_str(), center_x - (int)(fl_width( info.title.c_str() ) / 2), img_y - leading );
	}

	int label_y = bottom_of_image + font_size + leading;
	if( !info.x_axis_label.empty() ) {
		fl_font( font, font_size );
		fl_draw( info.x_axis_label.c_str(),
			center_x - (int)(fl_width( info.x_axis_label.c_str() ) / 2), label_y );
		label_y += font_size + leading;
	}

	if( !info.extra_info.empty() ) {
		fl_font( font, font_size );
		int line_y = label_y + font_size;
		for( const auto &line : info.extra_info ) {
			fl_draw( line.c_str(), x_margin, line_y );
			line_y += leading + font_size;
		}
	}

	// The Y-axis label and ID stamp are rotated 90 degrees -- upstream's
	// "gsave 90 rotate ... show grestore" -- via fl_draw()'s own rotated
	// overload rather than Fl_Paged_Device::origin()/rotate(), which
	// rotates the whole graphics state (including anything drawn
	// afterward) and needs a careful reset; this rotates just the one
	// string, about the given point, leaving everything else alone.
	if( !info.y_axis_label.empty() ) {
		fl_font( font, font_size );
		int label_w = (int)fl_width( info.y_axis_label.c_str() );
		fl_draw( 90, info.y_axis_label.c_str(),
			x_margin - leading - 2, img_y + img_h / 2 + label_w / 2 );
	}

	if( !info.id_stamp.empty() ) {
		fl_font( font, std::max( 1, (int)((float)font_size * id_font_scale) ) );
		fl_draw( 90, info.id_stamp.c_str(), pw - x_margin, bottom_of_image );
	}
}

} // namespace

void FltkViewerUi::in_print( const PrintInfo &info, const PrintOptions &po )
{
	if( const char *test_file = getenv( "NCVIEW_TEST_PRINT_FILE" ) ) {
		// Headless test hook (tests/ui_smoke.sh's "print" case): the real
		// Fl_Printer::begin_job() pops a native, unclosable modal dialog
		// under Xvfb, so route straight to a PostScript file using the
		// identical layout code instead.
		FILE *f = fopen( test_file, "w" );
		if( f == nullptr ) return;
		Fl_PostScript_File_Device dev;
		dev.begin_job( f, 1 );	/* always returns 0; doesn't close f */
		dev.begin_page();
		renderPrintPage( dev, info, po );
		dev.end_page();
		dev.end_job();
		fclose( f );
		return;
	}

	Fl_Printer printer;
	if( printer.begin_job( 1 ) != 0 )	/* cancelled, or no printer available */
		return;
	printer.begin_page();
	renderPrintPage( printer, info, po );
	printer.end_page();
	printer.end_job();
}

Message FltkViewerUi::x_range( float old_min, float old_max, float global_min, float global_max, float *new_min, float *new_max, int *allvars )
{
	return instance()->rangeDialog( old_min, old_max, global_min, global_max, new_min, new_max, allvars );
}

int FltkViewerUi::x_seen_colormap_name( const char *name )
{
	return instance()->seenColormapName( name ) ? 1 : 0;
}

void FltkViewerUi::x_check_legal_colormap_loaded( void )
{
	instance()->checkLegalColormapLoaded();
}

void FltkViewerUi::x_create_colorbar( float user_min, float user_max, Transform transform )
{
	instance()->createColorbar( user_min, user_max, transform );
}

void FltkViewerUi::x_draw_colorbar( void )
{
	instance()->drawColorbar();
}

void FltkViewerUi::x_force_set_invert_state( int state )
{
	(void)state;
}

void FltkViewerUi::x_init_dim_info( const Stringlist *dim_list )
{
	instance()->makeDimButtons( dim_list );
}

void FltkViewerUi::x_set_var_sensitivity( const char *varname, int sens )
{
	(void)varname; (void)sens;
}

void FltkViewerUi::unlock_plot( void ) { ncview_ui::unlockPlot(); }

Stringlist *FltkViewerUi::get_persistent_X_state( void )
{
	return nullptr;
}

void FltkViewerUi::pix_to_rgb( ncv_pixel pix, int *r, int *g, int *b )
{
	instance()->pixelToRgb( pix, r, g, b );
}
