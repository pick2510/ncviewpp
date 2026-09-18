// Copyright (C) 2026 Dominik Strebel
//
// Tests for the multi-file ("virtual variable") read path -- the feature
// the program exists for, and per the "refine the architecture" plan's
// 2026-09-09 inventory, the largest untested surface in the tree. Every
// existing g_dataset.getData() test (test_nc_fixture.cc) uses a single-file
// variable, so g_dataset.getData()'s is_virtual/count[0]>1 branch that delegates
// to Dataset::getDataIterate() (core/src/dataset.cc, formerly file.cc's
// free-function fi_get_data_iterate() before Phase 6 moved it) had never
// executed; likewise
// virt_to_actual_place()'s (core/src/util.cc) multi-file branch and
// fi_dim_value_convert()'s (core/src/file.cc) cross-file time-unit
// reconciliation.
//
// NcFixture (tests/support/nc_fixture.h) deliberately doesn't cover
// virtual multi-file variables -- its own header comment says so -- so
// this file hand-rolls its fixture the way test_varlist.cc's
// make_virtual_piece() does, extended to carry a distinct per-file data
// offset (so a read spanning a file boundary can tell which file each
// value actually came from) and an optional distinct time-units string
// per file (to exercise fi_dim_value_convert's reconciliation).
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

// Writes a (time, lat, lon) file whose data values are deterministic and
// distinguishable per file: value(t, i, j) = data_offset + t*nlat*nlon +
// i*nlon + j, where t is the LOCAL (within-file) timestep. The time axis
// starts at time_offset_days (in time_units) and is spaced 1 unit apart.
std::string make_series_piece(const char *var_name, int nt, int nlat, int nlon,
                               double time_offset, float data_offset,
                               const char *time_units) {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_multifile_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_time, dim_lat, dim_lon;
    // The time dim must be the unlimited (record) dimension: fi_dim_value_
    // convert()'s cross-file reconciliation only fires when both files'
    // FDBlist::recdim_units are non-empty, which netcdf_fill_aux_data()
    // only ever populates from the record dimension's dimvar units
    // (file_netcdf.cc's netcdf_fi_recdim_id()) -- a same-sized-but-not-
    // unlimited "time" dim leaves recdim_units empty on both files and
    // silently skips the reconciliation this fixture exists to exercise.
    REQUIRE(nc_def_dim(ncid, "time", NC_UNLIMITED, &dim_time) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lat", nlat, &dim_lat) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lon", nlon, &dim_lon) == NC_NOERR);

    int var_time, var_lat, var_lon, var_data;
    REQUIRE(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_time, &var_time) == NC_NOERR);
    std::string units = time_units;
    REQUIRE(nc_put_att_text(ncid, var_time, "units", units.size(), units.c_str()) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, "lat", NC_FLOAT, 1, &dim_lat, &var_lat) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, "lon", NC_FLOAT, 1, &dim_lon, &var_lon) == NC_NOERR);
    int dims[3] = {dim_time, dim_lat, dim_lon};
    REQUIRE(nc_def_var(ncid, var_name, NC_FLOAT, 3, dims, &var_data) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);

    // nc_put_var_*() ("whole variable") infers the shape to write from
    // the dimensions' CURRENT lengths, which for a fresh unlimited
    // dimension is 0 -- it would silently write zero records. Use
    // nc_put_vara_*() with an explicit start/count instead, which is
    // what actually extends the record dimension to nt.
    std::vector<double> tvals(nt);
    for (int t = 0; t < nt; t++) tvals[t] = time_offset + t;
    size_t t_start[1] = {0}, t_count[1] = {(size_t)nt};
    REQUIRE(nc_put_vara_double(ncid, var_time, t_start, t_count, tvals.data()) == NC_NOERR);
    std::vector<float> latvals(nlat, 0.0f), lonvals(nlon, 0.0f);
    for (int i = 0; i < nlat; i++) latvals[i] = (float)i;
    for (int j = 0; j < nlon; j++) lonvals[j] = (float)j;
    REQUIRE(nc_put_var_float(ncid, var_lat, latvals.data()) == NC_NOERR);
    REQUIRE(nc_put_var_float(ncid, var_lon, lonvals.data()) == NC_NOERR);

    std::vector<float> data((size_t)nt * nlat * nlon);
    for (int t = 0; t < nt; t++)
        for (int i = 0; i < nlat; i++)
            for (int j = 0; j < nlon; j++)
                data[(size_t)t * nlat * nlon + i * nlon + j] =
                    data_offset + t * nlat * nlon + i * nlon + j;
    size_t d_start[3] = {0, 0, 0}, d_count[3] = {(size_t)nt, (size_t)nlat, (size_t)nlon};
    REQUIRE(nc_put_vara_float(ncid, var_data, d_start, d_count, data.data()) == NC_NOERR);
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

// Builds a 3-file, 3+2+4=9-timestep series of a (time,lat,lon) variable
// (lat=lon=2, so 4 values/timestep) via the same Dataset::addVariable()
// path core itself uses, with each file's data offset by a distinguishing
// multiple of 1000 so a value's origin file is obvious from its magnitude.
// All three files share the same time units, so this fixture is for the
// Dataset::getDataIterate()/virt_to_actual_place()/cacheScalarCoordInfo()
// tests, not the cross-file-units reconciliation test (which needs its
// own 2-file fixture with deliberately different units).
struct ThreeFileSeries {
    static constexpr int kNlat = 2, kNlon = 2;
    static constexpr int kCounts[3] = {3, 2, 4};
    static constexpr float kDataOffsets[3] = {0.0f, 1000.0f, 2000.0f};
    const char *var_name;
    std::string paths[3];
    NCVar *var = nullptr;

    explicit ThreeFileSeries(const char *name) : var_name(name) {
        double time_offset = 0.0;
        for (int f = 0; f < 3; f++) {
            paths[f] = make_series_piece(var_name, kCounts[f], kNlat, kNlon, time_offset,
                                          kDataOffsets[f], "days since 2000-01-01");
            int fileid = open_for_core(paths[f]);
            g_dataset.addVariable(var_name, fileid, paths[f].c_str());
            time_offset += kCounts[f];
        }
        var = g_dataset.findVariable(var_name);
        REQUIRE(var != nullptr);
        REQUIRE(var->is_virtual == true);
        REQUIRE(var->size[0] == 9); // 3+2+4
        REQUIRE(var->files.size() == 3);
    }
    ~ThreeFileSeries() {
        // Do NOT close the fileids: Dataset::addVariable() routes every one
        // through trackFile(), which owns them and closes them itself when
        // g_dataset is destroyed (see test_varlist.cc's identical note).
        for (auto &p : paths) std::remove(p.c_str());
    }

    // The expected value at virtual timestep vt, (i,j): resolves which
    // file vt falls in and what its local timestep is, then applies the
    // same formula make_series_piece() wrote.
    float expected(int vt, int i, int j) const {
        int local = vt, file_idx = 0;
        while (local >= kCounts[file_idx]) {
            local -= kCounts[file_idx];
            file_idx++;
        }
        return kDataOffsets[file_idx] + local * kNlat * kNlon + i * kNlon + j;
    }
};
constexpr int ThreeFileSeries::kCounts[3];
constexpr float ThreeFileSeries::kDataOffsets[3];

} // namespace

TEST_CASE("Dataset::getData: a read spanning a file boundary exercises getDataIterate") {
    ensure_ncview_misc_initialized();
    ThreeFileSeries series("multifile_boundary_read");
    NCVar *var = series.var;

    // virtual timesteps 2,3,4: file0's last timestep + file1's both
    // timesteps. count[0]==3 > 1, so g_dataset.getData() must delegate to
    // Dataset::getDataIterate() (dataset.cc) rather than the single-file path.
    size_t start[3] = {2, 0, 0};
    size_t count[3] = {3, 2, 2};
    std::vector<float> data(3 * 2 * 2);
    g_dataset.getData(var, start, count, data.data());

    for (int t = 0; t < 3; t++)
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++) {
                CAPTURE(t);
                CAPTURE(i);
                CAPTURE(j);
                CHECK(data[(size_t)t * 4 + i * 2 + j] == series.expected(2 + t, i, j));
            }
}

TEST_CASE("Dataset::getData: a read covering the whole series matches every file's values") {
    ensure_ncview_misc_initialized();
    ThreeFileSeries series("multifile_whole_series_read");
    NCVar *var = series.var;

    size_t start[3] = {0, 0, 0};
    size_t count[3] = {9, 2, 2};
    std::vector<float> data(9 * 2 * 2);
    g_dataset.getData(var, start, count, data.data());

    for (int t = 0; t < 9; t++)
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++) {
                CAPTURE(t);
                CAPTURE(i);
                CAPTURE(j);
                CHECK(data[(size_t)t * 4 + i * 2 + j] == series.expected(t, i, j));
            }
}

TEST_CASE("Dataset::getData: reads starting and ending mid-file resolve to the right values") {
    ensure_ncview_misc_initialized();
    ThreeFileSeries series("multifile_midfile_read");
    NCVar *var = series.var;

    // Starts mid-file1 (local index 1 of 2) and ends mid-file2 (local
    // index 1 of 4): virtual timesteps 4,5,6.
    size_t start[3] = {4, 0, 0};
    size_t count[3] = {3, 2, 2};
    std::vector<float> data(3 * 2 * 2);
    g_dataset.getData(var, start, count, data.data());

    for (int t = 0; t < 3; t++)
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++) {
                CAPTURE(t);
                CAPTURE(i);
                CAPTURE(j);
                CHECK(data[(size_t)t * 4 + i * 2 + j] == series.expected(4 + t, i, j));
            }
}

TEST_CASE("virt_to_actual_place: resolves every file's first and last virtual timestep") {
    ensure_ncview_misc_initialized();
    ThreeFileSeries series("multifile_virt_to_actual");
    NCVar *var = series.var;

    // (virtual timestep, expected file index, expected local index)
    struct Case { size_t vt; int file_idx; size_t local; };
    const Case cases[] = {
        {0, 0, 0}, {1, 0, 1}, {2, 0, 2},  // file0: 3 timesteps
        {3, 1, 0}, {4, 1, 1},              // file1: 2 timesteps
        {5, 2, 0}, {6, 2, 1}, {7, 2, 2}, {8, 2, 3}, // file2: 4 timesteps
    };

    for (const auto &c : cases) {
        CAPTURE(c.vt);
        size_t virt_pl[3] = {c.vt, 1, 1};
        size_t act_pl[3];
        FDBlist *file = nullptr;
        virt_to_actual_place(var, virt_pl, act_pl, &file);

        CHECK(file == var->files[c.file_idx].get());
        CHECK(act_pl[0] == c.local);
        // Non-time indices pass through unchanged.
        CHECK(act_pl[1] == 1);
        CHECK(act_pl[2] == 1);
    }
}

TEST_CASE("Dataset::cacheScalarCoordInfo: timestep_2_fdb maps every virtual timestep to its owning file") {
    ensure_ncview_misc_initialized();
    ThreeFileSeries series("multifile_timestep_2_fdb");
    NCVar *var = series.var;

    g_dataset.cacheScalarCoordInfo();

    REQUIRE(var->timestep_2_fdb.size() == 9);
    const int expected_file_idx[9] = {0, 0, 0, 1, 1, 2, 2, 2, 2};
    for (int vt = 0; vt < 9; vt++) {
        CAPTURE(vt);
        CHECK(var->timestep_2_fdb[vt] == var->files[expected_file_idx[vt]].get());
    }
}

TEST_CASE("Dataset::dimValue: reconciles a timelike dim's value across files with different units") {
    ensure_ncview_misc_initialized();

    // Two files, deliberately different time units on the same timelike
    // dimension: file0 counts from 2000-01-01, file1 from 2000-01-15 (14
    // days later). fi_dim_value_convert() (file.cc) is supposed to
    // reconcile a value read from file1 back into file0's units (the
    // first file's units are what var->dim[]->calendar/timelike-ness came
    // from -- see test_varlist.cc's comment on why dim info only ever
    // comes from the first file) so the whole series reads on one
    // consistent timeline.
    const char *var_name = "multifile_dim_value_convert";
    std::string path0 = make_series_piece(var_name, 3, 2, 2, 0.0, 0.0f, "days since 2000-01-01");
    std::string path1 = make_series_piece(var_name, 2, 2, 2, 5.0, 100.0f, "days since 2000-01-15");

    int fid0 = open_for_core(path0);
    g_dataset.addVariable(var_name, fid0, path0.c_str());
    int fid1 = open_for_core(path1);
    g_dataset.addVariable(var_name, fid1, path1.c_str());

    NCVar *var = g_dataset.findVariable(var_name);
    REQUIRE(var != nullptr);
    REQUIRE(var->is_virtual == true);
    REQUIRE(var->dim[0]->timelike == 1);

    // Virtual timestep 3 is file1's local timestep 0, stored as day 5.0
    // in file1's OWN units ("days since 2000-01-15" -> absolute day
    // 2000-01-20). Expressed in file0's units ("days since 2000-01-01"),
    // that same absolute date is day 19 (14 days between the two epochs
    // + 5). If fi_dim_value_convert() didn't run, this would come back
    // as the untranslated 5.0 instead.
    double val;
    char cval[1024];
    int has_bounds;
    double bound_min, bound_max;
    size_t cursor[3] = {3, 0, 0};
    nc_type type = g_dataset.dimValue(var, /*dim_id=*/0, /*virt_place=*/3, &val, cval,
                                 &has_bounds, &bound_min, &bound_max, cursor);

    CHECK(type == NC_DOUBLE);
    CHECK(val == doctest::Approx(19.0));

    // Phase 12f: this variable has no "bounds" attribute, so has_bounds
    // should come back false and bound_min/bound_max should be the
    // documented "no bounds" default (0.0), not whatever happened to be
    // sitting on the stack. Before Phase 12f, Dataset::dimValue() ran
    // dimValueConvert() on these two values unconditionally, regardless
    // of has_bounds -- meaning this test exercised a read of
    // uninitialized memory on every run without failing (ASan/UBSan
    // can't catch a read of uninitialized *stack* memory; that needs
    // MSan or Valgrind, neither configured in this project). This
    // assertion can only prove the values are now deterministically
    // zero, not that they were previously garbage on this exact run.
    CHECK(has_bounds == 0);
    CHECK(bound_min == 0.0);
    CHECK(bound_max == 0.0);

    std::remove(path0.c_str());
    std::remove(path1.c_str());
}
