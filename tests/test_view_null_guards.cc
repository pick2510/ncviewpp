// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for the ~12 view.cc entry points that each carry
// their own `if (view == NULL) ...` guard -- "refine the architecture"
// plan, Phase 2. That guard doesn't mean "View might be null" in general;
// it means "no variable is selected yet", a session fact, which is why
// these move onto ViewerSession/ViewerController. Written and verified
// against the UNMODIFIED free functions first (per this plan's
// characterization rule), so the exact no-op/early-return behavior each
// performs today is pinned down before anything moves.
#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "support/session_fixture.h"
#include "test_udunits_helper.h"

using ncview_test::SessionFixture;

// The global under test, defined in core/src/view.cc.
extern std::unique_ptr<ViewState> &view;

TEST_CASE("view.cc null guards: every relocated entry point is a documented no-op with no variable selected") {
    SessionFixture fx;
    ensure_ncview_misc_initialized();

    REQUIRE(view == nullptr);

    SUBCASE("view_current_nt returns 0") {
        CHECK(g_app.session.currentNt() == 0);
    }
    SUBCASE("change_view returns 0 and does not crash") {
        CHECK(g_app.controller.stepView(1, FRAMES) == 0);
        CHECK(view == nullptr);
    }
    SUBCASE("view_draw returns 0 and does not crash") {
        CHECK(g_app.controller.draw(true, false) == 0);
        CHECK(view == nullptr);
    }
    SUBCASE("view_change_cur_dim reports an error and does not crash") {
        char dim_name[] = "time";
        g_app.controller.changeCurDim(dim_name, Modifier::M1);
        CHECK(view == nullptr);
    }
    SUBCASE("view_set_cur_dim_index reports an error and does not crash") {
        g_app.controller.setCurDimIndex("time", 0);
        CHECK(view == nullptr);
    }
    SUBCASE("view_get_cur_dim_index returns 0") {
        CHECK(g_app.session.curDimIndex("time") == 0);
    }
    SUBCASE("invalidate_all_saveframes is a silent no-op") {
        g_app.session.invalidateAllSaveframes();
        CHECK(view == nullptr);
    }
    SUBCASE("view_report_position is a silent no-op") {
        g_app.controller.reportPosition(1, 1, 0);
        CHECK(view == nullptr);
    }
    SUBCASE("set_min_from_curdata is a silent no-op") {
        g_app.controller.setMinFromCurdata();
        CHECK(view == nullptr);
    }
    SUBCASE("set_max_from_curdata is a silent no-op") {
        g_app.controller.setMaxFromCurdata();
        CHECK(view == nullptr);
    }
    SUBCASE("plot_XY is a silent no-op") {
        g_app.controller.plotXY();
        CHECK(view == nullptr);
    }
    SUBCASE("view_recompute_colorbar is a silent no-op") {
        g_app.controller.recomputeColorbar();
        CHECK(view == nullptr);
    }
}

// do_print()/build_print_info() (do_print.cc) dereference view->variable
// and friends ~30 times with no null guard, reachable via Button::Print
// (viewer_controller.cc's dispatch()) before any variable is selected.
// Phase 3b of the "refine the architecture" plan confirmed this crashes
// (SIGSEGV) against the unguarded function, then added the guard --
// same "no variable selected yet" session fact as the entry points above.
TEST_CASE("do_print: Print with no variable selected is a silent no-op, not a crash") {
    SessionFixture fx;
    ensure_ncview_misc_initialized();
    print_init();

    REQUIRE(view == nullptr);
    do_print();
    CHECK(view == nullptr);
}
