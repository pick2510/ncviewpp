/*
 * ui/src/main_window.cc
 *
 * Copyright (C) 2026 Dominik Strebel
 */

#include "ncview_ui/main_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/names.h>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Hor_Slider.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Multi_Label.H>

namespace ncview_ui {

namespace {
// Matches FrameRenderer::render()'s pixel encoding (core/src/frame_renderer.cc):
// valid data occupies indices [10, 10+n_colors), everything below is reserved
// (missing/out of range). We don't have n_colors here, so just clamp to the array.
inline void lookup( const unsigned char *r, const unsigned char *g, const unsigned char *b,
                     unsigned char idx, unsigned char *out )
{
	out[0] = r[idx];
	out[1] = g[idx];
	out[2] = b[idx];
}
} // namespace

namespace {
// Small horizontal-gradient swatch for a colormap combo entry -- one column
// per on-screen pixel, sampled across the full [0,255] table so the preview
// looks like a miniature colorbar rather than a solid block.
constexpr int kColormapPreviewW = 32, kColormapPreviewH = 14;

Fl_RGB_Image *buildColormapPreview( const unsigned char *r, const unsigned char *g, const unsigned char *b )
{
	// alloc_array=1 below hands ownership of this buffer to the Fl_RGB_Image,
	// which delete[]s it when the image itself is destroyed.
	auto *buf = new unsigned char[ kColormapPreviewW * kColormapPreviewH * 3 ];
	for( int x = 0; x < kColormapPreviewW; x++ ) {
		int idx = x * 255 / (kColormapPreviewW - 1);
		for( int y = 0; y < kColormapPreviewH; y++ ) {
			unsigned char *px = buf + (y*kColormapPreviewW + x) * 3;
			px[0] = r[idx]; px[1] = g[idx]; px[2] = b[idx];
		}
	}
	auto *img = new Fl_RGB_Image( buf, kColormapPreviewW, kColormapPreviewH, 3 );
	img->alloc_array = 1;
	return img;
}
} // namespace

/* ===================== ImageView ===================== */

ImageView::ImageView( int x, int y, int w, int h ) : Fl_Widget( x, y, w, h )
{
	std::memset( colormap_r_, 128, sizeof(colormap_r_) );
	std::memset( colormap_g_, 128, sizeof(colormap_g_) );
	std::memset( colormap_b_, 128, sizeof(colormap_b_) );
}

ImageView::~ImageView() = default;

void ImageView::setColormap( const unsigned char *r, const unsigned char *g, const unsigned char *b )
{
	std::memcpy( colormap_r_, r, 256 );
	std::memcpy( colormap_g_, g, 256 );
	std::memcpy( colormap_b_, b, 256 );
	if( !pixels_.empty() ) {
		// Re-expand with the new colormap so a colormap change is
		// visible without waiting for the next frame.
		setData( pixels_.data(), width_, height_ );
	}
	redraw();
}

void ImageView::setData( const unsigned char *data, size_t width, size_t height )
{
	// A size change means a different variable or scan-dim layout is now
	// showing; start that view fresh rather than carrying over a zoom/pan
	// that was framed for the old data.
	if( width != width_ || height != height_ ) {
		zoom_ = 1.0;
		panx_ = pany_ = 0.0;
	}
	width_ = width;
	height_ = height;
	pixels_.assign( data, data + width*height );
	rgb_buf_.resize( width*height*3 );
	for( size_t i = 0; i < width*height; i++ )
		lookup( colormap_r_, colormap_g_, colormap_b_, pixels_[i], &rgb_buf_[i*3] );
	redraw();
}

namespace {
struct ZoomDrawCtx {
	const unsigned char *rgb;
	size_t width, height;
	double zoom, ox, oy;             // ox,oy: window-relative origin of buffer (0,0)
	unsigned char bg_r, bg_g, bg_b;  // shown outside the image bounds
};
} // namespace

void ImageView::draw()
{
	if( rgb_buf_.empty() || width_ == 0 || height_ == 0 ) {
		fl_color( FL_DARK2 );
		fl_rectf( x(), y(), w(), h() );
		return;
	}

	ZoomDrawCtx ctx;
	ctx.rgb = rgb_buf_.data();
	ctx.width = width_;
	ctx.height = height_;
	ctx.zoom = zoom_;
	// fl_draw_image()'s per-scanline callback receives coordinates local to
	// the drawn rectangle (cx starts at 0, cy is the scanline index within
	// it) -- not window-absolute ones -- so the origin here must be
	// widget-local too (contrast screenToBuffer() below, which converts
	// Fl::event_x()/event_y(), themselves window-relative, and so does
	// need x()/y()).
	ctx.ox = (w() - width_*zoom_) / 2.0 + panx_;
	ctx.oy = (h() - height_*zoom_) / 2.0 + pany_;
	Fl::get_color( FL_DARK2, ctx.bg_r, ctx.bg_g, ctx.bg_b );

	// Draw via a per-scanline callback rather than fl_draw_image() on a
	// pre-scaled buffer: this lets the display zoom continuously (including
	// shrinking to fit and magnifying well past the data's native
	// resolution) via simple nearest-neighbor sampling, without ever
	// resampling/copying the underlying rgb_buf_ itself.
	fl_draw_image( []( void *data, int cx, int cy, int w_line, unsigned char *buf ) {
			auto *c = static_cast<ZoomDrawCtx*>( data );
			double by = ( cy - c->oy ) / c->zoom;
			bool row_in_range = by >= 0.0 && by < (double)c->height;
			size_t row = row_in_range ? (size_t)by : 0;
			for( int i = 0; i < w_line; i++ ) {
				double bx = ( (cx + i) - c->ox ) / c->zoom;
				if( row_in_range && bx >= 0.0 && bx < (double)c->width ) {
					size_t idx = ( row * c->width + (size_t)bx ) * 3;
					buf[i*3+0] = c->rgb[idx+0];
					buf[i*3+1] = c->rgb[idx+1];
					buf[i*3+2] = c->rgb[idx+2];
				} else {
					buf[i*3+0] = c->bg_r;
					buf[i*3+1] = c->bg_g;
					buf[i*3+2] = c->bg_b;
				}
			}
		}, &ctx, x(), y(), w(), h(), 3 );
}

void ImageView::screenToBuffer( int win_x, int win_y, int *bx, int *by ) const
{
	double ox = x() + (w() - width_*zoom_) / 2.0 + panx_;
	double oy = y() + (h() - height_*zoom_) / 2.0 + pany_;
	if( bx ) *bx = (int)std::floor( (win_x - ox) / zoom_ );
	if( by ) *by = (int)std::floor( (win_y - oy) / zoom_ );
}

void ImageView::zoomAt( int win_x, int win_y, double factor )
{
	if( width_ == 0 || height_ == 0 ) return;
	double new_zoom = zoom_ * factor;
	if( new_zoom < kMinZoom ) new_zoom = kMinZoom;
	if( new_zoom > kMaxZoom ) new_zoom = kMaxZoom;
	if( new_zoom == zoom_ ) return;

	// Keep the buffer point currently under the cursor fixed on screen
	// (the usual "zoom toward the pointer" behavior), rather than zooming
	// around the image center.
	int bx, by;
	screenToBuffer( win_x, win_y, &bx, &by );

	zoom_ = new_zoom;
	double base_ox = x() + (w() - width_*zoom_) / 2.0;
	double base_oy = y() + (h() - height_*zoom_) / 2.0;
	panx_ = win_x - base_ox - bx*zoom_;
	pany_ = win_y - base_oy - by*zoom_;

	redraw();
}

int ImageView::handle( int event )
{
	switch( event ) {
		// FL_ENTER must be accepted (return 1) here, not just FL_MOVE:
		// Fl_Group::handle()'s FL_ENTER/FL_MOVE case sends FL_ENTER (not
		// FL_MOVE) the moment the mouse first enters a child and only
		// latches Fl::belowmouse() onto that child if its handle() returns
		// non-zero for that FL_ENTER. Since this case list previously
		// didn't include FL_ENTER, it fell through to Fl_Widget::handle()
		// (returns 0), so Fl_Group never latched belowmouse onto this
		// widget -- every subsequent plain mouse move (no button held) re-
		// entered the same "first entry" branch and got resent as another
		// FL_ENTER instead of FL_MOVE, so the position/value readout never
		// updated on hover. It worked only during an active drag because
		// FL_PUSH/FL_DRAG/FL_RELEASE route via Fl::pushed(), a completely
		// separate mechanism from belowmouse-based FL_MOVE dispatch.
		case FL_ENTER:
		case FL_PUSH:
		case FL_DRAG:
		case FL_MOVE: {
			if( event == FL_PUSH ) {
				press_x_ = Fl::event_x();
				press_y_ = Fl::event_y();
				pan_start_x_ = panx_;
				pan_start_y_ = pany_;
				dragging_ = false;
			} else if( event == FL_DRAG && Fl::event_button1() ) {
				// Left-button drag pans the view -- replaces upstream's
				// discrete Blowup button with direct, continuous navigation.
				// Gated by a small movement threshold so it doesn't eat the
				// plain-click-to-plot / Ctrl-click-to-set-min/max gestures
				// handled below on FL_RELEASE.
				int dx = Fl::event_x() - press_x_;
				int dy = Fl::event_y() - press_y_;
				if( !dragging_ && ( std::abs( dx ) > 3 || std::abs( dy ) > 3 ) )
					dragging_ = true;
				if( dragging_ ) {
					panx_ = pan_start_x_ + dx;
					pany_ = pan_start_y_ + dy;
					redraw();
				}
			}
			unsigned int mask = 0;
			if( Fl::event_button1() ) mask |= 1;
			if( Fl::event_button2() ) mask |= 2;
			if( Fl::event_button3() ) mask |= 4;
			int bx, by;
			screenToBuffer( Fl::event_x(), Fl::event_y(), &bx, &by );
			g_app.controller.reportPosition( bx, by, mask );
			// Middle-button press/drag highlights the corresponding cell in
			// the data-edit window, if one is open (matches upstream's
			// Btn2Up/Btn2Motion -> do_set_dataedit_place() translation).
			if( (event == FL_PUSH || event == FL_DRAG) && Fl::event_button2() )
				view->setDataeditPlace();
			return 1;
		}
		case FL_MOUSEWHEEL: {
			// Scroll to zoom, centered on the cursor -- replaces upstream's
			// discrete Blowup/Bl.Type buttons with continuous zoom.
			double factor = std::pow( 1.1, -Fl::event_dy() );
			zoomAt( Fl::event_x(), Fl::event_y(), factor );
			return 1;
		}
		case FL_RELEASE:
			// Matches upstream's ccontour_widget translations: a plain
			// left-button release pops up an XY plot along the cursor's
			// position; the same with Ctrl held instead sets the current
			// min/max (Btn1 -> min, Btn3 -> max) from the value under the
			// cursor. Suppressed if this release ends a pan drag rather
			// than an actual click.
			if( Fl::event_button() == FL_LEFT_MOUSE ) {
				if( dragging_ ) dragging_ = false;
				else if( Fl::event_state( FL_CTRL ) ) g_app.controller.setMinFromCurdata();
				else g_app.controller.plotXY();
			} else if( Fl::event_button() == FL_RIGHT_MOUSE && Fl::event_state( FL_CTRL ) ) {
				g_app.controller.setMaxFromCurdata();
			}
			return 1;
		default:
			return Fl_Widget::handle( event );
	}
}

/* ===================== Colorbar ===================== */

Colorbar::Colorbar( int x, int y, int w, int h ) : Fl_Widget( x, y, w, h )
{
	std::memset( colormap_r_, 128, sizeof(colormap_r_) );
	std::memset( colormap_g_, 128, sizeof(colormap_g_) );
	std::memset( colormap_b_, 128, sizeof(colormap_b_) );
}

void Colorbar::setColormap( const unsigned char *r, const unsigned char *g, const unsigned char *b )
{
	std::memcpy( colormap_r_, r, 256 );
	std::memcpy( colormap_g_, g, 256 );
	std::memcpy( colormap_b_, b, 256 );
	redraw();
}

void Colorbar::setRange( float user_min, float user_max, Transform transform )
{
	user_min_ = user_min;
	user_max_ = user_max;
	transform_ = transform;
	redraw();
}

void Colorbar::draw()
{
	int n_colors = options.n_colors > 0 ? options.n_colors : 200;
	int width = w() > 0 ? w() : 1;
	for( int px = 0; px < w(); px++ ) {
		// Shares FrameRenderer::colorIndex() (core/src/frame_renderer.cc)
		// with the actual image draw, rather than hand-copying its
		// transform/invert/scale formula -- upstream's cbar.c does the
		// same thing in cbar_make(). Without this the colorbar shows a
		// plain linear gradient that no longer matches the image whenever
		// a transform or "Invert Colormap" is active.
		double normval = (double)px / (double)width;
		int idx = FrameRenderer::colorIndex<double>(
			normval, transform_, options.invert_colors,
			n_colors, options.n_extra_colors );
		if( idx < 0 ) idx = 0;
		if( idx > 255 ) idx = 255;
		fl_color( fl_rgb_color( colormap_r_[idx], colormap_g_[idx], colormap_b_[idx] ) );
		fl_line( x()+px, y(), x()+px, y()+h() );
	}
	if( user_max_ <= user_min_ ) return;

	// Upstream targets one label per ~48px (6 chars * 8px, cbar_make()'s
	// "typical_label_width") and picks a "nice" 1/2/5x10^n step to hit that
	// count, rather than just labeling the two endpoints.
	int nlev_target = w() / 48 - 2;
	if( nlev_target < 2 ) return;

	double start, step;
	int nlev;
	if( !FrameRenderer::niceTickLevels( user_min_, user_max_, nlev_target, &start, &nlev, &step ) )
		return;

	fl_color( FL_BLACK );
	fl_font( FL_HELVETICA, 10 );
	double drange = user_max_ - user_min_;
	for( int i = 0; i < nlev; i++ ) {
		double val = start + step*i;
		double xfrac = (val - user_min_) / drange;
		if( xfrac < 0.0 || xfrac > 1.0 ) continue;

		char buf[64];
		snprintf( buf, sizeof(buf), "%g", val );
		int ptx = x() + (int)(xfrac * w() + 0.5);
		int sw = (int)fl_width( buf );
		int ptx_text = ptx - sw/2;
		if( ptx_text < x() ) ptx_text = x();
		if( ptx_text + sw > x()+w() ) ptx_text = x()+w() - sw;

		fl_line( ptx, y()+h(), ptx, y()+h()+3 );
		fl_draw( buf, ptx_text, y()+h()+13 );
	}
}

/* ===================== MainWindow ===================== */

// Lazily constructed: upstream calls in_create_colormap() (via
// initialize_colormaps()) *before* in_initialize() -- registering
// colormaps has to work before the window exists. Fl_Window/Fl_Widget
// construction itself doesn't touch the display (that happens on show()),
// so building the widgets early is safe.
MainWindow *instance()
{
	static MainWindow *w = new MainWindow();
	return w;
}

MainWindow::MainWindow()
{
	const int W = 900, H = 760;
	win_ = new NcviewWindow( W, H, "ncview" );

	// Fl_Sys_Menu_Bar: on macOS this becomes the native system menu bar at
	// the top of the screen (a documented drop-in replacement for
	// Fl_Menu_Bar -- its own constructor detaches it from win_ once built,
	// see Fl_Sys_Menu_Bar.cxx); on every other platform it behaves exactly
	// like an ordinary in-window Fl_Menu_Bar.
	menu_bar_ = new Fl_Sys_Menu_Bar( 0, 0, W, kMenuBarH );
	// Every action below just forwards to the exact same in_button_pressed()
	// path a toolbar button's click already used (buttonCallback() is a
	// plain Fl_Callback -- Fl_Menu_Item::callback is the same typedef, so
	// it works unchanged as a menu item's callback too), grouped as: File
	// (whole-session actions), Edit (dialogs that change/configure state),
	// View (display toggles and passive info). These are the 11 buttons
	// upstream and earlier revisions of this port kept on the main toolbar
	// that are used far less often than the animation transport and Min/
	// Max, which stay as buttons below.
	auto add_menu_item = [this]( const char *path, Button id ) {
		int idx = menu_bar_->add( path, 0, &MainWindow::buttonCallback, (void*)(intptr_t)static_cast<int>(id) );
		menu_items_[static_cast<int>(id)] = const_cast<Fl_Menu_Item*>( menu_bar_->menu() + idx );
	};
	add_menu_item( "File/Print...",              Button::Print );
	add_menu_item( "File/Quit",                  Button::Quit );
	add_menu_item( "Edit/Edit Data...",          Button::Edit );
	add_menu_item( "Edit/Set Scan Dimensions...", Button::Dimset );
	add_menu_item( "Edit/Options...",            Button::Options );
	add_menu_item( "View/Info",                  Button::Info );
	add_menu_item( "View/Range...",              Button::Range );
	// Upstream's right-click on the Range button set the color range from
	// just the current frame (do_range()'s Modifier::M3 path,
	// view_set_range_frame()) instead of the full dialog -- a real,
	// distinct action, not just a faster way to reach the same dialog.
	// There's no right-click gesture on a menu item, so it gets its own
	// entry instead of trying to reproduce the mouse binding.
	menu_bar_->add( "View/Range (Current Frame)", 0, &MainWindow::rangeFrameCallback );
	add_menu_item( "View/Transform",             Button::Transform );
	add_menu_item( "View/Interp",                Button::BlowupType );
	add_menu_item( "View/Invert Physical",       Button::InvertPhysical );
	add_menu_item( "View/Invert Colormap",       Button::InvertColormap );

	// Info rows above the image. Packing unrelated fields side-by-side in
	// fixed-width columns (the previous layout, and upstream's before it)
	// looks fine only when every field happens to be near its column's
	// worst-case width -- with typical short content (a two-letter variable
	// name, "Linear", an empty optional field) it instead reads as a
	// scattered mix of isolated words with big dead gaps around them. So
	// each independent, potentially-long field (name, frame/date, range,
	// position, per-variable extra info, scalar coords, extra time info)
	// gets its own full-width row and stacks vertically -- consistent left
	// margin, no visual "randomness" regardless of how much of any one row
	// is actually filled in. The one exception is the colormap/transform/
	// interpolation trio: always present, always short (a colormap name, one
	// of 4 transform words, "Repl"/"Bi-lin"), so they're packed tightly on
	// one row instead of each claiming a full line for a couple of words.
	// FL_ALIGN_CLIP is a belt-and-suspenders backstop: if some future
	// content still ends up wider than its column, it gets clipped instead
	// of silently drawing over whatever comes after it.
	// Each row above the colormap/transform/interpolation trio gets its own
	// bordered background box too -- same FL_ENGRAVED_BOX treatment as that
	// trio's box below, for a consistent look, and each created (so drawn)
	// before the label(s) that sit on top of it. These label rows sit
	// directly adjacent to each other (each one's y is exactly the previous
	// one's y+h, no gap), so unlike a normal "pad outward a bit" border
	// inset, insetting *inward* by kInfoRowInset on each side is what turns
	// that zero-gap label stacking into a visible gap between boxes (2x the
	// inset) and comfortable top/bottom padding around each box's own text
	// -- padding outward here would make each box taller than its row
	// spacing, overlapping the next row's box (this is exactly what the
	// first version of this got wrong). Width is fixed here and stretched
	// to the window's right edge in layout(), same as the labels themselves.
	// kInfoRowInset controls the gap between adjacent boxes (2x the inset);
	// kInfoRowPad is the extra padding, beyond that inset, that separates
	// each box's border from its own label text -- a plain label's default
	// ~14px font otherwise leaves almost no visible margin inside a box
	// only a couple of pixels taller than the text itself.
	constexpr int kInfoRowInset = 2;
	constexpr int kInfoRowPad = 4;
	constexpr int kInfoRowH = 20 + 2*kInfoRowPad;
	constexpr int kInfoTitleRowH = 22 + 2*kInfoRowPad;
	// Each label box shares its *exact* y and height with the engraved box
	// behind it (only x/width differ, to inset the text horizontally from
	// the border) so that FLTK's own default vertical centering -- which
	// centers within whatever rect a label owns, no flag needed -- centers
	// the text in the same rect the border is drawn around. Any attempt to
	// out-guess that centering with a manual offset (a prior version of
	// this code did) drifts out of sync with FLTK's actual metrics across
	// platforms/fonts; matching the rects exactly needs no such guess.
	const int kTitleRowY = kMenuBarH+5+kInfoRowInset, kTitleRowH = kInfoTitleRowH-2*kInfoRowInset;
	info_row_boxes_[0] = new Fl_Box( 6, kTitleRowY, W-12, kTitleRowH );
	labels_[static_cast<int>(Label::Title)]        = new Fl_Box( 10, kTitleRowY, W-20, kTitleRowH );
	const int kRow1Y = kMenuBarH+5+kInfoTitleRowH+kInfoRowInset, kRowH = kInfoRowH-2*kInfoRowInset;
	info_row_boxes_[1] = new Fl_Box( 6, kRow1Y, W-12, kRowH );
	labels_[static_cast<int>(Label::ScanvarName)] = new Fl_Box( 10, kRow1Y, W-20, kRowH );
	const int kRow2Y = kMenuBarH+5+kInfoTitleRowH+kInfoRowH+kInfoRowInset;
	info_row_boxes_[2] = new Fl_Box( 6, kRow2Y, W-12, kRowH );
	labels_[static_cast<int>(Label::ScanPlace)]   = new Fl_Box( 10, kRow2Y, W-20, kRowH );
	const int kRow3Y = kMenuBarH+5+kInfoTitleRowH+2*kInfoRowH+kInfoRowInset;
	info_row_boxes_[3] = new Fl_Box( 6, kRow3Y, W-12, kRowH );
	labels_[static_cast<int>(Label::DataExtrema)] = new Fl_Box( 10, kRow3Y, 300, kRowH );
	// Wide enough to actually show the full "Current: (i=.., j=..) val
	// (x=.., y=..)" string view_report_position() builds -- the old fixed
	// 200px width clipped it right after "(x=", silently hiding the x/y
	// coordinate values even though they were always part of the label
	// text and updating on every mouse move. Resized to track the window
	// edge in layout() below, same as Label::Title.
	labels_[static_cast<int>(Label::DataValue)]   = new Fl_Box( 320, kRow3Y, W-330, kRowH );
	for( auto *b : info_row_boxes_ ) b->box( FL_ENGRAVED_BOX );
	// A bordered box behind the trio below ties them together visually as
	// one "display settings" unit, distinct from the free-form info lines
	// around it -- created (and so drawn) before the labels it sits behind.
	// Same inward inset as the row boxes above, for the same reason (this
	// row starts right where Label::DataExtrema/DataValue's row ends).
	const int kDisplaySettingsY = kMenuBarH+5+kInfoTitleRowH+3*kInfoRowH;
	const int kSettingsRow0Y = kDisplaySettingsY+kInfoRowInset;
	auto *display_settings_box = new Fl_Box( 6, kSettingsRow0Y, 280, kRowH );
	display_settings_box->box( FL_ENGRAVED_BOX );
	// BlowupType ("Repl"/"Bi-lin") goes first/leftmost: unlike ColormapName
	// and Transform, it's essentially always meaningful, so it anchors to
	// the box's actual left edge instead of sitting stranded in the middle
	// whenever the other two happen to be blank.
	labels_[static_cast<int>(Label::BlowupType)]  = new Fl_Box( 10, kSettingsRow0Y, 70, kRowH );
	// No Label::Blowup ("M X<n>") box -- it showed core's discrete
	// pre-zoom pixel-buffer scale factor, which upstream's now-removed
	// Button::Blowup let you cycle. With that gone (replaced by ImageView's
	// continuous scroll/drag zoom, which this label never reflected anyway)
	// it was a static, unexplained number nobody could act on.
	labels_[static_cast<int>(Label::Transform)]    = new Fl_Box( 85, kSettingsRow0Y, 70, kRowH );
	labels_[static_cast<int>(Label::ColormapName)]= new Fl_Box( 160, kSettingsRow0Y, 120, kRowH );
	// These three rows have no engraved box of their own (only the trio
	// above does), so they keep the plain full kInfoRowH row height --
	// there's no border rect to match here, just even line spacing.
	labels_[static_cast<int>(Label::CcInfo1)]     = new Fl_Box( 10, kDisplaySettingsY+kInfoRowH, W-20, kInfoRowH );
	labels_[static_cast<int>(Label::Skip)]         = new Fl_Box( 10, kDisplaySettingsY+2*kInfoRowH, 150, kInfoRowH );
	labels_[static_cast<int>(Label::ScalarDims)]  = new Fl_Box( 170, kDisplaySettingsY+2*kInfoRowH, W-180, kInfoRowH );
	labels_[static_cast<int>(Label::CcInfo2)]     = new Fl_Box( 10, kDisplaySettingsY+3*kInfoRowH, W-20, kInfoRowH );
	for( auto *b : labels_ ) if( b ) { b->box( FL_NO_BOX ); b->align( FL_ALIGN_INSIDE | FL_ALIGN_LEFT | FL_ALIGN_CLIP ); }

	// Horizontal row now, centered (recenterVarPack()) and positioned below
	// dim_pack_ in layout() -- was a vertical stack of dropdowns in its own
	// left-hand column, which meant image_/colorbar_/dim_pack_ never got to
	// use the window's full width.
	var_pack_ = new Fl_Group( 10, 100, W-20, 30 );
	var_pack_->end();

	image_ = new ImageView( 10, 100, W-20, H-320 );
	colorbar_ = new Colorbar( 10, H-210, W-20, 20 );

	dim_pack_ = new Fl_Group( 10, H-180, W-20, 100 );
	dim_pack_->end();

	button_bar_ = new Fl_Group( 10, H-70, W-20, 60 );
	button_bar_->end();

	win_->end();
	// No win_->resizable(...): layout() (below) repositions/resizes every
	// managed widget itself on every resize (via NcviewWindow::on_resize),
	// so FLTK's own generic proportional child-resize would just be
	// redundant work immediately overwritten by layout() -- leaving no
	// resizable() child makes Fl_Group::resize() a no-op for our children.
	win_->on_resize = [this]( int w, int h ) { layout( w, h ); };
	win_->size_range( 700, 500 );
	layout( W, H );
}

// Explicit per-button pixel widths (rather than one fixed size for all)
// since a uniform 60px was too narrow for "Restart"/etc, leaving their
// labels crowding the button edges.
//
// Just the animation transport -- the controls actually clicked often
// enough, mid-session, to earn a permanently visible, single-click
// button. Everything else (Inv.Phys, Inv.Cmap, Transform,
// Interp, DimSet, Range, Edit, Info, Print, Options, Quit) moved into
// menu_bar_ instead (see the constructor) -- occasional actions and
// settings that are fine behind one extra click, freeing this bar to fit
// on a single row instead of wrapping.
//
// No Button::ColormapSelect entry here -- replaced by the colormap combobox
// in var_pack_ (see rebuildColormapChoice()), which shows every colormap's
// name and a preview swatch instead of cycling through them blind one at a
// time. Button::ColormapSelect/do_colormap_sel() still exist for the
// NCVIEW_TEST_BUTTON headless-test hook and any script driving buttons by
// id directly; they just have no on-screen button anymore.
// No Button::Minimum/Maximum entries -- do_set_minimum()/do_set_maximum()
// (core/src/do_buttons.cc) are empty stub bodies, faithfully preserved from
// upstream, which itself never actually wired a widget to them either (its
// x_interface.c never creates BUTTON_MINIMUM/BUTTON_MAXIMUM widgets, only
// this port's toolbar ever did) -- both buttons were pure decoration that
// happened to call into dead code. The real min/max-setting UI, in both
// upstream and here, is Ctrl+click on the image
// (ImageView::handle() -> set_min_from_curdata()/set_max_from_curdata()).
struct ButtonSpec { Button id; const char *text; int width; };
static const ButtonSpec kButtonSpecs[] = {
	{ Button::Rewind, "@|<", 40 }, { Button::Backwards, "@<", 40 }, { Button::Pause, "@||", 40 },
	{ Button::Forward, "@>", 40 }, { Button::Fastforward, "@>|", 40 }, { Button::Restart, "Restart", 65 },
	// No Button::Blowup here -- replaced by ImageView's scroll-to-zoom (mouse
	// wheel) and drag-to-pan (left-button drag), which give continuous
	// navigation instead of upstream's discrete button.
};

namespace {
// Dim-row geometry -- also needed by MainWindow::layout() (to size
// dim_pack_ to the actual current row count, not a fixed placeholder
// height), so these live here rather than down by rebuildDimRow()/
// recenterDimRow(), which also use them.
constexpr int kDimRowNameW = 120, kDimRowBtnW = 24, kDimRowSliderW = 220,
              kDimRowSpacing = 4, kDimRowH = 24;
constexpr int kDimRowContentW = kDimRowNameW + kDimRowSpacing + kDimRowBtnW
                              + kDimRowSpacing + kDimRowSliderW + kDimRowSpacing + kDimRowBtnW;

constexpr int kButtonBarH = 26, kButtonBarSpacing = 2;
// "Delay:" label + slider (options.frame_delay) is one atomic item at the
// end of the button list, alongside the plain buttons -- see
// MainWindow::rebuildButtonBar()'s comment on it.
constexpr int kDelayLabelW = 40, kDelaySliderW = 90;
constexpr int kDelayItemW = kDelayLabelW + kButtonBarSpacing + kDelaySliderW;
constexpr int kButtonBarNButtons = (int)(sizeof(kButtonSpecs)/sizeof(kButtonSpecs[0]));
constexpr int kButtonBarNItems = kButtonBarNButtons + 1;

int buttonBarItemWidth( int i ) { return i < kButtonBarNButtons ? kButtonSpecs[i].width : kDelayItemW; }

// Decides which items land in which row for a given available_width, and
// each row's total content width -- pure/no side effects, so
// MainWindow::layout() can call this to learn the button bar's total
// height *before* MainWindow::rebuildButtonBar() actually builds anything,
// which is what lets that final resize (and so the bar's real, final x/y)
// happen before rebuildButtonBar() needs to read it back, rather than
// after.
void computeButtonBarRows( int available_width, std::vector<int> &row_start_item, std::vector<int> &row_content_w )
{
	row_start_item.clear();
	row_content_w.clear();
	int cur_w = 0;
	for( int i = 0; i < kButtonBarNItems; i++ ) {
		int w = buttonBarItemWidth( i );
		int with_this = cur_w + ( cur_w > 0 ? kButtonBarSpacing : 0 ) + w;
		if( row_start_item.empty() || with_this > available_width ) {
			row_start_item.push_back( i );
			row_content_w.push_back( 0 );
			cur_w = 0;
		}
		cur_w += ( cur_w > 0 ? kButtonBarSpacing : 0 ) + w;
		row_content_w.back() = cur_w;
	}
}
} // namespace

void MainWindow::layout( int w, int h )
{
	const int kSideMargin = 10;
	// menu_bar_'s own height, plus 8 info rows (kInfoTitleRowH + 7 *
	// kInfoRowH, starting 5px in) -- see the row layout comment in the
	// constructor -- plus a small gap before image_/dim_pack_ start.
	const int kTopY = kMenuBarH + 232;
	const int kColorbarH = 20;
	const int kColorbarGap = 10;
	// Colorbar::draw() puts its tick labels ~13px below the swatch (plus
	// font ascent/descent), so the gap before dim_pack_ needs to clear that
	// text height -- 10px wasn't enough and let tick labels bleed into (and
	// render underneath) the dimension row widgets below.
	const int kColorbarLabelGap = 20;
	const int kDimGap = 10;
	const int kVarPackH = 30;  // one horizontal row of dropdowns; see the constructor
	const int kBottomMargin = 10;
	// image_/colorbar_ are sized using this *nominal* dim_pack_ height, not
	// the actual current row count -- so the plot area stays visually
	// stable across variable switches instead of growing/shrinking every
	// time the scannable-dimension count changes. The real row count
	// instead controls where dim_pack_/var_pack_/button_bar_ sit (below),
	// which is what actually makes that "lower block" move down/up as it
	// grows/shrinks, per its own comment.
	const int kNominalDimPackH = 100;

	menu_bar_->resize( 0, 0, w, kMenuBarH );

	// image_/colorbar_/dim_pack_/var_pack_/button_bar_ all now share this
	// same x/width -- there's no separate left-hand column any more (that
	// was var_pack_'s, before it moved to its own row below dim_pack_), so
	// nothing needs to be centered/aligned against a *narrower* "plot area"
	// than the window's own usable width.
	int content_w = w - 2*kSideMargin;

	// button_bar_'s height depends on how many rows the current width
	// wraps it into -- doesn't depend on dim row count, so this can be
	// computed independently of the stacking below. computeButtonBarRows()
	// (a pure row-break calculation, no widgets) gets the row count for
	// that up front, so button_bar_ can be moved to its real final
	// position *before* rebuildButtonBar() -- which needs to read that
	// position back to place each row -- runs.
	std::vector<int> button_bar_row_start, button_bar_row_w;
	computeButtonBarRows( content_w, button_bar_row_start, button_bar_row_w );
	int button_bar_n_rows = (int)button_bar_row_start.size();
	int button_bar_h = button_bar_n_rows*kButtonBarH
	                  + ( button_bar_n_rows > 0 ? (button_bar_n_rows-1)*kButtonBarSpacing : 0 );

	// Nominal (dim-row-count-independent) sizing for image_/colorbar_ only.
	int nominal_button_bar_y = h - kBottomMargin - button_bar_h;
	int nominal_var_pack_y = nominal_button_bar_y - kDimGap - kVarPackH;
	int nominal_dim_pack_y = nominal_var_pack_y - kDimGap - kNominalDimPackH;
	int colorbar_y = nominal_dim_pack_y - kColorbarLabelGap - kColorbarH;
	int image_h = colorbar_y - kColorbarGap - kTopY;
	if( image_h < 40 ) image_h = 40;  // keep something sane at extreme window sizes

	image_->resize( kSideMargin, kTopY, content_w, image_h );
	colorbar_->resize( kSideMargin, colorbar_y, content_w, kColorbarH );

	// Real stacking: dim_pack_/var_pack_/button_bar_ start right where the
	// colorbar actually ends and stack top-down using dim_pack_'s *actual*
	// current row count (0 before any variable is selected) -- so
	// var_pack_ always starts right after the last real dim row, with no
	// dead space between them, and switching to a variable with more
	// scannable dimensions grows dim_pack_ downward and pushes
	// var_pack_/button_bar_ down with it, rather than the image shrinking
	// to compensate. A variable with enough extra dims can therefore push
	// button_bar_ below the window's original bottom edge -- accepted
	// trade-off for a stable plot area; resize the window taller if needed.
	int dim_pack_n_rows = (int)dim_rows_.size();
	int dim_pack_h = dim_pack_n_rows > 0 ? dim_pack_n_rows*kDimRowH : kDimRowH;
	int dim_pack_y = colorbar_y + kColorbarH + kColorbarLabelGap;
	int var_pack_y = dim_pack_y + dim_pack_h + kDimGap;
	int button_bar_y = var_pack_y + kVarPackH + kDimGap;

	dim_pack_->resize( kSideMargin, dim_pack_y, content_w, dim_pack_h );
	var_pack_->resize( kSideMargin, var_pack_y, content_w, kVarPackH );
	recenterVarPack();
	button_bar_->resize( kSideMargin, button_bar_y, content_w, button_bar_h );
	rebuildButtonBar( content_w );

	// dim_pack_'s x/y/w just changed; recompute every row's own position
	// from that (recenterDimRow() reads dim_pack_ directly, not any row's
	// own possibly-stale position) and re-center its children within it.
	for( auto &row : dim_rows_ ) recenterDimRow( row );

	// These labels' text is unbounded in length (a variable name, a frame's
	// date string with bounds, a mouse-position readout, "extra info"), so
	// rather than pick a fixed width that's either too cramped on a small
	// window or wastes space on a wide one, stretch each to the window's
	// right edge on every resize -- same as Title above.
	auto stretch_to_edge = [&]( Label id ) {
		Fl_Box *b = labels_[static_cast<int>(id)];
		if( b == nullptr ) return;
		b->size( w - kSideMargin - b->x(), b->h() );
	};
	if( labels_[static_cast<int>(Label::Title)] ) labels_[static_cast<int>(Label::Title)]->size( w - 2*kSideMargin, labels_[static_cast<int>(Label::Title)]->h() );
	stretch_to_edge( Label::ScanvarName );
	stretch_to_edge( Label::ScanPlace );
	stretch_to_edge( Label::DataValue );
	stretch_to_edge( Label::CcInfo1 );
	stretch_to_edge( Label::ScalarDims );
	stretch_to_edge( Label::CcInfo2 );

	// The decorative row boxes behind Title/ScanvarName/ScanPlace/
	// DataExtrema+DataValue track the window's right edge the same way
	// (their x=6 inset, vs. the labels' x=10, is matched on the right too).
	for( auto *b : info_row_boxes_ )
		if( b ) b->size( w - 6 - b->x(), b->h() );

	win_->redraw();
}

// Rebuilds the button bar as however many rows of buttons fit in
// available_width -- replaces the old single Fl_Pack::HORIZONTAL row, which
// simply ran off the right edge of the window once the buttons' total width
// exceeded it (which it always did, even at the original fixed 900px window
// width). button_bar_ is a plain Fl_Group; each row is centered within
// available_width and positioned at button_bar_'s *current* x/y -- which
// MainWindow::layout() must therefore set (via computeButtonBarRows() to
// learn the height this will need, then button_bar_->resize()) before
// calling this, not after, or rows end up positioned relative to wherever
// button_bar_ was left by the previous layout() call instead of where it
// actually is now.
void MainWindow::rebuildButtonBar( int available_width )
{
	button_bar_->clear();
	if( available_width < 60 ) available_width = 60;

	std::vector<int> row_start_item, row_content_w;
	computeButtonBarRows( available_width, row_start_item, row_content_w );

	for( size_t r = 0; r < row_start_item.size(); r++ ) {
		int start = row_start_item[r];
		int end = ( r+1 < row_start_item.size() ) ? row_start_item[r+1] : kButtonBarNItems;
		int content_w = row_content_w[r];
		int row_x = ( available_width - content_w ) / 2;
		if( row_x < 0 ) row_x = 0;
		row_x += button_bar_->x();
		int row_y = button_bar_->y() + (int)r * ( kButtonBarH + kButtonBarSpacing );

		auto *row = new Fl_Pack( row_x, row_y, content_w, kButtonBarH );
		row->type( Fl_Pack::HORIZONTAL );
		row->spacing( kButtonBarSpacing );
		// Every child below is added explicitly via row->add(), not FLTK's
		// construct-time auto-parenting, so this doesn't need to stay
		// "current" for that -- but leaving it current (Fl_Group's
		// constructor always calls current(this), and nothing else ever
		// closes it) meant Fl_Group::current() stayed pointed at the
		// last-built row long after rebuildButtonBar() returned. Any later
		// top-level Fl_Window built anywhere in the app (e.g. the "Plot
		// Along Dimension" popup, plot_window.cc's PlotWindow::create)
		// then got silently auto-parented as an X11 child of *this* row --
		// and transitively of the main window -- instead of becoming its
		// own top-level window, which is exactly the "plot window draws
		// inside the main window" bug.
		row->end();

		for( int i = start; i < end; i++ ) {
			if( i < kButtonBarNButtons ) {
				const auto &spec = kButtonSpecs[i];
				auto *btn = new Fl_Button( 0, 0, spec.width, kButtonBarH, spec.text );
				btn->callback( &MainWindow::buttonCallback, (void*)(intptr_t)static_cast<int>(spec.id) );
				row->add( btn );
				buttons_[static_cast<int>(spec.id)] = btn;
				// Phase 13c: a new Fl_Button is active; re-apply whatever
				// sensitivity core last asked for, or this rebuild
				// silently re-enables buttons that were deliberately
				// turned off (BUTTONS_2D_OFF for a variable with no 2-D
				// picture, BUTTONS_TIMEAXIS_OFF, BUTTONS_ALL_OFF after
				// invalidate_variable()).
				if( button_insensitive_[static_cast<int>(spec.id)] )
					btn->deactivate();
			} else {
				// "Delay:" label + a slider controlling options.frame_delay
				// (0.0 = fastest, 1.0 = slowest -- see do_buttons.cc's
				// DELAY_DELTA/DELAY_OFFSET, which turn this into the actual
				// timer interval for Rewind/Fastforward's held-down auto-
				// repeat). Upstream's x_interface.c placed this scrollbar
				// directly in the button box too (scrollspeed_widget)
				// rather than in a dialog, adjusted by feel while an
				// animation is actually playing -- so it stays here rather
				// than moving into menu_bar_ with the occasional-use
				// actions.
				auto *delay_label = new Fl_Box( 0, 0, kDelayLabelW, kButtonBarH, "Delay:" );
				row->add( delay_label );
				auto *delay_slider = new Fl_Hor_Slider( 0, 0, kDelaySliderW, kButtonBarH );
				delay_slider->bounds( 0.0, 1.0 );
				delay_slider->value( options.frame_delay );
				delay_slider->callback( []( Fl_Widget *w, void * ) {
					options.frame_delay = static_cast<Fl_Slider*>(w)->value();
				} );
				row->add( delay_slider );
			}
		}
		button_bar_->add( row );
	}
}

void MainWindow::buttonCallback( Fl_Widget *, void *data )
{
	Button id = static_cast<Button>( (int)(intptr_t)data );
	// Upstream's Ctrl+click on the transport buttons jumps by a percentage
	// of the file's frame count instead of one frame (do_rewind/
	// do_backwards/do_forward/do_fastforward's Modifier::M2 path) -- real,
	// physical toolbar buttons, so the gesture translates directly, unlike
	// the menu-item cases below.
	Modifier mod = Modifier::M1;
	switch( id ) {
	case Button::Rewind:
	case Button::Backwards:
	case Button::Forward:
	case Button::Fastforward:
		if( Fl::event_ctrl() ) mod = Modifier::M2;
		break;
	default:
		break;
	}
	in_button_pressed( id, mod );
}

void MainWindow::rangeFrameCallback( Fl_Widget *, void * )
{
	in_button_pressed( Button::Range, Modifier::M3 );
}

void MainWindow::varChoiceCallback( Fl_Widget *w, void * )
{
	// The variable name is stashed as each menu item's user_data (set in
	// populateVarList()), not read back from the item's label -- labels are
	// escaped ('/' and '&' are FLTK menu-path/shortcut metacharacters) so
	// they can't be used directly as the real NCVar name.
	auto *choice = static_cast<Fl_Choice*>( w );
	const Fl_Menu_Item *item = choice->mvalue();
	if( item == nullptr || item->user_data() == nullptr ) return;
	in_variable_selected( (const char *)item->user_data() );
}

namespace {
// FLTK's Fl_Menu_::add(const char*) treats '/' as a submenu path separator
// and '&' as a shortcut-underline marker; escape both so variable names
// containing them (netCDF4 group paths, say) still display as plain text
// in a single flat menu instead of being silently split into submenus.
std::string escapeMenuLabel( const char *name )
{
	std::string out;
	for( const char *p = name; *p; ++p ) {
		if( *p == '/' || *p == '&' ) out += '\\';
		out += *p;
	}
	return out;
}
// Fixed per-dropdown width for var_pack_'s children -- it's a horizontal
// row now (see the constructor), not a vertical stack filling its own
// column, so each Fl_Choice needs its own explicit width rather than
// var_pack_->w() (which would size every dropdown to the *entire* row).
constexpr int kVarChoiceW = 180;
} // namespace

void MainWindow::populateVarList()
{
	var_pack_->clear();
	var_choices_.clear();

	// Group variables by their number of non-degenerate dimensions, same
	// buckets upstream's x_sort_vars_by_ndims() uses for "menu" var-selection
	// style (1d, 2d, 3d, 4d, 5-or-more), alpha-sorted within each bucket.
	std::vector<NCVar*> buckets[5];
	for( auto &v : g_app.session.dataset().variablesMutable() ) {
		int d = v->effective_dimensionality;
		int idx = ( d >= 1 && d <= 4 ) ? d - 1 : 4;
		buckets[idx].push_back( v.get() );
	}
	for( auto &b : buckets )
		std::sort( b.begin(), b.end(),
			[]( const NCVar *a, const NCVar *c ) { return a->name < c->name; } );

	static const char *kBucketSuffix[5] = { "1d", "2d", "3d", "4d", "5d" };
	for( int i = 0; i < 5; i++ ) {
		if( buckets[i].empty() ) continue;

		auto *choice = new Fl_Choice( 0, 0, kVarChoiceW, 24 );
		char header[64];
		std::snprintf( header, sizeof(header), "(%zu) %s vars", buckets[i].size(), kBucketSuffix[i] );
		choice->add( header, 0, nullptr, nullptr, FL_MENU_INACTIVE );
		for( NCVar *v : buckets[i] ) {
			std::string label = escapeMenuLabel( v->name.c_str() );
			choice->add( label.c_str(), 0, &MainWindow::varChoiceCallback, (void *)v->name.c_str() );
		}
		choice->value( 0 );
		var_pack_->add( choice );
		var_choices_.push_back( choice );
	}
	rebuildColormapChoice();
	recenterVarPack();
	var_pack_->redraw();
}

// Replaces the old "Colormap" button-bar button (which just cycled through
// colormaps_ one at a time with no indication of what any of them looked
// like) with a combobox showing every colormap's name plus a small preview
// swatch, so the choice can be made directly instead of by cycling blind.
// Rebuilt alongside the variable-bucket combos above it (populateVarList()
// clears and rebuilds var_pack_ as a whole on every file load), which is
// what makes it move up/down with them: it's simply the last child of the
// same vertical pack, so it sits directly below however many dimensionality
// buckets (1d/2d/3d/4d/5d) the current file's variables happen to produce.
void MainWindow::rebuildColormapChoice()
{
	colormap_choice_ = new Fl_Choice( 0, 0, kVarChoiceW, 24 );
	for( size_t i = 0; i < colormaps_.size(); i++ ) {
		std::string label = escapeMenuLabel( colormaps_[i].name.c_str() );
		int idx = colormap_choice_->add( label.c_str(), 0, &MainWindow::colormapChoiceCallback,
			colormap_cb_data_[i].get() );
		if( i < colormap_previews_.size() ) {
			auto *item = const_cast<Fl_Menu_Item *>( &colormap_choice_->menu()[idx] );
			// Fl_Menu_Item has no native way to show an icon and text
			// together -- Fl_Image::label(Fl_Menu_Item*) replaces the
			// label with an image-only one, discarding the name text
			// entirely (confirmed: it silently left every item blank).
			// Fl_Multi_Label is FLTK's actual mechanism for pairing an
			// image with text on a menu item. Deliberately heap-allocated
			// and never freed -- this only runs once per file load
			// (populateVarList()), same bounded-leak tradeoff already used
			// for the dim-row callback closures below.
			auto *ml = new Fl_Multi_Label;
			ml->typea = FL_IMAGE_LABEL;
			ml->labela = (const char *)colormap_previews_[i];
			ml->typeb = FL_NORMAL_LABEL;
			ml->labelb = item->label();
			ml->label( item );
		}
	}
	if( current_colormap_ >= 0 && current_colormap_ < (int)colormaps_.size() )
		colormap_choice_->value( current_colormap_ );
	var_pack_->add( colormap_choice_ );
}

void MainWindow::colormapChoiceCallback( Fl_Widget *w, void * )
{
	auto *choice = static_cast<Fl_Choice*>( w );
	const Fl_Menu_Item *item = choice->mvalue();
	if( item == nullptr ) return;
	auto *cb_data = static_cast<ColormapCbData*>( item->user_data() );
	if( cb_data == nullptr || cb_data->window == nullptr ) return;
	if( cb_data->index >= cb_data->window->colormaps_.size() ) return;
	in_colormap_selected( cb_data->window->colormaps_[cb_data->index].name.c_str() );
}

void MainWindow::setLabel( Label label_id, const char *s )
{
	int idx = static_cast<int>( label_id );
	if( idx < 0 || idx >= (int)(sizeof(labels_)/sizeof(labels_[0])) ) return;
	if( labels_[idx] == nullptr ) return;
	labels_[idx]->copy_label( s );
	// copy_label() alone doesn't schedule a repaint -- without this, the
	// i/j/value label under the cursor (Label::DataValue, updated on every
	// FL_MOVE via view_report_position()) only appeared to change when some
	// unrelated event happened to trigger a redraw (a click, a resize),
	// making mouse-over tracking look frozen.
	labels_[idx]->redraw();

	// ScalarDims (CF scalar-coordinate values, e.g. "XTIME=60.0 minutes
	// since ...") shares its row with Skip, sitting to Skip's right at
	// x=170 -- but Skip is blank for most files (it only holds text for
	// non-scannable extra dimensions), which left ScalarDims looking
	// indented/centered instead of flush left. Snap it to the row's own
	// left margin whenever Skip has nothing to show, and back to its
	// normal offset the moment Skip does. Re-checked from either side
	// (whichever of the two just changed) since callers may update them
	// in either order.
	if( label_id == Label::ScalarDims || label_id == Label::Skip ) {
		auto *scalar_dims = labels_[static_cast<int>( Label::ScalarDims )];
		auto *skip = labels_[static_cast<int>( Label::Skip )];
		if( scalar_dims != nullptr ) {
			bool skip_empty = ( skip == nullptr || skip->label() == nullptr || skip->label()[0] == '\0' );
			int new_x = skip_empty ? 10 : 170;
			int new_w = window()->w() - new_x - 10;
			if( scalar_dims->x() != new_x || scalar_dims->w() != new_w ) {
				scalar_dims->resize( new_x, scalar_dims->y(), new_w, scalar_dims->h() );
				scalar_dims->redraw();
			}
		}
	}
}

void MainWindow::setSensitive( Button button_id, int state )
{
	// Button::ColormapSelect has no entry in buttons_[] any more (see
	// kButtonSpecs) -- core's set_buttons() still toggles it as part of
	// BUTTONS_ALL_ON/BUTTONS_ALL_OFF, so route it to the combobox instead.
	if( button_id == Button::ColormapSelect ) {
		if( colormap_choice_ ) { if( state ) colormap_choice_->activate(); else colormap_choice_->deactivate(); }
		return;
	}
	int idx = static_cast<int>( button_id );
	if( idx < 0 || idx >= (int)(sizeof(buttons_)/sizeof(buttons_[0])) ) return;
	// Remembered so rebuildButtonBar() can re-apply it: it clear()s
	// button_bar_ and constructs new, active Fl_Buttons on every relayout,
	// which until Phase 13c silently discarded whatever state core had
	// last asked for.
	button_insensitive_[idx] = ( state == 0 );
	// A given Button id has either a toolbar button (buttons_) or a menu
	// item (menu_items_), never both -- whichever one this id actually has
	// gets (de)activated, the other slot is just null.
	if( buttons_[idx] != nullptr ) {
		if( state ) buttons_[idx]->activate();
		else buttons_[idx]->deactivate();
	}
	if( menu_items_[idx] != nullptr ) {
		if( state ) menu_items_[idx]->activate();
		else menu_items_[idx]->deactivate();
		// update() (a no-op on non-Mac builds -- see Fl_Menu_Bar::update())
		// is what actually pushes an Fl_Menu_Item flag change like this one
		// out to macOS's native system menu bar; redraw() alone only
		// affects an ordinary in-window Fl_Menu_Bar.
		menu_bar_->update();
		menu_bar_->redraw();
	}
}

void MainWindow::indicateActiveVar( const char *var_name )
{
	for( auto *choice : var_choices_ ) {
		const Fl_Menu_Item *items = choice->menu();
		for( int i = 0; items[i].text != nullptr; i++ ) {
			const char *nm = (const char *)items[i].user_data();
			if( nm && std::strcmp( nm, var_name ) == 0 ) {
				choice->value( i );
				choice->redraw();
				return;
			}
		}
	}
}

namespace {
// Fl_Slider::draw() confines its own label rendering to the knob's small
// rectangle (see the draw_label(xsl,ysl,wsl,hsl) call in FLTK's
// Fl_Slider.cxx) -- fine for a short numeric readout right next to the
// knob, but useless for a full string (a date, a coordinate) that needs
// the whole widget's width to be legible: at a low value the knob is only
// a few pixels wide, clipping the text down to a single character right
// at the widget's left edge. Rather than use label() at all (and get that
// stray knob-confined fragment drawn a second time, in the wrong place),
// this keeps its own display text and draws it centered across the full
// widget instead.
class DimValueSlider : public Fl_Slider {
public:
	DimValueSlider( int X, int Y, int W, int H ) : Fl_Slider( X, Y, W, H ) { type( FL_HOR_SLIDER ); }
	void setDisplayText( const char *s ) { display_text_ = s ? s : ""; redraw(); }
	void draw() override
	{
		Fl_Slider::draw();
		fl_push_clip( x(), y(), w(), h() );
		fl_color( active_r() ? labelcolor() : FL_INACTIVE_COLOR );
		fl_font( labelfont(), labelsize() );
		fl_draw( display_text_.c_str(), x(), y(), w(), h(), FL_ALIGN_CENTER );
		fl_pop_clip();
	}
private:
	std::string display_text_;
};
} // namespace

void MainWindow::rebuildDimRow( DimRow &row )
{
	// row.index (this row's stacking position within dim_pack_, set by the
	// caller before this runs) plus dim_pack_'s own x()/y() -- which, being
	// a plain Fl_Widget field, is always current the instant dim_pack_ is
	// resized, unlike a would-be sibling row's position under Fl_Pack's
	// draw()-time relayout -- is what actually places this row, both here
	// and in recenterDimRow() below (called again on every window resize).
	row.group = new Fl_Group( dim_pack_->x(), dim_pack_->y() + row.index*kDimRowH, dim_pack_->w(), kDimRowH );
	row.group->begin();
	row.name_box = new Fl_Box( 0, 0, kDimRowNameW, 22, "" );
	row.name_box->copy_label( row.name.c_str() );
	row.name_box->box( FL_FLAT_BOX );
	row.prev_btn = new Fl_Button( 0, 0, kDimRowBtnW, 22, "@<" );
	row.value_slider = new DimValueSlider( 0, 0, kDimRowSliderW, 22 );
	row.value_slider->box( FL_DOWN_BOX );
	// The slider's own numeric value is just an index into the dimension
	// (bounds/current position set once the dim's size is known, in
	// fillDimInfo() below); what the user actually reads is the formatted
	// value (a date, a coordinate, ...), drawn centered across the full
	// widget by DimValueSlider::draw() above.
	row.value_slider->step( 1 );
	// Fl_Widget's own default is FL_WHEN_RELEASE (see Fl_Widget.cxx) --
	// fine for a text input, wrong for a slider: it made the knob visibly
	// slide under the mouse (that part is handled inside Fl_Slider itself,
	// independent of the callback) while the value text next to it and the
	// displayed 2-D slice both sat frozen on the pre-drag value the whole
	// time, only jumping to the real one on mouse-up -- indistinguishable,
	// mid-drag, from the control not responding at all. FL_WHEN_CHANGED
	// re-reads and redraws on every step instead, same as actually holding
	// prev_btn/next_btn down would.
	row.value_slider->when( FL_WHEN_CHANGED );
	row.next_btn = new Fl_Button( 0, 0, kDimRowBtnW, 22, "@>" );
	row.group->end();
	row.group->resizable( nullptr );  // keep the fixed-size/centered layout on resize; see recenterDimRow()
	dim_pack_->add( row.group );
	recenterDimRow( row );

	// Callback data (dim name + modifier, or just the dim name for the
	// slider) must outlive the callback; owned by the row itself
	// (prev_cb_data/next_cb_data/slider_cb_data) and freed in
	// clearDimButtons() when the row is torn down.
	row.prev_cb_data = std::make_unique<DimStepCbData>( DimStepCbData{ row.name, Modifier::M3, &g_app.controller } );
	row.next_cb_data = std::make_unique<DimStepCbData>( DimStepCbData{ row.name, Modifier::M1, &g_app.controller } );
	row.slider_cb_data = std::make_unique<DimSliderCbData>( DimSliderCbData{ row.name, &g_app.controller } );
	row.prev_btn->callback( &MainWindow::dimStepCallback, row.prev_cb_data.get() );
	row.next_btn->callback( &MainWindow::dimStepCallback, row.next_cb_data.get() );
	row.value_slider->callback( &MainWindow::dimSliderCallback, row.slider_cb_data.get() );
}

// Recomputes this row's absolute position (from dim_pack_'s current x/y/w
// and the row's own index -- never by reading another widget's possibly
// stale position) and re-centers its four children within it. Called once
// at creation (rebuildDimRow() above) and again on every window resize
// (layout() below), since dim_pack_'s width -- and so the centering offset
// -- changes with it.
void MainWindow::recenterDimRow( DimRow &row )
{
	if( row.group == nullptr ) return;
	int row_w = dim_pack_->w();
	row.group->resize( dim_pack_->x(), dim_pack_->y() + row.index*kDimRowH, row_w, kDimRowH );
	int left = ( row_w - kDimRowContentW ) / 2;
	if( left < 0 ) left = 0;
	int x = row.group->x(), y = row.group->y();
	row.name_box->resize( x + left, y, kDimRowNameW, 22 );
	left += kDimRowNameW + kDimRowSpacing;
	row.prev_btn->resize( x + left, y, kDimRowBtnW, 22 );
	left += kDimRowBtnW + kDimRowSpacing;
	row.value_slider->resize( x + left, y, kDimRowSliderW, 22 );
	left += kDimRowSliderW + kDimRowSpacing;
	row.next_btn->resize( x + left, y, kDimRowBtnW, 22 );
}

// Centers var_pack_'s current children (whatever mix of variable-bucket
// dropdowns and the colormap combobox populateVarList()/rebuildColormapChoice()
// last built) within its own width, the same way recenterDimRow() centers a
// dim row's children -- Fl_Pack's left-to-right packing can't do this,
// hence var_pack_ being a plain Fl_Group. Called after (re)building those
// children and again on every window resize (layout()), since var_pack_'s
// width -- and so the centering offset -- changes with it.
void MainWindow::recenterVarPack()
{
	int n = var_pack_->children();
	if( n == 0 ) return;
	int content_w = -2;  // -2 to cancel the one extra "+2" the loop below adds before the first child
	for( int i = 0; i < n; i++ ) content_w += 2 + var_pack_->child(i)->w();
	int left = ( var_pack_->w() - content_w ) / 2;
	if( left < 0 ) left = 0;
	int x = var_pack_->x() + left;
	for( int i = 0; i < n; i++ ) {
		Fl_Widget *c = var_pack_->child(i);
		int y = var_pack_->y() + ( var_pack_->h() - c->h() ) / 2;
		c->resize( x, y, c->w(), c->h() );
		x += c->w() + 2;
	}
}

void MainWindow::dimStepCallback( Fl_Widget *, void *data )
{
	auto *p = static_cast<DimStepCbData*>(data);
	p->controller->changeCurDim( (char *)p->name.c_str(), p->modifier );
}

void MainWindow::dimSliderCallback( Fl_Widget *w, void *data )
{
	auto *p = static_cast<DimSliderCbData*>(data);
	auto *slider = static_cast<Fl_Slider*>(w);
	p->controller->setCurDimIndex( p->name.c_str(), lround( slider->value() ) );
}

void MainWindow::makeDimButtons( const Stringlist *dim_list )
{
	clearDimButtons();
	if( dim_list != nullptr )
	for( auto &e : *dim_list ) {
		DimRow row;
		row.name = e.string;
		row.index = (int)dim_rows_.size();
		rebuildDimRow( row );
		dim_rows_.push_back( std::move( row ) );
	}
	// A plain dim_pack_->redraw() isn't enough -- route through the same
	// full relayout a resize already triggers, which is what actually
	// re-centers every row (recenterDimRow(), called from layout() below)
	// for the current dim_pack_ width.
	layout( win_->w(), win_->h() );
}

void MainWindow::clearDimButtons()
{
	dim_pack_->clear();
	dim_rows_.clear();
}

void MainWindow::fillDimInfo( const NCDim *d, int /*please_flip*/ )
{
	if( d == nullptr ) return;
	for( auto &row : dim_rows_ ) {
		if( row.name == d->name ) {
			row.name_box->copy_label( !d->long_name.empty() ? d->long_name.c_str() : d->name.c_str() );
			row.name_box->redraw();
			// The dim's size is only known once (here, at variable
			// selection/scan-dim-set time), unlike its current place
			// (view_change_cur_dim()/view_set_cur_dim_index()), which
			// changes on every step -- so bounds are set here and the
			// slider's value is kept current in setCurDimValue() below,
			// called every time the place actually changes.
			size_t size = d->size > 0 ? d->size : 1;
			row.value_slider->bounds( 0, (double)(size-1) );
			row.value_slider->value( (double)g_app.session.curDimIndex( d->name.c_str() ) );
			break;
		}
	}
}

void MainWindow::setCurDimValue( const char *name, const char *value )
{
	// setDisplayText() below schedules its own redraw() -- see
	// DimValueSlider's comment for why it draws this itself instead of
	// going through label()/copy_label() the way other widgets here do.
	for( auto &row : dim_rows_ ) {
		if( row.name == name ) {
			static_cast<DimValueSlider*>( row.value_slider )->setDisplayText( value );
			row.value_slider->value( (double)g_app.session.curDimIndex( name ) );
			return;
		}
	}
}

void MainWindow::indicateActiveDim( Dimension /*dimension*/, const char *dim_name )
{
	for( auto &row : dim_rows_ )
		row.name_box->labelfont( row.name == dim_name ? FL_HELVETICA_BOLD : FL_HELVETICA );
	dim_pack_->redraw();
}

void MainWindow::draw2DField( const unsigned char *data, size_t width, size_t height, size_t /*timestep*/ )
{
	image_->setData( data, width, height );
}

void MainWindow::setImageVisible( bool visible )
{
	if( image_ == nullptr )
		return;
	if( visible == (image_->visible() != 0) )
		return;

	// hide() also stops FLTK routing events to the widget, which is the
	// half that matters: ImageView::handle() feeds clicks straight into
	// ViewerController::plotXY()/setMin/MaxFromCurdata() and middle-drag
	// into View::setDataeditPlace(), none of which a variable without a
	// 2-D field can answer. The window redraw is what actually clears the
	// stale picture from the screen -- a hidden child does not repaint the
	// area it used to occupy on its own.
	if( visible ) image_->show();
	else          image_->hide();
	if( win_ ) win_->redraw();
}

void MainWindow::createColormap( const char *name, const unsigned char *r, const unsigned char *g, const unsigned char *b )
{
	NamedColormap cm;
	cm.name = name;
	std::memcpy( cm.r, r, 256 );
	std::memcpy( cm.g, g, 256 );
	std::memcpy( cm.b, b, 256 );
	colormaps_.push_back( cm );
	colormap_previews_.push_back( buildColormapPreview( r, g, b ) );
	colormap_cb_data_.push_back( std::make_unique<ColormapCbData>(
		ColormapCbData{ colormaps_.size() - 1, this } ) );
	if( current_colormap_ < 0 ) {
		current_colormap_ = 0;
		image_->setColormap( r, g, b );
		colorbar_->setColormap( r, g, b );
	}
}

bool MainWindow::seenColormapName( const char *name ) const
{
	for( const auto &cm : colormaps_ )
		if( cm.name == name ) return true;
	return false;
}

void MainWindow::checkLegalColormapLoaded()
{
	if( current_colormap_ < 0 && !colormaps_.empty() ) {
		current_colormap_ = 0;
		image_->setColormap( colormaps_[0].r, colormaps_[0].g, colormaps_[0].b );
		colorbar_->setColormap( colormaps_[0].r, colormaps_[0].g, colormaps_[0].b );
	}
}

char *MainWindow::installNextColormap( int do_widgets )
{
	if( colormaps_.empty() ) return nullptr;
	current_colormap_ = (current_colormap_ + 1) % (int)colormaps_.size();
	auto &cm = colormaps_[current_colormap_];
	image_->setColormap( cm.r, cm.g, cm.b );
	colorbar_->setColormap( cm.r, cm.g, cm.b );
	if( do_widgets ) {
		setLabel( Label::ColormapName, cm.name.c_str() );
		if( colormap_choice_ ) { colormap_choice_->value( current_colormap_ ); colormap_choice_->redraw(); }
	}
	return (char *)cm.name.c_str();
}

char *MainWindow::installPrevColormap( int do_widgets )
{
	if( colormaps_.empty() ) return nullptr;
	current_colormap_ = (current_colormap_ - 1 + (int)colormaps_.size()) % (int)colormaps_.size();
	auto &cm = colormaps_[current_colormap_];
	image_->setColormap( cm.r, cm.g, cm.b );
	colorbar_->setColormap( cm.r, cm.g, cm.b );
	if( do_widgets ) {
		setLabel( Label::ColormapName, cm.name.c_str() );
		if( colormap_choice_ ) { colormap_choice_->value( current_colormap_ ); colormap_choice_->redraw(); }
	}
	return (char *)cm.name.c_str();
}

char *MainWindow::installColormapByName( const char *name, int do_widgets )
{
	for( size_t i = 0; i < colormaps_.size(); i++ ) {
		if( colormaps_[i].name != name ) continue;
		current_colormap_ = (int)i;
		auto &cm = colormaps_[i];
		image_->setColormap( cm.r, cm.g, cm.b );
		colorbar_->setColormap( cm.r, cm.g, cm.b );
		if( do_widgets ) {
			setLabel( Label::ColormapName, cm.name.c_str() );
			if( colormap_choice_ ) { colormap_choice_->value( current_colormap_ ); colormap_choice_->redraw(); }
		}
		return (char *)cm.name.c_str();
	}
	return nullptr;
}

void MainWindow::createColorbar( float user_min, float user_max, Transform transform )
{
	colorbar_->setRange( user_min, user_max, transform );
}

void MainWindow::drawColorbar()
{
	colorbar_->redraw();
}

int MainWindow::set2DSize( size_t width, size_t height )
{
	if( width == last_2d_width_ && height == last_2d_height_ ) return 0;
	int retval = (width > last_2d_width_) ? 1 : -1;
	last_2d_width_ = width;
	last_2d_height_ = height;
	return retval;
}

void MainWindow::setCursorBusy( bool busy )
{
	if( busy ) win_->cursor( FL_CURSOR_WAIT );
	else win_->cursor( FL_CURSOR_DEFAULT );
}

void MainWindow::pixelToRgb( ncv_pixel pix, int *r, int *g, int *b ) const
{
	// Upstream's x_interface.c implementation returned X11 XColor-style
	// 16-bit channel values (do_print.c's only caller right-shifts by 8 to
	// get back to 8 bits: "fprintf(outf, "%02x%02x%02x", (r>>8), (g>>8),
	// (b>>8))"). Our colormap tables are plain 8-bit, so scale up the same
	// way X11 itself does (value16 = value8*257, i.e. value8 replicated
	// into both bytes) rather than changing do_print.cc's contract.
	unsigned char r8, g8, b8;
	if( current_colormap_ < 0 || current_colormap_ >= (int)colormaps_.size() ) {
		r8 = g8 = b8 = (unsigned char)pix;
	} else {
		const auto &cm = colormaps_[current_colormap_];
		r8 = cm.r[pix]; g8 = cm.g[pix]; b8 = cm.b[pix];
	}
	if( r ) *r = r8 * 257;
	if( g ) *g = g8 * 257;
	if( b ) *b = b8 * 257;
}

void MainWindow::queryPointerPosition( int *x, int *y ) const
{
	image_->screenToBuffer( Fl::event_x(), Fl::event_y(), x, y );
}

} // namespace ncview_ui
