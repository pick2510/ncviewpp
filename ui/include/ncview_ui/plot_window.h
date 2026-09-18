/*
 * ui/include/ncview_ui/plot_window.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * The M4 replacement for upstream's src/interface/plot_xy.c +
 * src/interface/SciPlot.c: a small popup window showing one or more XY
 * line plots ("Plot Along Dimension" from the main window's variable
 * browser). Unlike SciPlot (a custom Xt widget with a ~30-entry-point API,
 * see docs/PORTING.md), PlotWidget only implements what plot_xy.c actually uses:
 * autoscaled/user-set linear or log axes, up to MAX_LINES_PER_PLOT lines
 * with a legend, a data dump, and PostScript export.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Widget.H>
#include <FL/fl_draw.H>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

namespace ncview_ui {

struct PlotLine {
	std::vector<double> x, y;
	std::string legend;
	Fl_Color color;
};

// Draws the axes, lines and legend for one XY plot. Everything about a
// SciPlot widget that plot_xy.c actually touches (scale queries/sets, log
// toggles, an x-axis tick-label formatter, mouse position reporting, data
// export, PostScript export) is a member here instead of a separate ~30
// function C API.
class PlotWidget : public Fl_Widget {
public:
	PlotWidget( int x, int y, int w, int h );

	int addLine( size_t n, const double *xvals, const double *yvals, const char *legend, Fl_Color color );

	void setTitle( const char *s )  { title_ = s ? s : ""; }
	void setXLabel( const char *s ) { xlabel_ = s ? s : ""; }
	void setYLabel( const char *s ) { ylabel_ = s ? s : ""; }

	void setXLog( bool on ) { xlog_ = on; redraw(); }
	void setYLog( bool on ) { ylog_ = on; redraw(); }
	bool xLog() const { return xlog_; }
	bool yLog() const { return ylog_; }

	void setXAutoScale();
	void setYAutoScale();
	void setXUserScale( double lo, double hi );
	void setYUserScale( double lo, double hi );
	void queryXScale( float *lo, float *hi ) const { *lo = (float)xmin_; *hi = (float)xmax_; }
	void queryYScale( float *lo, float *hi ) const { *lo = (float)ymin_; *hi = (float)ymax_; }

	void setXAxisFormatter( std::function<void(float,char*,size_t)> fmt ) { xfmt_ = std::move( fmt ); }
	void setMotionCallback( std::function<void(float,float)> cb ) { motion_cb_ = std::move( cb ); }

	void exportData( FILE *f ) const;
	void print() const;

	int  numLines() const { return (int)lines_.size(); }

	void draw() override;
	int  handle( int event ) override;

private:
	struct Area { int x, y, w, h; };
	Area plotArea() const;
	double dataToScreenX( const Area &a, double v ) const;
	double dataToScreenY( const Area &a, double v ) const;
	double screenToDataX( const Area &a, int sx ) const;
	double screenToDataY( const Area &a, int sy ) const;
	void   recomputeAutoRangeX();
	void   recomputeAutoRangeY();

	std::vector<PlotLine> lines_;
	std::string title_, xlabel_, ylabel_;
	bool xlog_ = false, ylog_ = false;
	bool xauto_ = true, yauto_ = true;
	double xmin_ = 0, xmax_ = 1, ymin_ = 0, ymax_ = 1;
	std::function<void(float,char*,size_t)> xfmt_;
	std::function<void(float,float)> motion_cb_;
};

// The popup window: PlotWidget plus the Close/Print/Dump/Locked controls,
// the X-axis dimension chooser, and the log-scale checkboxes from
// upstream's x_popup_XY_graph(). Owns itself -- deleted when closed.
class PlotWindow {
public:
	static PlotWindow *create( size_t n, int dimindex, const double *xvals, const double *yvals,
			const char *x_axis_title, const char *y_axis_title, const char *title,
			const char *legend, const Stringlist *scannable_dims, int screen_x, int screen_y );

	void addLine( size_t n, const double *xvals, const double *yvals, const char *legend );
	bool locked() const { return locked_; }
	void setLocked( bool locked );
	int  dimIndex() const { return dimindex_; }
	void setIndex( int i ) { index_ = i; }
	int  index() const { return index_; }

private:
	explicit PlotWindow( int dimindex );
	static void closeCallback( Fl_Widget *, void *data );
	static void printCallback( Fl_Widget *, void *data );
	static void dumpCallback( Fl_Widget *, void *data );
	static void lockedCallback( Fl_Widget *, void *data );
	static void xAxisChoiceCallback( Fl_Widget *, void *data );
	static void xLogCallback( Fl_Widget *, void *data );
	static void yLogCallback( Fl_Widget *, void *data );
	static void xRangeCallback( Fl_Widget *, void *data );
	static void yRangeCallback( Fl_Widget *, void *data );
	void rangeButton( bool x_axis );

	Fl_Double_Window *win_ = nullptr;
	PlotWidget *plot_ = nullptr;
	Fl_Check_Button *locked_btn_ = nullptr;
	Fl_Choice *xaxis_choice_ = nullptr;
	std::vector<std::string> axis_names_;
	int dimindex_;
	int index_ = -1;
	bool locked_ = true;
	int next_color_ = 0;
};

// Entry points used by ui/src/interface_fltk.cc.
int  popupXYGraph( size_t n, int dimindex, double *xvals, double *yvals,
		const char *x_axis_title, const char *y_axis_title, const char *title,
		const char *legend, const Stringlist *scannable_dims );
void unlockPlot();

} // namespace ncview_ui
