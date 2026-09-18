// Copyright (C) 2026 Dominik Strebel
//
// Tests for SessionFixture itself (tests/support/session_fixture.h) --
// "refine the architecture" plan, Phase 0a. These exist to prove the
// isolation guarantee other tests will come to depend on: that a
// SessionFixture on the stack gives its TEST_CASE a Dataset/View/
// FrameCache/UI-recording state indistinguishable from a freshly started
// process, regardless of what an earlier TEST_CASE (run before or after
// it, in either doctest execution order) left behind.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "support/session_fixture.h"
#include "test_udunits_helper.h"

using ncview_test::SessionFixture;

namespace {

// A plain 2-D (lat, lon) variable -- matches
// test_controller_characterization.cc's make_sample_file() shape (minus
// the time axis, unneeded here) so set_scan_variable()'s ordinary 2-D
// path runs, not the effective_dimensionality==1 XY-plot shortcut.
std::string make_one_var_file(const char *var_name) {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_sessfix_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_lat, dim_lon;
    REQUIRE(nc_def_dim(ncid, "lat", 2, &dim_lat) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lon", 2, &dim_lon) == NC_NOERR);
    int var_data;
    int dims[2] = {dim_lat, dim_lon};
    REQUIRE(nc_def_var(ncid, var_name, NC_FLOAT, 2, dims, &var_data) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);
    std::vector<float> vals(4, 1.0f);
    REQUIRE(nc_put_var_float(ncid, var_data, vals.data()) == NC_NOERR);
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

// Loads and selects a fresh variable exactly as
// test_controller_characterization.cc's select_fresh_variable() does --
// this file needs the same preconditions (blowup_default_size set before
// set_scan_variable() runs, misc initialized) to reach view.cc's ordinary
// 2-D path without crashing.
NCVar *load_and_select(const char *var_name, const std::string &path) {
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    int fid = open_for_core(path);
    g_dataset.addVariable(var_name, fid, path.c_str());
    NCVar *var = g_dataset.findVariable(var_name);
    REQUIRE(var != nullptr);
    in_variable_selected(var_name);
    return var;
}

} // namespace

TEST_CASE("SessionFixture: Dataset is empty on entry, even after a prior test added variables") {
    {
        SessionFixture fx;
        std::string path = make_one_var_file("leftover_var");
        int fid = open_for_core(path);
        g_dataset.addVariable("leftover_var", fid, path.c_str());
        CHECK(g_dataset.findVariable("leftover_var") != nullptr);
        std::remove(path.c_str());
        // fx destructs here, resetting g_app.session -- including closing
        // the file just opened, via Dataset's own destructor.
    }

    SessionFixture fx2;
    CHECK(n_vars_in_list(variables) == 0);
    CHECK(g_dataset.findVariable("leftover_var") == nullptr);
}

TEST_CASE("SessionFixture: the SAME variable name can be reused across fixtures without colliding") {
    // Before SessionFixture existed, every test needing a variable had to
    // mint a name unique across the whole binary (see test_varlist.cc's
    // own comment on this). A fresh Dataset per fixture removes that
    // constraint entirely -- this reuses the exact same name twice.
    {
        SessionFixture fx;
        std::string path = make_one_var_file("reused_name");
        int fid = open_for_core(path);
        g_dataset.addVariable("reused_name", fid, path.c_str());
        REQUIRE(g_dataset.findVariable("reused_name") != nullptr);
        std::remove(path.c_str());
    }
    {
        SessionFixture fx;
        CHECK(g_dataset.findVariable("reused_name") == nullptr);
        std::string path = make_one_var_file("reused_name");
        int fid = open_for_core(path);
        g_dataset.addVariable("reused_name", fid, path.c_str());
        CHECK(g_dataset.findVariable("reused_name") != nullptr);
        std::remove(path.c_str());
    }
}

TEST_CASE("SessionFixture: the active view is null on entry, even after a prior test selected one") {
    {
        SessionFixture fx;
        std::string path = make_one_var_file("view_leftover");
        load_and_select("view_leftover", path);
        CHECK(view != nullptr);
        std::remove(path.c_str());
    }

    SessionFixture fx2;
    CHECK(view == nullptr);
}

TEST_CASE("SessionFixture: UI call recording is cleared on entry") {
    {
        SessionFixture fx;
        in_error("something recorded by the previous scope");
        CHECK_FALSE(g_recorded_calls.empty());
    }

    SessionFixture fx2;
    CHECK(g_recorded_calls.empty());
}

TEST_CASE("SessionFixture: scripted dialog responses reset to their default (Message::OK)") {
    {
        SessionFixture fx;
        g_dialog_response = Message::Cancel;
        g_range_response = Message::Cancel;
        g_printer_options_response = Message::Cancel;
        g_set_scan_dims_response = -1;
    }

    SessionFixture fx2;
    CHECK(g_dialog_response == Message::OK);
    CHECK(g_range_response == Message::OK);
    CHECK(g_printer_options_response == Message::OK);
    CHECK(g_set_scan_dims_response == 0);
}

TEST_CASE("SessionFixture: framestore is invalid on entry, even after a prior test filled it") {
    {
        SessionFixture fx;
        std::string path = make_one_var_file("frame_leftover");
        load_and_select("frame_leftover", path);
        REQUIRE(view != nullptr);
        g_app.controller.draw(1, 1);
        std::remove(path.c_str());
    }

    SessionFixture fx2;
    CHECK_FALSE(framestore.valid());
}

TEST_CASE("SessionFixture: an early REQUIRE failure still resets state on unwind") {
    // Simulates a test that fails partway through: the fixture's
    // destructor still runs during stack unwinding, exactly like any
    // other RAII object, so this doesn't need its own try/catch -- it
    // just documents the property for anyone reading the header comment.
    auto leaves_state_dirty_then_throws = [] {
        SessionFixture fx;
        std::string path = make_one_var_file("unwind_var");
        int fid = open_for_core(path);
        g_dataset.addVariable("unwind_var", fid, path.c_str());
        std::remove(path.c_str());
        throw std::runtime_error("simulated mid-test failure");
    };
    CHECK_THROWS_AS(leaves_state_dirty_then_throws(), std::runtime_error);

    SessionFixture fx2;
    CHECK(g_dataset.findVariable("unwind_var") == nullptr);
}
