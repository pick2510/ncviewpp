// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for g_app.controller.draw() (Phase 2: becomes
// ViewerController::draw()) -- framestore caching on/off, forced range
// recompute, and autoscale. Written against the unmodified free function
// first, per this plan's characterization rule.
//
// Phase 12a added a regression test (below) for the specific bug this
// comment used to describe as an open gap: set_scan_variable()/
// View::changeDat() could leave lockout_view_changes stuck true forever
// on dataToPixels()'s Cancel-the-dialog failure path, silently no-op'ing
// every later draw(). That's now fixed with an RAII guard
// (view_internal.h's LockoutViewChangesGuard) and covered directly.
// Still not covered: genuine *re-entrancy* -- a nested draw() call
// actually firing from inside a UI callback that a modal dialog
// triggers mid-draw. RecordingViewerUi's stubs don't re-enter core that
// way, and building that wiring is its own piece of work.
#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "support/nc_fixture.h"
#include "support/session_fixture.h"
#include "test_udunits_helper.h"

using ncview_test::Constant;
using ncview_test::NcFixture;
using ncview_test::SessionFixture;

// The global under test, defined in core/src/view.cc.
extern std::unique_ptr<ViewState> &view;
extern FrameCache &framestore;

namespace {

void select_draw_variable(NcFixture &nc, const char *var_name, int nt) {
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    nc.dim("time", nt).dim("lat", 2).dim("lon", 2)
      .timeAxis("time", "days since 2000-01-01")
      .coord("lat").coord("lon")
      .var(var_name, {"time", "lat", "lon"});
    int fid = nc.openForCore();
    g_dataset.addVariable(var_name, fid, nc.path().c_str());
    in_variable_selected(var_name);
}

} // namespace

TEST_CASE("view_draw: framestore stays invalid when save_frames is off") {
    SessionFixture fx;
    NcFixture nc;
    // ensure_ncview_misc_initialized() runs the real initialize_misc()
    // exactly once per process, and initialize_misc() itself calls
    // reset_session_defaults() -- which sets options.save_frames back to
    // DEFAULT_SAVEFRAMES (true). Call it *before* overriding save_frames
    // below, not after: whichever test in the suite happens to run first
    // triggers that one-time call, and setting save_frames=false first
    // would silently be undone by it if select_draw_variable() (which
    // also calls ensure_ncview_misc_initialized()) ran first instead --
    // exactly the kind of order-dependence a shuffled-order run exists to
    // catch.
    ensure_ncview_misc_initialized();
    options.save_frames = false;
    select_draw_variable(nc, "draw_no_framestore", 3);

    CHECK_FALSE(framestore.valid());
    CHECK(g_app.controller.draw(true, false) == 0);
    CHECK_FALSE(framestore.valid());
}

TEST_CASE("view_draw: framestore caches a drawn frame when save_frames is on") {
    SessionFixture fx;
    NcFixture nc;
    options.save_frames = true;
    select_draw_variable(nc, "draw_framestore", 3);
    REQUIRE(framestore.valid());

    CHECK(g_app.controller.draw(true, false) == 0);
    CHECK(framestore.lookup(0) != nullptr);

    options.save_frames = false;
}

TEST_CASE("view_draw: force_range_to_frame recomputes user_min/user_max from the current frame") {
    SessionFixture fx;
    NcFixture nc;
    select_draw_variable(nc, "draw_forced_range", 3);
    REQUIRE(view != nullptr);

    view->variable->user_min = -999;
    view->variable->user_max = 999;
    // allow_framestore_usage=false: save_frames defaults to true (see
    // ncview.cc's DEFAULT_SAVEFRAMES), so the frame selection just drew is
    // already cached -- with allow_framestore_usage=true, view_draw's own
    // framestore-hit path returns early *before* the force_range_to_frame
    // recompute ever runs, an interaction only visible by actually running
    // this rather than reading the code.
    CHECK(g_app.controller.draw(false, true) == 0);
    // The synthetic Ramp variable is nonconstant per frame, so forcing a
    // recompute must move the range away from the placeholder values.
    CHECK(view->variable->user_min != -999);
    CHECK(view->variable->user_max != 999);
}

TEST_CASE("view_draw: autoscale recomputes the range on every draw, even without force_range_to_frame") {
    SessionFixture fx;
    NcFixture nc;
    select_draw_variable(nc, "draw_autoscale", 3);
    REQUIRE(view != nullptr);
    options.autoscale = true;

    view->variable->user_min = -999;
    view->variable->user_max = 999;
    CHECK(g_app.controller.draw(true, false) == 0);
    CHECK(view->variable->user_min != -999);
    CHECK(view->variable->user_max != 999);

    options.autoscale = false;
}

TEST_CASE("view_draw: a constant-valued variable still draws without crashing") {
    SessionFixture fx;
    NcFixture nc;
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    nc.dim("time", 2).dim("lat", 2).dim("lon", 2)
      .timeAxis("time", "days since 2000-01-01")
      .coord("lat").coord("lon")
      .var("draw_constant", {"time", "lat", "lon"}, Constant{5.0f});
    int fid = nc.openForCore();
    g_dataset.addVariable("draw_constant", fid, nc.path().c_str());
    in_variable_selected("draw_constant");
    REQUIRE(view != nullptr);

    // Must not crash even though every value (and so global_min ==
    // global_max) is identical -- view_draw's own degenerate-range path.
    g_app.controller.draw(true, false);
}

TEST_CASE("view_draw: in_set_2d_size fires on a fresh session's first draw, even at a size an earlier test already reported") {
    // Regression test (Phase 11h): ViewerController::draw() used to gate
    // its in_set_2d_size call behind a pair of function-local `static
    // size_t last_x_size, last_y_size` -- process-global state
    // SessionFixture never reset. in_variable_selected() -> a fresh
    // variable selection reports its size TWICE on a truly fresh
    // session: once unconditionally from set_scan_variable() itself
    // (view.cc's own `ui.in_set_2d_size(...)` call, no gate), and once
    // more from the internal draw() that set_scan_variable() triggers
    // via stepView() -- draw()'s own copy is gated on "has the UI last
    // seen this exact size", which is true (size unchanged) only because
    // the *previous frame it drew* was already this size, and false (so
    // it correctly re-reports) on any session's genuine first draw.
    //
    // Every TEST_CASE above this one in this file selects a variable via
    // select_draw_variable(), which always builds the same lat=2/lon=2
    // shape -- so by the time this test runs, the leaked static already
    // holds that exact scaled size from an earlier, unrelated test's
    // draw(). Under the bug, this test's own draw()-via-stepView() call
    // -- a fresh SessionFixture's genuine first draw -- incorrectly finds
    // "size unchanged" against that leftover state and skips its report,
    // leaving only the one unconditional call from set_scan_variable and
    // undercounting by exactly one. This is also the actual,
    // previously-undiagnosed mechanism behind the long-documented
    // 6663-vs-6664 order-dependent assertion count in test_do_print.cc
    // (known since Phase 7a): whichever test happens to draw immediately
    // before it determines whether this exact branch fires.
    SessionFixture fx;
    NcFixture nc;
    select_draw_variable(nc, "draw_2d_size_fresh_session", 3);

    int set_2d_size_count = 0;
    for (const auto &s : g_recorded_calls)
        if (s == "in_set_2d_size")
            set_2d_size_count++;
    CHECK(set_2d_size_count == 2);
}

TEST_CASE("View::changeDat: lockout_view_changes is released even when dataToPixels() is cancelled") {
    // Regression test (Phase 12a). set_scan_variable()/View::changeDat()
    // used to set lockout_view_changes = true immediately before calling
    // dataToPixels(), then reset it to false on the very next line --
    // except dataToPixels() (render_pipeline.cc) returns -1 on a real,
    // reachable failure (the user pressing Cancel on the "min and max
    // both 0" dialog), and both functions `return` on that failure path
    // before ever reaching the reset. Left stuck, ViewerController::
    // draw() checks the flag first thing and no-ops -- returning 0,
    // success -- without drawing, and can never clear a flag it didn't
    // set itself: every later draw silently does nothing until the next
    // *successful* set_scan_variable()/changeDat() call.
    SessionFixture fx;
    NcFixture nc;
    select_draw_variable(nc, "lockout_recovery", 3);
    REQUIRE(view != nullptr);
    // Ramp-generated data means global_min != global_max, so the
    // failure path below does NOT also call invalidate_variable()
    // (which would null the view out from under this test -- it's only
    // called when global_min == global_max).
    REQUIRE(view->variable->global_min != view->variable->global_max);

    // Force dataToPixels()'s "min and max both 0" branch, then script a
    // Cancel response so it takes the return(-1) path instead of
    // recomputing the range.
    view->variable->user_min = 0;
    view->variable->user_max = 0;
    g_dialog_response = Message::Cancel;

    view->changeDat(0, view->data[0]); // value unchanged -- only exercises the lockout path

    g_dialog_response = Message::OK; // restore the fixture's own default
    // Give the view a normal, non-degenerate range again so the recovery
    // draw() below can succeed on its own merits -- isolating "was the
    // lockout flag released" from "is the range still degenerate".
    view->variable->user_min = view->variable->global_min;
    view->variable->user_max = view->variable->global_max;

    g_recorded_calls.clear();
    CHECK(g_app.controller.draw(true, false) == 0);

    bool drew = false;
    for (const auto &s : g_recorded_calls)
        if (s == "in_draw_2d_field")
            drew = true;
    CHECK(drew); // fails under the bug: draw() silently no-ops instead
}
