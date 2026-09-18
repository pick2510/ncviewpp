// Copyright (C) 2026 Dominik Strebel
//
// Phase 6 ("refine the architecture" plan): characterizes the new
// NetCDFFile::open() factory -- the RAII lifecycle guarantee nothing
// previously proved (nc_open()'s success path never went through a type
// whose destructor calls nc_close(), since fi_initialize()/
// netcdf_fi_initialize() just return a bare int on success and exit(-1) on
// failure), plus the failure paths that exit() makes untestable through
// the production entry points.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include <doctest/doctest.h>

#include "ncview/dataset.h"
#include "ncview/includes.h"

namespace {

// Same HDF5-file-locking workaround as test_file_netcdf.cc (see its
// comment): must be set before the first netCDF/HDF5 call in the process.
// doctest links both test_file_netcdf.cc and this file into one binary, so
// whichever one's global runs first "wins" -- either is fine since they
// set the same value.
struct DisableHdf5FileLocking {
    DisableHdf5FileLocking() {
#ifdef _WIN32
        _putenv_s( "HDF5_USE_FILE_LOCKING", "FALSE" );
#else
        setenv( "HDF5_USE_FILE_LOCKING", "FALSE", 0 );
#endif
    }
} g_disable_hdf5_file_locking_netcdf_file;

std::string unique_tmp_path() {
    std::string path_template =
        (std::filesystem::temp_directory_path() / "ncview_test_ncf_XXXXXX").string();
    int fd = mkstemp( &path_template[0] );
    REQUIRE( fd >= 0 );
    close( fd );
    return path_template;
}

// Minimal valid netCDF file: one dim, one var. Caller must std::remove() it.
std::string make_sample_file() {
    std::string path = unique_tmp_path();

    int ncid;
    REQUIRE( nc_create( path.c_str(), NC_CLOBBER, &ncid ) == NC_NOERR );
    int dim_x;
    REQUIRE( nc_def_dim( ncid, "x", 3, &dim_x ) == NC_NOERR );
    int var_x;
    REQUIRE( nc_def_var( ncid, "x", NC_FLOAT, 1, &dim_x, &var_x ) == NC_NOERR );
    REQUIRE( nc_enddef( ncid ) == NC_NOERR );
    float vals[3] = { 1.0f, 2.0f, 3.0f };
    REQUIRE( nc_put_var_float( ncid, var_x, vals ) == NC_NOERR );
    REQUIRE( nc_close( ncid ) == NC_NOERR );

    return path;
}

// A file that exists, is readable, and definitely isn't netCDF.
std::string make_non_netcdf_file() {
    std::string path = unique_tmp_path();
    std::ofstream f( path );
    f << "this is not a netCDF file\n";
    f.close();
    return path;
}

} // namespace

TEST_CASE("NetCDFFile::open: succeeds on a real netCDF file") {
    std::string path = make_sample_file();
    int errcode = -12345;

    auto file = NetCDFFile::open( path, &errcode );

    CHECK( file.has_value() );
    CHECK( errcode == NC_NOERR );
    CHECK( file->id() >= 0 );

    std::remove( path.c_str() );
}

TEST_CASE("NetCDFFile::open: the nc_errcode out-parameter is optional") {
    std::string path = make_sample_file();

    // No crash, no UB, when the caller doesn't want the error code.
    auto file = NetCDFFile::open( path );
    CHECK( file.has_value() );

    std::remove( path.c_str() );
}

TEST_CASE("NetCDFFile::open: fails on a nonexistent path, without exit()ing") {
    // The whole point: netcdf_fi_initialize()/fi_initialize() exit(-1) on
    // exactly this input, which is why this failure mode has never been
    // testable in-process before.
    int errcode = NC_NOERR;
    auto file = NetCDFFile::open( "/no/such/path/ncview_test_does_not_exist.nc", &errcode );

    CHECK_FALSE( file.has_value() );
    CHECK( errcode != NC_NOERR );
}

TEST_CASE("NetCDFFile::open: fails on a readable but non-netCDF file") {
    std::string path = make_non_netcdf_file();
    int errcode = NC_NOERR;

    auto file = NetCDFFile::open( path, &errcode );

    CHECK_FALSE( file.has_value() );
    CHECK( errcode != NC_NOERR );

    std::remove( path.c_str() );
}

TEST_CASE("NetCDFFile: destruction closes the underlying file id (RAII)") {
    std::string path = make_sample_file();
    int fileid;
    {
        auto file = NetCDFFile::open( path );
        REQUIRE( file.has_value() );
        fileid = file->id();
        // Confirm it's actually open while the wrapper is alive.
        int ndims;
        CHECK( nc_inq( fileid, &ndims, nullptr, nullptr, nullptr ) == NC_NOERR );
    } // NetCDFFile destructor runs here

    // The id must now be invalid -- nc_inq() on a closed id fails.
    int ndims;
    CHECK( nc_inq( fileid, &ndims, nullptr, nullptr, nullptr ) != NC_NOERR );

    std::remove( path.c_str() );
}

TEST_CASE("NetCDFFile: reopening the same path twice gives two independent, live ids") {
    std::string path = make_sample_file();

    auto file_a = NetCDFFile::open( path );
    auto file_b = NetCDFFile::open( path );
    REQUIRE( file_a.has_value() );
    REQUIRE( file_b.has_value() );
    CHECK( file_a->id() != file_b->id() );

    int ndims;
    CHECK( nc_inq( file_a->id(), &ndims, nullptr, nullptr, nullptr ) == NC_NOERR );
    CHECK( nc_inq( file_b->id(), &ndims, nullptr, nullptr, nullptr ) == NC_NOERR );

    std::remove( path.c_str() );
}

TEST_CASE("NetCDFFile: move construction transfers ownership, doesn't double-close") {
    std::string path = make_sample_file();

    auto file_opt = NetCDFFile::open( path );
    REQUIRE( file_opt.has_value() );
    int fileid = file_opt->id();

    NetCDFFile moved_to( std::move( *file_opt ) );
    CHECK( moved_to.id() == fileid );
    CHECK( file_opt->id() == -1 ); // moved-from: no longer owns anything

    // file_opt's destructor (id() == -1) must be a no-op; moved_to's
    // destructor is what actually closes fileid. If either double-closed
    // or under-closed, this file_opt going out of scope after moved_to
    // does (reverse declaration order) would misbehave under ASan/UBSan.
    int ndims;
    CHECK( nc_inq( fileid, &ndims, nullptr, nullptr, nullptr ) == NC_NOERR );

    std::remove( path.c_str() );
}

TEST_CASE("NetCDFFile: move assignment closes the target's previous file first") {
    std::string path_a = make_sample_file();
    std::string path_b = make_sample_file();

    auto file_a = NetCDFFile::open( path_a );
    auto file_b = NetCDFFile::open( path_b );
    REQUIRE( file_a.has_value() );
    REQUIRE( file_b.has_value() );
    int fileid_a = file_a->id();
    int fileid_b = file_b->id();

    *file_a = std::move( *file_b );

    // file_a's original id (fileid_a) must have been closed by the
    // move-assignment operator's close() call, not leaked.
    int ndims;
    CHECK( nc_inq( fileid_a, &ndims, nullptr, nullptr, nullptr ) != NC_NOERR );
    // file_a now owns what used to be file_b's id, and it's still live.
    CHECK( file_a->id() == fileid_b );
    CHECK( nc_inq( fileid_b, &ndims, nullptr, nullptr, nullptr ) == NC_NOERR );

    std::remove( path_a.c_str() );
    std::remove( path_b.c_str() );
}

TEST_CASE("NetCDFFile: opening N files and letting them all go closes every id") {
    const int N = 5;
    std::vector<std::string> paths;
    std::vector<int> fileids;
    {
        std::vector<NetCDFFile> files;
        for( int i = 0; i < N; i++ ) {
            paths.push_back( make_sample_file() );
            auto f = NetCDFFile::open( paths.back() );
            REQUIRE( f.has_value() );
            fileids.push_back( f->id() );
            files.push_back( std::move( *f ) );
            }
        // All N still open here.
        for( int fid : fileids ) {
            int ndims;
            CHECK( nc_inq( fid, &ndims, nullptr, nullptr, nullptr ) == NC_NOERR );
            }
    } // every NetCDFFile in 'files' destructs here

    for( int fid : fileids ) {
        int ndims;
        CHECK( nc_inq( fid, &ndims, nullptr, nullptr, nullptr ) != NC_NOERR );
        }
    for( const auto &p : paths )
        std::remove( p.c_str() );
}
