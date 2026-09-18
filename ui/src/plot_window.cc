/*
 * ui/src/plot_window.cc
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * See ncview_ui/plot_window.h for what this replaces (SciPlot.c + plot_xy.c).
 */
#include "ncview_ui/plot_window.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include <FL/Fl.H>
#include <FL/Fl_Float_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Window.H>
#include <FL/fl_ask.H>

namespace ncview_ui {

/* ================= PlotWidget ================================================ */

PlotWidget::PlotWidget( int x, int y, int w, int h ) : Fl_Widget( x, y, w, h )
{
	box( FL_DOWN_BOX );
	color( FL_WHITE );
}

int PlotWidget::addLine( size_t n, const double *xvals, const double *yvals, const char *legend, Fl_Color color )
{
	PlotLine line;
	line.x.assign( xvals, xvals + n );
	line.y.assign( yvals, yvals + n );
	line.legend = legend ? legend : "";
	line.color = color;
	lines_.push_back( std::move( line ) );

	if( xauto_ ) recomputeAutoRangeX();
	if( yauto_ ) recomputeAutoRangeY();
	redraw();
	return (int)lines_.size() - 1;
}

void PlotWidget::recomputeAutoRangeX()
{
	bool have = false;
	double lo = 0, hi = 1;
	for( const auto &l : lines_ )
		for( double v : l.x ) {
			if( !have || v < lo ) lo = v;
			if( !have || v > hi ) hi = v;
			have = true;
		}
	if( have ) { xmin_ = lo; xmax_ = hi; }
}

void PlotWidget::recomputeAutoRangeY()
{
	bool have = false;
	double lo = 0, hi = 1;
	for( const auto &l : lines_ )
		for( double v : l.y ) {
			if( !have || v < lo ) lo = v;
			if( !have || v > hi ) hi = v;
			have = true;
		}
	if( have ) { ymin_ = lo; ymax_ = hi; }
}

void PlotWidget::setXAutoScale() { xauto_ = true; recomputeAutoRangeX(); redraw(); }
void PlotWidget::setYAutoScale() { yauto_ = true; recomputeAutoRangeY(); redraw(); }
void PlotWidget::setXUserScale( double lo, double hi ) { xauto_ = false; xmin_ = lo; xmax_ = hi; redraw(); }
void PlotWidget::setYUserScale( double lo, double hi ) { yauto_ = false; ymin_ = lo; ymax_ = hi; redraw(); }

PlotWidget::Area PlotWidget::plotArea() const
{
	// Fixed margins: left for Y tick labels, bottom for X tick labels +
	// x-axis title, top for the plot title, right for the legend.
	Area a;
	a.x = x() + 55;
	a.y = y() + 25;
	a.w = w() - 55 - 130;
	a.h = h() - 25 - 40;
	if( a.w < 10 ) a.w = 10;
	if( a.h < 10 ) a.h = 10;
	return a;
}

namespace {
double toAxis( double v, bool log_scale )
{
	if( !log_scale ) return v;
	return std::log10( v > 0 ? v : 1e-30 );
}
} // namespace

double PlotWidget::dataToScreenX( const Area &a, double v ) const
{
	double lo = toAxis( xmin_, xlog_ ), hi = toAxis( xmax_, xlog_ ), val = toAxis( v, xlog_ );
	if( hi == lo ) hi = lo + 1;
	return a.x + (val - lo) / (hi - lo) * a.w;
}

double PlotWidget::dataToScreenY( const Area &a, double v ) const
{
	double lo = toAxis( ymin_, ylog_ ), hi = toAxis( ymax_, ylog_ ), val = toAxis( v, ylog_ );
	if( hi == lo ) hi = lo + 1;
	// screen Y grows downward; data max is at the top of the plot area.
	return a.y + a.h - (val - lo) / (hi - lo) * a.h;
}

double PlotWidget::screenToDataX( const Area &a, int sx ) const
{
	double lo = toAxis( xmin_, xlog_ ), hi = toAxis( xmax_, xlog_ );
	double frac = a.w > 0 ? (double)(sx - a.x) / a.w : 0.0;
	double val = lo + frac * (hi - lo);
	return xlog_ ? std::pow( 10.0, val ) : val;
}

double PlotWidget::screenToDataY( const Area &a, int sy ) const
{
	double lo = toAxis( ymin_, ylog_ ), hi = toAxis( ymax_, ylog_ );
	double frac = a.h > 0 ? (double)(a.y + a.h - sy) / a.h : 0.0;
	double val = lo + frac * (hi - lo);
	return ylog_ ? std::pow( 10.0, val ) : val;
}

void PlotWidget::draw()
{
	draw_box();
	Area a = plotArea();

	fl_color( FL_BLACK );
	fl_rect( a.x, a.y, a.w, a.h );

	/* ---- axis ticks ---- */
	const int kTicks = 5;
	fl_font( FL_HELVETICA, 10 );
	for( int i = 0; i <= kTicks; i++ ) {
		double frac = (double)i / kTicks;
		double lo = toAxis( xmin_, xlog_ ), hi = toAxis( xmax_, xlog_ );
		double axis_val = lo + frac * (hi - lo);
		double data_val = xlog_ ? std::pow( 10.0, axis_val ) : axis_val;
		int sx = a.x + (int)(frac * a.w);

		fl_line( sx, a.y + a.h, sx, a.y + a.h + 4 );
		char buf[64];
		if( xfmt_ ) xfmt_( (float)data_val, buf, sizeof(buf) );
		else snprintf( buf, sizeof(buf), "%g", data_val );
		fl_draw( buf, sx - 30, a.y + a.h + 6, 60, 16, FL_ALIGN_CENTER );
	}
	for( int i = 0; i <= kTicks; i++ ) {
		double frac = (double)i / kTicks;
		double lo = toAxis( ymin_, ylog_ ), hi = toAxis( ymax_, ylog_ );
		double axis_val = lo + frac * (hi - lo);
		double data_val = ylog_ ? std::pow( 10.0, axis_val ) : axis_val;
		int sy = a.y + a.h - (int)(frac * a.h);

		fl_line( a.x - 4, sy, a.x, sy );
		char buf[64];
		snprintf( buf, sizeof(buf), "%g", data_val );
		fl_draw( buf, a.x - 54, sy - 8, 50, 16, FL_ALIGN_RIGHT );
	}

	/* ---- title / axis labels ---- */
	fl_font( FL_HELVETICA_BOLD, 12 );
	fl_draw( title_.c_str(), x(), y(), w() - 130, 20, FL_ALIGN_CENTER );
	fl_font( FL_HELVETICA, 10 );
	fl_draw( xlabel_.c_str(), a.x, a.y + a.h + 22, a.w, 16, FL_ALIGN_CENTER );
	fl_draw( ylabel_.c_str(), x() + 2, y(), 50, 16, FL_ALIGN_LEFT );

	/* ---- lines ---- */
	fl_push_clip( a.x, a.y, a.w, a.h );
	for( const auto &l : lines_ ) {
		fl_color( l.color );
		size_t n = l.x.size();
		if( n >= 2 ) {
			fl_begin_line();
			for( size_t i = 0; i < n; i++ )
				fl_vertex( dataToScreenX( a, l.x[i] ), dataToScreenY( a, l.y[i] ) );
			fl_end_line();
		}
		for( size_t i = 0; i < n; i++ ) {
			int sx = (int)dataToScreenX( a, l.x[i] );
			int sy = (int)dataToScreenY( a, l.y[i] );
			fl_pie( sx - 2, sy - 2, 5, 5, 0, 360 );
		}
	}
	fl_pop_clip();

	/* ---- legend ---- */
	int lx = a.x + a.w + 10, ly = a.y + 4;
	fl_font( FL_HELVETICA, 10 );
	for( const auto &l : lines_ ) {
		fl_color( l.color );
		fl_pie( lx, ly, 8, 8, 0, 360 );
		fl_color( FL_BLACK );
		fl_draw( l.legend.c_str(), lx + 12, ly - 2, 110, 16, FL_ALIGN_LEFT );
		ly += 16;
	}
}

int PlotWidget::handle( int event )
{
	switch( event ) {
		case FL_ENTER:
			return 1;
		case FL_MOVE: {
			Area a = plotArea();
			if( motion_cb_ )
				motion_cb_( (float)screenToDataX( a, Fl::event_x() ), (float)screenToDataY( a, Fl::event_y() ) );
			return 1;
		}
	}
	return Fl_Widget::handle( event );
}

void PlotWidget::exportData( FILE *f ) const
{
	for( size_t li = 0; li < lines_.size(); li++ ) {
		const auto &l = lines_[li];
		fprintf( f, "# %s\n", l.legend.c_str() );
		for( size_t i = 0; i < l.x.size(); i++ )
			fprintf( f, "%g\t%g\n", l.x[i], l.y[i] );
		fprintf( f, "\n" );
	}
}

} // namespace ncview_ui

/* Fl_Printer pulls in platform print-dialog headers we don't want leaking
 * into the class declaration above; include it only here. */
#include <FL/Fl_Printer.H>

namespace ncview_ui {

void PlotWidget::print() const
{
	Fl_Printer printer;
	if( printer.begin_job( 1 ) != 0 )	/* cancelled, or no printer available */
		return;
	printer.begin_page();
	int pw, ph;
	printer.printable_rect( &pw, &ph );
	float scale_x = (float)pw / (float)w();
	float scale_y = (float)ph / (float)h();
	float scale = scale_x < scale_y ? scale_x : scale_y;
	if( scale < 1.0f ) printer.scale( scale );
	printer.print_widget( const_cast<PlotWidget*>(this) );
	printer.end_page();
	printer.end_job();
}

/* ================= PlotWindow ================================================= */

namespace {
struct ModalResult { bool ok = false; };
void modalOkCallback( Fl_Widget *w, void *data ) { static_cast<ModalResult*>(data)->ok = true; w->window()->hide(); }
void modalCancelCallback( Fl_Widget *w, void * ) { w->window()->hide(); }

const Fl_Color kLineColors[MAX_LINES_PER_PLOT] = { FL_RED, FL_BLACK, FL_BLUE, FL_DARK_GREEN, FL_MAGENTA };

// Slot table for the (at most MAX_PLOT_XY) live plot windows -- mirrors
// upstream's plot_XY_popup_widget[]/plot_XY_locked[] arrays in plot_xy.c.
// A window's slot index doubles as the "plot_index" handed back through
// in_popup_XY_graph(), which core stashes per-plot state under (view.cc's
// plot_XY_dim[]), so it must stay stable for the window's lifetime.
std::array<std::unique_ptr<PlotWindow>, MAX_PLOT_XY> g_plot_windows;
int g_last_popup_x = 18, g_last_popup_y = 18;

int lockedPlotIndex()
{
	for( int i = 0; i < MAX_PLOT_XY; i++ )
		if( g_plot_windows[i] != nullptr && g_plot_windows[i]->locked() )
			return i;
	return -1;
}

int freePlotIndex()
{
	for( int i = 0; i < MAX_PLOT_XY; i++ )
		if( g_plot_windows[i] == nullptr )
			return i;
	return -1;
}
} // namespace

PlotWindow::PlotWindow( int dimindex ) : dimindex_( dimindex ) {}

PlotWindow *PlotWindow::create( size_t n, int dimindex, const double *xvals, const double *yvals,
		const char *x_axis_title, const char *y_axis_title, const char *title,
		const char *legend, const Stringlist *scannable_dims, int screen_x, int screen_y )
{
	auto *pw = new PlotWindow( dimindex );

	pw->win_ = new Fl_Double_Window( screen_x, screen_y, 650, 380, title );
	pw->win_->begin();

	auto *close_btn = new Fl_Button( 10, 10, 60, 25, "Close" );
	close_btn->callback( closeCallback, pw );
	auto *print_btn = new Fl_Button( 75, 10, 60, 25, "Print" );
	print_btn->callback( printCallback, pw );
	auto *dump_btn = new Fl_Button( 140, 10, 60, 25, "Dump" );
	dump_btn->callback( dumpCallback, pw );
	pw->locked_btn_ = new Fl_Check_Button( 205, 10, 70, 25, "Locked" );
	pw->locked_btn_->value( 1 );
	pw->locked_btn_->callback( lockedCallback, pw );

	pw->xaxis_choice_ = new Fl_Choice( 320, 10, 150, 25 );
	if( scannable_dims != nullptr )
	for( auto &e : *scannable_dims ) {
		pw->axis_names_.push_back( e.string );
		pw->xaxis_choice_->add( e.string.c_str() );
	}
	pw->xaxis_choice_->value( 0 );
	pw->xaxis_choice_->callback( xAxisChoiceCallback, pw );

	auto *xlog = new Fl_Check_Button( 480, 10, 40, 25, "X log" );
	xlog->callback( xLogCallback, pw );
	auto *ylog = new Fl_Check_Button( 530, 10, 40, 25, "Y log" );
	ylog->callback( yLogCallback, pw );
	auto *xrange_btn = new Fl_Button( 580, 10, 30, 25, "X\xE2\x86\x95" );
	xrange_btn->tooltip( "Set X range" );
	xrange_btn->callback( xRangeCallback, pw );
	auto *yrange_btn = new Fl_Button( 615, 10, 30, 25, "Y\xE2\x86\x95" );
	yrange_btn->tooltip( "Set Y range" );
	yrange_btn->callback( yRangeCallback, pw );

	pw->plot_ = new PlotWidget( 10, 45, 630, 325 );
	pw->plot_->setTitle( title );
	pw->plot_->setXLabel( x_axis_title );
	pw->plot_->setYLabel( y_axis_title );
	pw->plot_->addLine( n, xvals, yvals, legend, kLineColors[0] );
	pw->next_color_ = 1;
	pw->plot_->setMotionCallback( [pw]( float xv, float yv ) {
		view_report_position_vals( xv, yv, pw->index() );
	} );

	pw->win_->resizable( pw->plot_ );
	pw->win_->end();
	pw->win_->show();
	return pw;
}

void PlotWindow::addLine( size_t n, const double *xvals, const double *yvals, const char *legend )
{
	if( next_color_ >= MAX_LINES_PER_PLOT ) return;
	plot_->addLine( n, xvals, yvals, legend, kLineColors[next_color_] );
	next_color_++;
	if( next_color_ >= MAX_LINES_PER_PLOT ) setLocked( false );
}

void PlotWindow::setLocked( bool locked )
{
	locked_ = locked;
	locked_btn_->value( locked ? 1 : 0 );
}

void PlotWindow::closeCallback( Fl_Widget *, void *data )
{
	auto *pw = static_cast<PlotWindow*>( data );
	// Reclaim ownership from the slot table before tearing down the FLTK
	// window, but don't let pw actually go away until after -- matches the
	// pre-RAII code's own delete-last ordering.
	std::unique_ptr<PlotWindow> owner;
	if( pw->index_ >= 0 && pw->index_ < MAX_PLOT_XY )
		owner = std::move( g_plot_windows[pw->index_] );
	pw->win_->hide();
	Fl::delete_widget( pw->win_ );
}

void PlotWindow::printCallback( Fl_Widget *, void *data )
{
	auto *pw = static_cast<PlotWindow*>( data );
	pw->plot_->print();
}

void PlotWindow::dumpCallback( Fl_Widget *, void *data )
{
	auto *pw = static_cast<PlotWindow*>( data );

	Fl_Native_File_Chooser chooser;
	chooser.title( "Dump plot data to file" );
	chooser.type( Fl_Native_File_Chooser::BROWSE_SAVE_FILE );
	chooser.options( Fl_Native_File_Chooser::SAVEAS_CONFIRM );
	chooser.preset_file( "ncview.dump" );
	switch( chooser.show() ) {
		case -1: fl_alert( "Error choosing file: %s", chooser.errmsg() ); return;
		case 1:  return;
		default: break;
	}
	if( chooser.filename() == nullptr ) return;

	FILE *f = fopen( chooser.filename(), "w" );
	if( f == nullptr ) {
		fl_alert( "Cannot open file for writing!" );
		return;
	}
	pw->plot_->exportData( f );
	fclose( f );
}

void PlotWindow::lockedCallback( Fl_Widget *w, void *data )
{
	auto *pw = static_cast<PlotWindow*>( data );
	auto *cb = static_cast<Fl_Check_Button*>( w );
	if( cb->value() ) {
		// Only one plot can be locked (i.e. accepting new lines) at a time.
		unlockPlot();
		pw->setLocked( true );
	} else {
		pw->locked_ = false;
	}
}

void PlotWindow::xAxisChoiceCallback( Fl_Widget *, void *data )
{
	auto *pw = static_cast<PlotWindow*>( data );
	int idx = pw->xaxis_choice_->value();
	if( idx < 0 || idx >= (int)pw->axis_names_.size() ) return;
	view->setXYPlotAxis( (char *)pw->axis_names_[idx].c_str() );
}

void PlotWindow::xLogCallback( Fl_Widget *w, void *data )
{
	auto *pw = static_cast<PlotWindow*>( data );
	pw->plot_->setXLog( static_cast<Fl_Check_Button*>(w)->value() != 0 );
}

void PlotWindow::yLogCallback( Fl_Widget *w, void *data )
{
	auto *pw = static_cast<PlotWindow*>( data );
	pw->plot_->setYLog( static_cast<Fl_Check_Button*>(w)->value() != 0 );
}

void PlotWindow::rangeButton( bool x_axis )
{
	float old_min, old_max;
	if( x_axis ) plot_->queryXScale( &old_min, &old_max );
	else         plot_->queryYScale( &old_min, &old_max );

	Fl_Window win( 300, 150, x_axis ? "X Range" : "Y Range" );
	char buf[64];

	Fl_Box min_label( 10, 15, 60, 25, "Min:" );
	Fl_Float_Input min_input( 80, 15, 200, 25 );
	snprintf( buf, sizeof(buf), "%g", old_min );
	min_input.value( buf );

	Fl_Box max_label( 10, 45, 60, 25, "Max:" );
	Fl_Float_Input max_input( 80, 45, 200, 25 );
	snprintf( buf, sizeof(buf), "%g", old_max );
	max_input.value( buf );

	Fl_Check_Button autoscale_cb( 10, 75, 200, 25, "Autoscale" );
	(void)min_label; (void)max_label;

	ModalResult result;
	Fl_Return_Button ok( 70, 110, 70, 30, "OK" );
	ok.callback( modalOkCallback, &result );
	Fl_Button cancel( 150, 110, 70, 30, "Cancel" );
	cancel.callback( modalCancelCallback, nullptr );

	win.end();
	win.set_modal();
	win.show();
	while( win.shown() ) Fl::wait();

	if( !result.ok ) return;

	if( autoscale_cb.value() ) {
		if( x_axis ) plot_->setXAutoScale(); else plot_->setYAutoScale();
	} else {
		double lo = atof( min_input.value() ), hi = atof( max_input.value() );
		if( x_axis ) plot_->setXUserScale( lo, hi ); else plot_->setYUserScale( lo, hi );
	}
}

void PlotWindow::xRangeCallback( Fl_Widget *, void *data ) { static_cast<PlotWindow*>(data)->rangeButton( true ); }
void PlotWindow::yRangeCallback( Fl_Widget *, void *data ) { static_cast<PlotWindow*>(data)->rangeButton( false ); }

/* ================= module-level plot management ================================ */

int popupXYGraph( size_t n, int dimindex, double *xvals, double *yvals,
		const char *x_axis_title, const char *y_axis_title, const char *title,
		const char *legend, const Stringlist *scannable_dims )
{
	int locked_index = lockedPlotIndex();
	if( locked_index != -1 ) {
		g_plot_windows[locked_index]->addLine( n, xvals, yvals, legend );
		return locked_index;
	}

	int index = freePlotIndex();
	if( index == -1 ) {
		x_error( "Reached maximum # of XY plots!" );
		return -1;
	}

	PlotWindow *pw = PlotWindow::create( n, dimindex, xvals, yvals, x_axis_title, y_axis_title,
			title, legend, scannable_dims, g_last_popup_x, g_last_popup_y );
	pw->setIndex( index );
	g_plot_windows[index].reset( pw );
	g_last_popup_x += 10;
	g_last_popup_y += 10;
	return index;
}

void unlockPlot()
{
	for( int i = 0; i < MAX_PLOT_XY; i++ )
		if( g_plot_windows[i] != nullptr )
			g_plot_windows[i]->setLocked( false );
}

} // namespace ncview_ui
