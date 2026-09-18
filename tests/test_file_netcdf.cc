// Copyright (C) 2026 Dominik Strebel
//
// Integration tests for core/src/file_netcdf.cc against a real, synthetic
// netCDF file (written and read back via the plain netCDF C API) -- this is
// the file-I/O boundary the rest of core's pure-logic tests deliberately
// don't exercise.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

namespace {

// HDF5 (netCDF-4's storage backend) has taken out an advisory file lock on
// every open file by default since 1.10.0. That's actively hostile to a
// test that creates, closes, and immediately reopens the same short-lived
// file: confirmed on CI as an outright hang on Windows (the process never
// returned; no crash, no timeout from netCDF itself, just stuck) the first
// time this test actually got far enough to hit real file I/O rather than
// failing fast on an earlier bug. This is a well-known HDF5 gotcha on
// Windows, network filesystems, and various CI sandboxes -- HDF5_USE_FILE_
// LOCKING=FALSE is the documented escape hatch. Must be set before the
// first netCDF/HDF5 call in the process.
struct DisableHdf5FileLocking {
    DisableHdf5FileLocking() {
#ifdef _WIN32
        _putenv_s( "HDF5_USE_FILE_LOCKING", "FALSE" );
#else
        setenv( "HDF5_USE_FILE_LOCKING", "FALSE", 0 );
#endif
    }
} g_disable_hdf5_file_locking;

// Creates a small netCDF file with dims time(3), lat(4), lon(5) and
// variables lat(lat), lon(lon), time(time), temp(time,lat,lon); returns its
// path. Caller must std::remove() it.
std::string make_sample_file() {
    // A hardcoded "/tmp/..." template isn't valid on Windows -- mkstemp()
    // itself is portable (mingw-w64 provides it), but the path needs to
    // come from the platform's actual temp directory.
    std::string path_template =
        (std::filesystem::temp_directory_path() / "ncview_test_XXXXXX").string();
    int fd = mkstemp(&path_template[0]);
    REQUIRE(fd >= 0);
    close(fd); // nc_create() below re-creates it; mkstemp() just reserves a unique name.
    std::string path = path_template;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);

    int dim_time, dim_lat, dim_lon;
    REQUIRE(nc_def_dim(ncid, "time", 3, &dim_time) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lat", 4, &dim_lat) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lon", 5, &dim_lon) == NC_NOERR);

    int var_time, var_lat, var_lon, var_temp;
    REQUIRE(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_time, &var_time) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_time, "units", 21, "days since 2000-01-01") == NC_NOERR);

    REQUIRE(nc_def_var(ncid, "lat", NC_FLOAT, 1, &dim_lat, &var_lat) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_lat, "units", 13, "degrees_north") == NC_NOERR);

    REQUIRE(nc_def_var(ncid, "lon", NC_FLOAT, 1, &dim_lon, &var_lon) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_lon, "units", 12, "degrees_east") == NC_NOERR);

    int temp_dims[3] = {dim_time, dim_lat, dim_lon};
    REQUIRE(nc_def_var(ncid, "temp", NC_FLOAT, 3, temp_dims, &var_temp) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_temp, "units", 1, "K") == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_temp, "long_name", 11, "temperature") == NC_NOERR);
    float fill = -999.0f;
    REQUIRE(nc_put_att_float(ncid, var_temp, "_FillValue", NC_FLOAT, 1, &fill) == NC_NOERR);

    REQUIRE(nc_enddef(ncid) == NC_NOERR);

    double time_vals[3] = {0.0, 1.0, 2.0};
    REQUIRE(nc_put_var_double(ncid, var_time, time_vals) == NC_NOERR);
    float lat_vals[4] = {10.0f, 20.0f, 30.0f, 40.0f};
    REQUIRE(nc_put_var_float(ncid, var_lat, lat_vals) == NC_NOERR);
    float lon_vals[5] = {-100.0f, -90.0f, -80.0f, -70.0f, -60.0f};
    REQUIRE(nc_put_var_float(ncid, var_lon, lon_vals) == NC_NOERR);

    float temp_vals[3 * 4 * 5];
    for (int i = 0; i < 3 * 4 * 5; i++) temp_vals[i] = (float)i;
    REQUIRE(nc_put_var_float(ncid, var_temp, temp_vals) == NC_NOERR);

    REQUIRE(nc_close(ncid) == NC_NOERR);
    return path;
}

// UPDATE (Phase 6): this comment used to say netcdf_dim_name_to_id()/
// netcdf_dim_id_to_name() internally called the dispatching fi_n_dims() --
// that's no longer true (Phase 6 broke that circular dependency; they call
// netcdf_fi_n_dims() directly now, confirmed by reading file_netcdf.cc, not
// assumed) and no function this file exercises still checks file_type via
// a fi_*() dispatcher. determine_file_type() is called below regardless,
// since it's the only way core's file_type module-static gets set at all
// and other tests/production code depend on it having run by this point in
// the process; it needs a real file to probe, so this runs after the
// sample file exists but before opening it for real.
int open_sample_file(const std::string &path) {
    Stringlist *files = nullptr;
    stringlist_add_string(&files, path.c_str());
    determine_file_type(files);
    stringlist_delete_entire_list(files);
    return netcdf_fi_initialize(const_cast<char *>(path.c_str()));
}

// RAII wrapper: opens the sample file via the same entry points core
// itself uses, and always cleans up the fileid + tmp file.
struct SampleFile {
    std::string path;
    int fileid;
    SampleFile() : path(make_sample_file()), fileid(open_sample_file(path)) {}
    ~SampleFile() {
        netcdf_fi_close(fileid);
        std::remove(path.c_str());
    }
};

// A one-variable file for netcdf_fill_value()/netcdf_fill_aux_data() tests,
// with a caller-supplied set of float attributes on the data variable (any
// of missing_value/_FillValue/scale_factor/add_offset, or a global
// missing_value) -- covers the attribute-precedence and unpacking logic
// that, before this, had zero test coverage anywhere (confirmed by grep;
// "refine the architecture" plan, Phase 6).
struct FillValueFile {
    std::string path;
    int fileid;

    FillValueFile( std::optional<float> var_missing_value,
                   std::optional<float> fill_value_attr,
                   std::optional<float> global_missing_value,
                   std::optional<float> scale_factor,
                   std::optional<float> add_offset,
                   std::optional<float> valid_min = std::nullopt,
                   std::optional<float> valid_max = std::nullopt,
                   bool add_unrelated_units_attr = false ) {
        auto tmpl = (std::filesystem::temp_directory_path() / "ncview_fillval_XXXXXX").string();
        int fd = mkstemp( &tmpl[0] );
        REQUIRE( fd >= 0 );
        close( fd );
        path = tmpl;

        int ncid;
        REQUIRE( nc_create( path.c_str(), NC_CLOBBER, &ncid ) == NC_NOERR );
        int dim_x;
        REQUIRE( nc_def_dim( ncid, "x", 3, &dim_x ) == NC_NOERR );
        int varid;
        REQUIRE( nc_def_var( ncid, "data", NC_FLOAT, 1, &dim_x, &varid ) == NC_NOERR );

        if( var_missing_value )
            REQUIRE( nc_put_att_float( ncid, varid, "missing_value", NC_FLOAT, 1, &*var_missing_value ) == NC_NOERR );
        if( fill_value_attr )
            REQUIRE( nc_put_att_float( ncid, varid, "_FillValue", NC_FLOAT, 1, &*fill_value_attr ) == NC_NOERR );
        if( global_missing_value )
            REQUIRE( nc_put_att_float( ncid, NC_GLOBAL, "missing_value", NC_FLOAT, 1, &*global_missing_value ) == NC_NOERR );
        if( scale_factor )
            REQUIRE( nc_put_att_float( ncid, varid, "scale_factor", NC_FLOAT, 1, &*scale_factor ) == NC_NOERR );
        if( add_offset )
            REQUIRE( nc_put_att_float( ncid, varid, "add_offset", NC_FLOAT, 1, &*add_offset ) == NC_NOERR );
        if( valid_min )
            REQUIRE( nc_put_att_float( ncid, varid, "valid_min", NC_FLOAT, 1, &*valid_min ) == NC_NOERR );
        if( valid_max )
            REQUIRE( nc_put_att_float( ncid, varid, "valid_max", NC_FLOAT, 1, &*valid_max ) == NC_NOERR );
        if( add_unrelated_units_attr )
            REQUIRE( nc_put_att_text( ncid, varid, "units", 1, "K" ) == NC_NOERR );

        REQUIRE( nc_enddef( ncid ) == NC_NOERR );
        float vals[3] = { 1.0f, 2.0f, 3.0f };
        REQUIRE( nc_put_var_float( ncid, varid, vals ) == NC_NOERR );
        REQUIRE( nc_close( ncid ) == NC_NOERR );

        fileid = open_sample_file( path );
    }
    ~FillValueFile() {
        netcdf_fi_close( fileid );
        std::remove( path.c_str() );
    }
};

} // namespace

TEST_CASE("file_netcdf: n_dims and var_size match the variable's real shape") {
    SampleFile f;
    CHECK(netcdf_fi_n_dims(f.fileid, (char *)"temp") == 3);
    CHECK(netcdf_fi_n_dims(f.fileid, (char *)"lat") == 1);

    size_t *size = netcdf_fi_var_size(f.fileid, (char *)"temp");
    REQUIRE(size != nullptr);
    CHECK(size[0] == 3); // time
    CHECK(size[1] == 4); // lat
    CHECK(size[2] == 5); // lon
}

TEST_CASE("file_netcdf: scannable_dims lists every dim of a 3-D variable") {
    SampleFile f;
    Stringlist *dims = netcdf_scannable_dims(f.fileid, (char *)"temp");
    REQUIRE(dims != nullptr);
    CHECK(stringlist_len(dims) == 3);
    REQUIRE(dims->size() == 3);
    CHECK((*dims)[0].string == "time");
    CHECK((*dims)[1].string == "lat");
    CHECK((*dims)[2].string == "lon");
}

TEST_CASE("file_netcdf: dim name/id lookups round-trip") {
    SampleFile f;
    int lat_id = netcdf_dim_name_to_id(f.fileid, (char *)"temp", (char *)"lat");
    CHECK(lat_id == 1); // second dim of temp(time,lat,lon)

    std::string name = netcdf_dim_id_to_name(f.fileid, (char *)"temp", lat_id);
    CHECK(name == "lat");

    CHECK(netcdf_dim_name_to_id(f.fileid, (char *)"temp", (char *)"not_a_dim") == -1);
}

TEST_CASE("file_netcdf: var and dim units come back as written") {
    SampleFile f;
    CHECK(netcdf_var_units(f.fileid, (char *)"temp") == "K");
    CHECK(netcdf_dim_units(f.fileid, (char *)"lat") == "degrees_north");
    CHECK(netcdf_long_var_name(f.fileid, (char *)"temp") == "temperature");
}

TEST_CASE("file_netcdf: dim value reads back real coordinate data") {
    SampleFile f;
    CHECK(netcdf_has_dim_values(f.fileid, (char *)"lat") != 0);

    double val;
    char cval[256];
    int has_bounds;
    double bmin, bmax;
    nc_type type = netcdf_dim_value(f.fileid, (char *)"lat", 2, &val, cval, 0,
                                     &has_bounds, &bmin, &bmax);
    // Numeric dimvars are always normalized to NC_DOUBLE on the way out
    // (only NC_CHAR dimvars keep their own type) -- see the case block in
    // netcdf_dim_value().
    CHECK(type == NC_DOUBLE);
    CHECK(val == doctest::Approx(30.0));
    CHECK(has_bounds == 0);
}

TEST_CASE("netcdf_dim_value: an unsigned-typed (netCDF-4) coordinate variable reports no bounds, not garbage (Phase 12f)") {
    // Every type netcdf_dim_value() actually handles by name (NC_BYTE/
    // SHORT/LONG/FLOAT/DOUBLE/INT64/CHAR) was already covered by the test
    // above. Any netCDF-4 numeric type added since 1993 -- NC_UINT here --
    // falls through to the function's `default:` case, which before Phase
    // 12f never wrote *return_has_bounds at all: a caller reading it was
    // reading whatever garbage happened to be on its own stack. This test
    // can only prove the value is now deterministically 0, not that it was
    // previously garbage on this exact run -- ASan/UBSan don't catch reads
    // of uninitialized *stack* memory (that needs MSan or Valgrind, neither
    // configured in this project).
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_uint_dim_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_NETCDF4 | NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_lev;
    REQUIRE(nc_def_dim(ncid, "lev", 3, &dim_lev) == NC_NOERR);
    int var_lev;
    REQUIRE(nc_def_var(ncid, "lev", NC_UINT, 1, &dim_lev, &var_lev) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);
    unsigned int lev_vals[3] = {100u, 200u, 300u};
    REQUIRE(nc_put_var_uint(ncid, var_lev, lev_vals) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);

    int fileid = open_sample_file(path);

    double val;
    char cval[256];
    int has_bounds;
    double bmin, bmax;
    nc_type type = netcdf_dim_value(fileid, (char *)"lev", 1, &val, cval, 0,
                                     &has_bounds, &bmin, &bmax);
    // The default branch falls back to the virtual place, not the real
    // (unreadable-as-double-by-name) value.
    CHECK(type == NC_DOUBLE);
    CHECK(has_bounds == 0);
    CHECK(bmin == 0.0);
    CHECK(bmax == 0.0);

    netcdf_fi_close(fileid);
    std::remove(path.c_str());
}

TEST_CASE("netcdf_dim_value: a scalar variable name-colliding with a dimension degrades instead of aborting (Phase 12g)") {
    // netcdf_dimvar_id() matches a "dimvar" purely by name (any variable
    // whose name equals a dimension's name, regardless of its own rank or
    // type -- confirmed by reading its loop, which only ever checks
    // strcmp(dim_name, var_name)). So a file can perfectly legally contain
    // a rank-0 (scalar) variable whose name happens to collide with an
    // unrelated dimension's name; before Phase 12g, netcdf_dim_value()'s
    // numeric path treated any n_dims != 1 as fatal and exit()'d the whole
    // process over it -- turning an ordinary, valid file into a crash.
    // Phase 12g degrades instead, the same "warn and fall back to
    // virt_place" shape the function's own default: (unhandled-type) case
    // already used.
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_scalar_collision_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_station;
    REQUIRE(nc_def_dim(ncid, "station", 3, &dim_station) == NC_NOERR);
    // A rank-0 variable named "station" -- unrelated to the "station" dim
    // in shape, but name-matched by netcdf_dimvar_id() all the same.
    int var_station;
    REQUIRE(nc_def_var(ncid, "station", NC_DOUBLE, 0, nullptr, &var_station) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);
    double station_scalar = 42.0;
    REQUIRE(nc_put_var_double(ncid, var_station, &station_scalar) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);

    int fileid = open_sample_file(path);

    double val;
    char cval[256];
    int has_bounds;
    double bmin, bmax;
    // Before the fix, this line never returns -- the process exit()'d.
    nc_type type = netcdf_dim_value(fileid, (char *)"station", 1, &val, cval, 7,
                                     &has_bounds, &bmin, &bmax);
    CHECK(type == NC_DOUBLE);
    CHECK(val == doctest::Approx(7.0)); // falls back to virt_place, not the scalar's real value (42.0)
    CHECK(has_bounds == 0);

    netcdf_fi_close(fileid);
    std::remove(path.c_str());
}

TEST_CASE("netcdf_dim_value: a bounds variable with >50 vertices degrades to the plain coordinate instead of aborting (Phase 12h)") {
    // netcdf_dim_value()'s fixed-size `boundvals[50]` array can't hold an
    // arbitrary CF bounds variable's vertex count -- unstructured/polygonal
    // cell bounds routinely exceed 50 vertices, so this is a real, valid
    // file shape, not a corrupt one. Before Phase 12h this exit()'d the
    // whole process; now it falls back to the plain, bounds-less read of
    // the coordinate itself (the coordinate variable is fine; only the
    // bounds decoration is unsupported).
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_big_bounds_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    const int nv = 60; // > 50

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_cell, dim_nv;
    REQUIRE(nc_def_dim(ncid, "cell", 4, &dim_cell) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "nv", nv, &dim_nv) == NC_NOERR);
    int var_cell, var_bnds;
    REQUIRE(nc_def_var(ncid, "cell", NC_DOUBLE, 1, &dim_cell, &var_cell) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_cell, "bounds", 9, "cell_bnds") == NC_NOERR);
    int bdims[2] = {dim_cell, dim_nv};
    REQUIRE(nc_def_var(ncid, "cell_bnds", NC_DOUBLE, 2, bdims, &var_bnds) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);
    double cell_vals[4] = {10.0, 20.0, 30.0, 40.0};
    REQUIRE(nc_put_var_double(ncid, var_cell, cell_vals) == NC_NOERR);
    std::vector<double> bnds_vals(4 * nv, 0.0);
    REQUIRE(nc_put_var_double(ncid, var_bnds, bnds_vals.data()) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);

    int fileid = open_sample_file(path);

    double val;
    char cval[256];
    int has_bounds;
    double bmin, bmax;
    // Before the fix, this line never returns -- the process exit()'d.
    nc_type type = netcdf_dim_value(fileid, (char *)"cell", 2, &val, cval, 99,
                                     &has_bounds, &bmin, &bmax);
    CHECK(type == NC_DOUBLE);
    CHECK(val == doctest::Approx(30.0)); // the real coordinate value, not virt_place
    CHECK(has_bounds == 0);

    netcdf_fi_close(fileid);
    std::remove(path.c_str());
}

TEST_CASE("netcdf_dim_value: a normal (<=50-vertex) bounds variable still averages to the center value") {
    // Companion to the >50-vertices degrade test above: nothing in the
    // existing suite exercises the ordinary bounds-averaging path at all,
    // so this closes a real coverage gap on the path Phase 12h's
    // restructuring (introducing `use_bounds`) touched most.
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_small_bounds_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_cell, dim_nv;
    REQUIRE(nc_def_dim(ncid, "cell", 4, &dim_cell) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "nv", 2, &dim_nv) == NC_NOERR);
    int var_cell, var_bnds;
    REQUIRE(nc_def_var(ncid, "cell", NC_DOUBLE, 1, &dim_cell, &var_cell) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_cell, "bounds", 9, "cell_bnds") == NC_NOERR);
    int bdims[2] = {dim_cell, dim_nv};
    REQUIRE(nc_def_var(ncid, "cell_bnds", NC_DOUBLE, 2, bdims, &var_bnds) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);
    double cell_vals[4] = {10.0, 20.0, 30.0, 40.0};
    REQUIRE(nc_put_var_double(ncid, var_cell, cell_vals) == NC_NOERR);
    // Cell 2 (0-indexed) has bounds [25, 45] -- deliberately NOT centered on
    // the stored coordinate value (30), matching the header comment's own
    // note that "some files have the dim value NOT centered between the
    // boundaries" and that netcdf_dim_value() reports the bounds' average
    // (35), not the stored coordinate (30), when bounds are present.
    double bnds_vals[8] = {5.0,15.0, 15.0,25.0, 25.0,45.0, 35.0,55.0};
    REQUIRE(nc_put_var_double(ncid, var_bnds, bnds_vals) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);

    int fileid = open_sample_file(path);

    double val;
    char cval[256];
    int has_bounds;
    double bmin, bmax;
    nc_type type = netcdf_dim_value(fileid, (char *)"cell", 2, &val, cval, 99,
                                     &has_bounds, &bmin, &bmax);
    CHECK(type == NC_DOUBLE);
    CHECK(has_bounds == 2);
    CHECK(val == doctest::Approx(35.0)); // (25+45)/2, not the stored 30.0
    CHECK(bmin == doctest::Approx(25.0));
    CHECK(bmax == doctest::Approx(45.0));

    netcdf_fi_close(fileid);
    std::remove(path.c_str());
}

TEST_CASE("netcdf_dim_value: a coordinate variable shorter than its own dimension degrades on out-of-range reads (Phase 12h)") {
    // netcdf_has_dim_values()/netcdf_dimvar_id() match a dimvar purely by
    // name, with no check that its length agrees with the dimension it's
    // named after. A perfectly legal (if unusual) file can have a "time"
    // dimension of size 10 while the "time" *variable* is actually defined
    // over a different, shorter dimension -- so a read at a `place` beyond
    // that variable's real length fails with NC_EINVALCOORDS. Before Phase
    // 12h this exit()'d the whole process on the read failure (introduced,
    // unchecked, by Phase 12f as the first fix that even looked at this
    // call's error code); now it degrades to virt_place.
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_short_dimvar_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_time, dim_short;
    REQUIRE(nc_def_dim(ncid, "time", 10, &dim_time) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "short", 3, &dim_short) == NC_NOERR);
    int var_time;
    // "time" the variable is 1-D, but over "short" (length 3), not "time"
    // (length 10) -- netcdf_dimvar_id() matches it to the "time" dimension
    // purely by name regardless.
    REQUIRE(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_short, &var_time) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);
    double time_vals[3] = {1.0, 2.0, 3.0};
    REQUIRE(nc_put_var_double(ncid, var_time, time_vals) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);

    int fileid = open_sample_file(path);

    double val;
    char cval[256];
    int has_bounds;
    double bmin, bmax;
    // place=5 is within the "time" dimension's nominal length (10) but past
    // the real "time" variable's actual length (3). Before the fix, this
    // line never returns -- the process exit()'d.
    nc_type type = netcdf_dim_value(fileid, (char *)"time", 5, &val, cval, 5,
                                     &has_bounds, &bmin, &bmax);
    CHECK(type == NC_DOUBLE);
    CHECK(val == doctest::Approx(5.0)); // falls back to virt_place
    CHECK(has_bounds == 0);

    netcdf_fi_close(fileid);
    std::remove(path.c_str());
}

TEST_CASE("netcdf_dim_value: a 1-D NC_CHAR dimvar shorter than its own dimension degrades on out-of-range reads (Phase 12h)") {
    // Same shape of bug as the numeric short-dimvar test above, but on the
    // 1-D NC_CHAR path: the "label" variable is 1-D NC_CHAR over a
    // different, shorter dimension than the "label" dimension it's
    // name-matched to. Before Phase 12h, nc_get_var1_text()'s failure here
    // (added by Phase 12g, unchecked before that) exit()'d the process.
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_short_char1d_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_label, dim_short;
    REQUIRE(nc_def_dim(ncid, "label", 10, &dim_label) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "short", 3, &dim_short) == NC_NOERR);
    int var_label;
    REQUIRE(nc_def_var(ncid, "label", NC_CHAR, 1, &dim_short, &var_label) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);
    char label_vals[3] = {'a', 'b', 'c'};
    REQUIRE(nc_put_var_text(ncid, var_label, label_vals) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);

    int fileid = open_sample_file(path);

    double val;
    char cval[256];
    int has_bounds;
    double bmin, bmax;
    // Before the fix, this line never returns -- the process exit()'d.
    nc_type type = netcdf_dim_value(fileid, (char *)"label", 5, &val, cval, 5,
                                     &has_bounds, &bmin, &bmax);
    CHECK(type == NC_DOUBLE); // degraded from NC_CHAR to virt_place
    CHECK(val == doctest::Approx(5.0));
    CHECK(has_bounds == 0);

    netcdf_fi_close(fileid);
    std::remove(path.c_str());
}

TEST_CASE("netcdf_dim_value: a 2-D NC_CHAR dimvar's first read fails and degrades cleanly (Phase 12h)") {
    // Exercises the trickiest edge of the 2-D NC_CHAR degrade fix: an
    // error on the very FIRST character read (i==0), where *(ret_val_char
    // + i - 1) would underflow if the NUL-termination step below the loop
    // ran unconditionally. It's guarded on `ret_type == NC_CHAR` now, which
    // is false on this path since the read never succeeds at all.
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_short_char2d_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_label, dim_short, dim_chars;
    REQUIRE(nc_def_dim(ncid, "label", 10, &dim_label) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "short", 3, &dim_short) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "chars", 5, &dim_chars) == NC_NOERR);
    int var_label;
    int ldims[2] = {dim_short, dim_chars};
    // "label" the variable is 2-D NC_CHAR over [short(3), chars(5)] -- but
    // name-matched to the "label" dimension (length 10) regardless.
    REQUIRE(nc_def_var(ncid, "label", NC_CHAR, 2, ldims, &var_label) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);
    char label_vals[15] = {'a','b',0,0,0, 'c','d',0,0,0, 'e','f',0,0,0};
    REQUIRE(nc_put_var_text(ncid, var_label, label_vals) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);

    int fileid = open_sample_file(path);

    double val;
    char cval[256];
    int has_bounds;
    double bmin, bmax;
    // place=5 is beyond the "short" dim's real length (3) -- the very
    // first nc_get_var1_text() call (i==0) fails.
    // Before the fix, this line never returns -- the process exit()'d.
    nc_type type = netcdf_dim_value(fileid, (char *)"label", 5, &val, cval, 5,
                                     &has_bounds, &bmin, &bmax);
    CHECK(type == NC_DOUBLE); // degraded from NC_CHAR to virt_place
    CHECK(val == doctest::Approx(5.0));
    CHECK(has_bounds == 0);

    netcdf_fi_close(fileid);
    std::remove(path.c_str());
}

TEST_CASE("file_netcdf: a name with no matching dimvar reports no values") {
    SampleFile f;
    // netcdf_has_dim_values()'s notion of "dimvar" is purely name-based (a
    // variable named identically to the dim, per netCDF's own coordinate-
    // variable convention -- see netcdf_dimvar_id()), with no cross-check
    // that a same-named dimension actually exists. A name matching nothing
    // in the file at all must not crash, just report no dim values.
    CHECK(netcdf_has_dim_values(f.fileid, (char *)"nonexistent") == 0);
}

// ===================== netcdf_fill_value(): attribute precedence and unpacking =====================
//
// Phase 6 of the "refine the architecture" plan: netcdf_fill_value()'s
// _FillValue/missing_value/valid_range precedence and its scale_factor/
// add_offset unpacking had zero test coverage anywhere (confirmed by grep
// before writing these). netcdf_fill_value() itself doesn't read valid_range
// at all -- that's netcdf_min_max_option_set()/netcdf_get_att_util(),
// exercised separately via Dataset::checkRanges() -- so "valid_range
// precedence" here means what the function's own attribute-checking order
// actually does: three float attributes checked in sequence, each one that
// exists overwriting *v, so the LAST one found wins.

TEST_CASE("netcdf_fill_value: with only _FillValue set, that value is used") {
    FillValueFile f( std::nullopt, 42.0f, std::nullopt, std::nullopt, std::nullopt );
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, nullptr );
    CHECK( v == doctest::Approx(42.0f) );
}

TEST_CASE("netcdf_fill_value: with only missing_value set, that value is used") {
    FillValueFile f( -999.0f, std::nullopt, std::nullopt, std::nullopt, std::nullopt );
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, nullptr );
    CHECK( v == doctest::Approx(-999.0f) );
}

TEST_CASE("netcdf_fill_value: _FillValue overrides a var-level missing_value") {
    // netcdf_fill_value() checks missing_value first, then _FillValue,
    // overwriting *v each time something is found -- so of these two,
    // whichever is checked LAST wins, which is _FillValue.
    FillValueFile f( -999.0f, 42.0f, std::nullopt, std::nullopt, std::nullopt );
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, nullptr );
    CHECK( v == doctest::Approx(42.0f) );
}

TEST_CASE("netcdf_fill_value: a global missing_value overrides both var-level attributes") {
    // The global missing_value check runs last of the three, so it wins
    // over both a var-level missing_value AND a var-level _FillValue --
    // a real, surprising-until-you-read-the-code precedence order.
    FillValueFile f( -999.0f, 42.0f, /*global_missing_value=*/-1.0f, std::nullopt, std::nullopt );
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, nullptr );
    CHECK( v == doctest::Approx(-1.0f) );
}

TEST_CASE("netcdf_fill_value: with no fill-related attribute at all, uses the type's netCDF default") {
    FillValueFile f( std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt );
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, nullptr );
    // "data" is NC_FLOAT.
    CHECK( v == doctest::Approx(NC_FILL_FLOAT) );
}

TEST_CASE("netcdf_fill_value: scale_factor and add_offset both apply to the found fill value") {
    FillValueFile f( std::nullopt, /*fill_value_attr=*/10.0f, std::nullopt,
                      /*scale_factor=*/2.0f, /*add_offset=*/1.0f );
    NetCDFOptions aux{};
    aux.scale_factor_set = true;
    aux.scale_factor = 2.0f;
    aux.add_offset_set = true;
    aux.add_offset = 1.0f;
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, &aux );
    CHECK( v == doctest::Approx(10.0f * 2.0f + 1.0f) );
}

TEST_CASE("netcdf_fill_value: scale_factor alone applies without an offset") {
    FillValueFile f( std::nullopt, /*fill_value_attr=*/10.0f, std::nullopt,
                      /*scale_factor=*/2.0f, std::nullopt );
    NetCDFOptions aux{};
    aux.scale_factor_set = true;
    aux.scale_factor = 2.0f;
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, &aux );
    CHECK( v == doctest::Approx(20.0f) );
}

TEST_CASE("netcdf_fill_value: add_offset alone applies without a scale") {
    FillValueFile f( std::nullopt, /*fill_value_attr=*/10.0f, std::nullopt,
                      std::nullopt, /*add_offset=*/5.0f );
    NetCDFOptions aux{};
    aux.add_offset_set = true;
    aux.add_offset = 5.0f;
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, &aux );
    CHECK( v == doctest::Approx(15.0f) );
}

TEST_CASE("netcdf_fill_value: a null aux_data skips scale/offset unpacking entirely") {
    // Documented at the call site (netcdf_fill_value()'s own comment):
    // aux_data is NULL for coordinate-variable reads, which have no
    // scale/offset attributes to apply. Confirms the found value passes
    // through unscaled rather than crashing on a null aux_data.
    FillValueFile f( std::nullopt, /*fill_value_attr=*/10.0f, std::nullopt, std::nullopt, std::nullopt );
    float v = 0.0f;
    netcdf_fill_value( f.fileid, (char *)"data", &v, nullptr );
    CHECK( v == doctest::Approx(10.0f) );
}

// ===================== netcdf_fill_aux_data(): populating NetCDFOptions from attributes =====================

TEST_CASE("netcdf_fill_aux_data: reads scale_factor/add_offset/valid_min/valid_max off the variable") {
    FillValueFile f( std::nullopt, std::nullopt, std::nullopt, /*scale_factor=*/3.0f, /*add_offset=*/7.0f,
                      /*valid_min=*/-5.0f, /*valid_max=*/5.0f );

    FDBlist fdb;
    fdb.filename = f.path;
    fdb.aux_data = std::make_unique<NetCDFOptions>();
    netcdf_fill_aux_data( f.fileid, (char *)"data", &fdb );

    CHECK( fdb.aux_data->scale_factor_set );
    CHECK( fdb.aux_data->scale_factor == doctest::Approx(3.0f) );
    CHECK( fdb.aux_data->add_offset_set );
    CHECK( fdb.aux_data->add_offset == doctest::Approx(7.0f) );
    CHECK( fdb.aux_data->valid_min_set );
    CHECK( fdb.aux_data->valid_max_set );
    // Confirmed by reading the code, not assumed (this plan's standing
    // rule): with add_offset AND scale_factor both set but no valid_range
    // attribute, netcdf_fill_aux_data()'s "assume they apply to the valid
    // range too" special case (file_netcdf.cc, right after the four
    // netcdf_get_att_util() calls) transforms valid_min/valid_max in
    // place -- so the values read back are NOT the raw -5/5 written above,
    // they're valid_min*scale_factor+add_offset and
    // valid_max*scale_factor+add_offset. First draft of this test
    // expected the raw values and failed; this is real, pre-existing
    // behavior, not a bug this phase should fix.
    CHECK( fdb.aux_data->valid_min == doctest::Approx(-5.0f * 3.0f + 7.0f) );
    CHECK( fdb.aux_data->valid_max == doctest::Approx(5.0f * 3.0f + 7.0f) );
}

TEST_CASE("netcdf_fill_aux_data: valid_min/valid_max come back unmodified with no scale_factor/add_offset") {
    // The companion case to the test above: with neither scale_factor nor
    // add_offset set, valid_min/valid_max are read back exactly as written.
    FillValueFile f( std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                      /*valid_min=*/-5.0f, /*valid_max=*/5.0f );

    FDBlist fdb;
    fdb.filename = f.path;
    fdb.aux_data = std::make_unique<NetCDFOptions>();
    netcdf_fill_aux_data( f.fileid, (char *)"data", &fdb );

    CHECK( fdb.aux_data->valid_min_set );
    CHECK( fdb.aux_data->valid_min == doctest::Approx(-5.0f) );
    CHECK( fdb.aux_data->valid_max_set );
    CHECK( fdb.aux_data->valid_max == doctest::Approx(5.0f) );
    CHECK_FALSE( fdb.aux_data->scale_factor_set );
    CHECK_FALSE( fdb.aux_data->add_offset_set );
}

TEST_CASE("netcdf_fill_aux_data: an unrelated attribute present, but no scale/offset, leaves them unset") {
    // n_atts == 0 short-circuits netcdf_fill_aux_data() before it ever
    // checks for scale_factor/add_offset/valid_min/valid_max -- giving the
    // variable an unrelated attribute (units) forces it past that early
    // return, so this actually exercises the "checked, not found" path
    // for each of the four attributes rather than the early-exit path.
    FillValueFile f( std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                      std::nullopt, std::nullopt, /*add_unrelated_units_attr=*/true );

    FDBlist fdb;
    fdb.filename = f.path;
    fdb.aux_data = std::make_unique<NetCDFOptions>();
    netcdf_fill_aux_data( f.fileid, (char *)"data", &fdb );

    CHECK_FALSE( fdb.aux_data->scale_factor_set );
    CHECK_FALSE( fdb.aux_data->add_offset_set );
    CHECK_FALSE( fdb.aux_data->valid_min_set );
    CHECK_FALSE( fdb.aux_data->valid_max_set );
}
