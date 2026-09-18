// Copyright (C) 2026 Dominik Strebel
//
// Regression tests for a user-reported crash: "if I change from 1d to 3d
// back and forward multiple times it can crash". Phase 13b.
//
// Selecting a 1-D variable leaves the session half-initialized on purpose:
// View::initialDetermineScanAxes()'s `case 1` sets y_axis_id = -1, and
// set_scan_variable()'s plot-and-return path (core/src/view.cc) then skips
// allocStorage()/fillViewData() entirely, so `data` stays empty. Meanwhile
// this port's FltkViewerUi::in_popdown_2d_window() was an empty no-op, so
// the *previous* variable's picture stayed on screen, clickable, with the
// whole toolbar still live.
//
// size[] / dim[] / dim_map_info[] / var_place[] are all std::vector, so
// indexing them by -1 is indexing by SIZE_MAX: a read or (worse) a write
// eight bytes before the buffer. Sweeping the 25 controls reachable in
// that state found 14 that did exactly that -- ASan heap-buffer-overflow
// or SIGSEGV every time, against unmodified code.
//
// Upstream had already met this scenario once and guarded exactly one
// caller: ViewerController::reportPosition(), whose comment names it
// ("click on a 2-d variable, then click on a 1-d variable, then move the
// pointer back over the displayed colormap of the (old) 2-d variable").
// The click, the buttons and every internal helper were missed. Every
// TEST_CASE below crashes without the View::has2dAxes()/has2dImage()
// guards this file exists to pin down.
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

// One file holding both shapes the user switches between: an ordinary
// (time, lat, lon) field and a bare (time) series. Both must live in the
// same file, because the crash needs the 3-D variable to have been
// displayed first -- that is what leaves a stale picture and an enabled
// toolbar behind for the 1-D variable to inherit.
void build_3d_and_1d(NcFixture &nc) {
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    nc.dim("time", 4).dim("lat", 3).dim("lon", 5)
      .timeAxis("time", "days since 2000-01-01")
      .coord("lat").coord("lon")
      .var("g3d", {"time", "lat", "lon"})
      .var("g1d", {"time"});
    int fid = nc.openForCore();
    g_dataset.addVariable("g3d", fid, nc.path().c_str());
    g_dataset.addVariable("g1d", fid, nc.path().c_str());

    // What ncview_main() (core/src/ncview.cc) does exactly once, after all
    // input files are read. Dataset::addVariable() does NOT do it, so a
    // test that goes straight to addVariable() -- as every test in this
    // tree does -- leaves effective_dimensionality at 0 for every
    // variable. That matters here specifically: set_scan_variable()'s
    // `effective_dimensionality == 1` branch is what routes a 1-D
    // variable to the plot-and-return path in the first place, so
    // skipping this would exercise a state the real application never
    // reaches, and would *manufacture* out-of-bounds writes rather than
    // reproduce the reported ones.
    for (auto &owner : g_dataset.variablesMutable()) {
        NCVar *v = owner.get();
        v->effective_dimensionality = 0;
        for (int d = 0; d < v->n_dims; d++)
            if (v->size[d] > 1) v->effective_dimensionality++;
    }
}

// Display the 3-D variable, draw it (so there really is a picture and a
// populated frame store), then select the 1-D variable -- the exact
// sequence from the bug report, leaving the View the guards must cope
// with.
void show_3d_then_1d(NcFixture &nc) {
    build_3d_and_1d(nc);

    in_variable_selected("g3d");
    REQUIRE(view != nullptr);
    REQUIRE(view->has2dImage());
    g_app.controller.draw(true, false);

    in_variable_selected("g1d");
    REQUIRE(view != nullptr);
    // The state under test: an X axis, no Y axis, and no data buffer.
    REQUIRE(view->x_axis_id == 0);
    REQUIRE(view->y_axis_id == -1);
    REQUIRE_FALSE(view->has2dAxes());
    REQUIRE_FALSE(view->has2dImage());

    g_query_pointer_x = 4;
    g_query_pointer_y = 4;
}

} // namespace

TEST_CASE("1-D variable selected: a left click on the stale 2-D pane does not write out of bounds (Phase 13b)") {
    // ViewerController::plotXY(), reached from ui/src/main_window.cc's
    // FL_RELEASE handler. The worst of the fourteen: `start[view->y_axis_id]
    // = data_y` is an out-of-bounds heap WRITE, so it corrupts whatever
    // allocation happens to sit before var_place's buffer and the process
    // dies later, somewhere else -- which is why the user saw it only
    // "sometimes", after switching "back and forward multiple times".
    SessionFixture fx;
    NcFixture nc;
    show_3d_then_1d(nc);

    g_app.controller.plotXY();

    // Reaching this line at all (no ASan abort) is the point. No plot
    // should have been produced from a variable that has no 2-D field.
    CHECK(view->y_axis_id == -1);
}

TEST_CASE("1-D variable selected: a ctrl-click on the stale 2-D pane does not write out of bounds (Phase 13b)") {
    // setMinFromCurdata()/setMaxFromCurdata() (ctrl-left and ctrl-right in
    // ui/src/main_window.cc). These crashed one level deeper than plotXY:
    // inside View::fillViewData(), whose count[v->y_axis_id] = ... is
    // itself an out-of-bounds write, before getData() then writes a
    // heap-garbage number of floats into an empty `data`.
    SessionFixture fx;
    NcFixture nc;
    show_3d_then_1d(nc);

    const float min_before = view->variable->user_min;
    const float max_before = view->variable->user_max;

    g_app.controller.setMinFromCurdata();
    g_app.controller.setMaxFromCurdata();

    CHECK(view->variable->user_min == min_before);
    CHECK(view->variable->user_max == max_before);
}

TEST_CASE("1-D variable selected: a mouse hover over the stale 2-D pane stays safe (Phase 13b)") {
    // The one case upstream already guarded, kept as a characterization
    // test so the guard is not lost: it is the reason the click paths
    // looked safe by analogy and were never checked.
    SessionFixture fx;
    NcFixture nc;
    show_3d_then_1d(nc);

    g_app.controller.reportPosition(4, 4, 0);

    CHECK(view->y_axis_id == -1);
}

TEST_CASE("1-D variable selected: the Axes button reports an error instead of reading out of bounds (Phase 13b)") {
    // View::setScanDims() opens with two dim[axis_id]->name reads. dim is
    // a std::vector<std::unique_ptr<NCDim>>, so dim[-1] materializes a
    // unique_ptr out of unrelated heap bytes and ->name dereferences it.
    SessionFixture fx;
    NcFixture nc;
    show_3d_then_1d(nc);
    resetStubRecording();

    view->setScanDims();

    // Never reached the dialog: the error is reported before any UI call.
    bool asked_for_dims = false;
    for (const auto &call : g_recorded_calls)
        if (call == "in_set_scan_dims") asked_for_dims = true;
    CHECK_FALSE(asked_for_dims);
}

TEST_CASE("1-D variable selected: the Edit button reports an error instead of reading out of bounds (Phase 13b)") {
    // View::dataEdit() sizes its cell vector from size[x]*size[y]; with
    // y_axis_id == -1 that count comes from heap bytes, and the loop then
    // reads that many floats out of an empty `data`.
    SessionFixture fx;
    NcFixture nc;
    show_3d_then_1d(nc);
    resetStubRecording();

    view->dataEdit();

    bool opened_grid = false;
    for (const auto &call : g_recorded_calls)
        if (call == "x_dataedit") opened_grid = true;
    CHECK_FALSE(opened_grid);
}

TEST_CASE("1-D variable selected: the Print button reports an error instead of reading out of bounds (Phase 13b)") {
    // do_print() already had the "no variable selected" guard; this is the
    // other precondition it was missing. build_print_info() reads the
    // display axes roughly thirty more times below the crash point.
    SessionFixture fx;
    NcFixture nc;
    show_3d_then_1d(nc);
    print_init();
    resetStubRecording();

    do_print();

    bool printed = false;
    for (const auto &call : g_recorded_calls)
        if (call == "in_print") printed = true;
    CHECK_FALSE(printed);
}

TEST_CASE("1-D variable selected: an overlay is refused but turning one off still works (Phase 13b)") {
    // gen_overlay_internal() and friends index dim[], size[] and
    // dim_map_info[] by both display axes. OVERLAY_NONE is deliberately
    // still allowed through the guard -- it touches none of that, and is
    // how the user gets back to a clean state.
    SessionFixture fx;
    NcFixture nc;
    show_3d_then_1d(nc);

    do_overlay(OVERLAY_P8DEG, NULL, true);
    CHECK_FALSE(options.overlay->doit);

    do_overlay(OVERLAY_NONE, NULL, true);
    CHECK_FALSE(options.overlay->doit);
    CHECK(g_app.session.currentOverlay() == OVERLAY_NONE);
}

TEST_CASE("1-D variable selected: blowup / invert-physical / the internal helpers stay in bounds (Phase 13b)") {
    // The remaining controls from the sweep, each of which crashed on its
    // own. Grouped because they share one assertion -- surviving the call.
    SessionFixture fx;
    NcFixture nc;
    show_3d_then_1d(nc);

    SUBCASE("changeBlowup (the M button, and the scroll-wheel zoom)") {
        view->changeBlowup(1, true, true);
        CHECK(view->y_axis_id == -1);
    }
    SUBCASE("invertPhysical (reaches initSaveframes)") {
        g_app.controller.invertPhysical(Modifier::M1);
        CHECK(view->y_axis_id == -1);
    }
    SUBCASE("allocStorage") {
        view->allocStorage();
        // Still no buffer: there is no 2-D shape to size one from.
        CHECK(view->data.empty());
    }
    SUBCASE("fillViewData") {
        view->fillViewData();
        CHECK(view->data.empty());
    }
    SUBCASE("initSaveframes") {
        view->initSaveframes();
        CHECK(view->y_axis_id == -1);
    }
    SUBCASE("redrawDimensionInfo") {
        view->redrawDimensionInfo();
        CHECK(view->y_axis_id == -1);
    }
    SUBCASE("setDataeditPlace (a middle-button press on the stale pane)") {
        view->setDataeditPlace();
        CHECK(view->y_axis_id == -1);
    }
    SUBCASE("dataToPixels reports 'not renderable' rather than rendering garbage") {
        // Unlike its seven siblings above, this one does NOT crash without
        // its guard: the 1-D path never runs initMinMax(), so
        // have_set_range is false and dataToPixels() returns -1 at its
        // first line regardless. The guard below it is deliberate defence
        // in depth for the whole render path, not a reproduced crash --
        // recorded here so the distinction is not lost.
        CHECK(view->dataToPixels() < 0);
    }
}

TEST_CASE("1-D variable selected: hasMissingData answers instead of walking off an empty buffer (Phase 13b)") {
    // This one guarded its axis id correctly and still crashed: `data`
    // being *sized* is a separate precondition, and the 1-D path never
    // calls allocStorage(). x_axis_id is a perfectly good 0 here, so the
    // existing guard let the read loop through against an empty vector --
    // a real SIGSEGV, not a quiet overread.
    SessionFixture fx;
    NcFixture nc;
    show_3d_then_1d(nc);

    CHECK(view->hasMissingData());
}

TEST_CASE("switching 3-D to 1-D and back repeatedly leaves a working 2-D view (Phase 13b)") {
    // The user's sequence, end to end: the guards must not have turned the
    // 3-D variable into a casualty. Four round trips, drawing each time.
    SessionFixture fx;
    NcFixture nc;
    build_3d_and_1d(nc);

    for (int i = 0; i < 4; i++) {
        in_variable_selected("g3d");
        REQUIRE(view != nullptr);
        CHECK(view->has2dImage());
        CHECK(view->x_axis_id == 2);	// lon
        CHECK(view->y_axis_id == 1);	// lat
        CHECK(view->dataToPixels() == 0);
        CHECK(g_app.controller.draw(true, false) == 0);

        in_variable_selected("g1d");
        REQUIRE(view != nullptr);
        CHECK_FALSE(view->has2dAxes());
        CHECK(g_app.controller.draw(true, false) == 0);
    }
}

TEST_CASE("1-D variable selected: the 2-D-only buttons are disabled and the pane is popped down (Phase 13c)") {
    // The other half of the fix. Phase 13b stops the 2-D-only controls
    // corrupting the heap; this stops them being offered at all.
    //
    // set_scan_variable()'s 1-D path used to return before ever calling
    // setScanButtons(), so the toolbar kept whatever state the PREVIOUS
    // variable left it in -- BUTTONS_ALL_ON after any ordinary 2-D field.
    // Combined with FltkViewerUi::in_popdown_2d_window() being an empty
    // no-op, the user was looking at a stale picture surrounded by live
    // buttons, none of which the selected variable could answer.
    SessionFixture fx;
    NcFixture nc;
    show_3d_then_1d(nc);

    auto sensitivity = [](Button b) {
        auto it = g_button_sensitivity.find(static_cast<int>(b));
        REQUIRE(it != g_button_sensitivity.end());
        return it->second;
    };

    // Everything that acts on the 2-D picture.
    CHECK(sensitivity(Button::Dimset) == 0);
    CHECK(sensitivity(Button::Print) == 0);
    CHECK(sensitivity(Button::Edit) == 0);
    CHECK(sensitivity(Button::Blowup) == 0);
    CHECK(sensitivity(Button::BlowupType) == 0);
    CHECK(sensitivity(Button::Transform) == 0);
    CHECK(sensitivity(Button::InvertPhysical) == 0);
    // The tape-recorder row: no 2-D picture also means no scan axis.
    CHECK(sensitivity(Button::Forward) == 0);
    CHECK(sensitivity(Button::Fastforward) == 0);
    CHECK(sensitivity(Button::Rewind) == 0);

    // Deliberately still usable: these act on the variable, not the
    // picture, and the Phase 13b sweep confirmed each is safe here.
    CHECK(sensitivity(Button::ColormapSelect) == 1);
    CHECK(sensitivity(Button::InvertColormap) == 1);
    CHECK(sensitivity(Button::Range) == 1);
    CHECK(sensitivity(Button::Info) == 1);

    // And core did ask the UI to take the stale picture off screen.
    bool popped_down = false;
    for (const auto &call : g_recorded_calls)
        if (call == "in_popdown_2d_window") popped_down = true;
    CHECK(popped_down);
}

TEST_CASE("selecting a 2-D variable re-enables the buttons and pops the pane back up (Phase 13c)") {
    // The return leg: the new BUTTONS_2D_OFF state must not be sticky, or
    // switching back from a 1-D variable would leave the application
    // permanently crippled -- a worse bug than the one being fixed.
    SessionFixture fx;
    NcFixture nc;
    show_3d_then_1d(nc);
    resetStubRecording();

    in_variable_selected("g3d");
    REQUIRE(view != nullptr);
    REQUIRE(view->has2dImage());

    auto sensitivity = [](Button b) {
        auto it = g_button_sensitivity.find(static_cast<int>(b));
        REQUIRE(it != g_button_sensitivity.end());
        return it->second;
    };

    CHECK(sensitivity(Button::Dimset) == 1);
    CHECK(sensitivity(Button::Print) == 1);
    CHECK(sensitivity(Button::Edit) == 1);
    CHECK(sensitivity(Button::Blowup) == 1);
    CHECK(sensitivity(Button::InvertPhysical) == 1);
    CHECK(sensitivity(Button::Forward) == 1);

    bool popped_up = false;
    for (const auto &call : g_recorded_calls)
        if (call == "in_popup_2d_window") popped_up = true;
    CHECK(popped_up);
}
