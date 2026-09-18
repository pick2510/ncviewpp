// Copyright (C) 2026 Dominik Strebel
//
// Tests for NetCDFFile/Dataset (core/src/dataset.cc) -- OOP_redesign plan,
// Step 5. Before this class existed, no file ncview ever opened was closed
// (fi_close() had zero callers anywhere in core/ui); these tests exist
// specifically to confirm that's no longer true, and that a single
// physical file shared by many variables (the normal case: Dataset::addVariable()
// calls Dataset::trackFile() once per variable in a file, all with the same
// fileid) is only tracked -- and closed -- once.
#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>

#include <doctest/doctest.h>

#include "ncview/dataset.h"
#include "ncview/includes.h"
#include "ncview/protos.h"

namespace {

// A minimal, valid netCDF file -- content doesn't matter, only that
// nc_open()/nc_close() on it succeed.
std::string make_empty_file() {
    std::string path_template =
        (std::filesystem::temp_directory_path() / "ncview_dataset_test_XXXXXX").string();
    int fd = mkstemp(&path_template[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = path_template;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);
    return path;
}

int open_file(const std::string &path) {
    // NetCDFFile::close() (Phase 6) now calls netcdf_fi_close() directly
    // rather than going through file.cc's file_type-dispatching fi_close()
    // -- so this determine_file_type() call is no longer load-bearing for
    // NetCDFFile destruction specifically. Kept anyway (harmless) since
    // other functions this test file may exercise still dispatch through
    // file.cc, and to keep every TEST_CASE below self-contained regardless
    // of what other test files ran (or were filtered out) first, same as
    // test_file_netcdf.cc's open_sample_file().
    Stringlist *files = nullptr;
    stringlist_add_string(&files, path.c_str());
    determine_file_type(files);
    stringlist_delete_entire_list(files);

    int fileid;
    REQUIRE(nc_open(path.c_str(), NC_NOWRITE, &fileid) == NC_NOERR);
    return fileid;
}

// True if `fileid` is still a live, usable netCDF handle.
bool fileid_is_open(int fileid) {
    int ndims;
    return nc_inq(fileid, &ndims, nullptr, nullptr, nullptr) == NC_NOERR;
}

} // namespace

TEST_CASE("NetCDFFile closes its fileid exactly once, on destruction") {
    std::string path = make_empty_file();
    int fileid = open_file(path);
    REQUIRE(fileid_is_open(fileid));

    {
        NetCDFFile f(fileid);
        CHECK(f.id() == fileid);
        CHECK(fileid_is_open(fileid)); // still open while the wrapper is alive
    }

    CHECK_FALSE(fileid_is_open(fileid)); // closed once the wrapper is destroyed

    std::remove(path.c_str());
}

TEST_CASE("NetCDFFile move transfers ownership without double-closing") {
    std::string path = make_empty_file();
    int fileid = open_file(path);

    {
        NetCDFFile f1(fileid);
        NetCDFFile f2(std::move(f1));
        CHECK(f2.id() == fileid);
        CHECK(fileid_is_open(fileid));
        // f1 is now moved-from and must not also try to close `fileid`
        // when it goes out of scope alongside f2 below.
    }

    CHECK_FALSE(fileid_is_open(fileid));
    std::remove(path.c_str());
}

TEST_CASE("Dataset::trackFile deduplicates by fileid: many variables in one "
          "file share a single NetCDFFile owner") {
    std::string path = make_empty_file();
    int fileid = open_file(path);

    Dataset ds;
    NetCDFFile *first = ds.trackFile(fileid);
    NetCDFFile *second = ds.trackFile(fileid);   // as Dataset::addVariable() does for
    NetCDFFile *third = ds.trackFile(fileid);    // every subsequent var in the same file
    CHECK(first == second);
    CHECK(first == third);
    CHECK(first->id() == fileid);

    std::remove(path.c_str());
}

TEST_CASE("Dataset closes every distinct tracked file exactly once on destruction") {
    std::string path1 = make_empty_file();
    std::string path2 = make_empty_file();
    int fileid1 = open_file(path1);
    int fileid2 = open_file(path2);

    {
        Dataset ds;
        ds.trackFile(fileid1);
        ds.trackFile(fileid1); // dedup: must not cause a double-close below
        ds.trackFile(fileid2);
    }

    CHECK_FALSE(fileid_is_open(fileid1));
    CHECK_FALSE(fileid_is_open(fileid2));

    std::remove(path1.c_str());
    std::remove(path2.c_str());
}

TEST_CASE("repeated open/wrap/close cycles work") {
    for (int i = 0; i < 5; i++) {
        std::string path = make_empty_file();
        int fileid = open_file(path);
        {
            NetCDFFile f(fileid);
        }
        CHECK_FALSE(fileid_is_open(fileid));
        std::remove(path.c_str());
    }
}
