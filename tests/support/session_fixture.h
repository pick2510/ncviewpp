// Copyright (C) 2026 Dominik Strebel
//
// SessionFixture: RAII test isolation for g_app (ncview/app_context.h).
// OOP_redesign "refine the architecture" plan, Phase 0a.
//
// Every doctest TEST_CASE in ncview_core_tests runs in the same process,
// sharing one g_app -- a ViewerSession (owning a Dataset, whose NCVars/
// open netCDF files persist for the binary's whole lifetime) plus whatever
// the currently-installed ViewerUi (RecordingViewerUi, stub_interface.cc)
// has recorded. Until now this was managed by convention: test_varlist.cc
// documents a "each test needs a unique variable name" rule so two tests'
// Dataset::addVariable() calls don't collide, and test_time_fmt.cc had a
// latent double-close bug that existed specifically because ownership of
// a shared, never-reset Dataset wasn't obvious from the test code. That
// convention doesn't scale as the OOP_redesign refactor phases add many
// more test cases that select variables, drive playback, and open files.
//
// A SessionFixture instance gives its TEST_CASE a clean slate on
// construction and puts everything back on destruction (including on an
// early return via a failed REQUIRE), by *move-assigning* a fresh,
// default-constructed ViewerSession into g_app.session in place, rather
// than replacing g_app.session itself: several `extern` bridge
// references (g_dataset, view, framestore, pixel_transform -- see
// ncview/protos.h) are bound once, at startup, to specific g_app.session
// sub-objects, and a future ViewerController is expected to hold a
// ViewerSession& too (see the "refine the architecture" plan's Phase 2).
// Only in-place reset (`g_app.session = ViewerSession();`) keeps every
// one of those references valid across a fixture's lifetime; replacing
// g_app.session with a newly-constructed object would leave them
// dangling.
//
// Closing every netCDF file a test opened is not this fixture's job to
// re-verify: NetCDFFile's destructor (ncview/dataset.h) already closes on
// destruction unconditionally, tested directly by test_dataset.cc, and
// resetting Dataset here (via ViewerSession's move-assignment) destroys
// every NetCDFFile the outgoing session owned, exactly like process exit
// would. What this fixture adds is making that happen *between* test
// cases instead of only at process exit -- the actual isolation property
// under test.
#pragma once

#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ncview/includes.h"
#include "ncview/protos.h"

// Declared in stub_interface.cc.
extern std::vector<std::string> g_recorded_calls;

// Captured/scripted by in_create_colormap()/x_seen_colormap_name() -- see
// stub_interface.cc's own comment. Added for Phase 8's test_colormaps.cc.
struct CreatedColormap {
	std::string name;
	std::array<unsigned char, 256> r, g, b;
};
extern std::vector<CreatedColormap> g_created_colormaps;
extern std::vector<std::string> g_seen_colormap_names;
extern Message g_dialog_response;
extern Message g_range_response;
extern Message g_printer_options_response;
extern int g_set_scan_dims_response;
extern bool g_set_scan_dims_populate;
// Button enumerator value -> last sensitivity set_buttons() applied to it.
extern std::map<int,int> g_button_sensitivity;
extern bool g_have_last_print_info;
extern PrintInfo g_last_print_info;
extern PrintOptions g_last_print_options;
extern std::function<void(PrintOptions &)> g_printer_options_override;
extern int g_query_pointer_x;
extern int g_query_pointer_y;
extern bool g_have_last_xy_plot;
extern size_t g_last_xy_n;
extern int g_last_xy_dimindex;
extern std::vector<double> g_last_xy_xvals;
extern std::vector<double> g_last_xy_yvals;
extern std::string g_last_xy_x_axis_title;
extern std::string g_last_xy_legend;
extern void resetStubRecording();

// The fake one-shot timer queue (stub_interface.cc) -- lets a test drive
// playback (rewind/fastforward's re-arming) and the file-growth poll
// (View::checkNewData()) by firing the callback core handed the UI,
// instead of that callback being silently dropped. See fireTimer()'s own
// comment for the one-shot semantics this reproduces.
extern bool timerIsArmed();
extern unsigned long timerDelayMs();
extern bool fireTimer();

namespace ncview_test {

// Construct at the top of any TEST_CASE that selects a variable, opens a
// file, drives the controller/UI seam, or otherwise mutates g_app/options
// -- which by now is most of ncview_core_tests. Cheap to construct (a
// handful of move-assignments); safe to nest inside a scope that also
// uses ScratchHome (scratch_home.h) for rc-file tests, in either order.
class SessionFixture {
public:
	SessionFixture() {
		resetSession();
		resetStubRecording();
	}

	~SessionFixture() {
		resetSession();
	}

	SessionFixture( const SessionFixture & ) = delete;
	SessionFixture &operator=( const SessionFixture & ) = delete;

private:
	// Move-assigning a fresh, default-constructed ViewerSession discards
	// the outgoing Dataset (closing every file it owned), the active
	// ViewState (there is none by default), the FrameCache, and all four
	// settings groups -- everything ViewerSession owns, in one place,
	// exactly matching the "5 globals" this class replaced. See the
	// class-level comment for why this is a move-assignment into the
	// existing g_app.session rather than a fresh object swapped in.
	static void resetSession() {
		g_app.session = ViewerSession();
		// Discovered while building this fixture: a freshly
		// default-constructed ViewerSession leaves options.overlay
		// (SessionDisplayPrefs, see viewer_session.h) null, because in
		// real deployments it's initialize_misc()'s job to allocate it,
		// once, at process startup -- something no code path had ever
		// needed to redo mid-process before this fixture existed.
		// reset_session_defaults() is exactly that initialization,
		// split out of initialize_misc() (ncview.cc) so it can be
		// re-run here without repeating udu_utinit(NULL), which is
		// NOT safe to call more than once (see test_udunits_helper.h).
		reset_session_defaults();
	}

	// No separate Options save/restore: every one of Options's fields is
	// itself a reference member bound to storage living inside
	// g_app.session (see viewer_session.h's header comment) -- there is
	// no Options storage anywhere else to reset. Options also has no
	// default constructor or copy/move assignment (reference members
	// disallow both), so it couldn't be snapshotted this way even if
	// there were something to snapshot. resetSession() above already
	// restores everything `options.foo` can read.
};

} // namespace ncview_test
