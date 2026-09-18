// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for the XY-plot-from-a-click feature:
// View::setXYPlotAxis()/View::plotXYSc() (core/src/view.cc, the single
// largest View:: method in the tree at 256 lines) and their real UI entry
// point ViewerController::plotXY() (core/src/viewer_controller.cc), plus
// the round-trip back out through View::plotXYFmtXVal() and the free
// function view_report_position_vals(). All at zero test coverage before
// this file -- part of round 4's reassessment, see the plan's "Reassess
// here, round 4" section.
//
// Driving plotXY() needs a scriptable mouse position: in_query_pointer_position()
// (tests/stub_interface.cc) previously hardcoded (0,0). Extended with
// g_query_pointer_x/y, mirroring the g_printer_options_override pattern
// Phase 4a already established for scripting a UI answer.
#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>

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

// A (time, lat, lon) variable with a real time axis, so setXYPlotAxis("time")
// has a timelike dimension to resolve and plotXYFmtXVal()/
// view_report_position_vals() have something to format specially.
void select_xy_variable(NcFixture &nc, const char *var_name, int nt, int nlat, int nlon) {
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    nc.dim("time", nt).dim("lat", nlat).dim("lon", nlon)
      .timeAxis("time", "days since 2000-01-01")
      .coord("lat").coord("lon")
      .var(var_name, {"time", "lat", "lon"});
    int fid = nc.openForCore();
    g_dataset.addVariable(var_name, fid, nc.path().c_str());
    in_variable_selected(var_name);
}

bool recorded(const char *prefix) {
    for (const auto &call : g_recorded_calls)
        if (call.rfind(prefix, 0) == 0)
            return true;
    return false;
}

// A (station, time, lat, lon) data var -- the same (time, lat, lon) image
// shape every other test in this file uses (time as the plot axis, lat/lon
// as the x/y image axes -- View::redrawDimensionInfo() and friends assume
// at least that much, confirmed the hard way: a 2-D (time, station)-only
// variable segfaults in View::redrawDimensionInfo() during selection,
// there being no x/y image axis left once time is the plot axis) plus one
// extra leading "station" axis that is itself a 2-D NC_CHAR dimvar
// (station, strlen). station has size 1, so it's always the non-plotted,
// non-image "other" dim plotXYSc()'s legend loop (view.cc) reads via
// Dataset::dimValue() into a fixed-size stack buffer. strlen_size chars,
// none NUL, so netcdf_dim_value()'s read loop runs the full strlen_size
// iterations (limited only by the dimvar's own trailing dim, not by
// finding a terminator early) before it force-NULs the last byte --
// NcFixture can't express an NC_CHAR dimvar at all, hence the inline
// build, the established idiom for shapes outside NcFixture's scope (see
// the Phase 12f/12g tests in test_file_netcdf.cc).
std::string make_char_station_file(int nt, int nlat, int nlon, int strlen_size) {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_char_station_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_station, dim_time, dim_lat, dim_lon, dim_strlen;
    REQUIRE(nc_def_dim(ncid, "station", 1, &dim_station) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "time", nt, &dim_time) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lat", nlat, &dim_lat) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lon", nlon, &dim_lon) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "strlen", strlen_size, &dim_strlen) == NC_NOERR);

    int var_time, var_lat, var_lon, var_station, var_data;
    REQUIRE(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_time, &var_time) == NC_NOERR);
    const char *time_units = "days since 2000-01-01";
    REQUIRE(nc_put_att_text(ncid, var_time, "units", strlen(time_units), time_units) == NC_NOERR);

    REQUIRE(nc_def_var(ncid, "lat", NC_FLOAT, 1, &dim_lat, &var_lat) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, "lon", NC_FLOAT, 1, &dim_lon, &var_lon) == NC_NOERR);

    int station_dims[2] = {dim_station, dim_strlen};
    REQUIRE(nc_def_var(ncid, "station", NC_CHAR, 2, station_dims, &var_station) == NC_NOERR);

    int data_dims[4] = {dim_station, dim_time, dim_lat, dim_lon};
    REQUIRE(nc_def_var(ncid, "data", NC_FLOAT, 4, data_dims, &var_data) == NC_NOERR);

    REQUIRE(nc_enddef(ncid) == NC_NOERR);

    std::vector<double> time_vals(nt);
    for (int i = 0; i < nt; i++) time_vals[i] = (double)i;
    REQUIRE(nc_put_var_double(ncid, var_time, time_vals.data()) == NC_NOERR);

    std::vector<float> lat_vals(nlat), lon_vals(nlon);
    for (int i = 0; i < nlat; i++) lat_vals[i] = (float)i;
    for (int i = 0; i < nlon; i++) lon_vals[i] = (float)i;
    REQUIRE(nc_put_var_float(ncid, var_lat, lat_vals.data()) == NC_NOERR);
    REQUIRE(nc_put_var_float(ncid, var_lon, lon_vals.data()) == NC_NOERR);

    // Every byte 'A' -- no embedded NUL, so netcdf_dim_value()'s read loop
    // is bounded only by strlen_size (the trailing dim's length), not by
    // finding a terminator early.
    std::string station_name(strlen_size, 'A');
    size_t start[2] = {0, 0}, count[2] = {1, (size_t)strlen_size};
    REQUIRE(nc_put_vara_text(ncid, var_station, start, count, station_name.data()) == NC_NOERR);

    std::vector<float> data_vals((size_t)nt * nlat * nlon);
    for (size_t i = 0; i < data_vals.size(); i++) data_vals[i] = (float)i;
    REQUIRE(nc_put_var_float(ncid, var_data, data_vals.data()) == NC_NOERR);

    REQUIRE(nc_close(ncid) == NC_NOERR);
    return path;
}

} // namespace

TEST_CASE("View::determineScanAxes: plot_XY_axis already defaults to the scan axis, not -1") {
    // Surprising on first read of plotXY() alone -- it has an "Error! I
    // have no valid axis to plot along!" branch for plot_XY_axis==-1
    // (view.cc:948-951) that reads like a real precondition a caller must
    // satisfy first. It isn't reachable through normal selection:
    // View::create() (view.cc:1405) sets plot_XY_axis=-1, but
    // determineScanAxes() (view.cc:726-729), called moments later by the
    // same set_scan_variable() path in_variable_selected() drives, always
    // overwrites it with scan_axis_id (or x_axis_id if there's no scan
    // axis) before selection finishes -- so a freshly-selected variable
    // always has a usable plot axis already.
    SessionFixture fx;
    NcFixture nc;
    select_xy_variable(nc, "xyplot_axis_default", 4, 2, 2);
    REQUIRE(view != nullptr);
    CHECK(view->plot_XY_axis == view->scan_axis_id);
    CHECK(view->plot_XY_axis == 0); // "time" is dim index 0 of {time,lat,lon} and is the scan axis
}

TEST_CASE("setXYPlotAxis: resolves a named dimension and is a silent no-op with no existing lines") {
    SessionFixture fx;
    NcFixture nc;
    select_xy_variable(nc, "xyplot_axis_resolve", 4, 2, 2);
    REQUIRE(view != nullptr);
    REQUIRE(view->plot_XY_axis == 0); // the auto-selected default (see above)

    char label[] = "lat";
    view->setXYPlotAxis(label);

    CHECK(view->plot_XY_axis == 1); // "lat" is dim index 1 of {time,lat,lon}
    // plot_XY_nlines is still 0 at this point (no click has happened yet),
    // so setXYPlotAxis()'s "replot every existing line" loop has nothing
    // to do -- no popup should have been triggered.
    CHECK_FALSE(recorded("in_popup_XY_graph"));
}

TEST_CASE("setXYPlotAxis: an unknown dimension name reports an error and leaves the axis unchanged") {
    SessionFixture fx;
    NcFixture nc;
    select_xy_variable(nc, "xyplot_axis_unknown", 4, 2, 2);
    int before = view->plot_XY_axis;

    char label[] = "no_such_dim";
    view->setXYPlotAxis(label);

    CHECK(view->plot_XY_axis == before);
    CHECK(recorded("in_dialog")); // in_error() routes through in_dialog()
}

TEST_CASE("plotXY: a normal in-bounds click plots the time series at that (lat,lon) and records a popup") {
    SessionFixture fx;
    NcFixture nc;
    select_xy_variable(nc, "xyplot_click", 4, 2, 2);
    REQUIRE(view != nullptr);
    char label[] = "time";
    view->setXYPlotAxis(label);
    REQUIRE(view->plot_XY_axis == 0);

    // Click the (lat=1, lon=0) cell. mouse_xy_to_data_xy() divides by
    // options.blowup (set by View::calculateBlowup() during selection
    // above) for a positive blowup; land safely mid-cell with +blowup/2.
    int blowup = options.blowup;
    REQUIRE(blowup > 0);
    g_query_pointer_x = 0 * blowup + blowup / 2;      // lon index 0
    g_query_pointer_y = 0 * blowup + blowup / 2;       // see inversion note below

    g_app.controller.plotXY();

    REQUIRE(recorded("in_popup_XY_graph"));
    CHECK(view->plot_XY_nlines == 1);

    // Read back plot_XY_yvals via g_dataset.getData() at the SAME
    // (lat,lon) plotXY() actually resolved (view->plot_XY_position[0]),
    // rather than assuming which cell the click landed on -- Y gets
    // inverted from screen to data coordinates unless options.invert_physical.
    size_t start[3], count[3];
    for (int i = 0; i < 3; i++) { start[i] = view->plot_XY_position[0][i]; count[i] = 1; }
    start[0] = 0; count[0] = 4; // full time axis, matching plotXYSc()'s own count[X_axis]=n
    std::vector<float> expected_y(4);
    g_dataset.getData(view->variable, start, count, expected_y.data());

    // plot_XY_xvals/yvals are file-scope statics inside view.cc (internal
    // linkage) -- the stub's in_popup_XY_graph() capture (g_last_xy_*) is
    // the only place their contents are actually observable from a test.
    REQUIRE(g_last_xy_n == 4);
    REQUIRE(g_last_xy_xvals.size() == 4);
    REQUIRE(g_last_xy_yvals.size() == 4);
    for (int t = 0; t < 4; t++) {
        CAPTURE(t);
        CHECK(g_last_xy_xvals[t] == doctest::Approx((double)t)); // "days since 2000-01-01": 0,1,2,3
        CHECK(g_last_xy_yvals[t] == doctest::Approx((double)expected_y[t]));
    }
    CHECK(g_last_xy_dimindex == 0); // dim 0 == "time", the plotted axis
}

TEST_CASE("plotXY: a click past the right/bottom edge clamps to the last valid cell instead of erroring") {
    SessionFixture fx;
    NcFixture nc;
    select_xy_variable(nc, "xyplot_edge_click", 4, 2, 3); // lat size 2, lon size 3
    char label[] = "time";
    view->setXYPlotAxis(label);

    // Click far outside the image -- plotXY() must clamp data_x/data_y to
    // size-1 (view.cc:968-969) rather than reading out of bounds.
    g_query_pointer_x = 1000000;
    g_query_pointer_y = 1000000;

    g_app.controller.plotXY();

    REQUIRE(recorded("in_popup_XY_graph"));
    int x_axis_id = view->x_axis_id, y_axis_id = view->y_axis_id;
    size_t nlon = view->variable->size[x_axis_id];
    // X isn't inverted, so its clamp is exactly what it looks like: the
    // last valid column. CHECK(view->plot_XY_position[0][x_axis_id] == nlon - 1) below.
    //
    // Y is different, and this is the real finding: plotXY() clamps
    // data_y to size-1 FIRST (view.cc:969), THEN inverts it for screen-vs-
    // data Y orientation (view.cc:976-977, since options.invert_physical
    // defaults false) via `data_y = y_size - data_y - 1`. Clamping to
    // size-1 and then inverting that lands back on 0, not size-1 -- an
    // off-the-bottom click clamps to the FIRST data row, not the last,
    // under the default (non-inverted) orientation. Pinned as-is; not a
    // bug to fix in a coverage-only phase.
    CHECK(view->plot_XY_position[0][x_axis_id] == nlon - 1);
    CHECK(view->plot_XY_position[0][y_axis_id] == 0);
}

TEST_CASE("plotXYFmtXVal: formats a timelike axis via fmt_time and a plain axis with %g") {
    SessionFixture fx;
    NcFixture nc;
    select_xy_variable(nc, "xyplot_fmt", 4, 2, 2);
    REQUIRE(view != nullptr);
    REQUIRE(options.t_conv); // reset_session_defaults() turns this on

    char buf[128];
    view->plotXYFmtXVal(2.0f, 0, buf, sizeof(buf)); // dim 0 == "time", timelike
    // A plain "%g" formatting of 2.0 would just be "2" -- fmt_time()
    // instead produces a calendar date, so the two must differ.
    CHECK(std::string(buf) != "2");

    char buf2[128];
    view->plotXYFmtXVal(1.0f, 1, buf2, sizeof(buf2)); // dim 1 == "lat", not timelike
    CHECK(std::string(buf2) == "1");
}

TEST_CASE("view_report_position_vals: reads back plot_XY_dim[] from the last plot and sets the DataValue label") {
    SessionFixture fx;
    NcFixture nc;
    select_xy_variable(nc, "xyplot_report_pos", 4, 2, 2);
    char label[] = "time";
    view->setXYPlotAxis(label);
    g_query_pointer_x = options.blowup / 2;
    g_query_pointer_y = options.blowup / 2;
    g_app.controller.plotXY();
    REQUIRE(g_last_xy_dimindex == 0); // confirms the plot ran, so plot_XY_dim[0] is now set
    g_recorded_calls.clear();

    // dim 0 ("time") is timelike and options.t_conv is on, so this must
    // format xval via fmt_time() rather than a plain "%g" -- pin the
    // label's shape, not fmt_time()'s exact output (that's test_time_fmt.cc's
    // job), the same way plotXYFmtXVal's test above does.
    view_report_position_vals(2.0f, 42.0f, 0);

    bool found = false;
    for (const auto &call : g_recorded_calls) {
        if (call.rfind("in_set_label:Current: x=", 0) == 0) {
            found = true;
            CHECK(call.find("y=42") != std::string::npos);
            CHECK(call.find("x=2,") == std::string::npos); // NOT the plain-%g form
        }
    }
    CHECK(found);
}

TEST_CASE("View::plotXYSc: an over-long NC_CHAR coordinate on a non-plotted axis "
          "does not overflow the legend's fixed-size buffer (Phase 12g)") {
    // Dataset::dimValue()'s documented contract (dataset.cc) requires the
    // caller's return_val_char buffer to be at least 1024 bytes.
    // plotXYSc()'s temp_string was only 128 before Phase 12g -- a CF-legal
    // 2-D NC_CHAR coordinate variable (e.g. a station name) longer than
    // that overflows it the moment that axis shows up in the plot legend.
    // This test only proves the fix doesn't crash under ASan; it does NOT
    // independently reconstruct the pre-fix overflow here (that was
    // confirmed once, by hand, by temporarily reverting the buffer size
    // and rerunning this exact test under a scratch ASan build -- see the
    // Phase 12g commit message).
    SessionFixture fx;
    const int nt = 3, nlat = 2, nlon = 2;
    const int strlen_size = 200; // comfortably over the old 128-byte buffer
    std::string path = make_char_station_file(nt, nlat, nlon, strlen_size);

    Stringlist *files = nullptr;
    stringlist_add_string(&files, path.c_str());
    determine_file_type(files);
    stringlist_delete_entire_list(files);
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    int fid = netcdf_fi_initialize(const_cast<char *>(path.c_str()));
    g_dataset.addVariable("data", fid, path.c_str());
    in_variable_selected("data");
    REQUIRE(view != nullptr);

    char label[] = "time";
    view->setXYPlotAxis(label);
    REQUIRE(view->plot_XY_axis == 1); // "time" is dim 1 of {station, time, lat, lon}

    size_t start[4] = {0, 0, 0, 0}, count[4] = {1, (size_t)nt, 1, 1};
    view->plotXYSc(start, count);

    REQUIRE(recorded("in_popup_XY_graph"));
    // The station value made it into the legend intact -- not just "didn't
    // crash" but "read the right, (almost) full-length data". Only
    // strlen_size-1 'A's, not strlen_size: netcdf_dim_value()'s read loop
    // reads all strlen_size characters (none is NUL, so nothing stops it
    // early), then unconditionally force-NULs the LAST one to guarantee a
    // terminated C string -- pre-existing behavior this test isn't
    // changing, so the last 'A' this fixture wrote is always sacrificed.
    CHECK(g_last_xy_legend.find(std::string(strlen_size - 1, 'A')) != std::string::npos);

    std::remove(path.c_str());
}
