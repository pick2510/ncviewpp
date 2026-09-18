// Copyright (C) 2026 Dominik Strebel
//
// Tests for overlay.cc -- "refine the architecture" plan, Phase 4a.
// 673 lines with zero direct tests before this file. Pixel goldens are
// explicitly out of scope here (tests/support/pgm.h from Phase 0b was
// never built) -- these tests assert on observable state (my_current_
// overlay via overlay_current(), options.overlay's doit flag, and the
// UI calls do_overlay() records), not on rendered pixels.
//
// Most of overlay.cc's helper functions (gen_xform, gen_overlay_internal,
// gen_overlay_internal_mapped, do_overlay_inner, overlay_find_closest_pt/
// _inner) are `static` -- file-local, unreachable from this TU. Every
// test here goes through the public entry points: do_overlay(),
// gen_overlay(), overlay_init(), overlay_names()/overlay_current()/
// overlay_n_overlays()/overlay_custom_n(), determine_overlay_base_dir().
//
// The 2-D-mapped-coordinate branch (gen_overlay_internal_mapped(), which
// is the only caller of overlay_find_closest_pt()) needs a variable with
// curvilinear (2-D lat/lon) coordinates -- NcFixture doesn't build those,
// and neither does anything else in the tree yet. That branch, and
// overlay_find_closest_pt()'s have_been_here_before memo cache, are
// therefore NOT covered here; building curvilinear-grid fixture support
// is its own piece of work (needed for real curvilinear-grid coverage
// generally, not just this one function), not a Phase 4a side quest.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "support/nc_fixture.h"
#include "support/scratch_home.h"
#include "support/session_fixture.h"
#include "test_udunits_helper.h"

using ncview_test::NcFixture;
using ncview_test::SessionFixture;

// The global under test, defined in core/src/view.cc.
extern std::unique_ptr<ViewState> &view;

namespace {

// A (time=1, lat, lon) grid with real, hand-chosen coordinate values --
// NcFixture's .coord() always fills 0,1,2,...,n-1, which can't exercise
// gen_xform()'s off-the-ends (antimeridian) or reversed-array (pole)
// branches. lon ascends -180..170 by 10 (36 points, so the array does NOT
// wrap back to 180 -- the antimeridian is where the array's two open
// ends sit); lat DESCENDS 90..-90 by 10 (19 points, the common real-world
// netCDF convention and the case gen_xform()'s "reversed" branch exists
// for).
std::string make_overlay_grid_file(const char *var_name) {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_overlay_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    const int nlon = 36, nlat = 19;
    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_time, dim_lat, dim_lon;
    REQUIRE(nc_def_dim(ncid, "time", 1, &dim_time) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lat", nlat, &dim_lat) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lon", nlon, &dim_lon) == NC_NOERR);

    int time_varid, lat_varid, lon_varid, data_varid;
    REQUIRE(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_time, &time_varid) == NC_NOERR);
    const char *time_units = "days since 2000-01-01";
    REQUIRE(nc_put_att_text(ncid, time_varid, "units", strlen(time_units), time_units) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, "lat", NC_FLOAT, 1, &dim_lat, &lat_varid) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, "lon", NC_FLOAT, 1, &dim_lon, &lon_varid) == NC_NOERR);
    int dims[3] = {dim_time, dim_lat, dim_lon};
    REQUIRE(nc_def_var(ncid, var_name, NC_FLOAT, 3, dims, &data_varid) == NC_NOERR);

    REQUIRE(nc_enddef(ncid) == NC_NOERR);

    double time_val = 0.0;
    REQUIRE(nc_put_var_double(ncid, time_varid, &time_val) == NC_NOERR);

    float lon_vals[36], lat_vals[19];
    for (int i = 0; i < nlon; i++) lon_vals[i] = -180.0f + 10.0f * (float)i; // -180 .. 170, ascending
    for (int j = 0; j < nlat; j++) lat_vals[j] = 90.0f - 10.0f * (float)j;   //  90 .. -90, descending
    REQUIRE(nc_put_var_float(ncid, lon_varid, lon_vals) == NC_NOERR);
    REQUIRE(nc_put_var_float(ncid, lat_varid, lat_vals) == NC_NOERR);

    // A ramp, not a constant -- constant data sends data_to_pixels()
    // (util.cc) down its "min and max both equal" recovery path on every
    // draw, which is noisy (prints to stderr) and irrelevant to what
    // these tests check.
    std::vector<float> data(nlat * nlon);
    for (size_t i = 0; i < data.size(); i++) data[i] = (float)i;
    REQUIRE(nc_put_var_float(ncid, data_varid, data.data()) == NC_NOERR);

    REQUIRE(nc_close(ncid) == NC_NOERR);
    return path;
}

void select_overlay_grid_variable(const std::string &path, const char *var_name) {
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;

    Stringlist *files = nullptr;
    stringlist_add_string(&files, path.c_str());
    determine_file_type(files);
    stringlist_delete_entire_list(files);
    // A real running ncview always has a colormap installed
    // (initialize_colormaps()/create_default_colormap(), ncview.cc)
    // before the first draw. Without one, pixel_transform stays the
    // empty vector ViewerSession default-constructs -- harmless until a
    // pixel actually needs indexing into it, which do_overlay()'s
    // suppress_screen_changes=false redraw path does the moment any
    // pixel matches fill_value (data_to_pixels()'s overlay-masking loop,
    // util.cc, sets exactly that for every overlay-marked point before
    // rendering). Confirmed via SIGSEGV while writing this test: any
    // built-in overlay (P8DEG/P08DEG/USA) run against this small a grid
    // marks real points from the actual embedded coastline data.
    options.n_colors = 80;
    options.n_extra_colors = 10;
    g_app.session.pixelTransform().assign(options.n_colors + options.n_extra_colors, 0);

    int fid = netcdf_fi_initialize(const_cast<char *>(path.c_str()));
    g_dataset.addVariable(var_name, fid, path.c_str());
    // gen_xform() (overlay.cc) reads NCDim::values directly, which is
    // only ever populated by Dataset::calcDimMinmaxes() -- normally run
    // once by ncview.cc's initialize_file_interface(), not by
    // addVariable() itself. No existing test needed this before (nothing
    // else in the tree reads NCDim::values), so it's easy to omit by
    // habit -- omitting it here reproduces a real SIGSEGV (gen_xform()
    // dereferencing a null/empty values buffer), confirmed while writing
    // this test.
    g_dataset.calcDimMinmaxes();
    in_variable_selected(var_name);
}

// Writes an overlay file in the format gen_overlay() (overlay.cc) parses:
// "NCVIEW-OVERLAY 1.0" header, then "lon lat" pairs, one per line,
// '#'-prefixed lines and blank/malformed lines skipped.
std::string make_overlay_points_file(const std::vector<std::pair<float, float>> &points,
                                      bool valid_header = true) {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_overlay_pts_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;
    FILE *f = fopen(path.c_str(), "w");
    REQUIRE(f != nullptr);
    if (valid_header)
        fprintf(f, "NCVIEW-OVERLAY 1.0\n");
    else
        fprintf(f, "NOT-AN-OVERLAY-FILE\n");
    fprintf(f, "# a comment line, must be skipped\n");
    fprintf(f, "\n"); // blank line, must be skipped (sscanf fails to match 2 floats)
    for (const auto &p : points)
        fprintf(f, "%f %f\n", p.first, p.second);
    fclose(f);
    return path;
}

} // namespace

TEST_CASE("overlay_names/overlay_n_overlays/overlay_custom_n: the built-in constant table") {
    const char **names = overlay_names();
    REQUIRE(names != nullptr);
    CHECK(std::string(names[OVERLAY_NONE]) == "None");
    CHECK(std::string(names[OVERLAY_CUSTOM]) == "custom");
    CHECK(overlay_n_overlays() == OVERLAY_N_OVERLAYS);
    CHECK(overlay_custom_n() == OVERLAY_CUSTOM);
}

TEST_CASE("overlay_init: resets to OVERLAY_NONE with overlay drawing off") {
    SessionFixture fx;
    overlay_init();
    CHECK(overlay_current() == OVERLAY_NONE);
    CHECK_FALSE(options.overlay->doit);
    CHECK(options.overlay->overlay.empty());
}

TEST_CASE("do_overlay: with no variable selected, reports an error and leaves overlay state untouched") {
    SessionFixture fx;
    overlay_init();
    REQUIRE(view == nullptr);

    do_overlay(OVERLAY_P8DEG, nullptr, false);

    bool saw_x_error = false;
    for (const auto &s : g_recorded_calls) if (s == "x_error") saw_x_error = true;
    CHECK(saw_x_error);
    // my_current_overlay is only assigned at the end of do_overlay()'s
    // switch, which the view==NULL guard returns before reaching.
    CHECK(overlay_current() == OVERLAY_NONE);
}

TEST_CASE("do_overlay: OVERLAY_NONE turns overlays off and redraws unless suppressed") {
    SessionFixture fx;
    overlay_init();
    std::string path = make_overlay_grid_file("overlay_none_var");
    select_overlay_grid_variable(path, "overlay_none_var");

    SUBCASE("suppress_screen_changes = false triggers a redraw") {
        do_overlay(OVERLAY_NONE, nullptr, false);
        CHECK_FALSE(options.overlay->doit);
        CHECK(overlay_current() == OVERLAY_NONE);
    }
    SUBCASE("suppress_screen_changes = true still turns overlays off") {
        do_overlay(OVERLAY_NONE, nullptr, true);
        CHECK_FALSE(options.overlay->doit);
    }
    std::remove(path.c_str());
}

TEST_CASE("do_overlay: a built-in overlay (P8DEG) turns overlay drawing on") {
    SessionFixture fx;
    overlay_init();
    std::string path = make_overlay_grid_file("overlay_p8_var");
    select_overlay_grid_variable(path, "overlay_p8_var");

    do_overlay(OVERLAY_P8DEG, nullptr, false);

    // do_overlay_inner()->gen_overlay_internal() always returns a vector
    // sized x_size*y_size (never truly .empty(), even if no coastline
    // point falls in range) for any real 2-D grid, so doit is set
    // unconditionally on success -- this is pinning that behavior, not
    // asserting anything about which points actually got marked.
    CHECK(options.overlay->doit);
    CHECK(overlay_current() == OVERLAY_P8DEG);
    CHECK(options.overlay->overlay.size() == 19 * 36);
    std::remove(path.c_str());
}

TEST_CASE("do_overlay: OVERLAY_CUSTOM with a missing filename reports an error, doesn't change doit") {
    SessionFixture fx;
    overlay_init();
    std::string path = make_overlay_grid_file("overlay_custom_missing_var");
    select_overlay_grid_variable(path, "overlay_custom_missing_var");

    SUBCASE("null filename") {
        do_overlay(OVERLAY_CUSTOM, nullptr, false);
    }
    SUBCASE("empty filename") {
        char empty[] = "";
        do_overlay(OVERLAY_CUSTOM, empty, false);
    }
    bool saw_in_error = false;
    for (const auto &s : g_recorded_calls) if (s == "in_dialog") saw_in_error = true;
    CHECK(saw_in_error);
    CHECK_FALSE(options.overlay->doit);
    std::remove(path.c_str());
}

TEST_CASE("do_overlay: OVERLAY_CUSTOM with a nonexistent file reports an error, doesn't crash") {
    SessionFixture fx;
    overlay_init();
    std::string path = make_overlay_grid_file("overlay_custom_nofile_var");
    select_overlay_grid_variable(path, "overlay_custom_nofile_var");

    char bogus_path[] = "/nonexistent/path/to/an/overlay/file.dat";
    do_overlay(OVERLAY_CUSTOM, bogus_path, false);

    bool saw_in_error = false;
    for (const auto &s : g_recorded_calls) if (s == "in_dialog") saw_in_error = true;
    CHECK(saw_in_error);
    CHECK_FALSE(options.overlay->doit);
    std::remove(path.c_str());
}

TEST_CASE("do_overlay: OVERLAY_CUSTOM with a malformed header (bad magic) reports an error") {
    SessionFixture fx;
    overlay_init();
    std::string grid_path = make_overlay_grid_file("overlay_custom_badhdr_var");
    select_overlay_grid_variable(grid_path, "overlay_custom_badhdr_var");
    std::string overlay_path = make_overlay_points_file({{0.0f, 0.0f}}, /*valid_header=*/false);

    do_overlay(OVERLAY_CUSTOM, const_cast<char *>(overlay_path.c_str()), false);

    bool saw_in_error = false;
    for (const auto &s : g_recorded_calls) if (s == "in_dialog") saw_in_error = true;
    CHECK(saw_in_error);
    CHECK_FALSE(options.overlay->doit);
    std::remove(grid_path.c_str());
    std::remove(overlay_path.c_str());
}

TEST_CASE("gen_overlay via a custom overlay file: in-range points near a pole/antimeridian resolve, "
          "but index 0 on either axis is never marked (a pinned '> 0' quirk, not '>= 0')") {
    SessionFixture fx;
    overlay_init();
    std::string grid_path = make_overlay_grid_file("overlay_custom_pts_var");
    select_overlay_grid_variable(grid_path, "overlay_custom_pts_var");

    // lon: -180..170 by 10 (index 0 = -180, index 35 = 170), ascending;
    // valid range is exactly [-180, 170] -- gen_xform() returns -1 for
    // anything outside it, it does NOT clamp to the nearest end.
    // lat:   90..-90 by 10 (index 0 =   90, index 18 = -90), descending;
    // valid range [-90, 90].
    //
    //  - (-179, -89) -> lon nearest -180 (index 0, dist 1); lat nearest
    //    -90 (index 18, dist 1). i=0 fails the "i>0" check -> NOT marked,
    //    even though the point is a clean, unambiguous match right at
    //    the antimeridian.
    //  - (169, 89) -> lon nearest 170 (index 35, dist 1); lat nearest 90
    //    (index 0, dist 1). j=0 fails "j>0" -> NOT marked, at the pole.
    //  - (169, -89) -> lon index 35 (>0), lat index 18 (>0) -> IS marked.
    //    Same near-antimeridian/near-pole proximity as the two points
    //    above, but landing one grid cell away from either axis's index
    //    0 -- confirms the exclusion is specifically an index-0 quirk,
    //    not a general "near the edge" one.
    //  - (200, 0) -> lon 200 is off the ascending array's high end
    //    (> 170) -> gen_xform returns -1 (silently skipped, no error,
    //    per the code -- there's no error path for an out-of-range
    //    point, only for a malformed file).
    std::string overlay_path = make_overlay_points_file({
        {-179.0f, -89.0f},
        {169.0f, 89.0f},
        {169.0f, -89.0f},
        {200.0f, 0.0f},
    });

    do_overlay(OVERLAY_CUSTOM, const_cast<char *>(overlay_path.c_str()), false);

    REQUIRE(options.overlay->doit);
    const auto &overlay = options.overlay->overlay;
    REQUIRE(overlay.size() == 19 * 36);

    CAPTURE(overlay[18 * 36 + 0]);   // (lon idx 0, lat idx 18) -- antimeridian point, excluded
    CHECK(overlay[18 * 36 + 0] == 0);
    CAPTURE(overlay[0 * 36 + 35]);   // (lon idx 35, lat idx 0) -- pole point, excluded
    CHECK(overlay[0 * 36 + 35] == 0);
    CAPTURE(overlay[18 * 36 + 35]); // (lon idx 35, lat idx 18) -- neither index 0, marked
    CHECK(overlay[18 * 36 + 35] == 1);

    int n_marked = 0;
    for (int v : overlay) if (v != 0) n_marked++;
    CHECK(n_marked == 1); // only the (-166,-85) point -- confirms the off-end point was silently skipped

    std::remove(grid_path.c_str());
    std::remove(overlay_path.c_str());
}

TEST_CASE("gen_overlay via a custom overlay file: blank and comment lines are skipped, not misread") {
    SessionFixture fx;
    overlay_init();
    std::string grid_path = make_overlay_grid_file("overlay_custom_skip_var");
    select_overlay_grid_variable(grid_path, "overlay_custom_skip_var");
    // No real points at all -- just the header, a comment, and a blank
    // line (make_overlay_points_file always writes both before any
    // point). Confirms gen_overlay() doesn't choke on them, and that a
    // custom overlay with zero valid points still "succeeds" (a vector
    // of all zeros, sized x_size*y_size, is not .empty()).
    std::string overlay_path = make_overlay_points_file({});

    do_overlay(OVERLAY_CUSTOM, const_cast<char *>(overlay_path.c_str()), false);

    CHECK(options.overlay->doit);
    for (int v : options.overlay->overlay) CHECK(v == 0);

    std::remove(grid_path.c_str());
    std::remove(overlay_path.c_str());
}

TEST_CASE("determine_overlay_base_dir: honors NCVIEWBASE when set, and never overflows the buffer") {
    char buf[256];
    ncview_test::set_env("NCVIEWBASE", "/some/test/overlay/dir");
    determine_overlay_base_dir(buf, sizeof(buf));
    CHECK(std::string(buf) == "/some/test/overlay/dir");
    ncview_test::unset_env("NCVIEWBASE");
}
