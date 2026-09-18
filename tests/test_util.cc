// Copyright (C) 2026 Dominik Strebel
//
// Unit tests for the pure, global-state-free helpers in core/src/util.cc.
#include <cstring>
#include <string>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

TEST_CASE("util: clip_f clamps into range") {
    float below = -5.0f, inside = 0.5f, above = 5.0f;
    clip_f(&below, 0.0f, 1.0f);
    clip_f(&inside, 0.0f, 1.0f);
    clip_f(&above, 0.0f, 1.0f);
    CHECK(below == 0.0f);
    CHECK(inside == 0.5f);
    CHECK(above == 1.0f);
}

TEST_CASE("util: close_enough uses a relative tolerance scaled to fill") {
    CHECK(close_enough(1.0f, 1.0f) != 0);
    CHECK(close_enough(1.0f, 2.0f) == 0);
    // fill=1e6: tolerance scales to 1e6*1e-5 = 10, so 1e6+5 is "close enough".
    CHECK(close_enough(1000005.0f, 1000000.0f) != 0);
    CHECK(close_enough(1000020.0f, 1000000.0f) == 0);
    // fill=0: tolerance is the fixed absolute 1e-5.
    CHECK(close_enough(0.0f, 0.0f) != 0);
    CHECK(close_enough(0.001f, 0.0f) == 0);
    // Sign of fill flips the sign of the tolerance internally but the
    // comparison must still work symmetrically for a negative fill value.
    CHECK(close_enough(-1000005.0f, -1000000.0f) != 0);
}

TEST_CASE("util: strncmp_nocase ignores case") {
    CHECK(strncmp_nocase("Longitude", "longitude", 9) == 0);
    CHECK(strncmp_nocase("LONGITUDE", "longitude", 9) == 0);
    CHECK(strncmp_nocase("Longitude", "latitude", 4) != 0);
    // Only the first n characters matter.
    CHECK(strncmp_nocase("LatX", "LatY", 3) == 0);
    CHECK(strncmp_nocase("LatX", "LatY", 4) != 0);
}

TEST_CASE("util: strncmp_nocase rejects null arguments") {
    CHECK(strncmp_nocase(nullptr, "x", 1) == -1);
    CHECK(strncmp_nocase("x", nullptr, 1) == -1);
}

TEST_CASE("util: unpack_groupname handles a plain varname with no groups") {
    char groupname[1024];
    CHECK(unpack_groupname("temperature", -1, groupname) == 0);
    CHECK(std::strcmp(groupname, "/") == 0);
    CHECK(unpack_groupname("temperature", -2, groupname) == 0);
    CHECK(std::strcmp(groupname, "temperature") == 0);
}

TEST_CASE("util: unpack_groupname extracts full and individual group levels") {
    char groupname[1024];
    char varname[] = "forecast/model_a/temp";

    // -1: full group path (everything before the last slash).
    CHECK(unpack_groupname(varname, -1, groupname) == 0);
    CHECK(std::strcmp(groupname, "forecast/model_a") == 0);

    // -2: just the trailing component (the leaf, i.e. the var name itself).
    CHECK(unpack_groupname(varname, -2, groupname) == 0);
    CHECK(std::strcmp(groupname, "temp") == 0);

    // 0, 1: the individual group path components, outermost first.
    CHECK(unpack_groupname(varname, 0, groupname) == 0);
    CHECK(std::strcmp(groupname, "forecast") == 0);
    CHECK(unpack_groupname(varname, 1, groupname) == 0);
    CHECK(std::strcmp(groupname, "model_a") == 0);
}

TEST_CASE("util: varname_no_groups splits leaf name from group path") {
    char sans_groups[1024], group[1024];

    varname_no_groups("/forecast/temp", sans_groups, group);
    CHECK(std::strcmp(sans_groups, "temp") == 0);
    CHECK(std::strcmp(group, "/forecast") == 0);

    // No groups at all: the whole name is the leaf, and groupname comes
    // back empty (not untouched garbage).
    varname_no_groups("temp", sans_groups, group);
    CHECK(std::strcmp(sans_groups, "temp") == 0);
    CHECK(group[0] == '\0');

    // A NULL groupname output pointer must be tolerated (some callers
    // only want the leaf name).
    varname_no_groups("forecast/temp", sans_groups, nullptr);
    CHECK(std::strcmp(sans_groups, "temp") == 0);
}

// Phase 10b (fuzzing pass): unpack_groupname()/varname_no_groups() assume
// their varname argument fits in MAX_NC_NAME (256) -- true for a single
// netCDF name, which the library itself caps at that length, but NOT true
// for the *group-path* prefix these functions actually receive in
// practice (built in file_netcdf.cc from nc_inq_grpname_full(), which has
// no length or depth limit of its own: netCDF-4/HDF5 group nesting isn't
// bounded). A file with enough nested groups produced two real,
// confirmed-under-ASan bugs here before this phase added a length guard:
// unpack_groupname()'s idx_slash[] stack array overflowing past >255
// slashes, and (independently, at a shorter length) its ts[] copy being
// silently truncated by snprintf while idx_slash[]'s indices -- computed
// against the original, untruncated string -- still pointed past that
// truncated content. Both functions now exit() with an error instead,
// matching this file's own established style for otherwise-impossible
// inputs. These tests pin correct behavior right up to that boundary; the
// exit() path itself is deliberately NOT exercised here, same reasoning as
// test_file_layer.cc not calling determine_file_type()'s exit() branch.
TEST_CASE("util: unpack_groupname/varname_no_groups handle many nested groups, up to the boundary") {
    // Single-character group names keep this comfortably under
    // MAX_NC_NAME (256) in total length while still exercising deep
    // nesting: 100 "a/" pairs (200 chars) + "leaf" (4) = 204.
    const int n_groups = 100;
    std::string varname;
    for (int i = 0; i < n_groups; i++) {
        varname += "a/";
    }
    varname += "leaf";
    REQUIRE(varname.size() < 256);

    char groupname[1024];
    CHECK(unpack_groupname(varname.c_str(), -2, groupname) == 0);
    CHECK(std::strcmp(groupname, "leaf") == 0);
    CHECK(unpack_groupname(varname.c_str(), 0, groupname) == 0);
    CHECK(std::strcmp(groupname, "a") == 0);
    CHECK(unpack_groupname(varname.c_str(), n_groups - 1, groupname) == 0);
    CHECK(std::strcmp(groupname, "a") == 0);
    CHECK(unpack_groupname(varname.c_str(), -1, groupname) == 0);
    CHECK(std::strlen(groupname) == varname.size() - 5 /* strlen("/leaf") */);

    char sans_groups[1024], group[1024];
    varname_no_groups(varname.c_str(), sans_groups, group);
    CHECK(std::strcmp(sans_groups, "leaf") == 0);
}

TEST_CASE("util: count_nslashes counts every slash, including adversarial inputs") {
    CHECK(count_nslashes("") == 0);
    CHECK(count_nslashes("noslashes") == 0);
    CHECK(count_nslashes("/") == 1);
    CHECK(count_nslashes("a/b/c") == 2);
    CHECK(count_nslashes("///") == 3);

    // count_nslashes itself has no fixed-size buffer -- confirm it stays
    // correct well past unpack_groupname's MAX_NC_NAME boundary, since it's
    // the one function of the three with no such limit to worry about.
    std::string many_slashes(1000, '/');
    CHECK(count_nslashes(many_slashes.c_str()) == 1000);
}
