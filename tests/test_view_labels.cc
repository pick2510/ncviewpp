// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for view.cc's UI-label cluster --
// View::redrawDimensionInfo()/showCurrentDimValues()/labelDimensions()
// (~300 lines combined) and View::constructScalarCoordStr() (97 lines) --
// all at zero test coverage before this file. Part of round 4's
// reassessment, see the plan's "Reassess here, round 4" section.
//
// Extends RecordingViewerUi's in_indicate_active_dim()/in_set_cur_dim_value()
// to record their arguments (previously just the bare call name), the same
// "capture what actually left the function" pattern used throughout this
// plan (do_print.cc's in_print(), plotXYSc()'s in_popup_XY_graph() above).
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
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

void select_labels_variable(NcFixture &nc, const char *var_name, int nt, int nlat, int nlon) {
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

int count_matching(const char *prefix) {
    int n = 0;
    for (const auto &call : g_recorded_calls)
        if (call.rfind(prefix, 0) == 0)
            n++;
    return n;
}

// Builds a (time,lat,lon) file whose data var carries a "coordinates"
// attribute naming a true scalar (0-D) coordinate variable -- the CF
// pattern handle_dim_mapping_scalar() (var_metadata.cc) recognizes.
//
// Finding while writing this fixture: constructScalarCoordStr() is only
// ever reached through its one caller, View::scanToPlace(), which itself
// returns immediately whenever scan_axis_id==-1 (view.cc:436-437) -- and
// with exactly 2 dims, BOTH are consumed as the X/Y image axes, leaving
// no scan axis at all. So its "displaying_along_time_dim" branch
// (view.cc:1863, "dim index 0 is being used as an X or Y image axis")
// can only fire on a var with 3+ dims where dim 0 is NOT the default scan
// axis -- which default axis assignment never produces (dim 0 is always
// the scan axis once there's a genuine one). Reaching that branch would
// need a manual axis reassignment (setAxis()) on top of everything else
// here; out of scope for this coverage pass. This fixture instead uses a
// normal 3-D var (time is the scan axis, as usual), which exercises the
// two branches that normal navigation actually reaches: plain and
// timelike scalar coordinates.
std::string make_scalar_coord_piece(const char *data_var, const char *coord_var,
                                     int nt, int nlat, int nlon, float coord_value,
                                     const char *coord_units) {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_scalarcoord_XXXXXX").string();
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

    int var_time, var_lat, var_lon, var_coord, var_data;
    REQUIRE(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_time, &var_time) == NC_NOERR);
    std::string tunits = "days since 2000-01-01";
    REQUIRE(nc_put_att_text(ncid, var_time, "units", tunits.size(), tunits.c_str()) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, "lat", NC_FLOAT, 1, &dim_lat, &var_lat) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, "lon", NC_FLOAT, 1, &dim_lon, &var_lon) == NC_NOERR);

    REQUIRE(nc_def_var(ncid, coord_var, NC_FLOAT, 0, nullptr, &var_coord) == NC_NOERR); // true scalar: 0 dims
    if (coord_units && coord_units[0])
        REQUIRE(nc_put_att_text(ncid, var_coord, "units", strlen(coord_units), coord_units) == NC_NOERR);

    int dims[3] = {dim_time, dim_lat, dim_lon};
    REQUIRE(nc_def_var(ncid, data_var, NC_FLOAT, 3, dims, &var_data) == NC_NOERR);
    std::string coords_att = coord_var;
    REQUIRE(nc_put_att_text(ncid, var_data, "coordinates", coords_att.size(), coords_att.c_str()) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);

    std::vector<double> tvals(nt);
    for (int t = 0; t < nt; t++) tvals[t] = t;
    REQUIRE(nc_put_var_double(ncid, var_time, tvals.data()) == NC_NOERR);
    std::vector<float> latvals(nlat);
    for (int i = 0; i < nlat; i++) latvals[i] = (float)i;
    REQUIRE(nc_put_var_float(ncid, var_lat, latvals.data()) == NC_NOERR);
    std::vector<float> lonvals(nlon);
    for (int j = 0; j < nlon; j++) lonvals[j] = (float)j;
    REQUIRE(nc_put_var_float(ncid, var_lon, lonvals.data()) == NC_NOERR);
    REQUIRE(nc_put_var_float(ncid, var_coord, &coord_value) == NC_NOERR);
    std::vector<float> data((size_t)nt * nlat * nlon, 1.0f);
    REQUIRE(nc_put_var_float(ncid, var_data, data.data()) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);
    return path;
}

int open_for_core(const std::string &path) {
    Stringlist *files = nullptr;
    stringlist_add_string(&files, path.c_str());
    determine_file_type(files);
    stringlist_delete_entire_list(files);
    return netcdf_fi_initialize(const_cast<char *>(path.c_str()));
}

} // namespace

TEST_CASE("labelDimensions: indicates the active X/Y/Scan dims by name") {
    SessionFixture fx;
    NcFixture nc;
    select_labels_variable(nc, "label_xyz", 3, 2, 2);
    REQUIRE(view != nullptr);
    REQUIRE(view->x_axis_id == 2);   // lon
    REQUIRE(view->y_axis_id == 1);   // lat
    REQUIRE(view->scan_axis_id == 0); // time
    g_recorded_calls.clear(); // selection itself already ran labelDimensions() once

    view->labelDimensions();

    CHECK(recorded("in_indicate_active_dim:X:lon"));
    CHECK(recorded("in_set_cur_dim_value:lon:-X-"));
    CHECK(recorded("in_indicate_active_dim:Y:lat"));
    CHECK(recorded("in_set_cur_dim_value:lat:-Y-"));
    CHECK(recorded("in_indicate_active_dim:Scan:time"));
    // Only X and Y get the "-X-"/"-Y-" placeholder value -- the scan axis
    // gets its real current value elsewhere (showCurrentDimValues()), not here.
    CHECK(count_matching("in_set_cur_dim_value:time:") == 0);
}

TEST_CASE("showCurrentDimValues: sets the scannable time axis's current value") {
    SessionFixture fx;
    NcFixture nc;
    select_labels_variable(nc, "label_curval", 5, 2, 2);
    REQUIRE(view != nullptr);
    g_app.controller.stepView(2, FRAMES);
    g_recorded_calls.clear();

    view->showCurrentDimValues();

    // "days since 2000-01-01" at day 2 -> a formatted date string via
    // Dataset::dimValue(); pin that SOME value reaches in_set_cur_dim_value
    // for "time" rather than re-deriving fmt_time()'s exact text (that's
    // test_time_fmt.cc's job).
    CHECK(count_matching("in_set_cur_dim_value:time:") == 1);
}

TEST_CASE("redrawDimensionInfo: inits the dim list, fills every dim once, then delegates to showCurrentDimValues/labelDimensions") {
    SessionFixture fx;
    NcFixture nc;
    select_labels_variable(nc, "label_redraw", 3, 2, 2);
    REQUIRE(view != nullptr);
    g_recorded_calls.clear();

    view->redrawDimensionInfo();

    CHECK(recorded("x_init_dim_info"));
    CHECK(count_matching("in_fill_dim_info") == 3); // once per dim: time, lat, lon
    // Delegated calls' own effects (labelDimensions()/showCurrentDimValues()),
    // proving the delegation actually happened rather than just compiling:
    CHECK(recorded("in_indicate_active_dim:X:lon"));
    CHECK(count_matching("in_set_cur_dim_value:time:") == 1);
}

TEST_CASE("constructScalarCoordStr: a plain (non-timelike) scalar coordinate formats as \"name=value units\"") {
    SessionFixture fx;
    std::string path = make_scalar_coord_piece("scalar_plain_var", "height", 3, 2, 2, 5.0f, "m");
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    int fid = open_for_core(path);
    g_dataset.addVariable("scalar_plain_var", fid, path.c_str());
    g_dataset.cacheScalarCoordInfo();
    in_variable_selected("scalar_plain_var");
    REQUIRE(view != nullptr);
    NCVar *var = g_dataset.findVariable("scalar_plain_var");
    REQUIRE(var->scalar_dim_map_info.size() == 1);
    // A normal 3-D (time,lat,lon) var: time is the scan axis (index 0),
    // lat/lon are Y/X -- so displaying_along_time_dim (view.cc:1863,
    // "(x_axis_id==0)||(y_axis_id==0)") is false here, and this hits the
    // plain-value branch (view.cc:1904) rather than the "range" one.
    REQUIRE(view->x_axis_id != 0);
    REQUIRE(view->y_axis_id != 0);

    // constructScalarCoordStr() is private; its sole effect a test can
    // observe is via its one caller, View::scanToPlace() (view.cc:509-510),
    // reached through the public stepView() entry point (delta==0 redraws
    // in place without moving, matching test_view_navigation.cc's own
    // "expose event" case).
    g_recorded_calls.clear();
    g_app.controller.stepView(0, FRAMES);

    // strip_trailing_zeros() (view.cc) leaves one digit after the decimal
    // point rather than fully collapsing to an integer -- "5.0", not "5".
    CHECK(recorded("in_set_label:height=5.0 m"));
    std::remove(path.c_str());
}

TEST_CASE("constructScalarCoordStr: a timelike scalar coordinate formats as a calendar date, not a raw number") {
    // "minutes since ..." units on the scalar coordinate is exactly what
    // udu_utistime() recognizes as timelike (var_metadata.cc:239-242,
    // mirroring handle_time_dim()'s own check for a real dimension) --
    // the WRF "XTIME" pattern this branch exists for.
    SessionFixture fx;
    std::string path = make_scalar_coord_piece("scalar_time_var", "XTIME", 3, 2, 2, 120.0f,
                                                "minutes since 2000-01-01 00:00:00");
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    int fid = open_for_core(path);
    g_dataset.addVariable("scalar_time_var", fid, path.c_str());
    g_dataset.cacheScalarCoordInfo();
    in_variable_selected("scalar_time_var");
    REQUIRE(view != nullptr);
    NCVar *var = g_dataset.findVariable("scalar_time_var");
    REQUIRE(var->scalar_dim_map_info.size() == 1);
    REQUIRE(var->scalar_dim_map_info[0]->timelike == 1);

    g_recorded_calls.clear();
    g_app.controller.stepView(0, FRAMES);

    // 120 minutes after 2000-01-01 00:00 is 2000-01-01 02:00 -- pin the
    // shape (an "in_set_label:XTIME=" prefix, no raw "120 minutes" units
    // string), not fmt_time()'s exact rendering (that's test_time_fmt.cc's
    // job).
    CHECK(recorded("in_set_label:XTIME="));
    CHECK_FALSE(recorded("in_set_label:XTIME=120 minutes"));
    std::remove(path.c_str());
}
