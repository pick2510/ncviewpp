// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for the three workflows the OOP_redesign plan's
// migration sequence moves first (variable selection in Step 6/7, range
// acceptance/cancellation and playback in Step 7) -- written *before* any
// of that code moves, using the new call-recording + scripted-dialog-
// response support added to tests/stub_interface.cc for exactly this
// purpose. These pin down today's actual behavior (what gets called, and
// what a cancelled dialog does or doesn't trigger) so later refactoring
// steps have a regression net that doesn't depend on reading the old
// procedural code correctly by eye.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "test_udunits_helper.h"

// Declared in stub_interface.cc.
extern std::vector<std::string> g_recorded_calls;
extern Message g_dialog_response;
extern Message g_range_response;
extern void resetStubRecording();

// The global under test, defined in core/src/view.cc.
extern std::unique_ptr<ViewState> &view;
// Real deployments always set this in the UI's own in_initialize()
// (ui/src/interface_fltk.cc) before any variable is selected --
// View::calculateBlowup() divides by it unconditionally, and leaving it at
// its zero-initialized default turns into a divide-by-zero -> UB
// float-to-int conversion that shows up here as an absurd blowup value
// and a failed huge-vector allocation. The stub's in_initialize() is a
// no-op (there's no real window to size), so tests that select a
// variable must set this themselves, exactly as the real UI does.
extern Options options;

namespace {

bool called( const char *name ) {
    return std::find(g_recorded_calls.begin(), g_recorded_calls.end(), std::string(name))
           != g_recorded_calls.end();
}

// A (time, lat, lon) file with a real UDUNITS time axis, big enough
// (nt >= 2) that frame-stepping/playback has somewhere to go.
std::string make_sample_file( const char *var_name, int nt, int nlat, int nlon ) {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_ctrl_char_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_time, dim_lat, dim_lon;
    REQUIRE(nc_def_dim(ncid, "time", nt, &dim_time) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lat", nlat, &dim_lat) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lon", nlon, &dim_lon) == NC_NOERR);

    int var_time, var_lat, var_lon, var_data;
    REQUIRE(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_time, &var_time) == NC_NOERR);
    std::string units = "days since 2000-01-01";
    REQUIRE(nc_put_att_text(ncid, var_time, "units", units.size(), units.c_str()) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, "lat", NC_FLOAT, 1, &dim_lat, &var_lat) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, "lon", NC_FLOAT, 1, &dim_lon, &var_lon) == NC_NOERR);
    int dims[3] = {dim_time, dim_lat, dim_lon};
    REQUIRE(nc_def_var(ncid, var_name, NC_FLOAT, 3, dims, &var_data) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);

    std::vector<double> tvals(nt);
    for (int i = 0; i < nt; i++) tvals[i] = (double)i;
    REQUIRE(nc_put_var_double(ncid, var_time, tvals.data()) == NC_NOERR);
    std::vector<float> latvals(nlat, 0.0f), lonvals(nlon, 0.0f);
    for (int i = 0; i < nlat; i++) latvals[i] = (float)i;
    for (int i = 0; i < nlon; i++) lonvals[i] = (float)i;
    REQUIRE(nc_put_var_float(ncid, var_lat, latvals.data()) == NC_NOERR);
    REQUIRE(nc_put_var_float(ncid, var_lon, lonvals.data()) == NC_NOERR);
    // Varying values (not a constant fill) so data_to_pixels() takes its
    // normal path instead of the degenerate min==max recursion.
    std::vector<float> data(nt * nlat * nlon);
    for (size_t i = 0; i < data.size(); i++) data[i] = 1.0f + (float)i;
    REQUIRE(nc_put_var_float(ncid, var_data, data.data()) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);
    return path;
}

int open_for_core( const std::string &path ) {
    Stringlist *files = nullptr;
    stringlist_add_string(&files, path.c_str());
    determine_file_type(files);
    stringlist_delete_entire_list(files);
    return netcdf_fi_initialize(const_cast<char *>(path.c_str()));
}

// Loads a fresh, uniquely-named variable and selects it (mirroring what
// the UI's variable combobox callback does via in_variable_selected()),
// leaving the global `view` populated -- the precondition every test
// below needs. Returns the path so the caller can clean it up.
std::string select_fresh_variable( const char *var_name ) {
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    std::string path = make_sample_file(var_name, 2, 2, 2);
    int fid = open_for_core(path);
    g_dataset.addVariable(var_name, fid, path.c_str());
    resetStubRecording();
    in_variable_selected(var_name);
    return path;
}

} // namespace

TEST_CASE("in_variable_selected: selecting a variable populates the view and draws a frame") {
    const char *var_name = "ctrl_char_var_select";
    std::string path = select_fresh_variable(var_name);

    CHECK(view != nullptr);
    REQUIRE(view->variable != nullptr);
    CHECK(view->variable->name == var_name);

    // set_scan_variable()'s happy path (a real 2-D field, not the
    // effective_dimensionality==1 XY-plot shortcut) busies the cursor,
    // sizes the 2-D display, and draws the first frame.
    CHECK(called("in_set_cursor_busy"));
    CHECK(called("in_set_2d_size"));
    CHECK(called("in_draw_2d_field"));

    std::remove(path.c_str());
}

TEST_CASE("ViewerController::range: cancelling the dialog leaves the range/display untouched") {
    const char *var_name = "ctrl_char_var_range_cancel";
    std::string path = select_fresh_variable(var_name);
    float min_before = view->variable->user_min, max_before = view->variable->user_max;

    resetStubRecording();
    g_range_response = Message::Cancel;
    g_app.controller.range(Modifier::M1);

    CHECK(called("x_range"));
    // view_set_range() returns immediately on Message::Cancel, before
    // touching user_min/max or redrawing -- confirmed by reading
    // core/src/view.cc:view_set_range().
    CHECK_FALSE(called("in_draw_2d_field"));
    CHECK(view->variable->user_min == min_before);
    CHECK(view->variable->user_max == max_before);

    std::remove(path.c_str());
}

TEST_CASE("ViewerController::range: accepting the dialog redraws") {
    const char *var_name = "ctrl_char_var_range_ok";
    std::string path = select_fresh_variable(var_name);

    resetStubRecording();
    g_range_response = Message::OK;
    g_app.controller.range(Modifier::M1);

    CHECK(called("x_range"));
    CHECK(called("in_draw_2d_field"));

    std::remove(path.c_str());
}

TEST_CASE("playback: fastforward arms the timer and sets Button::Fastforward; pause clears it") {
    const char *var_name = "ctrl_char_var_playback";
    std::string path = select_fresh_variable(var_name);

    resetStubRecording();
    g_app.controller.fastforward(Modifier::M1);
    CHECK(which_button_pressed() == Button::Fastforward);
    CHECK(called("in_timer_clear")); // fastforward() always clears the previous timer first
    CHECK(called("in_timer_set"));   // and re-arms itself, since stepping from frame 0->1 succeeds

    resetStubRecording();
    g_app.controller.pause(Modifier::M1);
    CHECK(which_button_pressed() == Button::Pause);
    CHECK(called("in_timer_clear"));
    CHECK_FALSE(called("in_timer_set"));

    std::remove(path.c_str());
}
