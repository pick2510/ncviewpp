// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for the variable-list machinery: Dataset::
// addVariable() (and the internal new_fdblist() helper it calls) and
// Dataset::findVariable() (core/src/dataset.cc), plus is_scannable() and
// n_vars_in_list() (core/src/util.cc). This is the code Phase 5 of
// modernization.md replaces wholesale (NCVar/
// FDBlist's void*/AnyPtr-based intrusive linked lists become
// std::vector<std::unique_ptr<...>>), and it had no test
// before this file -- test_file_netcdf.cc only exercises the netcdf_*()
// dispatch layer below it, never the NCVar-building logic on top.
//
// The one behavior this test exists specifically to pin down: when the
// same variable spans more than one input file (a "virtual" variable, in
// upstream's own terminology -- e.g. one year of monthly data per file),
// var->size (the NCVar-level accumulated size) grows as each file is
// added, but each FDBlist's own var_size stays that file's real size, and
// (more subtly) the NCDim objects in var->dim[] are only ever populated
// from the FIRST file's fill_dim_structs() call (called from Dataset::
// addVariable()) -- dim->size does NOT
// track the accumulated total the way var->size does. Getting this
// relationship wrong in Phase 5's rewrite would be exactly the kind of
// silent, hard-to-notice regression a characterization test is for.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "test_udunits_helper.h"

namespace {

// Writes a (time, lat, lon) file: a "temp" data variable plus time/lat/lon
// coordinate variables, with the time axis starting at time_offset_days and
// spaced 1 day apart -- enough for handle_time_dim() to recognize it as a
// real UDUNITS time axis (see core/src/util.cc:handle_time_dim()).
std::string make_virtual_piece(const char *var_name, int nt, int nlat, int nlon,
                                double time_offset_days) {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_varlist_XXXXXX").string();
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
    for (int i = 0; i < nt; i++) tvals[i] = time_offset_days + i;
    REQUIRE(nc_put_var_double(ncid, var_time, tvals.data()) == NC_NOERR);
    std::vector<float> latvals(nlat, 0.0f), lonvals(nlon, 0.0f);
    for (int i = 0; i < nlat; i++) latvals[i] = (float)i;
    for (int i = 0; i < nlon; i++) lonvals[i] = (float)i;
    REQUIRE(nc_put_var_float(ncid, var_lat, latvals.data()) == NC_NOERR);
    REQUIRE(nc_put_var_float(ncid, var_lon, lonvals.data()) == NC_NOERR);
    std::vector<float> data(nt * nlat * nlon, 1.0f);
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

TEST_CASE("is_scannable: dim 0 (the record dim) is always scannable, others need size>1") {
    NCVar var{};
    var.size = { 1, 1, 4 };

    CHECK(is_scannable(&var, 0) != 0);  // dim 0 is special-cased true regardless of size
    CHECK(is_scannable(&var, 1) == 0);  // size 1, not dim 0 -> not scannable
    CHECK(is_scannable(&var, 2) != 0);  // size 4 -> scannable
}

TEST_CASE("n_vars_in_list: counts entries in the vector") {
    std::vector<std::unique_ptr<NCVar>> empty_list;
    CHECK(n_vars_in_list(empty_list) == 0);

    std::vector<std::unique_ptr<NCVar>> three;
    three.push_back(std::make_unique<NCVar>());
    three.push_back(std::make_unique<NCVar>());
    three.push_back(std::make_unique<NCVar>());
    CHECK(n_vars_in_list(three) == 3);
}

TEST_CASE("add_var_to_list: a variable spanning two files becomes virtual, "
          "accumulates var->size and var->dim[0]->size together, but each "
          "file's own FDBlist keeps its own size") {
    ensure_ncview_misc_initialized();

    // Use a variable name unique to this test case (the global `variables`
    // list persists for the whole test binary's lifetime -- see Dataset::
    // findVariable(), a plain linear scan with no removal API), so this
    // can't collide with any other TEST_CASE's variable.
    const char *var_name = "temp_virtual_test";
    std::string path1 = make_virtual_piece(var_name, 3, 2, 2, 0.0);   // 3 timesteps
    std::string path2 = make_virtual_piece(var_name, 2, 2, 2, 3.0);  // 2 more, contiguous

    int nvars_before = n_vars_in_list(variables);

    int fid1 = open_for_core(path1);
    g_dataset.addVariable(var_name, fid1, path1.c_str());

    NCVar *var = g_dataset.findVariable(var_name);
    REQUIRE(var != nullptr);
    CHECK(var->is_virtual == false); // only one file so far
    CHECK(var->size[0] == 3);        // time
    CHECK(var->n_dims == 3);
    CHECK(n_vars_in_list(variables) == nvars_before + 1); // exactly one new NCVar

    int fid2 = open_for_core(path2);
    g_dataset.addVariable(var_name, fid2, path2.c_str());

    // Re-fetch: Dataset::addVariable() mutates the existing NCVar in place
    // for a variable it already knows about, so `var` is still valid, but
    // re-fetching documents that findVariable() finds the same, not a new,
    // node.
    NCVar *var2 = g_dataset.findVariable(var_name);
    CHECK(var2 == var);
    CHECK(var->is_virtual == true);
    CHECK(var->size[0] == 5); // 3 + 2, accumulated across both files
    CHECK(n_vars_in_list(variables) == nvars_before + 1); // still exactly one NCVar

    // The two FDBlist entries live in var->files, each keeping its OWN
    // file's size, not the accumulated total.
    REQUIRE(var->files.size() == 2);
    FDBlist *f0 = var->files[0].get();
    REQUIRE(f0 != nullptr);
    CHECK(f0->index == 0);
    CHECK(f0->var_size[0] == 3);

    FDBlist *f1 = var->files[1].get();
    REQUIRE(f1 != nullptr);
    CHECK(f1->index == 1);
    CHECK(f1->var_size[0] == 2);
    CHECK(f1 == var->files.back().get());

    // var->dim[] is only ever populated from the FIRST file's
    // fill_dim_structs() call (see Dataset::addVariable()'s "already
    // exists" branch -- it never re-derives dim structs for later files),
    // but that branch keeps dim->size in sync with the same accumulation
    // it applies to var->size[0] (see the "kept in sync" comment there):
    // a real macOS bug report traced the "time" dimension row's slider
    // being stuck unresponsive, while its prev/next buttons worked fine,
    // to exactly this field going stale for a multi-file (e.g.
    // one-timestep-per-file, WRF-style) virtual variable -- MainWindow::
    // fillDimInfo() sets the slider's bounds from dim->size, while the
    // buttons read var->size[0] directly, so a stale dim->size silently
    // froze only the slider.
    REQUIRE(var->dim[0] != nullptr);
    CHECK(var->dim[0]->name == "time");
    CHECK(var->dim[0]->size == 5);
    CHECK(var->dim[0]->timelike == 1); // handle_time_dim() recognized the udunits time axis

    // Do NOT netcdf_fi_close(fid1/fid2) here: Dataset::addVariable() routes
    // every fileid through trackFile() (OOP_redesign Step 5),
    // which took ownership of both fds and will close them itself when the
    // Dataset is destroyed (for the global g_dataset, at process exit) --
    // closing them again here would be a double-close of an already-closed
    // fileid.
    std::remove(path1.c_str());
    std::remove(path2.c_str());
}
