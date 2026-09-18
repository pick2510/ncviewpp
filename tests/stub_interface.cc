// Copyright (C) 2026 Dominik Strebel
//
// Headless implementation of the ncview/interface.h seam, as a
// RecordingViewerUi (ncview/viewer_ui.h). Proves ncview_core has no hidden
// UI dependency: if this file plus ncview_core links (see
// tests/CMakeLists.txt, which force-links the whole archive), the seam is
// clean. Every method here is a minimal, non-interactive stand-in -- never
// called from a real UI, only from tests.
//
// OOP_redesign plan, Step 9b: this used to be ~48 free functions directly
// satisfying ncview/interface.h's declarations; those declarations are now
// satisfied once, by core/src/viewer_ui_bridge.cc's forwarders onto
// whichever ViewerUi is currently installed (g_app.ui). This file's job
// is now to provide that ViewerUi for test binaries: g_recording_ui is a
// single global RecordingViewerUi instance, and g_app.ui is pointed at
// it via a namespace-scope initializer below, before any test's main()
// runs.
#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "ncview/viewer_ui.h"

// --- Call recording + scripted dialog responses -----------------------
// Added for the OOP_redesign migration (see the plan's Step 1): later
// steps move workflows currently spread across do_buttons.cc/view.cc into
// a ViewerController, and need a way to characterize "what did core ask
// the UI to do, and in what order" (variable selection, playback) plus
// "what happens when the user cancels a dialog" (range/print/dimset)
// *before* moving that logic, as a regression net. Every method below
// appends its own name (and, where it helps distinguish calls, one
// human-readable argument) to g_recorded_calls; the handful of
// dialog-shaped methods return a caller-settable response instead of
// always succeeding. resetStubRecording() clears all of this back to its
// default (every dialog answers as if nothing was cancelled) -- call it
// at the start of any test that inspects g_recorded_calls or overrides a
// response, since doctest runs every TEST_CASE in the same process.
std::vector<std::string> g_recorded_calls;
Message g_dialog_response = Message::OK;
Message g_range_response = Message::OK;
Message g_printer_options_response = Message::OK;
std::map<int,int> g_button_sensitivity;
int g_set_scan_dims_response = 0;
// Phase 13a: when false, in_set_scan_dims() leaves *new_dim_list exactly as
// the caller passed it, which is what MainWindow::scanDimsDialog() does on
// its two early-out paths (Cancel, and an empty dim_list). Lets a test drive
// a real cancel instead of the stub's default "accept unchanged" answer.
bool g_set_scan_dims_populate = true;

// Captured by in_print() below so Phase 4a's do_print.cc tests can assert
// on what build_print_info() actually produced, rather than only that
// in_print was called. PrintInfo::pixels is a borrowed pointer (see its
// own doc comment in defines.h) -- copying the struct copies the pointer
// value, not the data; only valid to read (never dereferenced by these
// tests) until the next do_print()/draw() call.
bool g_have_last_print_info = false;
PrintInfo g_last_print_info;
PrintOptions g_last_print_options;

// Captured/scripted by in_create_colormap()/x_seen_colormap_name() for
// Phase 8's test_colormaps.cc -- see the fuller comment at their use sites
// below.
struct CreatedColormap {
	std::string name;
	std::array<unsigned char, 256> r, g, b;
};
std::vector<CreatedColormap> g_created_colormaps;
std::vector<std::string> g_seen_colormap_names;

// Scripted printer_options() dialog answer: applied to *po (do_print.cc's
// file-static printopts, passed by pointer) before printer_options()
// returns, the same way a real dialog would apply the user's edits --
// lets a test flip one printopts.include_* flag without any accessor
// into do_print.cc's file-static state. Mirrors the std::function-based
// scripting the timer queue above already uses. Defaults to a no-op, so
// do_print() runs with print_init()'s plain defaults unless a test sets
// this.
std::function<void(PrintOptions &)> g_printer_options_override;

// Scripted in_query_pointer_position() answer for ViewerController::plotXY()
// tests (Phase 7a): upstream's real hook reads the actual mouse position
// from the windowing system, which the stub can't reproduce, so it
// defaults to (0,0) and a test that needs a specific window position (an
// in-bounds click, a click past an axis's edge to exercise plotXY()'s own
// clamp) sets this pair first. Mirrors g_printer_options_override's
// std::function-based scripting, just for a plain value instead of an
// edit callback.
int g_query_pointer_x = 0;
int g_query_pointer_y = 0;

// Captured by in_popup_XY_graph() for Phase 7a's plotXYSc() tests:
// plot_XY_xvals/plot_XY_yvals/plot_XY_dim[] are file-scope statics inside
// view.cc (internal linkage), so a test can't read them directly -- this
// is the only place their contents actually leave the function, exactly
// the way g_last_print_info captures do_print.cc's file-static printopts.
bool g_have_last_xy_plot = false;
size_t g_last_xy_n = 0;
int g_last_xy_dimindex = -1;
std::vector<double> g_last_xy_xvals;
std::vector<double> g_last_xy_yvals;
std::string g_last_xy_x_axis_title;
std::string g_last_xy_legend;

// --- Fake timer queue ---------------------------------------------------
// "Refine the architecture" plan, Phase 0b: in_timer_set() used to just
// record its own name and drop the callback, which is why playback
// (do_buttons.cc's rewind()/fastforward(), whose Modifier::M1 paths only
// ever advance by re-arming this same one-shot slot from inside their own
// callback -- see view.cc's comment on the shared timer slot) and the
// file-growth poll (view_check_new_data()) had zero test coverage: there
// was no way to actually fire the callback core handed over. Upstream's
// real interface.h contract is a genuine one-shot timer (the callback
// must re-arm itself via a fresh in_timer_set() call if it wants to run
// again, exactly like an X/FLTK timeout) -- fireTimer() reproduces that:
// it takes ownership of the pending callback and clears the "armed" flag
// *before* invoking it, so a callback that calls in_timer_set() again
// (as every real one does) correctly re-arms a fresh timer rather than
// stepping on the one being fired.
std::function<void()> g_pending_timer_callback;
unsigned long g_pending_timer_delay_ms = 0;
bool g_timer_armed = false;

void resetStubRecording()
{
	g_recorded_calls.clear();
	g_dialog_response = Message::OK;
	g_range_response = Message::OK;
	g_printer_options_response = Message::OK;
	g_set_scan_dims_response = 0;
	g_set_scan_dims_populate = true;
	g_button_sensitivity.clear();
	g_have_last_print_info = false;
	g_last_print_info = PrintInfo();
	g_last_print_options = PrintOptions();
	g_printer_options_override = nullptr;
	g_pending_timer_callback = nullptr;
	g_pending_timer_delay_ms = 0;
	g_timer_armed = false;
	g_query_pointer_x = 0;
	g_query_pointer_y = 0;
	g_have_last_xy_plot = false;
	g_last_xy_n = 0;
	g_last_xy_dimindex = -1;
	g_last_xy_xvals.clear();
	g_last_xy_yvals.clear();
	g_last_xy_x_axis_title.clear();
	g_last_xy_legend.clear();
	g_created_colormaps.clear();
	g_seen_colormap_names.clear();
}

bool timerIsArmed() { return g_timer_armed; }
unsigned long timerDelayMs() { return g_pending_timer_delay_ms; }

// Fires the pending timer, if one is armed; a no-op (returns false)
// otherwise -- callers that don't know whether a timer is pending (e.g.
// a test loop simulating several seconds passing) should check the
// return value rather than assuming REQUIRE(timerIsArmed()) beforehand.
bool fireTimer()
{
	if (!g_timer_armed) return false;
	std::function<void()> callback = std::move(g_pending_timer_callback);
	g_pending_timer_callback = nullptr;
	g_timer_armed = false;
	callback();
	return true;
}

// Captured for test_colormaps.cc (Phase 8): in_create_colormap() previously
// only recorded its own name into g_recorded_calls, discarding the actual
// name/r/g/b payload -- fine for tests that only care whether a colormap
// load happened, but ncview.cc's colormap machinery (initialize_colormaps(),
// init_cmap_from_file()) had zero coverage until this phase, and asserting
// only "in_create_colormap was called" can't tell a correct load from a
// silently-wrong one (e.g. an off-by-one in a .ncmap parser). Same category
// of extension as Phase 4a's printer_options()/in_print() capture.
// (Declared up near the top of the file, alongside the other captured
// globals, so resetStubRecording() below can clear them.)

// Captured for test_view_data_edit.cc: View::dataEdit() builds this vector
// and hands it to x_dataedit() by reference. Historical note: this used to
// be a raw char** with a "the UI frees it" contract that FltkViewerUi's
// real implementation never actually honored (a genuine leak, fixed in
// Phase 12b by making ownership structural via std::vector<std::string> --
// see docs/PORTING.md's Phase 12b entry). The stub just copies it here for the
// test to inspect; no manual free is needed any more.
std::vector<std::string> g_last_dataedit_lines;
int g_last_dataedit_nx = 0;

class RecordingViewerUi : public ViewerUi {
public:
	void in_display_stuff(const char*, const char*) override { g_recorded_calls.push_back("in_display_stuff"); }
	void in_set_edit_place(size_t, int, int, int, int) override { g_recorded_calls.push_back("in_set_edit_place"); }
	void in_indicate_active_var(const char *name) override { g_recorded_calls.push_back(std::string("in_indicate_active_var:") + (name ? name : "")); }
	void in_indicate_active_dim(Dimension d, const char *name) override {
		const char *which = d == Dimension::X ? "X" : d == Dimension::Y ? "Y" : d == Dimension::Scan ? "Scan" : "None";
		g_recorded_calls.push_back(std::string("in_indicate_active_dim:") + which + ":" + (name ? name : ""));
	}
	void in_parse_args(int*, char**) override { g_recorded_calls.push_back("in_parse_args"); }
	void in_initialize() override { g_recorded_calls.push_back("in_initialize"); }
	void in_set_label(Label, const char *s) override { g_recorded_calls.push_back(std::string("in_set_label:") + (s ? s : "")); }
	void in_process_user_input() override { g_recorded_calls.push_back("in_process_user_input"); }
	void in_draw_2d_field(const unsigned char*, size_t, size_t, size_t) override { g_recorded_calls.push_back("in_draw_2d_field"); }
	void in_create_colormap(const char *name, const ncv_pixel r[256], const ncv_pixel g[256], const ncv_pixel b[256]) override {
		g_recorded_calls.push_back("in_create_colormap");
		CreatedColormap cm;
		cm.name = name ? name : "";
		for (int i = 0; i < 256; i++) { cm.r[i] = r[i]; cm.g[i] = g[i]; cm.b[i] = b[i]; }
		g_created_colormaps.push_back(std::move(cm));
	}
	char *in_install_next_colormap(int) override { g_recorded_calls.push_back("in_install_next_colormap"); return nullptr; }
	int in_set_2d_size(size_t, size_t) override { g_recorded_calls.push_back("in_set_2d_size"); return 0; }
	// The button id/state pair is recorded as well as the call name (Phase
	// 13c): set_buttons() (core/src/view.cc) applies a whole named state at
	// once -- BUTTONS_ALL_ON / TIMEAXIS_OFF / 2D_OFF / ALL_OFF -- and which
	// buttons a state leaves enabled is the thing worth asserting on. The
	// bare call-name push stays for the existing ordering tests.
	void in_set_sensitive(Button b, int state) override {
		g_recorded_calls.push_back("in_set_sensitive");
		g_button_sensitivity[static_cast<int>(b)] = state;
	}
	Message in_dialog(const char*, int) override { g_recorded_calls.push_back("in_dialog"); return g_dialog_response; }
	void in_fill_dim_info(const NCDim*, int) override { g_recorded_calls.push_back("in_fill_dim_info"); }
	void in_set_cur_dim_value(const char *name, const char *val) override { g_recorded_calls.push_back(std::string("in_set_cur_dim_value:") + (name ? name : "") + ":" + (val ? val : "")); }
	void in_set_cursor_busy() override { g_recorded_calls.push_back("in_set_cursor_busy"); }
	void in_set_cursor_normal() override { g_recorded_calls.push_back("in_set_cursor_normal"); }
	// Echoes back the current X/Y axes ("Y-axis first, then X-axis", per
	// ViewerUi::in_set_scan_dims()'s contract) as an "accept unchanged"
	// answer.
	//
	// Phase 1 populated this unconditionally because setScanDims()'s
	// cancel check was dead (it compared against Message::Cancel, which
	// is 2, while this seam returns 0/1), so the caller *always* fell
	// through to dereferencing *new_dim_list -- and the conclusion drawn
	// at the time was "a real UI's dialog always populates the list on
	// every path that survives that point; this stub needs to as well or
	// every call crashes."
	//
	// That conclusion was wrong, and it is why the bug survived to Phase
	// 13a: MainWindow::scanDimsDialog() (ui/src/main_window_dialogs.cc)
	// returns 0 on Cancel WITHOUT writing *new_dim_list, so the real UI
	// segfaulted on every cancel while this stub quietly papered over it.
	// setScanDims() now honours the 0/1 status; set
	// g_set_scan_dims_populate = false to reproduce what the FLTK dialog
	// actually does on Cancel.
	int in_set_scan_dims(const Stringlist*, const char *cur_x_name, const char *cur_y_name, Stringlist **new_dim_list) override {
		g_recorded_calls.push_back("in_set_scan_dims");
		if (!g_set_scan_dims_populate) return g_set_scan_dims_response;
		if (new_dim_list) {
			*new_dim_list = nullptr;
			if (cur_y_name) stringlist_add_string(new_dim_list, cur_y_name);
			if (cur_x_name) stringlist_add_string(new_dim_list, cur_x_name);
		}
		return g_set_scan_dims_response;
	}
	void in_flush() override { g_recorded_calls.push_back("in_flush"); }
	int in_popup_XY_graph(size_t n, int dimindex, double *xvals, double *yvals, const char *x_axis_title, const char*, const char*, const char *legend, const Stringlist*) override {
		g_recorded_calls.push_back("in_popup_XY_graph");
		g_have_last_xy_plot = true;
		g_last_xy_n = n;
		g_last_xy_dimindex = dimindex;
		g_last_xy_xvals.assign(xvals, xvals + n);
		g_last_xy_yvals.assign(yvals, yvals + n);
		g_last_xy_x_axis_title = x_axis_title ? x_axis_title : "";
		g_last_xy_legend = legend ? legend : "";
		return 0;
	}
	void in_query_pointer_position(int *x, int *y) override { g_recorded_calls.push_back("in_query_pointer_position"); if (x) *x = g_query_pointer_x; if (y) *y = g_query_pointer_y; }
	void in_popup_2d_window() override { g_recorded_calls.push_back("in_popup_2d_window"); }
	void in_popdown_2d_window() override { g_recorded_calls.push_back("in_popdown_2d_window"); }
	void in_timer_clear() override {
		g_recorded_calls.push_back("in_timer_clear");
		g_pending_timer_callback = nullptr;
		g_timer_armed = false;
	}
	int in_report_auto_overlay() override { g_recorded_calls.push_back("in_report_auto_overlay"); return 0; }
	void in_timer_set(std::function<void()> callback, unsigned long delay_millisec) override {
		g_recorded_calls.push_back("in_timer_set");
		g_pending_timer_callback = std::move(callback);
		g_pending_timer_delay_ms = delay_millisec;
		g_timer_armed = true;
	}
	char *in_install_prev_colormap(int) override { g_recorded_calls.push_back("in_install_prev_colormap"); return nullptr; }
	char *in_install_colormap_by_name(const char*, int) override { g_recorded_calls.push_back("in_install_colormap_by_name"); return nullptr; }
	Stringlist *in_choose_input_files() override { g_recorded_calls.push_back("in_choose_input_files"); return nullptr; }
	Message in_choose_save_file(const char*, const char*, char*, size_t) override { g_recorded_calls.push_back("in_choose_save_file"); return Message::Cancel; }

	void set_options() override { g_recorded_calls.push_back("set_options"); }
	Message printer_options(PrintOptions *po) override {
		g_recorded_calls.push_back("printer_options");
		if (g_printer_options_override) g_printer_options_override(*po);
		return g_printer_options_response;
	}
	void in_print(const PrintInfo &info, const PrintOptions &po) override {
		g_recorded_calls.push_back("in_print");
		g_last_print_info = info;
		g_last_print_options = po;
		g_have_last_print_info = true;
	}
	Message x_range(float min, float max, float, float, float *ret_min, float *ret_max, int *allvars) override {
		g_recorded_calls.push_back("x_range");
		// Even on Message::OK, write through *ret_min/*ret_max/*allvars (as
		// "the user left the range unchanged") rather than leaving them
		// uninitialized -- a caller that only checks for Message::Cancel
		// before using them (as view_set_range() does) would otherwise read
		// garbage.
		if (ret_min) *ret_min = min;
		if (ret_max) *ret_max = max;
		if (allvars) *allvars = 0;
		return g_range_response;
	}
	void x_dataedit(std::vector<std::string> &cells, int nx) override {
		g_recorded_calls.push_back("x_dataedit");
		g_last_dataedit_lines = cells;
		g_last_dataedit_nx = nx;
	}
	int x_seen_colormap_name(const char *name) override {
		g_recorded_calls.push_back("x_seen_colormap_name");
		for (const auto &seen : g_seen_colormap_names)
			if (name != nullptr && seen == name) return 1;
		return 0;
	}
	void x_check_legal_colormap_loaded() override { g_recorded_calls.push_back("x_check_legal_colormap_loaded"); }
	void x_create_colorbar(float, float, Transform) override { g_recorded_calls.push_back("x_create_colorbar"); }
	void x_draw_colorbar() override { g_recorded_calls.push_back("x_draw_colorbar"); }
	void x_error(const char *message) override { g_recorded_calls.push_back("x_error"); std::fprintf(stderr, "ncview error: %s\n", message ? message : "(null)"); }
	void x_force_set_invert_state(int) override { g_recorded_calls.push_back("x_force_set_invert_state"); }
	void x_init_dim_info(const Stringlist*) override { g_recorded_calls.push_back("x_init_dim_info"); }
	void x_set_var_sensitivity(const char*, int) override { g_recorded_calls.push_back("x_set_var_sensitivity"); }
	void unlock_plot() override { g_recorded_calls.push_back("unlock_plot"); }
	Stringlist *get_persistent_X_state() override { g_recorded_calls.push_back("get_persistent_X_state"); return nullptr; }
	void pix_to_rgb(ncv_pixel pix, int *r, int *g, int *b) override { if (r) *r = pix; if (g) *g = pix; if (b) *b = pix; }
};

namespace {
RecordingViewerUi g_recording_ui;
} // namespace

// Called explicitly from each test binary's main() (tests/main.cc), NOT
// from a static initializer here: g_app (ncview/app_context.h) now
// contains a ViewerSession/ViewerController with non-trivial members
// (Dataset's vectors, etc.), so g_app itself requires dynamic
// initialization -- its constructor can run either before or after this
// TU's own static initializers, in unspecified order (the classic static-
// initialization-order fiasco). A static initializer here that set
// g_app.ui directly used to be safe back when g_viewer_ui was its own
// bare, constant-initialized pointer global; now that it's a member of a
// dynamically-initialized aggregate, setting it too early gets silently
// overwritten when g_app's own constructor subsequently runs and
// default-member-initializes ui back to nullptr. main() is guaranteed to
// run after all static initialization completes, so calling this from
// there sidesteps the ordering question entirely.
void installRecordingViewerUi()
{
	g_app.ui = &g_recording_ui;
}
