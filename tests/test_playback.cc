// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for ViewerController's playback actions
// (core/src/viewer_controller.cc: rewind/fastforward/pause/restart),
// using stub_interface.cc's fake one-shot timer queue (added for this
// purpose -- "refine the architecture" plan, Phase 0b). Before that
// queue existed, in_timer_set() silently dropped every callback core
// handed the UI, so none of this had ever actually run under test:
// rewind()/fastforward()'s Modifier::M1 paths only advance the movie
// because their own timer callback re-arms itself and steps the frame
// again, exactly the behavior nothing could previously observe.
#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "support/nc_fixture.h"
#include "support/session_fixture.h"
#include "test_udunits_helper.h"

using ncview_test::NcFixture;
using ncview_test::SessionFixture;

namespace {

// Loads and selects a (time, lat, lon) variable with `nt` frames from an
// already-built NcFixture, leaving `view` populated and its scan axis on
// time (axis 0). The fixture's own destructor cleans up the underlying
// file -- callers don't remove it themselves.
void select_playback_variable(NcFixture &nc, const char *var_name, int nt) {
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

TEST_CASE("playback: rewind arms a timer that steps backward one frame at a time") {
    SessionFixture fx;
    NcFixture nc;
    select_playback_variable(nc, "playback_rewind", 5);
    // Land on a middle frame first so rewind has somewhere to go.
    g_app.controller.restart(Modifier::M1);
    g_app.controller.stepView(2, FRAMES);
    REQUIRE(current_frame() == 2);

    g_app.controller.rewind(Modifier::M1);
    CHECK(current_frame() == 1); // stepView(-1, FRAMES) already ran synchronously
    REQUIRE(timerIsArmed());

    REQUIRE(fireTimer());
    CHECK(current_frame() == 0);
    CHECK(timerIsArmed()); // the fired callback re-armed itself
}

TEST_CASE("playback: fastforward arms a timer that steps forward one frame at a time") {
    SessionFixture fx;
    NcFixture nc;
    select_playback_variable(nc, "playback_fastforward", 5);
    g_app.controller.restart(Modifier::M1);
    REQUIRE(current_frame() == 0);

    g_app.controller.fastforward(Modifier::M1);
    CHECK(current_frame() == 1);
    REQUIRE(timerIsArmed());

    REQUIRE(fireTimer());
    CHECK(current_frame() == 2);
    CHECK(timerIsArmed());

    REQUIRE(fireTimer());
    CHECK(current_frame() == 3);
}

TEST_CASE("playback: pause clears any pending timer") {
    SessionFixture fx;
    NcFixture nc;
    select_playback_variable(nc, "playback_pause", 5);
    g_app.controller.restart(Modifier::M1);
    g_app.controller.fastforward(Modifier::M1);
    REQUIRE(timerIsArmed());

    g_app.controller.pause(Modifier::M1);
    CHECK_FALSE(timerIsArmed());

    // A stale, already-fired callback must not silently resume playback:
    // there is nothing pending to fire.
    CHECK_FALSE(fireTimer());
}

TEST_CASE("playback: restart seeks to frame 0 and does not itself arm a timer") {
    SessionFixture fx;
    NcFixture nc;
    select_playback_variable(nc, "playback_restart", 5);
    g_app.controller.restart(Modifier::M1);
    g_app.controller.stepView(3, FRAMES);
    REQUIRE(current_frame() == 3);

    g_app.controller.restart(Modifier::M1);
    CHECK(current_frame() == 0);
    // restart() explicitly clears the timer (in_timer_clear()) rather
    // than arming a new one -- the button handlers that follow it
    // (rewind()/fastforward()) are the ones responsible for arming.
    CHECK_FALSE(timerIsArmed());
}

TEST_CASE("playback: fireTimer() is a documented no-op when nothing is armed") {
    SessionFixture fx;
    CHECK_FALSE(timerIsArmed());
    CHECK_FALSE(fireTimer());
}
