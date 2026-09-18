// Copyright (C) 2026 Dominik Strebel
//
// Tests for do_print()/build_print_info() (core/src/do_print.cc) --
// "refine the architecture" plan, Phase 4a. build_print_info() is 128
// lines with roughly 30 unguarded view-> dereferences and had zero
// direct tests before this file; the no-variable-selected path (where
// dereferencing view-> would crash) is Phase 3b's fix, already covered
// by test_view_null_guards.cc's do_print TEST_CASE -- this file only
// exercises the "variable selected" path build_print_info() actually
// runs on.
//
// build_print_info() is static (file-local to do_print.cc), so it can't
// be called directly from this TU -- every test here goes through the
// public do_print() entry point (ViewerController::print() calls it) and
// inspects the result via RecordingViewerUi::in_print()'s captured
// PrintInfo/PrintOptions (g_last_print_info/g_last_print_options,
// stub_interface.cc -- extended for this phase; it used to discard both
// arguments).
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
#include "support/session_fixture.h"
#include "test_udunits_helper.h"

using ncview_test::NcFixture;
using ncview_test::SessionFixture;

// The global under test, defined in core/src/view.cc.
extern std::unique_ptr<ViewState> &view;

namespace {

// Loads and selects a (time, lat, lon) variable with no units/long_name
// attributes on anything -- NcFixture doesn't set any, so this is also
// the fixture for build_print_info()'s empty-string fallback paths
// (main_long_name falls back to the variable's own name; x_units/y_units/
// main_units all read back as "").
void select_print_variable(NcFixture &nc, const char *var_name, int nt = 3) {
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    print_init();
    nc.dim("time", nt).dim("lat", 2).dim("lon", 2)
      .timeAxis("time", "days since 2000-01-01")
      .coord("lat").coord("lon")
      .var(var_name, {"time", "lat", "lon"});
    int fid = nc.openForCore();
    g_dataset.addVariable(var_name, fid, nc.path().c_str());
    in_variable_selected(var_name);
}

// Hand-rolled (not NcFixture -- it has no attribute-setting API) file
// with a "long_name"/"units" pair on the data variable, so the non-empty
// title path (main_long_name + " (" + main_units + ")") is exercised too,
// not just the fallback.
std::string make_annotated_file(const char *var_name) {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_print_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_time, dim_lat, dim_lon;
    REQUIRE(nc_def_dim(ncid, "time", 2, &dim_time) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lat", 2, &dim_lat) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lon", 2, &dim_lon) == NC_NOERR);

    int dims[3] = {dim_time, dim_lat, dim_lon};
    int varid;
    REQUIRE(nc_def_var(ncid, var_name, NC_FLOAT, 3, dims, &varid) == NC_NOERR);
    const char *long_name = "Annotated Temperature";
    const char *units = "kelvin";
    REQUIRE(nc_put_att_text(ncid, varid, "long_name", strlen(long_name), long_name) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, varid, "units", strlen(units), units) == NC_NOERR);

    int time_varid;
    REQUIRE(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_time, &time_varid) == NC_NOERR);
    const char *time_units = "days since 2000-01-01";
    REQUIRE(nc_put_att_text(ncid, time_varid, "units", strlen(time_units), time_units) == NC_NOERR);

    REQUIRE(nc_enddef(ncid) == NC_NOERR);

    double time_vals[2] = {0.0, 1.0};
    REQUIRE(nc_put_var_double(ncid, time_varid, time_vals) == NC_NOERR);
    float data[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    REQUIRE(nc_put_var_float(ncid, varid, data) == NC_NOERR);

    REQUIRE(nc_close(ncid) == NC_NOERR);
    return path;
}

} // namespace

TEST_CASE("do_print: with a variable selected, printer_options and in_print are both called") {
    SessionFixture fx;
    NcFixture nc;
    select_print_variable(nc, "print_basic_var");

    do_print();

    bool saw_printer_options = false, saw_in_print = false;
    for (const auto &s : g_recorded_calls) {
        if (s == "printer_options") saw_printer_options = true;
        if (s == "in_print") saw_in_print = true;
    }
    CHECK(saw_printer_options);
    CHECK(saw_in_print);
    REQUIRE(g_have_last_print_info);
}

TEST_CASE("do_print: cancelling the printer_options dialog skips in_print entirely") {
    SessionFixture fx;
    NcFixture nc;
    select_print_variable(nc, "print_cancel_var");

    g_printer_options_response = Message::Cancel;
    do_print();

    CHECK_FALSE(g_have_last_print_info);
    for (const auto &s : g_recorded_calls)
        CHECK(s != "in_print");
}

TEST_CASE("build_print_info: title falls back to the variable's own name with no long_name/units") {
    SessionFixture fx;
    NcFixture nc;
    select_print_variable(nc, "print_fallback_var");

    do_print();

    REQUIRE(g_have_last_print_info);
    CHECK(g_last_print_info.title == "print_fallback_var");
}

TEST_CASE("build_print_info: title uses long_name + units when present") {
    SessionFixture fx;
    std::string path = make_annotated_file("print_annotated_var");
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    print_init();

    Stringlist *files = nullptr;
    stringlist_add_string(&files, path.c_str());
    determine_file_type(files);
    stringlist_delete_entire_list(files);
    int fid = netcdf_fi_initialize(const_cast<char *>(path.c_str()));
    g_dataset.addVariable("print_annotated_var", fid, path.c_str());
    in_variable_selected("print_annotated_var");

    do_print();

    REQUIRE(g_have_last_print_info);
    CHECK(g_last_print_info.title == "Annotated Temperature (kelvin)");
    std::remove(path.c_str());
}

TEST_CASE("build_print_info: printopts.include_* flags independently gate each PrintInfo field") {
    SessionFixture fx;
    NcFixture nc;
    select_print_variable(nc, "print_flags_var");

    SUBCASE("include_title off leaves title empty") {
        g_printer_options_override = [](PrintOptions &po) { po.include_title = false; };
        do_print();
        REQUIRE(g_have_last_print_info);
        CHECK(g_last_print_info.title.empty());
    }
    SUBCASE("include_axis_labels off leaves both axis labels empty") {
        g_printer_options_override = [](PrintOptions &po) { po.include_axis_labels = false; };
        do_print();
        REQUIRE(g_have_last_print_info);
        CHECK(g_last_print_info.x_axis_label.empty());
        CHECK(g_last_print_info.y_axis_label.empty());
    }
    SUBCASE("include_extra_info off leaves extra_info empty") {
        g_printer_options_override = [](PrintOptions &po) { po.include_extra_info = false; };
        do_print();
        REQUIRE(g_have_last_print_info);
        CHECK(g_last_print_info.extra_info.empty());
    }
    SUBCASE("include_id off leaves id_stamp empty") {
        g_printer_options_override = [](PrintOptions &po) { po.include_id = false; };
        do_print();
        REQUIRE(g_have_last_print_info);
        CHECK(g_last_print_info.id_stamp.empty());
    }
    SUBCASE("all flags on (print_init()'s defaults) populates every field") {
        do_print();
        REQUIRE(g_have_last_print_info);
        CHECK_FALSE(g_last_print_info.title.empty());
        CHECK_FALSE(g_last_print_info.x_axis_label.empty());
        CHECK_FALSE(g_last_print_info.y_axis_label.empty());
        CHECK_FALSE(g_last_print_info.extra_info.empty());
        CHECK_FALSE(g_last_print_info.id_stamp.empty());
    }
}

TEST_CASE("build_print_info: axis labels fall back to the dimension's own name with no long_name") {
    SessionFixture fx;
    NcFixture nc;
    select_print_variable(nc, "print_axis_var");

    do_print();

    REQUIRE(g_have_last_print_info);
    // netcdf_dim_longname() falls back to the dim name itself when there's
    // no "long_name" attribute (file_netcdf.cc) -- x/y axis order matches
    // NcFixture's var() dim order (time, lat, lon), and View::create()'s
    // default axis assignment puts the last two dims on X/Y.
    CHECK(g_last_print_info.x_axis_label == "lon");
    CHECK(g_last_print_info.y_axis_label == "lat");
}

TEST_CASE("build_print_info: a multi-file series reaches the 'Name of file' extra-info line") {
    SessionFixture fx;
    NcFixture nc1, nc2;
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    print_init();

    const char *var_name = "print_series_var";
    nc1.dim("time", 2).dim("lat", 2).dim("lon", 2)
       .timeAxis("time", "days since 2000-01-01")
       .coord("lat").coord("lon")
       .var(var_name, {"time", "lat", "lon"});
    int fid1 = nc1.openForCore();
    g_dataset.addVariable(var_name, fid1, nc1.path().c_str());

    nc2.dim("time", 2).dim("lat", 2).dim("lon", 2)
       .timeAxis("time", "days since 2000-01-01")
       .coord("lat").coord("lon")
       .var(var_name, {"time", "lat", "lon"});
    int fid2 = nc2.openForCore();
    g_dataset.addVariable(var_name, fid2, nc2.path().c_str());

    in_variable_selected(var_name);

    do_print();

    REQUIRE(g_have_last_print_info);
    bool saw_file_line = false;
    for (const auto &line : g_last_print_info.extra_info)
        if (line.find("File ") != std::string::npos) {
            saw_file_line = true;
            // fdb->filename (virt_to_actual_place()'s resolved file for
            // the current virtual place) must be one of the two real
            // paths, not an empty/garbage string from a dangling FDBlist*.
            CHECK((line.find(nc1.path()) != std::string::npos ||
                   line.find(nc2.path()) != std::string::npos));
        }
    CHECK(saw_file_line);
}
