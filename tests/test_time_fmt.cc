// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for ncview's time-axis formatting and granularity
// logic: fmt_time() (core/src/util.cc, the single dispatch point core
// actually calls) and the three time standards it fans out to --
// udu_fmt_time() (core/src/udu.cc, TimeStandard::Udunits), epic_fmt_time()
// (core/src/epic_time.cc, TimeStandard::Epic0), and fmt_time()'s own inline
// TimeStandard::Months formatting -- plus udu_calc_tgran()'s granularity
// classification. test_calendar.cc already covers calcalcs.cc's raw
// calendar arithmetic and udu_utistime(); none of that reaches the actual
// display-string formatting this file guards. See modernization.md Phase
// 0b.
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "ncview/calcalcs.h"
#include "test_udunits_helper.h"

namespace {

// A minimal timelike NCDim -- only the fields fmt_time()/udu_fmt_time()/
// epic_fmt_time() actually read.
NCDim make_time_dim(char *units, char *calendar, TimeStandard time_std, TimeGranularity tgran) {
    NCDim d{};
    d.name = "time";
    d.units = units ? units : "";
    d.calendar = calendar ? calendar : "";
    d.timelike = 1;
    d.time_std = time_std;
    d.tgran = tgran;
    return d;
}

std::string run_fmt_time(NCDim &dim, double val, int include_granularity = 1) {
    char buf[128];
    fmt_time(buf, sizeof(buf), val, &dim, include_granularity);
    return std::string(buf);
}

// Builds a synthetic netCDF file with a single time-only variable, spaced
// step_days apart, and wires it into a real NCVar via the same
// Dataset::addVariable()/fill_dim_structs() path core itself uses (this is
// what udu_calc_tgran() actually needs: a real NCDim with a populated
// ->units string and a real file to read two sample values back from via
// Dataset::dimValue() -- there's no lighter-weight way to exercise it).
struct TgranFixture {
    std::string path;
    int fileid;
    NCVar *var;

    TgranFixture(const char *var_name, int n, double step_days) {
        auto tmpl = (std::filesystem::temp_directory_path() / "ncview_tgran_XXXXXX").string();
        int fd = mkstemp(&tmpl[0]);
        REQUIRE(fd >= 0);
        close(fd);
        path = tmpl;

        // Two variables, matching the shape test_file_netcdf.cc's own
        // fixture uses: a "time" coordinate variable (a dimvar -- named
        // identically to its dimension, netCDF's own coordinate-variable
        // convention, and the only place fill_dim_structs() looks for a
        // dimension's ->units) plus a data variable using it as its one
        // dimension. udu_calc_tgran() needs the *data* variable's NCVar,
        // not the dimvar's -- v->dim[0]->units comes from the dimvar.
        int ncid;
        REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
        int dim_time;
        REQUIRE(nc_def_dim(ncid, "time", n, &dim_time) == NC_NOERR);
        int var_time;
        REQUIRE(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dim_time, &var_time) == NC_NOERR);
        std::string units = "days since 2000-01-01";
        REQUIRE(nc_put_att_text(ncid, var_time, "units", units.size(), units.c_str()) == NC_NOERR);
        int var_data;
        REQUIRE(nc_def_var(ncid, var_name, NC_FLOAT, 1, &dim_time, &var_data) == NC_NOERR);
        REQUIRE(nc_enddef(ncid) == NC_NOERR);
        std::vector<double> vals(n);
        for (int i = 0; i < n; i++) vals[i] = i * step_days;
        REQUIRE(nc_put_var_double(ncid, var_time, vals.data()) == NC_NOERR);
        std::vector<float> data_vals(n, 0.0f);
        REQUIRE(nc_put_var_float(ncid, var_data, data_vals.data()) == NC_NOERR);
        REQUIRE(nc_close(ncid) == NC_NOERR);

        Stringlist *files = nullptr;
        stringlist_add_string(&files, path.c_str());
        determine_file_type(files);
        stringlist_delete_entire_list(files);
        fileid = netcdf_fi_initialize(const_cast<char *>(path.c_str()));

        g_dataset.addVariable(var_name, fileid, path.c_str());
        var = g_dataset.findVariable(var_name);
        REQUIRE(var != nullptr);
    }
    ~TgranFixture() {
        // Do NOT netcdf_fi_close(fileid) here: Dataset::addVariable() routes
        // every fileid through trackFile() (OOP_redesign Step 5), which took
        // ownership of it and will close it itself when g_dataset is
        // destroyed -- closing it again here would double-close it (see
        // test_varlist.cc's identical note).
        std::remove(path.c_str());
    }
};

} // namespace

TEST_CASE("fmt_time: TimeStandard::Udunits, standard calendar, day granularity") {
    ensure_ncview_misc_initialized();
    NCDim dim = make_time_dim((char *)"days since 2000-01-01", (char *)"standard",
                               TimeStandard::Udunits, TimeGranularity::Day);

    CHECK(run_fmt_time(dim, 0) == "1-Jan-2000");
    // 2000 is a leap year: day 31 (0-indexed) is the day after Jan has used
    // its 31 days, i.e. Feb 1.
    CHECK(run_fmt_time(dim, 31) == "1-Feb-2000");
    // Day 60 = Jan(31) + Feb(29 in a leap year) => Mar 1.
    CHECK(run_fmt_time(dim, 60) == "1-Mar-2000");
}

TEST_CASE("fmt_time: TimeStandard::Udunits, 360_day calendar, every month is 30 days") {
    ensure_ncview_misc_initialized();
    NCDim dim = make_time_dim((char *)"days since 2000-01-01", (char *)"360_day",
                               TimeStandard::Udunits, TimeGranularity::Day);

    CHECK(run_fmt_time(dim, 0) == "1-Jan-2000");
    // No leap-year exception in 360_day: every month is exactly 30 days.
    CHECK(run_fmt_time(dim, 30) == "1-Feb-2000");
    CHECK(run_fmt_time(dim, 60) == "1-Mar-2000");
}

TEST_CASE("fmt_time: TimeStandard::Udunits, 365_day calendar never inserts a leap day") {
    ensure_ncview_misc_initialized();
    NCDim dim = make_time_dim((char *)"days since 2000-01-01", (char *)"365_day",
                               TimeStandard::Udunits, TimeGranularity::Day);

    // 2000 would be a leap year under "standard", but 365_day has no Feb 29
    // ever -- day 365 (0-indexed) rolls straight into the next Jan 1,
    // unlike the standard-calendar test above where day 366 would be needed.
    CHECK(run_fmt_time(dim, 365) == "1-Jan-2001");
}

TEST_CASE("fmt_time: TimeStandard::Udunits with TimeGranularity::Hour includes a time-of-day") {
    ensure_ncview_misc_initialized();
    NCDim dim = make_time_dim((char *)"hours since 2000-01-01", (char *)"standard",
                               TimeStandard::Udunits, TimeGranularity::Hour);

    CHECK(run_fmt_time(dim, 0) == "1-Jan-2000 00:00");
    CHECK(run_fmt_time(dim, 13) == "1-Jan-2000 13:00");
    CHECK(run_fmt_time(dim, 25) == "2-Jan-2000 01:00");
}

TEST_CASE("fmt_time: TimeStandard::Months formats a 1-based month index directly") {
    // No udunits/calendar involved at all -- this branch is pure integer
    // arithmetic inline in fmt_time() itself (core/src/util.cc), hand-
    // derived here rather than captured:
    //   year  = (int)((val-1)/12)
    //   month = clip((int)((val-1) - year*12 + .01), 0, 11)
    //   day   = (int)(((val-1) - year*12 - month) * 30) + 1
    NCDim dim = make_time_dim(nullptr, nullptr, TimeStandard::Months, TimeGranularity::Month);

    CHECK(run_fmt_time(dim, 1) == "Jan  1    1");  // year=0, month=0, day=1
    CHECK(run_fmt_time(dim, 13) == "Jan  1    2"); // one full 12-month cycle later
    CHECK(run_fmt_time(dim, 15) == "Mar  1    2"); // year=1, month=2 (Mar)
}

TEST_CASE("fmt_time: TimeStandard::Epic0 (True Julian Day) formats a known epoch") {
    // "True Julian Day" 2440588 at zero milliseconds-of-day is the
    // well-known Julian day number for 1970-01-01 (the Unix epoch,
    // conventionally JD 2440587.5 at 00:00 UTC -- ep_time_to_mdyhms's
    // day-number-only algorithm, with no fractional/millisecond part
    // supplied here, resolves it to the same calendar day at 00:00).
    NCDim dim = make_time_dim((char *)"True Julian Day", nullptr, TimeStandard::Epic0, TimeGranularity::Day);
    CHECK(run_fmt_time(dim, 2440588) == "1-Jan-1970 00:00");

    // One well-known reference point isn't enough to trust the day-count
    // arithmetic on its own -- cross-check against calcalcs' independent
    // standard-calendar implementation for a date 10000 days later.
    calcalcs_cal *cal = ccs_init_calendar("standard");
    REQUIRE(cal != nullptr);
    int y, m, d;
    REQUIRE(ccs_dayssince(cal, 1970, 1, 1, 10000, cal, &y, &m, &d) == 0);
    ccs_free_calendar(cal);

    char expected[32];
    static const char *months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    snprintf(expected, sizeof(expected), "%d-%s-%04d 00:00", d, months[m - 1], y);
    CHECK(run_fmt_time(dim, 2440588 + 10000) == expected);
}

TEST_CASE("udu_calc_tgran: classifies CF-style \"since\" units by actual timestep spacing") {
    // udu_calc_tgran() (core/src/udu.cc) computes its granularity
    // classification via ut_are_convertible(unit, seconds) +
    // ut_get_converter()/cv_convert_double(). This used to be broken for
    // every realistic input: confirmed by hand against this project's
    // vendored UDUNITS-2 (third_party/udunits2, pinned v2.2.28) that a unit
    // WITH a reference origin -- e.g. any CF-convention "<units> since
    // <reference-date>" string, which is how essentially every real netCDF
    // time axis is declared -- is reported NOT convertible to plain,
    // unreferenced "seconds" by ut_are_convertible() (a bare "days", with
    // no "since", correctly reports convertible=1). UDUNITS-2 treats a
    // "since"-qualified unit as "encoded time", a different kind from a
    // plain interval unit, and only considers two encoded-time units (or
    // two plain interval units) convertible to each other -- not one of
    // each. So the original `if (ut_are_convertible(unit, seconds) == 0)
    // return TimeGranularity::Sec;` guard fired for every realistic input,
    // regardless of the actual timestep spacing.
    //
    // Fixed by falling back to comparing against a "seconds since <epoch>"
    // unit (any epoch works -- the two converted values are only ever
    // subtracted below, so a constant offset cancels out) when the bare
    // "seconds" comparison fails. Confirmed against the vendored UDUNITS-2
    // that converting an encoded-time unit to a *differently-referenced*
    // encoded-time unit works correctly (e.g. "days since 2000-01-01" ->
    // "seconds since 1970-01-01" scales by exactly 86400 per day).
    ensure_ncview_misc_initialized();

    TgranFixture daily("temp_daily", 5, 1.0);       // 1-day spacing
    TgranFixture monthly("temp_monthly", 5, 30.0);  // 30-day spacing
    TgranFixture yearly("temp_yearly", 5, 365.0);   // 365-day spacing

    CHECK(udu_calc_tgran(daily.fileid, daily.var, 0) == TimeGranularity::Day);
    CHECK(udu_calc_tgran(monthly.fileid, monthly.var, 0) == TimeGranularity::Month);
    CHECK(udu_calc_tgran(yearly.fileid, yearly.var, 0) == TimeGranularity::Year);
}

TEST_CASE("fmt_time: a non-timelike dimension is a fatal internal error") {
    // fmt_time() itself checks dim->timelike before dispatching -- upstream
    // treats this as unreachable-in-practice programmer error (core.cc's
    // callers only ever pass a dim they've already confirmed is timelike)
    // and exit()s rather than returning an error code. Out of scope for
    // this pass (see modernization.md's "out of scope" decision on the
    // 192 exit() calls) to change, but worth pinning down as documented,
    // current behavior for whoever touches this next -- not run directly
    // since exit() would kill the test binary.
}
