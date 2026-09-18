// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for change_view() (Phase 2: becomes
// ViewerController::stepView()) -- frame/percent stepping, wraparound at
// both ends of the scan axis, and the delta==0 expose-event path. Written
// against the unmodified free function first, per this plan's
// characterization rule.
#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "support/nc_fixture.h"
#include "support/session_fixture.h"
#include "test_udunits_helper.h"

using ncview_test::NcFixture;
using ncview_test::SessionFixture;

// The global under test, defined in core/src/view.cc.
extern std::unique_ptr<ViewState> &view;

namespace {

void select_nav_variable(NcFixture &nc, const char *var_name, int nt) {
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

size_t current_frame() {
    REQUIRE(view != nullptr);
    REQUIRE(view->scan_axis_id != -1);
    return view->var_place[view->scan_axis_id];
}

} // namespace

TEST_CASE("stepView: FRAMES steps by exactly delta frames") {
    SessionFixture fx;
    NcFixture nc;
    select_nav_variable(nc, "nav_frames", 5);
    REQUIRE(current_frame() == 0);

    g_app.controller.stepView(2, FRAMES);
    CHECK(current_frame() == 2);

    g_app.controller.stepView(-1, FRAMES);
    CHECK(current_frame() == 1);
}

TEST_CASE("stepView: FRAMES wraps to 0 past the last frame") {
    SessionFixture fx;
    NcFixture nc;
    select_nav_variable(nc, "nav_wrap_fwd", 5);
    g_app.controller.stepView(4, FRAMES);
    REQUIRE(current_frame() == 4);

    g_app.controller.stepView(1, FRAMES);
    CHECK(current_frame() == 0);
}

TEST_CASE("stepView: FRAMES wraps to the last frame going below 0") {
    SessionFixture fx;
    NcFixture nc;
    select_nav_variable(nc, "nav_wrap_bwd", 5);
    REQUIRE(current_frame() == 0);

    g_app.controller.stepView(-1, FRAMES);
    CHECK(current_frame() == 4);
}

TEST_CASE("stepView: stop_on_restart pauses on the last frame instead of wrapping") {
    SessionFixture fx;
    NcFixture nc;
    select_nav_variable(nc, "nav_stop_on_restart", 3);
    g_app.controller.stepView(2, FRAMES);
    REQUIRE(current_frame() == 2);

    options.stop_on_restart = true;
    g_app.controller.stepView(1, FRAMES);
    // Would-wrap-to-0 returns immediately, before calling
    // View::scanToPlace(0) -- so the frame index is left exactly where it
    // was (2), not reset to 0. This is what actually stops the movie: the
    // position never advances past the last frame, rather than visibly
    // landing back on frame 0.
    CHECK(current_frame() == 2);
    options.stop_on_restart = false;
}

TEST_CASE("stepView: PERCENT interprets delta as a percentage of scan size") {
    SessionFixture fx;
    NcFixture nc;
    select_nav_variable(nc, "nav_percent", 100);
    REQUIRE(current_frame() == 0);

    // 10% of 100 frames == 10.
    g_app.controller.stepView(10, PERCENT);
    CHECK(current_frame() == 10);
}

TEST_CASE("stepView: PERCENT always moves at least one frame") {
    SessionFixture fx;
    NcFixture nc;
    select_nav_variable(nc, "nav_percent_min", 5);
    REQUIRE(current_frame() == 0);

    // 1% of 5 frames truncates to 0 -- stepView clamps that up to a
    // minimum single-frame step so PERCENT navigation never stalls.
    g_app.controller.stepView(1, PERCENT);
    CHECK(current_frame() == 1);
}

TEST_CASE("stepView: delta==0 redraws in place without stepping (the expose-event path)") {
    SessionFixture fx;
    NcFixture nc;
    select_nav_variable(nc, "nav_delta_zero", 5);
    g_app.controller.stepView(2, FRAMES);
    REQUIRE(current_frame() == 2);

    CHECK(g_app.controller.stepView(0, FRAMES) == 0);
    CHECK(current_frame() == 2);
}
