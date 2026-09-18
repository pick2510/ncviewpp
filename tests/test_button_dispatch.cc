// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for in_button_pressed()'s dispatch onto
// ViewerController (core/src/do_buttons.cc / viewer_controller.cc).
// "Refine the architecture" plan, Phase 1: do_buttons.cc's 21 do_*()
// functions are about to collapse from real logic into two-line
// forwarders onto g_app.controller -- these tests pin down, for every
// reachable Button enumerator, that in_button_pressed() actually routes
// to a real action (not the dispatch() switch's `default: exit(-1)`
// case, and not a silent no-op where one isn't already documented as
// intentional) before that plumbing is deleted.
//
// Button::Quit is deliberately excluded from the exhaustive loop below:
// ViewerController::quit() calls quit_app(), which calls exit(0) for
// real -- there is no test double for it, and running it here would
// kill the test process. Its routing is trivial enough (one line,
// unchanged by this phase) not to need a death-test.
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

bool called(const char *name) {
    for (const auto &s : g_recorded_calls)
        if (s == name) return true;
    return false;
}

// Every Button enumerator except Quit (see the file comment) -- kept as
// an explicit list, not a range-cast loop over the enum's underlying
// values, so removing or renumbering an enumerator is a compile error
// here rather than a silently-skipped gap.
const Button kAllButtonsExceptQuit[] = {
    Button::Rewind, Button::Backwards, Button::Pause, Button::Forward,
    Button::Fastforward, Button::ColormapSelect, Button::InvertPhysical,
    Button::InvertColormap, Button::Minimum, Button::Maximum,
    Button::Blowup, Button::Restart, Button::Transform, Button::Dimset,
    Button::Range, Button::BlowupType, Button::Edit, Button::Info,
    Button::Print, Button::Options,
};

const Modifier kAllModifiers[] = {
    Modifier::M1, Modifier::M2, Modifier::M3, Modifier::M4,
};

const char *button_name(Button b) {
    switch (b) {
        case Button::Rewind: return "Rewind";
        case Button::Backwards: return "Backwards";
        case Button::Pause: return "Pause";
        case Button::Forward: return "Forward";
        case Button::Fastforward: return "Fastforward";
        case Button::ColormapSelect: return "ColormapSelect";
        case Button::InvertPhysical: return "InvertPhysical";
        case Button::InvertColormap: return "InvertColormap";
        case Button::Minimum: return "Minimum";
        case Button::Maximum: return "Maximum";
        case Button::Quit: return "Quit";
        case Button::Blowup: return "Blowup";
        case Button::Restart: return "Restart";
        case Button::Transform: return "Transform";
        case Button::Dimset: return "Dimset";
        case Button::Range: return "Range";
        case Button::BlowupType: return "BlowupType";
        case Button::Edit: return "Edit";
        case Button::Info: return "Info";
        case Button::Print: return "Print";
        case Button::Options: return "Options";
        default: return "?";
    }
}

// Loads and selects a fresh (time, lat, lon) variable -- the precondition
// several button handlers assume unconditionally (e.g.
// ViewerController::invertPhysical() dereferences `view` directly, with
// no null guard: it's only ever reachable from a real UI once a variable
// is selected and its button enabled).
void select_a_variable(NcFixture &nc, const char *var_name) {
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    print_init(); // Button::Print's do_print() reads printopts, which
                   // otherwise stays zero-initialized.
    nc.dim("time", 3).dim("lat", 2).dim("lon", 2)
      .timeAxis("time", "days since 2000-01-01")
      .coord("lat").coord("lon")
      .var(var_name, {"time", "lat", "lon"});
    int fid = nc.openForCore();
    g_dataset.addVariable(var_name, fid, nc.path().c_str());
    in_variable_selected(var_name);
}

} // namespace

TEST_CASE("in_button_pressed: every non-Quit button routes to a real action, for every modifier") {
    SessionFixture fx;
    NcFixture nc;
    select_a_variable(nc, "dispatch_var");
    REQUIRE(view != nullptr);

    for (Button b : kAllButtonsExceptQuit) {
        for (Modifier m : kAllModifiers) {
            CAPTURE(button_name(b));
            CAPTURE(static_cast<int>(m));
            resetStubRecording();
            in_button_pressed(b, m);

            if (b == Button::Minimum || b == Button::Maximum) {
                // Documented no-ops (viewer_controller.h's own comment):
                // Button::Minimum/Maximum are still reachable via
                // interface_fltk.cc's NCVIEW_TEST_BUTTON name table, and
                // dropping their dispatch() cases entirely would turn
                // that path's outcome from a no-op into exit(-1). Pinned
                // here so nobody "fixes" them into doing something by
                // accident without a deliberate decision.
                CHECK(g_recorded_calls.empty());
            } else {
                // Every other button's handler is documented (viewer_
                // controller.cc) to always make at least one UI call
                // (arm/clear a timer, redraw, pop a dialog, ...) for
                // every modifier value -- proves dispatch() actually
                // reached a real action, not the switch's unreachable
                // `default: exit(-1)`.
                CHECK_FALSE(g_recorded_calls.empty());
            }
        }
    }
}

TEST_CASE("in_button_pressed: Range's M3 fast path skips the range dialog, unlike M1/M2/M4") {
    SessionFixture fx;
    NcFixture nc;
    select_a_variable(nc, "dispatch_range_var");

    resetStubRecording();
    in_button_pressed(Button::Range, Modifier::M1);
    CHECK(called("x_range"));

    resetStubRecording();
    in_button_pressed(Button::Range, Modifier::M3);
    CHECK_FALSE(called("x_range"));
}

TEST_CASE("in_button_pressed: Pause always clears the timer, even with nothing pending") {
    SessionFixture fx;
    NcFixture nc;
    select_a_variable(nc, "dispatch_pause_var");

    resetStubRecording();
    in_button_pressed(Button::Pause, Modifier::M1);
    CHECK(called("in_timer_clear"));
}
