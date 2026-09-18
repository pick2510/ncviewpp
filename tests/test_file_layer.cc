// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for the fi_*() dispatch layer (core/src/file.cc)
// -- "refine the architecture" plan, Phase 5a. Every existing test that
// touches file I/O opens files via netcdf_fi_initialize() directly and
// calls netcdf_*() functions, reaching straight past the fi_*() layer this
// file exists to pin down. That gap matters because Phase 6 intends to
// collapse fi_*()/netcdf_*() into one layer, and a collapse can't be
// proven safe against code nothing exercises.
//
// One thing this file's own tests had to confirm rather than assume,
// consistent with this plan's "inventory claims are leads, not facts"
// rule: file.cc's fi_*() forwarders all dispatch on the file-scope static
// `file_type`, which only ever holds FILE_TYPE_NETCDF (determined by
// determine_file_type(), the only setter, called via netcdf_fi_confirm()
// -- see file.cc). Every forwarder's `else` branch is fprintf+exit(-1),
// so testing the rejection branch here is out of scope: it would kill
// the test binary, not fail one assertion. determine_file_type() is
// tested for its accept path only.
//
// UPDATE (Phase 6): this file originally also pinned that
// Dataset::addVariable()'s `nfiles` parameter (threaded all the way
// through from fi_initialize()) was accepted but never read anywhere in
// its body -- confirmed by reading the function, not assumed. Phase 6
// removed that dead parameter from fi_initialize()/addVariables()/
// addVariable() entirely rather than leave it as documented dead weight,
// so the test that pinned its no-op-ness ("fi_initialize: the nfiles
// argument is threaded through but has no currently-observable effect")
// no longer applies and was deleted along with the parameter.
//
// UPDATE (Phase 6, continued): the 13 pure-forwarder fi_*() functions this
// file's "forwarder equivalence" section originally called by name no
// longer exist -- they were collapsed onto NetCDFFile methods (see
// dataset.h/dataset.cc), which was the point of writing this file in the
// first place: pin the dispatch layer's behavior so the collapse could be
// proven safe. The section below now calls the NetCDFFile methods
// directly (still comparing each against its underlying netcdf_*()
// counterpart) instead of the free functions that used to sit in front of
// them. This is a deliberate call-surface change, not a scope reduction:
// every equivalence this file originally asserted still holds, just
// against the method that now embodies it. BareFile below therefore owns
// a NetCDFFile (which closes itself on destruction, the same
// netcdf_fi_close() fi_close() used to dispatch to) instead of calling
// fi_close() by hand.
//
// The circular dependency this plan's round-3 survey found (file_netcdf.cc
// calling back UP into file.cc's fi_scannable_dims()/fi_n_dims() --
// file_netcdf.cc:200,455,552) is exercised implicitly by every test below
// that calls a netcdf_*() list/lookup function with `file_type` set, and
// explicitly by the two tests under "the circular call-back" further down.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/dataset.h"
#include "ncview/protos.h"
#include "support/session_fixture.h"
#include "test_udunits_helper.h"

using ncview_test::SessionFixture;

namespace {

// Writes a (time, lat, lon) file: temp(time,lat,lon) with units/long_name,
// lat/lon coordinate variables with units/long_name, and a "time" record
// (NC_UNLIMITED) dimension with a units attribute and, if non-empty, a
// calendar attribute -- enough surface to exercise every fi_*() forwarder
// at least once. Returns the path; caller must std::remove() it.
std::string make_layer_test_file(const char *var_name, int nt, int nlat, int nlon,
                                  const char *time_units, const char *calendar = "") {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_layer_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_time, dim_lat, dim_lon;
    REQUIRE(nc_def_dim(ncid, "time", NC_UNLIMITED, &dim_time) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lat", nlat, &dim_lat) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "lon", nlon, &dim_lon) == NC_NOERR);

    int var_time, var_lat, var_lon, var_data;
    REQUIRE(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_time, &var_time) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_time, "units", strlen(time_units), time_units) == NC_NOERR);
    if (calendar[0] != '\0')
        REQUIRE(nc_put_att_text(ncid, var_time, "calendar", strlen(calendar), calendar) == NC_NOERR);

    REQUIRE(nc_def_var(ncid, "lat", NC_FLOAT, 1, &dim_lat, &var_lat) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_lat, "units", 13, "degrees_north") == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_lat, "long_name", 8, "Latitude") == NC_NOERR);

    REQUIRE(nc_def_var(ncid, "lon", NC_FLOAT, 1, &dim_lon, &var_lon) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_lon, "units", 12, "degrees_east") == NC_NOERR);

    int dims[3] = {dim_time, dim_lat, dim_lon};
    REQUIRE(nc_def_var(ncid, var_name, NC_FLOAT, 3, dims, &var_data) == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_data, "units", 1, "K") == NC_NOERR);
    REQUIRE(nc_put_att_text(ncid, var_data, "long_name", 11, "temperature") == NC_NOERR);

    REQUIRE(nc_enddef(ncid) == NC_NOERR);

    std::vector<double> tvals(nt);
    for (int t = 0; t < nt; t++) tvals[t] = (double)t;
    size_t t_start[1] = {0}, t_count[1] = {(size_t)nt};
    REQUIRE(nc_put_vara_double(ncid, var_time, t_start, t_count, tvals.data()) == NC_NOERR);

    std::vector<float> latvals(nlat), lonvals(nlon);
    for (int i = 0; i < nlat; i++) latvals[i] = (float)i;
    for (int j = 0; j < nlon; j++) lonvals[j] = (float)j;
    REQUIRE(nc_put_var_float(ncid, var_lat, latvals.data()) == NC_NOERR);
    REQUIRE(nc_put_var_float(ncid, var_lon, lonvals.data()) == NC_NOERR);

    std::vector<float> data((size_t)nt * nlat * nlon);
    for (size_t i = 0; i < data.size(); i++) data[i] = (float)i;
    size_t d_start[3] = {0, 0, 0}, d_count[3] = {(size_t)nt, (size_t)nlat, (size_t)nlon};
    REQUIRE(nc_put_vara_float(ncid, var_data, d_start, d_count, data.data()) == NC_NOERR);

    REQUIRE(nc_close(ncid) == NC_NOERR);
    return path;
}

// Runs determine_file_type() on `path` -- the only way, outside file.cc, to
// set the file_type static every fi_*() forwarder dispatches on. Every test
// below needs this before calling anything in file.cc or file_netcdf.cc.
void run_determine_file_type(const std::string &path) {
    Stringlist *files = nullptr;
    stringlist_add_string(&files, path.c_str());
    determine_file_type(files);
    stringlist_delete_entire_list(files);
}

// Opens `path` via netcdf_fi_initialize() directly (NOT fi_initialize(),
// and NOT routed through Dataset) -- for the forwarder-equivalence tests,
// which want a bare fileid to call both the fi_*() and netcdf_*() side of
// each pair on, without a Dataset entry complicating cleanup. Matches
// test_file_netcdf.cc's/test_dataset.cc's own open_sample_file() pattern.
int open_bare(const std::string &path) {
    run_determine_file_type(path);
    return netcdf_fi_initialize(const_cast<char *>(path.c_str()));
}

// A file, its bare fileid (for comparing a NetCDFFile method's result
// against the underlying netcdf_*() call directly), and a NetCDFFile
// wrapping that same id -- closed automatically on destruction via
// NetCDFFile's own destructor (netcdf_fi_close(), the same call fi_close()
// used to dispatch to), so file_type still gets exercised on the way in
// via determine_file_type(), just not on the way out via fi_close()
// specifically (fi_close() is untouched by Phase 6 and stays covered by
// its own equivalence, not this fixture's teardown).
struct BareFile {
    std::string path;
    int fileid;
    NetCDFFile file;
    static constexpr int nt = 3, nlat = 4, nlon = 5;
    explicit BareFile(const char *var_name, const char *time_units = "days since 2000-01-01",
                       const char *calendar = "")
        : path(make_layer_test_file(var_name, nt, nlat, nlon, time_units, calendar)),
          fileid(open_bare(path)), file(fileid) {}
    ~BareFile() {
        std::remove(path.c_str());
    }
};

} // namespace

// ===================== The 13 collapsed fi_*() forwarders, now NetCDFFile methods =====================
// Each asserted to return exactly what its netcdf_*() counterpart returns
// for the same fileid/args -- a deliberately mechanical table whose value
// is that it must still pass, unchanged, after Phase 6's collapse (this
// file's job was to pin these before that happened, and now confirms it
// happened correctly).

TEST_CASE("NetCDFFile::listVars matches netcdf_fi_list_vars") {
    BareFile f("layer_list_vars");
    Stringlist *from_method = f.file.listVars();
    Stringlist *from_netcdf = netcdf_fi_list_vars(f.fileid);
    REQUIRE(from_method != nullptr);
    REQUIRE(from_netcdf != nullptr);
    REQUIRE(stringlist_len(from_method) == stringlist_len(from_netcdf));
    for (size_t i = 0; i < from_method->size(); i++)
        CHECK((*from_method)[i].string == (*from_netcdf)[i].string);
}

TEST_CASE("NetCDFFile::title matches netcdf_title") {
    BareFile f("layer_title");
    CHECK(f.file.title() == netcdf_title(f.fileid));
}

TEST_CASE("NetCDFFile::longVarName matches netcdf_long_var_name") {
    BareFile f("layer_long_name");
    CHECK(f.file.longVarName("layer_long_name") == netcdf_long_var_name(f.fileid, "layer_long_name"));
    CHECK(f.file.longVarName("layer_long_name") == "temperature");
}

TEST_CASE("NetCDFFile::varUnits matches netcdf_var_units") {
    BareFile f("layer_var_units");
    CHECK(f.file.varUnits("layer_var_units") == netcdf_var_units(f.fileid, "layer_var_units"));
    CHECK(f.file.varUnits("layer_var_units") == "K");
}

TEST_CASE("NetCDFFile::dimUnits matches netcdf_dim_units") {
    BareFile f("layer_dim_units");
    CHECK(f.file.dimUnits("lat") == netcdf_dim_units(f.fileid, "lat"));
    CHECK(f.file.dimUnits("lat") == "degrees_north");
}

TEST_CASE("NetCDFFile::nDims matches netcdf_fi_n_dims") {
    BareFile f("layer_n_dims");
    CHECK(f.file.nDims((char *)"layer_n_dims") == netcdf_fi_n_dims(f.fileid, (char *)"layer_n_dims"));
    CHECK(f.file.nDims((char *)"layer_n_dims") == 3);
}

TEST_CASE("NetCDFFile::scannableDims matches netcdf_scannable_dims") {
    BareFile f("layer_scannable");
    Stringlist *from_method = f.file.scannableDims((char *)"layer_scannable");
    Stringlist *from_netcdf = netcdf_scannable_dims(f.fileid, (char *)"layer_scannable");
    REQUIRE(from_method != nullptr);
    REQUIRE(from_netcdf != nullptr);
    REQUIRE(stringlist_len(from_method) == stringlist_len(from_netcdf));
    for (size_t i = 0; i < from_method->size(); i++)
        CHECK((*from_method)[i].string == (*from_netcdf)[i].string);
}

TEST_CASE("NetCDFFile::varSize matches netcdf_fi_var_size") {
    BareFile f("layer_var_size");
    size_t *from_method = f.file.varSize((char *)"layer_var_size");
    size_t *from_netcdf = netcdf_fi_var_size(f.fileid, (char *)"layer_var_size");
    REQUIRE(from_method != nullptr);
    REQUIRE(from_netcdf != nullptr);
    for (int i = 0; i < 3; i++)
        CHECK(from_method[i] == from_netcdf[i]);
    CHECK(from_method[0] == BareFile::nt);
    CHECK(from_method[1] == BareFile::nlat);
    CHECK(from_method[2] == BareFile::nlon);
}

TEST_CASE("NetCDFFile::dimIdToName matches netcdf_dim_id_to_name") {
    BareFile f("layer_dim_id_to_name");
    CHECK(f.file.dimIdToName("layer_dim_id_to_name", 1) ==
          netcdf_dim_id_to_name(f.fileid, "layer_dim_id_to_name", 1));
    CHECK(f.file.dimIdToName("layer_dim_id_to_name", 1) == "lat");
}

TEST_CASE("NetCDFFile::dimNameToId matches netcdf_dim_name_to_id") {
    BareFile f("layer_dim_name_to_id");
    CHECK(f.file.dimNameToId((char *)"layer_dim_name_to_id", (char *)"lat") ==
          netcdf_dim_name_to_id(f.fileid, (char *)"layer_dim_name_to_id", (char *)"lat"));
    CHECK(f.file.dimNameToId((char *)"layer_dim_name_to_id", (char *)"lat") == 1);
    // A dim name that doesn't exist on the variable: both sides must agree
    // on the -1 miss too, not just the hit.
    CHECK(f.file.dimNameToId((char *)"layer_dim_name_to_id", (char *)"nope") == -1);
}

TEST_CASE("netcdf_dim_name_to_id: a nonexistent variable name reports an error and returns -1, not exit()s (Phase 12e)") {
    // Regression test: netcdf_dim_name_to_id() used to exit(-1) when
    // nc_inq_varid_grp() couldn't find the named variable in the file --
    // safe to convert only once every caller checks for -1 before
    // indexing anything with the result (Phase 12e, steps 1-2). This
    // exercises the specific exit() site that "var not found" used to
    // trigger; the existing test above already covers the *other* -1
    // path (a dim name not found on an otherwise-valid variable).
    ncview_test::SessionFixture fx;
    BareFile f("layer_dim_name_to_id_novar");
    resetStubRecording();

    int result = netcdf_dim_name_to_id(f.fileid, (char *)"no_such_variable", (char *)"lat");
    CHECK(result == -1); // reaching this line at all is the main point

    bool reported_error = false;
    for (const auto &call : g_recorded_calls)
        if (call == "in_dialog") // in_error() forwards to in_dialog()
            reported_error = true;
    CHECK(reported_error);
}

TEST_CASE("NetCDFFile::dimLongname matches netcdf_dim_longname") {
    BareFile f("layer_dim_longname");
    CHECK(f.file.dimLongname("lat") == netcdf_dim_longname(f.fileid, "lat"));
    CHECK(f.file.dimLongname("lat") == "Latitude");
}

TEST_CASE("NetCDFFile::recdimId matches netcdf_fi_recdim_id -- and had no file_type guard at all, even as fi_recdim_id()") {
    // Unlike every other collapsed forwarder, file.cc's old fi_recdim_id()
    // had no `if (file_type != FILE_TYPE_NETCDF)` check at all -- it
    // unconditionally called netcdf_fi_recdim_id(). That made (and makes)
    // this equivalence trivially true by construction rather than by
    // dispatch, which is itself the behavior this test pins: the collapse
    // must not have "fixed" this by adding a guard as an incidental side
    // effect, since that would be a behavior change riding along on a
    // refactor.
    BareFile f("layer_recdim_id");
    CHECK(f.file.recdimId() == netcdf_fi_recdim_id(f.fileid));
    CHECK(f.file.recdimId() >= 0); // "time" is the unlimited dim
}

TEST_CASE("NetCDFFile::fillAuxData matches netcdf_fill_aux_data") {
    BareFile f("layer_fill_aux");
    // Neither function reads fdb->file (only ->filename/->recdim_units/
    // ->aux_data/->ut_unit_ptr are touched), so two bare FDBlists with no
    // NetCDFFile attached are enough to compare -- avoids constructing a
    // second NetCDFFile over the same already-tracked fileid, which would
    // double-close it.
    FDBlist via_method;
    via_method.filename = f.path;
    via_method.aux_data = std::make_unique<NetCDFOptions>();
    f.file.fillAuxData((char *)"layer_fill_aux", &via_method);

    FDBlist via_netcdf;
    via_netcdf.filename = f.path;
    via_netcdf.aux_data = std::make_unique<NetCDFOptions>();
    netcdf_fill_aux_data(f.fileid, (char *)"layer_fill_aux", &via_netcdf);

    CHECK(via_method.recdim_units == via_netcdf.recdim_units);
    CHECK(via_method.recdim_units == "days since 2000-01-01");
    CHECK((via_method.aux_data != nullptr) == (via_netcdf.aux_data != nullptr));
}

TEST_CASE("netcdf_fill_aux_data: a null aux_data no longer crashes (Phase 6 regression test)") {
    // Phase 5a found -- via a real SIGSEGV while constructing a bare
    // FDBlist for the test above, before it pre-allocated aux_data --
    // that netcdf_fill_aux_data() unconditionally dereferenced
    // fdb->aux_data.get() once the target variable has any attributes at
    // all (it does here -- "units"/"long_name"), with no null check.
    // Safe in production only because new_fdblist() (dataset.cc), the
    // sole real caller, always pre-allocates it first. 5a pinned this
    // as-is (tests-only phase); Phase 6 added a guard instead of leaving
    // it as a latent crash, since a collapse touching this function is
    // exactly the point at which "leave it as a known gap" stops being
    // the safer choice. This test proves the guard: recdim_units (set
    // before the guarded section) still gets filled in, and the call
    // simply returns without touching the null aux_data instead of
    // segfaulting.
    BareFile f("layer_fill_aux_null");

    FDBlist fdb;
    fdb.filename = f.path;
    REQUIRE(fdb.aux_data == nullptr);

    netcdf_fill_aux_data(f.fileid, (char *)"layer_fill_aux_null", &fdb);

    CHECK(fdb.recdim_units == "days since 2000-01-01");
    CHECK(fdb.aux_data == nullptr);
}

// ===================== fi_dim_calendar's override branch =====================

TEST_CASE("fi_dim_calendar: with no command-line override, forwards to netcdf_dim_calendar") {
    SessionFixture fx; // options.calendar defaults to empty on a fresh session
    BareFile f("layer_calendar_forward", "days since 2000-01-01", "noleap");
    CHECK(options.calendar.empty());
    CHECK(fi_dim_calendar(f.fileid, "time") == netcdf_dim_calendar(f.fileid, "time"));
    CHECK(fi_dim_calendar(f.fileid, "time") == "noleap");
}

TEST_CASE("fi_dim_calendar: options.calendar, when set, overrides the file's own calendar attribute") {
    SessionFixture fx;
    BareFile f("layer_calendar_override", "days since 2000-01-01", "noleap");
    REQUIRE(fi_dim_calendar(f.fileid, "time") == "noleap"); // file's own attribute, unset override

    options.calendar = "360_day";
    // file.cc:151-155 -- the command line wins outright; it doesn't even
    // consult the file for this dim once options.calendar is non-empty.
    CHECK(fi_dim_calendar(f.fileid, "time") == "360_day");
}

// ===================== determine_file_type =====================

TEST_CASE("determine_file_type: accepts a real netCDF file and sets file_type for later fi_*() calls") {
    // The rejection path (a non-netCDF or nonexistent file) is NOT tested
    // here: determine_file_type() exit(-1)s outright on failure (file.cc),
    // which would terminate the test binary rather than fail one assertion.
    // This is a known, deliberate coverage gap -- see this file's header
    // comment.
    BareFile f("layer_determine_type"); // BareFile's ctor already ran determine_file_type()
    // If file_type hadn't been set to FILE_TYPE_NETCDF, this and every
    // other fi_*()/NetCDFFile-method call in this file would have
    // exit(-1)'d already.
    CHECK(f.file.nDims((char *)"layer_determine_type") == 3);
}

// ===================== fi_initialize: open + fi_list_vars + Dataset::addVariables =====================

TEST_CASE("fi_initialize: opens the file and adds its variable(s) to the Dataset") {
    SessionFixture fx;
    ensure_ncview_misc_initialized();
    std::string path = make_layer_test_file("layer_fi_init", 3, 4, 5, "days since 2000-01-01");
    run_determine_file_type(path);

    int fileid = fi_initialize(const_cast<char *>(path.c_str()));

    NCVar *var = g_dataset.findVariable("layer_fi_init");
    REQUIRE(var != nullptr);
    CHECK(var->n_dims == 3);
    REQUIRE(var->files.size() == 1);
    CHECK(var->files[0]->id() == fileid);
    CHECK(var->is_virtual == false);

    std::remove(path.c_str());
}

// ===================== The circular call-back =====================
// file_netcdf.cc calls back UP into file.cc (fi_scannable_dims() at
// file_netcdf.cc:200, fi_n_dims() at :455 and :552) -- confirmed by the
// round-3 survey and, independently, by test_file_netcdf.cc's own header
// comment. Both tests below exist specifically as a safety net for Phase 6,
// which has to break this cycle deliberately rather than by accident.

TEST_CASE("the circular call-back: netcdf_fi_list_vars calls back into fi_scannable_dims") {
    // netcdf_fi_list_vars()'s inner loop (file_netcdf.cc:191-216) calls
    // fi_scannable_dims() -- not netcdf_scannable_dims() directly -- to
    // decide whether a variable belongs on the displayable list. With
    // file_type properly set, this round-trip must succeed and the data
    // variable (which has 2 scannable dims of size >1: lat, lon) must show
    // up on the list.
    BareFile f("layer_circular_scannable");
    Stringlist *vars = netcdf_fi_list_vars(f.fileid);
    REQUIRE(vars != nullptr);
    bool found = false;
    for (const auto &e : *vars) if (e.string == "layer_circular_scannable") found = true;
    CHECK(found);
}

TEST_CASE("the circular call-back: netcdf_dim_name_to_id/netcdf_dim_id_to_name call back into fi_n_dims") {
    // netcdf_dim_name_to_id() and netcdf_dim_id_to_name() (file_netcdf.cc:
    // 455, 552) both call fi_n_dims() -- not netcdf_fi_n_dims() -- to learn
    // how many dims the variable has before resolving the requested one.
    // With file_type set this must resolve correctly in both directions.
    BareFile f("layer_circular_ndims");
    int lat_id = netcdf_dim_name_to_id(f.fileid, (char *)"layer_circular_ndims", (char *)"lon");
    CHECK(lat_id == 2); // third dim of (time,lat,lon)
    std::string name = netcdf_dim_id_to_name(f.fileid, (char *)"layer_circular_ndims", lat_id);
    CHECK(name == "lon");
}
