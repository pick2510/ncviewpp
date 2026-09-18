/*
 * ui/include/ncview_ui/main_window.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * The FLTK implementation of ncview's main window. This is the M3 rewrite
 * of upstream's src/interface/x_interface.c (Xt/Xaw) -- same job (own the
 * 2-D field display, colorbar, button bar, labels, dimension controls,
 * variable selector), different toolkit. It is driven entirely through the
 * functions declared in ncview/interface.h (see ui/src/interface_fltk.cc,
 * which is the thin free-function layer FLTK callbacks and ncview_core call
 * into, delegating to this class).
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Pack.H>
#include <FL/Fl_Sys_Menu_Bar.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Slider.H>
#include <FL/Fl_Widget.H>

// This whole project is C++ throughout (core included), so these are
// ordinary C++-linkage includes -- no extern "C" needed, and it would be
// actively wrong here since in_timer_set() takes a std::function.
#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/frame_renderer.h"
#include "ncview/protos.h"

class ViewerController;

namespace ncview_ui {

// Displays the 2-D color-contour field. Owns nothing about the data; it's
// handed a fresh ncv_pixel buffer plus a shared 256-entry RGB colormap
// table each time core calls in_draw_2d_field().
class ImageView : public Fl_Widget {
public:
	ImageView( int x, int y, int w, int h );
	~ImageView() override;

	void setColormap( const unsigned char *r, const unsigned char *g, const unsigned char *b );
	void setData( const unsigned char *data, size_t width, size_t height );
	void draw() override;
	int handle( int event ) override;

	// Maps a window-relative point (Fl::event_x()/event_y() -- FLTK widget
	// coordinates are window-relative, not parent-relative) to a pixel
	// coordinate in the untransformed data buffer, undoing the current
	// zoom/pan/centering. Used both internally and by MainWindow so
	// in_query_pointer_position() reports positions consistent with what's
	// actually on screen, matching what view_report_position() gets.
	void screenToBuffer( int win_x, int win_y, int *bx, int *by ) const;

private:
	void zoomAt( int win_x, int win_y, double factor );

	unsigned char colormap_r_[256], colormap_g_[256], colormap_b_[256];
	std::vector<unsigned char> pixels_;   // index buffer, width_*height_
	std::vector<unsigned char> rgb_buf_;  // expanded RGB buffer, width_*height_*3
	size_t width_ = 0, height_ = 0;

	// Continuous zoom/pan replacing upstream's discrete Blowup button:
	// scroll wheel zooms (centered on the cursor), left-button drag pans.
	// zoom_ is a plain display multiplier applied on top of whatever
	// resolution core handed us -- it never touches core's own (still
	// automatic) blowup sizing.
	double zoom_ = 1.0;
	double panx_ = 0.0, pany_ = 0.0;
	static constexpr double kMinZoom = 0.1, kMaxZoom = 32.0;

	// Click-vs-drag disambiguation: a plain left-button release plots the
	// clicked point (see handle()'s FL_RELEASE case), but with panning now
	// also live on left-button drag, that release must be suppressed once
	// the button has moved far enough to count as a pan rather than a click.
	int press_x_ = 0, press_y_ = 0;
	double pan_start_x_ = 0.0, pan_start_y_ = 0.0;
	bool dragging_ = false;
};

// A simple horizontal gradient strip showing the current colormap over the
// data's [user_min, user_max] range. Upstream's cbar.c built a similarly
// simple strip (plus tick labels drawn elsewhere); this is the M3 minimum
// -- richer tick/label rendering is M4 polish.
class Colorbar : public Fl_Widget {
public:
	Colorbar( int x, int y, int w, int h );

	void setColormap( const unsigned char *r, const unsigned char *g, const unsigned char *b );
	void setRange( float user_min, float user_max, Transform transform );
	void draw() override;

private:
	unsigned char colormap_r_[256], colormap_g_[256], colormap_b_[256];
	float user_min_ = 0.f, user_max_ = 1.f;
	Transform transform_ = Transform::None;
};

// Payload for prev_btn/next_btn's callback (dimStepCallback): which dim,
// which direction/modifier, and the controller to call -- carried
// explicitly so the static callback never has to look one up by name.
struct DimStepCbData {
	std::string name;
	Modifier modifier;
	ViewerController *controller;
};

// Payload for value_slider's callback (dimSliderCallback): same reasoning.
struct DimSliderCbData {
	std::string name;
	ViewerController *controller;
};

struct DimRow {
	std::string name;
	int         index        = 0;        // this row's position in dim_pack_ -- see MainWindow::recenterDimRow()
	Fl_Group   *group        = nullptr;
	Fl_Box     *name_box     = nullptr;
	// A slider so the user can drag straight to a place instead of only
	// stepping one frame at a time with prev_btn/next_btn (which stay,
	// flanking it, for that single-step case) -- its label is the current
	// value's formatted text (a date, a coordinate, ...), not a number, so
	// it's drawn centered inside the slider itself rather than off to the
	// side the way Fl_Value_Slider's numeric readout would be.
	Fl_Slider  *value_slider = nullptr;
	Fl_Button  *prev_btn     = nullptr;
	Fl_Button  *next_btn     = nullptr;

	// Owns prev_btn/next_btn/value_slider's callback data (see
	// rebuildDimRow()) so it's freed when this row is torn down in
	// clearDimButtons(), instead of leaking on every rebuild.
	std::unique_ptr<DimStepCbData>   prev_cb_data;
	std::unique_ptr<DimStepCbData>   next_cb_data;
	std::unique_ptr<DimSliderCbData> slider_cb_data;
};

struct NamedColormap {
	std::string name;
	unsigned char r[256], g[256], b[256];
};

class MainWindow;

// Payload for colormap_choice_'s per-item callback (colormapChoiceCallback):
// which colormap index, and the MainWindow whose colormaps_/colormap
// selection it applies to -- carried explicitly instead of the callback
// reaching MainWindow::instance() by name.
struct ColormapCbData {
	size_t index;
	MainWindow *window;
};

// Fl_Double_Window has no resize callback of its own; this just forwards
// resize() to a std::function so MainWindow can re-run its layout whenever
// the user drags the window edge, instead of only laying out once at
// construction with a fixed 900x760.
class NcviewWindow : public Fl_Double_Window {
public:
	NcviewWindow( int w, int h, const char *title ) : Fl_Double_Window( w, h, title ) {}
	void resize( int X, int Y, int W, int H ) override
	{
		Fl_Double_Window::resize( X, Y, W, H );
		if( on_resize ) on_resize( W, H );
	}
	std::function<void(int,int)> on_resize;
};

class MainWindow {
public:
	MainWindow();

	Fl_Double_Window *window() { return win_; }

	// ---- ncview/interface.h contract -------------------------------
	void setLabel( Label label_id, const char *s );
	void setSensitive( Button button_id, int state );
	void indicateActiveVar( const char *var_name );
	void indicateActiveDim( Dimension dimension, const char *dim_name );
	void makeDimButtons( const Stringlist *dim_list );
	void clearDimButtons();
	void fillDimInfo( const NCDim *d, int please_flip );
	void setCurDimValue( const char *name, const char *value );
	void draw2DField( const unsigned char *data, size_t width, size_t height, size_t timestep );
	void createColormap( const char *name, const unsigned char *r, const unsigned char *g, const unsigned char *b );
	int  set2DSize( size_t width, size_t height );
	char *installNextColormap( int do_widgets );
	char *installPrevColormap( int do_widgets );
	char *installColormapByName( const char *name, int do_widgets );
	bool  seenColormapName( const char *name ) const;

	void  checkLegalColormapLoaded();
	void  createColorbar( float user_min, float user_max, Transform transform );
	void  drawColorbar();
	void  populateVarList();
	// Phase 13c: shows/hides the 2-D image pane, driven by core's
	// in_popup_2d_window()/in_popdown_2d_window() seam. Both were empty
	// no-ops in this port, so selecting a 1-D variable left the previous
	// variable's picture on screen and fully clickable -- see
	// tests/test_view_1d_guards.cc for what that reached.
	void  setImageVisible( bool visible );
	void  setCursorBusy( bool busy );
	void  pixelToRgb( ncv_pixel pix, int *r, int *g, int *b ) const;
	void  queryPointerPosition( int *x, int *y ) const;

	// ---- M4 dialogs -------------------------------------------------
	void setOptionsDialog();
	Message  rangeDialog( float old_min, float old_max, float global_min, float global_max,
			float *new_min, float *new_max, int *allvars );
	int  scanDimsDialog( const Stringlist *dim_list, const char *x_axis_name, const char *y_axis_name,
			Stringlist **new_dim_list );
	Message  printerOptionsDialog( PrintOptions *po );

private:
	// Height reserved for menu_bar_ at the very top of the window --
	// everything else (info rows, image, dim rows, button bar) sits this
	// many pixels lower than its own literal y in the constructor/layout(),
	// both of which reference this same constant rather than a second
	// hardcoded copy of it. Zero on macOS: menu_bar_ there is an
	// Fl_Sys_Menu_Bar, which puts the menu in the native system menu bar at
	// the top of the *screen*, not inside the window -- reserving in-window
	// space for it would just leave a blank gray strip at the top.
#ifdef __APPLE__
	static constexpr int kMenuBarH = 0;
#else
	static constexpr int kMenuBarH = 25;
#endif

	// Recomputes every widget's position/size for the current window
	// dimensions -- run once at construction and again on every live
	// resize (via NcviewWindow::on_resize), so the layout adapts instead
	// of clipping/overflowing at anything other than the original 900x760.
	void layout( int w, int h );
	// Builds the button bar's rows at button_bar_'s *current* position --
	// see its own comment for why the caller must set that (via
	// computeButtonBarRows() + button_bar_->resize()) before calling this.
	void rebuildButtonBar( int available_width );
	void rebuildDimRow( DimRow &row );
	void recenterDimRow( DimRow &row );
	void recenterVarPack();
	void rebuildColormapChoice();
	static void buttonCallback( Fl_Widget *w, void *data );
	static void rangeFrameCallback( Fl_Widget *w, void *data );
	static void varChoiceCallback( Fl_Widget *w, void *data );
	static void dimStepCallback( Fl_Widget *w, void *data );
	static void dimSliderCallback( Fl_Widget *w, void *data );
	static void colormapChoiceCallback( Fl_Widget *w, void *data );

	NcviewWindow     *win_ = nullptr;
	ImageView         *image_ = nullptr;
	Colorbar          *colorbar_ = nullptr;
	Fl_Menu_Bar       *menu_bar_ = nullptr;
	// Plain Fl_Group, not Fl_Pack -- rebuildButtonBar() positions each row
	// explicitly (centered horizontally), for the same reason dim_pack_ is
	// an Fl_Group rather than relying on Fl_Pack's own (lazy, draw()-time)
	// child layout: see dim_pack_'s comment below.
	Fl_Group          *button_bar_ = nullptr;
	// Plain Fl_Group, not Fl_Pack -- each row's position is computed
	// directly from its own index (see rebuildDimRow()/recenterDimRow()),
	// not from Fl_Pack's auto-stacking, which (per Fl_Pack::resize() in
	// FLTK's own source) only actually repositions children lazily inside
	// draw(), not immediately when the pack itself is resized. Reading a
	// row's "current" position between those two points (exactly what
	// MainWindow::layout() used to do right after resizing this) reads a
	// stale value, and this project isn't relying on FLTK-internal timing
	// to sort that out.
	Fl_Group          *dim_pack_ = nullptr;
	// Plain Fl_Group, not Fl_Pack -- its children (variable-bucket +
	// colormap dropdowns) are centered as a group (see recenterVarPack()),
	// which Fl_Pack's own left-to-right packing can't do.
	Fl_Group          *var_pack_ = nullptr;
	std::vector<Fl_Choice*> var_choices_;         // one per dimensionality bucket (1d, 2d, ...)
	Fl_Choice         *colormap_choice_ = nullptr; // last child of var_pack_; see rebuildColormapChoice()
	Fl_Box            *labels_[16] = {};          // indexed by LABEL_*
	Fl_Widget         *buttons_[32] = {};          // indexed by BUTTON_*
	// Phase 13c: the sensitivity core last asked for, per Button id, so it
	// survives rebuildButtonBar() -- which clear()s button_bar_ and builds
	// brand-new (and therefore active) Fl_Buttons on every relayout. Without
	// this, every set_buttons() state core applies is silently thrown away
	// the next time the window is laid out, BUTTONS_ALL_OFF included.
	// Stored inverted so that the zero-initialized default means
	// "sensitive", which is what a freshly built Fl_Button already is.
	bool               button_insensitive_[32] = {};
	// Parallel to buttons_[], for the actions moved into menu_bar_ instead
	// of staying toolbar buttons (see setSensitive(), which activates/
	// deactivates whichever of the two a given Button id actually has).
	// Index into menu_bar_->menu(), plus one so zero means "no menu item".
	// Not an Fl_Menu_Item*: every add() may reallocate the menu array,
	// leaving earlier pointers dangling.
	int                menu_item_index_[32] = {};
	// Purely decorative bordered boxes drawn behind the info rows above the
	// colormap/transform/interpolation row (Title; ScanvarName; ScanPlace;
	// DataExtrema+DataValue) -- one per row, each stretched to the window's
	// right edge in layout() the same way the labels inside it already are.
	Fl_Box            *info_row_boxes_[4] = {};

	std::vector<DimRow> dim_rows_;
	// Phase 12c: was set2DSize()'s own function-local `static size_t
	// last_w, last_h` -- the same leaked-comparison-state anti-pattern
	// Phase 11h fixed on the core side (ViewerController::draw()'s
	// last_x_size/last_y_size, moved onto ViewerSession::lastFrameSize()).
	// A real instance member here is no broader a change than that: this
	// class is already a process-lifetime singleton (see instance() in
	// interface_fltk.cc), so there's no reset semantics being introduced
	// or removed, just no longer hiding per-process state behind `static`
	// inside a method body where it looks instance-scoped but isn't.
	size_t last_2d_width_ = 0, last_2d_height_ = 0;
	// Phase 12c: was setOptionsDialog()'s own function-local `static
	// std::string custom_overlay_filename` -- same category as the two
	// above, moved here for the same reason.
	std::string custom_overlay_filename_;
	std::vector<NamedColormap> colormaps_;
	// One preview swatch image per colormaps_ entry, in the same order --
	// built once in createColormap() and reused across every
	// rebuildColormapChoice() call (colormaps_ itself never changes after
	// startup; only the var-bucket combos above it get rebuilt per file).
	// Owned for the life of this singleton, like the dim-row callback
	// closures below -- never explicitly freed.
	std::vector<Fl_RGB_Image*> colormap_previews_;
	// One per colormaps_ entry, built once alongside it in createColormap()
	// and reused across every rebuildColormapChoice() call -- same lifetime
	// as colormap_previews_ above.
	std::vector<std::unique_ptr<ColormapCbData>> colormap_cb_data_;
	int current_colormap_ = -1;
};

// The single MainWindow instance; created in in_initialize(), used by every
// free function in interface_fltk.cc.
MainWindow *instance();

} // namespace ncview_ui
