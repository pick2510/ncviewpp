// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for g_app.controller.changeCurDim()/g_app.controller.setCurDimIndex()/
// g_app.session.curDimIndex() (Phase 2: ViewerController::changeCurDim()/
// setCurDimIndex(), ViewerSession::curDimIndex()) -- round-trip, clamping
// at both ends, and rejecting an attempt to move the X or Y axis. Written
// against the unmodified free functions first, per this plan's
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
extern std::vector<std::string> g_recorded_calls;

namespace {

// A (level, lat, lon) variable -- "level" is a plain, non-scan dimension
// that g_app.controller.changeCurDim()/g_app.controller.setCurDimIndex() can step, since the
// X/Y axes default to the last two dims (lat, lon).
void select_dims_variable(NcFixture &nc, const char *var_name, int nlevel) {
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    nc.dim("level", nlevel).dim("lat", 2).dim("lon", 2)
      .coord("level").coord("lat").coord("lon")
      .var(var_name, {"level", "lat", "lon"});
    int fid = nc.openForCore();
    g_dataset.addVariable(var_name, fid, nc.path().c_str());
    in_variable_selected(var_name);
}

} // namespace

TEST_CASE("view_set_cur_dim_index/view_get_cur_dim_index: round-trip") {
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_roundtrip", 5);

    g_app.controller.setCurDimIndex("level", 3);
    CHECK(g_app.session.curDimIndex("level") == 3);

    g_app.controller.setCurDimIndex("level", 0);
    CHECK(g_app.session.curDimIndex("level") == 0);
}

TEST_CASE("view_set_cur_dim_index: clamps a negative place up to 0") {
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_clamp_low", 5);

    g_app.controller.setCurDimIndex("level", -3);
    CHECK(g_app.session.curDimIndex("level") == 0);
}

TEST_CASE("view_set_cur_dim_index: clamps a too-large place down to size-1") {
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_clamp_high", 5);

    g_app.controller.setCurDimIndex("level", 999);
    CHECK(g_app.session.curDimIndex("level") == 4);
}

TEST_CASE("view_change_cur_dim: Modifier::M1 steps forward by one, wrapping at the end") {
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_step_fwd", 3);
    REQUIRE(g_app.session.curDimIndex("level") == 0);

    char dim_name[] = "level";
    g_app.controller.changeCurDim(dim_name, Modifier::M1);
    CHECK(g_app.session.curDimIndex("level") == 1);
    g_app.controller.changeCurDim(dim_name, Modifier::M1);
    CHECK(g_app.session.curDimIndex("level") == 2);
    g_app.controller.changeCurDim(dim_name, Modifier::M1);
    CHECK(g_app.session.curDimIndex("level") == 0); // wraps
}

TEST_CASE("view_change_cur_dim: Modifier::M3 steps backward by one, wrapping at zero") {
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_step_bwd", 3);
    REQUIRE(g_app.session.curDimIndex("level") == 0);

    char dim_name[] = "level";
    g_app.controller.changeCurDim(dim_name, Modifier::M3);
    CHECK(g_app.session.curDimIndex("level") == 2); // wraps
    g_app.controller.changeCurDim(dim_name, Modifier::M3);
    CHECK(g_app.session.curDimIndex("level") == 1);
}

TEST_CASE("view_change_cur_dim: refuses to move the Y (or X) axis") {
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_reject_axis", 3);
    REQUIRE(view != nullptr);
    int y_axis_id = view->y_axis_id;
    size_t before = view->var_place[y_axis_id];

    char dim_name[] = "lat"; // the current Y axis
    g_app.controller.changeCurDim(dim_name, Modifier::M1);
    CHECK(view->var_place[y_axis_id] == before);
}

TEST_CASE("view_get_cur_dim_index: unknown dimension name returns 0") {
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_unknown", 3);

    CHECK(g_app.session.curDimIndex("no_such_dim") == 0);
}

TEST_CASE("View::setAxis: an unresolvable dimension name does not write out of bounds (Phase 12e)") {
    // Regression test for a real, previously-undiscovered memory-safety
    // bug: setAxis()'s Dimension::X/Y branches used to do
    // `var_place[new_id] = 0L` with no check that new_id (from
    // dimNameToId(), whose own doc comment documents -1 as a legitimate
    // "not found" return) wasn't -1. var_place is std::vector<size_t>,
    // so operator[](-1) is operator[](SIZE_MAX) -- an out-of-bounds heap
    // write, confirmed under ASan against the unmodified code before
    // this fix landed.
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_bad_axis", 3);
    REQUIRE(view != nullptr);

    // "no_such_dim" is not one of this variable's dims, so
    // dimNameToId() returns -1 for it -- exactly the condition that used
    // to reach the unchecked var_place[new_id] write.
    char bogus_name[] = "no_such_dim";
    view->setAxis(Dimension::X, bogus_name);
    // Reaching this line at all (without an ASan abort) is the main
    // point. The axis must be left unset (its prior valid id kept, or a
    // clearly-invalid -1), never silently "succeed" with a corrupted
    // var_place vector.
    CHECK(view->x_axis_id == -1);

    view->setAxis(Dimension::Y, bogus_name);
    CHECK(view->y_axis_id == -1);
}

TEST_CASE("view_change_cur_dim: an unresolvable dimension name does not read out of bounds (Phase 12e)") {
    // Regression test: changeCurDim() computed dimid via dimNameToId()
    // (which legitimately returns -1 for a dim not found on this
    // variable) and only guarded against it by accident, via the
    // `dimid == x_axis_id || dimid == y_axis_id` check -- which only
    // catches the case if one of those axis ids also happens to be -1.
    // Otherwise dim[dimid]/var_place[dimid] below were an out-of-bounds
    // read (operator[](SIZE_MAX) on std::vector). This variable has real
    // X/Y axes (not -1), so the accidental protection does not apply.
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_change_bad", 3);
    REQUIRE(view != nullptr);
    REQUIRE(view->x_axis_id != -1);
    REQUIRE(view->y_axis_id != -1);

    g_recorded_calls.clear();
    char bogus_name[] = "no_such_dim";
    g_app.controller.changeCurDim(bogus_name, Modifier::M1); // must not read out of bounds

    bool reported_error = false;
    for (const auto &call : g_recorded_calls)
        if (call == "in_dialog") // in_error() forwards to in_dialog()
            reported_error = true;
    CHECK(reported_error);
}

TEST_CASE("view_set_cur_dim_index: an unresolvable dimension name does not read out of bounds (Phase 12e)") {
    // Same bug, same fix, in setCurDimIndex() instead of changeCurDim().
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_setidx_bad", 3);
    REQUIRE(view != nullptr);
    REQUIRE(view->x_axis_id != -1);
    REQUIRE(view->y_axis_id != -1);

    g_recorded_calls.clear();
    g_app.controller.setCurDimIndex("no_such_dim", 0); // must not read out of bounds

    bool reported_error = false;
    for (const auto &call : g_recorded_calls)
        if (call == "in_dialog")
            reported_error = true;
    CHECK(reported_error);
}

TEST_CASE("View::setScanDims: pressing Cancel in the Axes dialog is a no-op, not a segfault (Phase 13a)") {
    // Regression test for a user-reported core dump ("I sometimes had core
    // dumps when I cancel an operation with a cancel button").
    //
    // setScanDims() tested the dialog's result against Message::Cancel,
    // which is 2, while in_set_scan_dims() returns a plain 0/1 -- so the
    // check never fired and the (*new_dim_list)[0] immediately below
    // dereferenced a Stringlist* that MainWindow::scanDimsDialog() had
    // deliberately left NULL on its cancel path. Confirmed SIGSEGV against
    // the unmodified function before this fix landed (revert the guard,
    // rebuild, run this test: the process dies with no doctest summary).
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_cancel_axes", 3);
    REQUIRE(view != nullptr);

    const int x_before = view->x_axis_id;
    const int y_before = view->y_axis_id;

    // What the FLTK dialog does on Cancel: return 0, leave *new_dim_list
    // exactly as the caller passed it (see stub_interface.cc).
    g_set_scan_dims_populate = false;
    g_set_scan_dims_response = 0;

    view->setScanDims();

    // Reaching this line at all is the main point; the axes must also be
    // untouched, since the user cancelled.
    CHECK(view->x_axis_id == x_before);
    CHECK(view->y_axis_id == y_before);
}

TEST_CASE("View::setScanDims: a dialog that reports success but returns no list is also a no-op (Phase 13a)") {
    // The other half of the guard. scanDimsDialog() has a second bare
    // `return 0` (an empty dim_list) that likewise never writes
    // *new_dim_list, and a future UI could get the pair out of step in
    // the opposite direction too. Checking the status alone would not
    // cover this; checking the list alone would not cover a UI that
    // populates the list and *then* reports cancellation.
    SessionFixture fx;
    NcFixture nc;
    select_dims_variable(nc, "dims_ok_but_no_list", 3);
    REQUIRE(view != nullptr);

    const int x_before = view->x_axis_id;
    const int y_before = view->y_axis_id;

    g_set_scan_dims_populate = false;
    g_set_scan_dims_response = 1;	/* "OK" -- but no list written */

    view->setScanDims();

    CHECK(view->x_axis_id == x_before);
    CHECK(view->y_axis_id == y_before);
}
