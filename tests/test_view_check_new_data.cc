// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for View::checkNewData() (core/src/view.cc:517-706)
// -- the once-a-second poll that lets ncview follow a file another process
// is actively appending records to. Flagged for a test in Phase 3b's own
// PORTING.md writeup ("the function is still untested... Add
// test_view_check_new_data.cc"), then that test was never actually written
// once the file-descriptor-leak lead 3b was chasing turned out false and
// 3b moved on to do_print.cc's real bug instead. Round 4's reassessment
// re-confirmed the gap independently (a fresh grep, not a memory of 3b)
// before this file closed it.
//
// NcFixture (tests/support/nc_fixture.h) always writes a fixed-size time
// dimension, which can never "grow" -- so, like test_multifile.cc, this
// hand-rolls its own fixture with an NC_UNLIMITED record dimension.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "support/session_fixture.h"
#include "test_udunits_helper.h"

using ncview_test::SessionFixture;

// The global under test, defined in core/src/view.cc.
extern std::unique_ptr<ViewState> &view;

namespace {

constexpr int kNlat = 2, kNlon = 2;

// Appends [start, start+count) new timesteps to an already-created
// growable file, through a SEPARATE ncid from whatever core has open --
// View::checkNewData() re-opens the file itself by path (view.cc:542-543)
// to check its on-disk size, so writing through a second, independent
// ncid here mirrors what an external process actually appending to the
// file would do.
void extend_growable_file(const char *path, const char *var_name, int start, int count) {
    int ncid;
    REQUIRE(nc_open(path, NC_WRITE, &ncid) == NC_NOERR);
    int var_time, var_data;
    REQUIRE(nc_inq_varid(ncid, "time", &var_time) == NC_NOERR);
    REQUIRE(nc_inq_varid(ncid, var_name, &var_data) == NC_NOERR);

    std::vector<double> tvals(count);
    for (int t = 0; t < count; t++) tvals[t] = start + t;
    size_t t_start[1] = {(size_t)start}, t_count[1] = {(size_t)count};
    REQUIRE(nc_put_vara_double(ncid, var_time, t_start, t_count, tvals.data()) == NC_NOERR);

    std::vector<float> data((size_t)count * kNlat * kNlon);
    for (int t = 0; t < count; t++)
        for (int i = 0; i < kNlat; i++)
            for (int j = 0; j < kNlon; j++)
                data[(size_t)t * kNlat * kNlon + i * kNlon + j] =
                    (float)((start + t) * kNlat * kNlon + i * kNlon + j);
    size_t d_start[3] = {(size_t)start, 0, 0}, d_count[3] = {(size_t)count, kNlat, kNlon};
    REQUIRE(nc_put_vara_float(ncid, var_data, d_start, d_count, data.data()) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);
}

// Creates a (time,lat,lon) file with an UNLIMITED time dimension and nt0
// initial timesteps -- required for checkNewData() to have anything to
// detect: a fixed-size "time" dim can never grow.
std::string make_growable_file(const char *var_name, int nt0) {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_growable_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_time, dim_lat, dim_lon;
    REQUIRE(nc_def_dim(ncid, "time", NC_UNLIMITED, &dim_time) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lat", kNlat, &dim_lat) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lon", kNlon, &dim_lon) == NC_NOERR);

    int var_time, var_lat, var_lon, var_data;
    REQUIRE(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_time, &var_time) == NC_NOERR);
    std::string units = "days since 2000-01-01";
    REQUIRE(nc_put_att_text(ncid, var_time, "units", units.size(), units.c_str()) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, "lat", NC_FLOAT, 1, &dim_lat, &var_lat) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, "lon", NC_FLOAT, 1, &dim_lon, &var_lon) == NC_NOERR);
    int dims[3] = {dim_time, dim_lat, dim_lon};
    REQUIRE(nc_def_var(ncid, var_name, NC_FLOAT, 3, dims, &var_data) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);

    std::vector<float> latvals(kNlat, 0.0f), lonvals(kNlon, 0.0f);
    for (int i = 0; i < kNlat; i++) latvals[i] = (float)i;
    for (int j = 0; j < kNlon; j++) lonvals[j] = (float)j;
    REQUIRE(nc_put_var_float(ncid, var_lat, latvals.data()) == NC_NOERR);
    REQUIRE(nc_put_var_float(ncid, var_lon, lonvals.data()) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);

    if (nt0 > 0)
        extend_growable_file(path.c_str(), var_name, 0, nt0);
    return path;
}

int open_for_core(const std::string &path) {
    Stringlist *files = nullptr;
    stringlist_add_string(&files, path.c_str());
    determine_file_type(files);
    stringlist_delete_entire_list(files);
    return netcdf_fi_initialize(const_cast<char *>(path.c_str()));
}

// Builds a growable file, opens it through core the normal way, and
// selects it into the View. Returns the NCVar* so tests can inspect
// size/files directly.
NCVar *select_growable_variable(const std::string &path, const char *var_name) {
    ensure_ncview_misc_initialized();
    options.blowup_default_size = 300;
    int fid = open_for_core(path);
    g_dataset.addVariable(var_name, fid, path.c_str());
    // Production (ncview.cc:792) calls this once, after all files are
    // added and before any variable is selected -- it's what populates
    // timestep_2_fdb, which checkNewData()'s growth path (view.cc:633-637)
    // extends incrementally rather than rebuilding.
    g_dataset.cacheScalarCoordInfo();
    in_variable_selected(var_name);
    return g_dataset.findVariable(var_name);
}

} // namespace

TEST_CASE("checkNewData: no growth on disk re-arms the 1-second timer and leaves the variable unchanged") {
    SessionFixture fx;
    std::string path = make_growable_file("check_no_growth", 3);
    NCVar *var = select_growable_variable(path, "check_no_growth");
    REQUIRE(view != nullptr);
    REQUIRE(view->scan_axis_id == 0); // the poll only runs when scanning the time (record) axis
    REQUIRE(var->size[0] == 3);

    view->checkNewData(0);

    CHECK(var->size[0] == 3); // unchanged: nothing was appended on disk
    CHECK(timerIsArmed());
    CHECK(timerDelayMs() == 1000);

    std::remove(path.c_str());
}

TEST_CASE("checkNewData: detects file growth and extends the variable's size and per-file var_size") {
    SessionFixture fx;
    std::string path = make_growable_file("check_growth", 3);
    NCVar *var = select_growable_variable(path, "check_growth");
    REQUIRE(view != nullptr);
    REQUIRE(var->size[0] == 3);
    REQUIRE(var->files.back()->var_size[0] == 3);
    size_t timestep_2_fdb_before = var->timestep_2_fdb.size();

    extend_growable_file(path.c_str(), "check_growth", 3, 2);
    view->checkNewData(0);

    CHECK(var->size[0] == 5);
    CHECK(var->files.back()->var_size[0] == 5);
    // The 2 newly-appended timesteps must resolve through timestep_2_fdb
    // exactly like the original 3 did -- checked directly rather than
    // trusting a read to merely not crash.
    CHECK(var->timestep_2_fdb.size() == timestep_2_fdb_before + 2);
    CHECK(var->timestep_2_fdb[3] == var->files.back().get());
    CHECK(var->timestep_2_fdb[4] == var->files.back().get());

    std::remove(path.c_str());
}

TEST_CASE("checkNewData: growth moves the current frame forward by exactly the number of new timesteps") {
    SessionFixture fx;
    std::string path = make_growable_file("check_growth_frame", 3);
    NCVar *var = select_growable_variable(path, "check_growth_frame");
    REQUIRE(view != nullptr);
    size_t frame_before = view->var_place[view->scan_axis_id];
    REQUIRE(frame_before == 0); // in_variable_selected() starts at frame 0

    extend_growable_file(path.c_str(), "check_growth_frame", 3, 2);
    view->checkNewData(0);

    // checkNewData()'s last line is g_app.controller.stepView(dt, FRAMES)
    // -- dt==2 new timesteps here -- which is what actually advances the
    // display to show the newly-arrived data.
    CHECK(view->var_place[view->scan_axis_id] == frame_before + 2);
    (void)var;

    std::remove(path.c_str());
}

TEST_CASE("checkNewData: sets an informative title label mentioning the new frame") {
    SessionFixture fx;
    std::string path = make_growable_file("check_growth_label", 3);
    select_growable_variable(path, "check_growth_label");
    REQUIRE(view != nullptr);
    g_recorded_calls.clear();

    extend_growable_file(path.c_str(), "check_growth_label", 3, 1);
    view->checkNewData(0);

    bool found_new_frame_label = false;
    for (const auto &call : g_recorded_calls)
        if (call.rfind("in_set_label:New frame found", 0) == 0)
            found_new_frame_label = true;
    CHECK(found_new_frame_label);

    std::remove(path.c_str());
}

TEST_CASE("checkNewData: repeated growth calls accumulate frame-rate history, capped at NFRAMES_RECORD") {
    // The n_new_frame_times/new_frame_times[]/new_frame_nframes[] history
    // (file-scope statics in view.cc) is otherwise untested. This
    // exercises the shift-left-when-full branch (view.cc:564-570) by
    // growing the file more times than NFRAMES_RECORD, which would
    // index out of bounds if that branch were ever wrong.
    SessionFixture fx;
    std::string path = make_growable_file("check_growth_history", 1);
    select_growable_variable(path, "check_growth_history");
    REQUIRE(view != nullptr);

    // NFRAMES_RECORD's exact value isn't part of this test's public
    // contract -- looping well past any plausible value is what matters,
    // not matching it exactly.
    for (int i = 0; i < 30; i++) {
        extend_growable_file(path.c_str(), "check_growth_history", 1 + i, 1);
        view->checkNewData(0);
    }

    NCVar *var = g_dataset.findVariable("check_growth_history");
    CHECK(var->size[0] == 31);

    std::remove(path.c_str());
}

TEST_CASE("checkNewData: a file that vanishes between polls reports an error instead of exit()ing (Phase 12d)"
        * doctest::skip(
#ifdef _WIN32
            true
#else
            false
#endif
        )) {
    // Regression test: checkNewData() used to reopen the file via
    // netcdf_fi_initialize(), which exit()s the whole process if the
    // reopen fails -- so a file deleted or replaced while ncview was
    // watching it for growth killed the entire application, not just
    // this one poll. Fixed to use NetCDFFile::open()'s testable
    // std::optional failure instead (Phase 6's open() primitive) and
    // report via in_error() + stop watching, rather than crash.
    //
    // Skipped on Windows: this test's premise is POSIX unlink-while-open
    // semantics -- std::remove() on the still-open file below is expected
    // to succeed (the underlying inode survives until the last open fd
    // closes), leaving nothing at that *path* for checkNewData()'s reopen
    // to find. Windows' file-sharing rules deny deleting a file that any
    // handle holds open without FILE_SHARE_DELETE, which the netCDF/HDF5
    // backend doesn't request -- so std::remove() itself fails there with
    // no ncview code involved. Confirmed via CI (Windows job,
    // MSYS2/MinGW64): this REQUIRE was the one and only failure on that
    // platform across 5 phases (12d-12h) before anyone checked CI status.
    SessionFixture fx;
    std::string path = make_growable_file("check_vanished", 3);
    NCVar *var = select_growable_variable(path, "check_vanished");
    REQUIRE(view != nullptr);
    REQUIRE(var->size[0] == 3);

    // Remove the file out from under the already-open View -- the
    // tracked NetCDFFile* (opened when the variable was selected) stays
    // a valid, already-open fileid on POSIX (the inode survives until
    // every fd referencing it closes), but checkNewData()'s own reopen
    // by *path* now has nothing to open.
    REQUIRE(std::remove(path.c_str()) == 0);

    g_recorded_calls.clear();
    view->checkNewData(0); // must not exit() the test binary

    // The variable's size is untouched -- checkNewData() bailed out
    // before touching anything that depends on the failed reopen.
    CHECK(var->size[0] == 3);

    bool reported_error = false;
    for (const auto &call : g_recorded_calls)
        if (call == "in_dialog") // in_error() forwards to in_dialog() -- see stub_interface.cc
            reported_error = true;
    CHECK(reported_error);

    // Degrades gracefully rather than nagging every second: does NOT
    // re-arm the growth-poll timer the way the "no growth" path does.
    CHECK_FALSE(timerIsArmed());
}
