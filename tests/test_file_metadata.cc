// Copyright (C) 2026 Dominik Strebel
//
// New coverage for file_netcdf.cc's netCDF-4 group-handling code -- "refine
// the architecture" plan, Phase 6. The round-3 survey found roughly 350
// lines of group support (ncdf_fi_name_of_group()/netcdf_fi_list_vars_inner()/
// netcdf_fi_list_vars_v4()/nc_inq_varid_grp(), file_netcdf.cc) at zero
// direct coverage: every existing fixture (NcFixture and every hand-rolled
// one in the other test files) builds a flat, single-group file. This file
// exercises the group path directly, plus the attribute-precedence edge
// cases (missing/empty/wrong-typed) that netcdf_get_char_att() documents
// but nothing tests, and the "char storage dimension" shape (a plain-text
// field with no corresponding coordinate variable) netcdf_scannable_dims()/
// netcdf_fi_list_vars_inner() were never run against.
//
// NcFixture doesn't support netCDF-4 groups (its own header comment lists
// this as out of scope), so this fixture is hand-rolled with raw nc_*()
// calls, the same pattern test_file_layer.cc/test_multifile.cc use for
// shapes NcFixture doesn't cover.
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

namespace {

// Layout built by make_metadata_test_file():
//
//   /x                (dim, size 5)
//   /t                (dim, NC_UNLIMITED)
//   /namelen          (dim, size 8 -- a plain char-storage dim, no
//                      corresponding coordinate variable)
//   /root_var(x)      float, units="K", long_name="Root Variable"
//   /ts_var(t)        float, units="s"  -- exercises an unlimited dim
//                      through the group-aware path
//   /label(namelen)   NC_CHAR -- a text field, not a numeric variable;
//                      pins whatever netcdf_fi_list_vars_inner()/
//                      netcdf_scannable_dims() actually do with one, since
//                      neither function excludes by nc_type (confirmed by
//                      reading file_netcdf.cc, not assumed)
//   /grp1/g1_var(x)   float, NO units attribute at all (missing case) --
//                      shares the root's "x" dim, which netCDF-4 groups are
//                      allowed to do
//   /grp1/grp2/g2_var(x)
//                     float, long_name="" (empty-string case) and units
//                     stored as NC_INT instead of NC_CHAR (wrong-type case)
//
// Returns the path; caller must std::remove() it.
std::string make_metadata_test_file() {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_meta_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER | NC_NETCDF4, &ncid) == NC_NOERR);

    int dim_x, dim_t, dim_namelen;
    REQUIRE(nc_def_dim(ncid, "x", 5, &dim_x) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "t", NC_UNLIMITED, &dim_t) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "namelen", 8, &dim_namelen) == NC_NOERR);

    int var_root, var_ts, var_label;
    REQUIRE(nc_def_var(ncid, "root_var", NC_FLOAT, 1, &dim_x, &var_root) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_root, "units", 1, "K") == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_root, "long_name", 13, "Root Variable") == NC_NOERR);

    REQUIRE(nc_def_var(ncid, "ts_var", NC_FLOAT, 1, &dim_t, &var_ts) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_ts, "units", 1, "s") == NC_NOERR);

    REQUIRE(nc_def_var(ncid, "label", NC_CHAR, 1, &dim_namelen, &var_label) == NC_NOERR);

    int grp1, grp2;
    REQUIRE(nc_def_grp(ncid, "grp1", &grp1) == NC_NOERR);
    REQUIRE(nc_def_grp(grp1, "grp2", &grp2) == NC_NOERR);

    int var_g1;
    // Groups may reference a dimension defined in an ancestor group by id
    // directly -- no re-declaration needed.
    REQUIRE(nc_def_var(grp1, "g1_var", NC_FLOAT, 1, &dim_x, &var_g1) == NC_NOERR);
    // Deliberately no units/long_name attribute at all on g1_var.

    int var_g2;
    REQUIRE(nc_def_var(grp2, "g2_var", NC_FLOAT, 1, &dim_x, &var_g2) == NC_NOERR);
    REQUIRE(nc_put_att_text(grp2, var_g2, "long_name", 0, "") == NC_NOERR);
    int wrong_type_units = 42;
    REQUIRE(nc_put_att_int(grp2, var_g2, "units", NC_INT, 1, &wrong_type_units) == NC_NOERR);

    REQUIRE(nc_enddef(ncid) == NC_NOERR);

    std::vector<float> xvals(5);
    for (int i = 0; i < 5; i++) xvals[i] = (float)i;
    REQUIRE(nc_put_var_float(ncid, var_root, xvals.data()) == NC_NOERR);
    REQUIRE(nc_put_var_float(grp1, var_g1, xvals.data()) == NC_NOERR);
    REQUIRE(nc_put_var_float(grp2, var_g2, xvals.data()) == NC_NOERR);

    std::vector<float> tvals(3);
    for (int i = 0; i < 3; i++) tvals[i] = (float)i;
    size_t t_start[1] = {0}, t_count[1] = {3};
    REQUIRE(nc_put_vara_float(ncid, var_ts, t_start, t_count, tvals.data()) == NC_NOERR);

    const char *label_text = "abcdefg";
    size_t l_start[1] = {0}, l_count[1] = {8};
    REQUIRE(nc_put_vara_text(ncid, var_label, l_start, l_count, label_text) == NC_NOERR);

    REQUIRE(nc_close(ncid) == NC_NOERR);
    return path;
}

void run_determine_file_type(const std::string &path) {
    Stringlist *files = nullptr;
    stringlist_add_string(&files, path.c_str());
    determine_file_type(files);
    stringlist_delete_entire_list(files);
}

struct MetaFile {
    std::string path;
    int fileid;
    MetaFile() : path(make_metadata_test_file()) {
        run_determine_file_type(path);
        fileid = netcdf_fi_initialize(const_cast<char *>(path.c_str()));
    }
    ~MetaFile() {
        netcdf_fi_close(fileid);
        std::remove(path.c_str());
    }
};

bool contains(Stringlist *list, const char *name) {
    if (list == nullptr) return false;
    for (const auto &e : *list)
        if (e.string == name) return true;
    return false;
}

} // namespace

// ===================== netCDF-4 groups and nested groups =====================

TEST_CASE("netcdf_fi_list_vars finds displayable variables in nested groups") {
    MetaFile f;
    Stringlist *vars = netcdf_fi_list_vars(f.fileid);
    REQUIRE(vars != nullptr);
    CHECK(contains(vars, "root_var"));
    CHECK(contains(vars, "ts_var"));
    // One level deep.
    CHECK(contains(vars, "grp1/g1_var"));
    // Two levels deep -- the recursive case ncdf_fi_name_of_group()/
    // netcdf_fi_list_vars_v4() exist for.
    CHECK(contains(vars, "grp1/grp2/g2_var"));
}

TEST_CASE("a variable name with an explicit group path resolves via nc_inq_varid_grp") {
    MetaFile f;
    CHECK(netcdf_fi_n_dims(f.fileid, (char *)"grp1/g1_var") == 1);
    size_t *size = netcdf_fi_var_size(f.fileid, (char *)"grp1/g1_var");
    REQUIRE(size != nullptr);
    CHECK(size[0] == 5);

    // Two levels deep.
    CHECK(netcdf_fi_n_dims(f.fileid, (char *)"grp1/grp2/g2_var") == 1);
    Stringlist *dims = netcdf_scannable_dims(f.fileid, (char *)"grp1/grp2/g2_var");
    REQUIRE(dims != nullptr);
    REQUIRE(stringlist_len(dims) >= 1);
    CHECK((*dims)[0].string == "x");
}

TEST_CASE("a group-scoped variable sharing an ancestor group's dimension reads correctly") {
    MetaFile f;
    // g1_var shares grp1's parent's "x" dim by id, not by re-declaring it --
    // confirm the actual float data round-trips through the group-aware
    // read path, not just its metadata.
    float val;
    size_t start[1] = {2}, count[1] = {1};
    int gid, varid;
    // Resolved with the plain netCDF-C group API (not nc_inq_varid_grp(),
    // which is file_netcdf.cc-internal with no protos.h declaration) --
    // this confirms the data itself, independent of ncview's own group
    // resolution exercised by the tests above.
    REQUIRE(nc_inq_grp_ncid(f.fileid, "grp1", &gid) == NC_NOERR);
    REQUIRE(nc_inq_varid(gid, "g1_var", &varid) == NC_NOERR);
    REQUIRE(nc_get_vara_float(gid, varid, start, count, &val) == NC_NOERR);
    CHECK(val == 2.0f);
}

TEST_CASE("netcdf_dim_units: a dim belonging to a variable nested 2 groups deep degrades instead of aborting (Phase 12i)") {
    // Regression test: netcdf_dim_id_to_name() qualifies a dim name with
    // the OWNING VARIABLE's group path, not the dim's own -- so g2_var's
    // "x" dim (actually defined at the file root, shared by id) is named
    // "grp1/grp2/x" here. netcdf_dimvar_id() then splits that at the LAST
    // slash (varname_no_groups()), handing nc_inq_grp_ncid() the two-level
    // path "grp1/grp2" -- which it can't resolve, since it only accepts a
    // simple, one-level group name. Before Phase 12i this exit()'d the
    // whole process during ordinary variable-metadata lookup, on a
    // perfectly valid netCDF-4 file with no unusual input.
    MetaFile f;
    CHECK(netcdf_dim_units(f.fileid, "grp1/grp2/x") == "");
    CHECK(netcdf_dim_longname(f.fileid, "grp1/grp2/x") == "grp1/grp2/x"); // falls back to the (qualified) dim name itself
}

TEST_CASE("netcdf_att_string: a variable inside a group resolves instead of aborting (Phase 12i)") {
    // Regression test: netcdf_att_string() was the only function left in
    // this file using a plain nc_inq_varid() (root-group-only) rather than
    // the group-aware nc_inq_varid_grp() every sibling function uses --
    // View::information()'s "Info" display always passes a group-qualified
    // name (netcdf_fi_list_vars_inner() prefixes every variable with its
    // group path), so this was a real correctness bug, not just a missing
    // safety check: the plain lookup could never have found a grouped
    // variable at all. Before Phase 12i, "Info" on any grouped variable
    // exit()'d the whole process.
    MetaFile f;
    resetStubRecording();

    std::string result = netcdf_att_string(f.fileid, "grp1/grp2/g2_var");

    // Reaching this line at all is the main point -- the old code exit()d
    // before returning anything.
    CHECK(result.find("long_name") != std::string::npos);
}

// ===================== Attribute precedence: missing / empty / wrong-typed =====================

TEST_CASE("a variable with no units attribute at all: netcdf_var_units returns empty") {
    MetaFile f;
    CHECK(netcdf_var_units(f.fileid, "grp1/g1_var") == "");
    CHECK(netcdf_long_var_name(f.fileid, "grp1/g1_var") == "");
}

TEST_CASE("an empty-string long_name attribute is indistinguishable from a missing one") {
    // netcdf_get_char_att()'s own comment documents this: a zero-length
    // attribute value returns the same empty string as "attribute not
    // found at all" -- pinning that here, not assuming it.
    MetaFile f;
    CHECK(netcdf_long_var_name(f.fileid, "grp1/grp2/g2_var") == "");
}

TEST_CASE("a units attribute stored as the wrong netCDF type (NC_INT, not NC_CHAR) is treated as absent") {
    MetaFile f;
    CHECK(netcdf_var_units(f.fileid, "grp1/grp2/g2_var") == "");
}

TEST_CASE("root_var's real units/long_name still read correctly alongside the edge cases above") {
    // Not every variable in this fixture is an edge case -- confirm the
    // straightforward path still works in the same file.
    MetaFile f;
    CHECK(netcdf_var_units(f.fileid, "root_var") == "K");
    CHECK(netcdf_long_var_name(f.fileid, "root_var") == "Root Variable");
}

// ===================== Unlimited dims, through the group-aware path =====================

TEST_CASE("an unlimited (record) dimension var reports correctly via the group-aware calls") {
    MetaFile f;
    CHECK(netcdf_fi_n_dims(f.fileid, (char *)"ts_var") == 1);
    Stringlist *dims = netcdf_scannable_dims(f.fileid, (char *)"ts_var");
    REQUIRE(dims != nullptr);
    REQUIRE(stringlist_len(dims) == 1);
    CHECK((*dims)[0].string == "t");
    CHECK(netcdf_var_units(f.fileid, "ts_var") == "s");
}

// ===================== A char (text) storage dimension =====================

TEST_CASE("a char-typed text variable: excluded from the displayable list, but still queryable by name (Phase 12j)") {
    // "label" is NC_CHAR, not a numeric field. Before Phase 12j,
    // netcdf_fi_list_vars_inner() didn't exclude by nc_type at all --
    // displayability was decided purely by dimension count/size, so a
    // sufficiently large NC_CHAR variable was offered as selectable
    // alongside numeric ones, and selecting it crashed the whole process
    // in netcdf_fi_get_data() (nc_get_vara_float() can't read NC_CHAR
    // data). Phase 12j added a type filter at listing time specifically
    // to close this gap, so "label" is now correctly excluded from the
    // displayable list -- this replaces the old "pinning actual (not
    // assumed) behavior" characterization test, whose whole point was to
    // surface exactly this quirk so it could be fixed.
    //
    // netcdf_fi_n_dims()/netcdf_scannable_dims() look a variable up by
    // name directly, independent of the displayable-variable list, so
    // "label" remains fully queryable by name -- only its presence in
    // the *listing* changed.
    MetaFile f;
    Stringlist *vars = netcdf_fi_list_vars(f.fileid);
    REQUIRE(vars != nullptr);
    CHECK_FALSE(contains(vars, "label"));

    CHECK(netcdf_fi_n_dims(f.fileid, (char *)"label") == 1);
    Stringlist *dims = netcdf_scannable_dims(f.fileid, (char *)"label");
    REQUIRE(dims != nullptr);
    REQUIRE(stringlist_len(dims) == 1);
    CHECK((*dims)[0].string == "namelen");
}

TEST_CASE("netcdf_fi_get_data: an NC_CHAR variable degrades to FILL_FLOAT instead of aborting (Phase 12j)") {
    // netcdf_fi_get_data() reads through nc_get_vara_float(), which
    // cannot read NC_CHAR data (confirmed empirically in Phase 12g,
    // fails with NC_ECHAR every time). Before Phase 12j this exit()'d
    // the whole process -- the type filter above stops "label" from
    // being offered via the display list, but this function is also
    // reachable directly by name (e.g. a "coordinates" attribute naming
    // a char variable, read at file-open time, never through the
    // display list) -- so it needs its own backstop regardless of the
    // filter.
    MetaFile f;
    size_t start[1] = {0};
    size_t count[1] = {8};
    float data[8];
    // Before the fix, this call never returns -- the process exit()'d.
    netcdf_fi_get_data(f.fileid, (char *)"label", start, count, data, NULL);
    for (int i = 0; i < 8; i++)
        CHECK(data[i] == FILL_FLOAT);
}

TEST_CASE("netcdf_fill_value: a grouped variable with no _FillValue attribute resolves a sane default (Phase 12j)") {
    // netcdf_fill_value()'s default-value branch used to call
    // nc_inq_vartype(file_id, varid, ...) -- file_id is the ROOT id, but
    // varid was resolved via nc_inq_varid_grp() and is group-relative.
    // For "g1_var" (in grp1, no _FillValue/missing_value attribute) this
    // queried the wrong variable's type against the root group instead
    // of grp1's, and on failure would leave *v uninitialized.
    //
    // Honest limitation: in THIS fixture the bug is not distinguishable
    // by this assertion alone -- g1_var's group-relative varid (0)
    // coincidentally collides with root_var's root-group varid (also 0,
    // also NC_FLOAT), and every other root variable that could collide
    // (label, NC_CHAR) falls into this same switch's own default: case,
    // which also yields NC_FILL_FLOAT. So passing file_id instead of gid
    // here happens to produce the same visible result in this specific
    // layout; verified by manually reverting the fix and re-running this
    // test, which still passed. The fix itself (gid instead of file_id,
    // matching every other lookup already in this function) was
    // confirmed correct by direct code inspection instead. This test is
    // retained as a smoke test for the code path, not as proof of the
    // bug.
    MetaFile f;
    float v = -12345.0f; // deliberately not NC_FILL_FLOAT, so a no-op fill_value lookup would be caught
    netcdf_fill_value(f.fileid, (char *)"grp1/g1_var", &v, NULL);
    CHECK(v == NC_FILL_FLOAT);
}

// ===================== An attribute of a modern netCDF-4 datatype =====================

namespace {
// A separate, minimal fixture: netcdf_att_string()/netcdf_global_att_string()
// only need one variable with one modern-typed attribute, not the whole
// group layout above.
struct UnhandledTypeFile {
    std::string path;
    int fileid;
    UnhandledTypeFile() : path(make_unhandled_type_test_file()) {
        run_determine_file_type(path);
        fileid = netcdf_fi_initialize(const_cast<char *>(path.c_str()));
    }
    ~UnhandledTypeFile() {
        netcdf_fi_close(fileid);
        std::remove(path.c_str());
    }
    static std::string make_unhandled_type_test_file() {
        auto tmpl = (std::filesystem::temp_directory_path() / "ncview_uint64_att_XXXXXX").string();
        int fd = mkstemp(&tmpl[0]);
        REQUIRE(fd >= 0);
        close(fd);
        std::string p = tmpl;

        int ncid, dim_x, var;
        REQUIRE(nc_create(p.c_str(), NC_CLOBBER | NC_NETCDF4, &ncid) == NC_NOERR);
        REQUIRE(nc_def_dim(ncid, "x", 3, &dim_x) == NC_NOERR);
        REQUIRE(nc_def_var(ncid, "v", NC_FLOAT, 1, &dim_x, &var) == NC_NOERR);
        // A normal, handled-type attribute alongside the modern one, so the
        // test can confirm the rest of the display still works -- the fix
        // is "skip the one bad attribute", not "give up on all of them".
        REQUIRE(nc_put_att_text(ncid, var, "units", 1, "K") == NC_NOERR);
        unsigned long long modern_val = 42;
        REQUIRE(nc_put_att_ulonglong(ncid, var, "modern_attr", NC_UINT64, 1, &modern_val) == NC_NOERR);
        // A global attribute of the same unhandled type, so
        // netcdf_global_att_string()'s identical fix gets exercised too.
        REQUIRE(nc_put_att_ulonglong(ncid, NC_GLOBAL, "modern_global_attr", NC_UINT64, 1, &modern_val) == NC_NOERR);
        REQUIRE(nc_enddef(ncid) == NC_NOERR);

        std::vector<float> xvals = {0, 1, 2};
        REQUIRE(nc_put_var_float(ncid, var, xvals.data()) == NC_NOERR);
        REQUIRE(nc_close(ncid) == NC_NOERR);
        return p;
    }
};
} // namespace

TEST_CASE("netcdf_att_string: a variable attribute of an unhandled netCDF-4 datatype (NC_UINT64) is skipped, not exit()ed (Phase 12d)") {
    // Regression test: the switch inside netcdf_att_string() only lists
    // the netCDF types that existed in 1993 (BYTE/CHAR/SHORT/LONG/FLOAT/
    // DOUBLE/NAT); any newer type -- NC_UINT64 here -- used to exit() the
    // whole process the instant this variable's info was displayed, on a
    // perfectly valid netCDF-4 file.
    UnhandledTypeFile f;
    resetStubRecording();

    std::string result = netcdf_att_string(f.fileid, "v");

    // Reaching this line at all is the main point -- the old code exit()d
    // before returning anything.
    CHECK(result.find("units") != std::string::npos); // the handled attribute still shows
    CHECK(result.find("modern_attr") != std::string::npos); // named, even though its value is skipped
    CHECK(result.find("skipped") != std::string::npos);

    bool reported_error = false;
    for (const auto &call : g_recorded_calls)
        if (call == "in_dialog") // in_error() forwards to in_dialog()
            reported_error = true;
    CHECK(reported_error);
}

TEST_CASE("netcdf_global_att_string: a global attribute of an unhandled netCDF-4 datatype (NC_UINT64) is skipped, not exit()ed (Phase 12d)") {
    UnhandledTypeFile f;
    resetStubRecording();

    // netcdf_att_string() calls netcdf_global_att_string() internally
    // (appending global attributes after the per-variable ones), so this
    // exercises both fixes in the same call -- confirmed by checking for
    // the global attribute's own name in the result.
    std::string result = netcdf_att_string(f.fileid, "v");

    CHECK(result.find("modern_global_attr") != std::string::npos);
    CHECK(result.find("Global attributes") != std::string::npos);

    bool reported_error = false;
    for (const auto &call : g_recorded_calls)
        if (call == "in_dialog")
            reported_error = true;
    CHECK(reported_error);
}
