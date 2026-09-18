// Copyright (C) 2026 Dominik Strebel
//
// NcFixture: a chainable RAII builder for the synthetic netCDF files
// tests need. "Refine the architecture" plan, Phase 0b.
//
// test_varlist.cc, test_time_fmt.cc, test_dataset.cc,
// test_controller_characterization.cc, and test_playback.cc each
// hand-roll their own ~30-line "create a temp file, def_dim a few axes,
// def_var a time coordinate with units, def_var the data variable,
// nc_put_var, nc_close" sequence, with only the shapes and values
// actually differing between them. NcFixture is that sequence, written
// once:
//
//   std::string path = ncview_test::NcFixture()
//       .dim("time", 5).dim("lat", 2).dim("lon", 2)
//       .timeAxis("time", "days since 2000-01-01")
//       .coord("lat").coord("lon")
//       .var("temp", {"time", "lat", "lon"})
//       .path();
//
// Building is deferred to the first call to path()/openForCore() (not
// each chained call), so dims/coords/vars can be declared in any order
// without juggling netCDF's define-mode/data-mode split by hand -- this
// class does that split internally, once.
//
// Does NOT attempt to cover every shape every existing test needs (a
// virtual multi-file variable, netCDF-4 groups, scalar coordinates) --
// those stay hand-rolled in the files that need them until a real need
// to generalize this further shows up. This covers the common
// (dims + optional time axis + optional plain coords + one data
// variable) shape that most tests actually want.
#pragma once

#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/protos.h"

namespace ncview_test {

// Data generators: given a flat (row-major, C order) index into the data
// variable's values, return the value to store there. Add more here as
// tests need them (e.g. a 2-D-aware Checker, AllMissing, WithNaN) rather
// than inventing a one-off fill loop in the test file that needs one.
struct Ramp {
    float start = 0.0f;
    float operator()(size_t i) const { return start + (float)i; }
};
struct Constant {
    float value;
    float operator()(size_t) const { return value; }
};

class NcFixture {
public:
    NcFixture() {
        auto tmpl = (std::filesystem::temp_directory_path() / "ncview_fixture_XXXXXX").string();
        int fd = mkstemp(&tmpl[0]);
        REQUIRE(fd >= 0);
        close(fd);
        path_ = tmpl;
    }
    ~NcFixture() {
        std::remove(path_.c_str());
    }

    NcFixture(const NcFixture &) = delete;
    NcFixture &operator=(const NcFixture &) = delete;
    // Move-only would need to guard the moved-from destructor against a
    // double std::remove(); not needed by any current caller (every use
    // is a single local chained expression), so just delete it -- add
    // real move support if a future test needs to return one by value
    // out of a helper function.
    NcFixture(NcFixture &&) = delete;
    NcFixture &operator=(NcFixture &&) = delete;

    NcFixture &dim(std::string name, int size) {
        REQUIRE_FALSE(built_);
        dims_.push_back({std::move(name), size});
        return *this;
    }

    // Defines dim_name as its own coordinate variable (netCDF's dimvar
    // convention -- a 1-D variable sharing its dimension's name), with
    // `units` and, if non-empty, a "calendar" attribute -- exactly what
    // handle_time_dim()/fill_dim_structs() look for to recognize a real
    // UDUNITS time axis. Values are 0, 1, 2, ... in the dimension's unit
    // (i.e. "days since ..." with consecutive integer day offsets).
    NcFixture &timeAxis(std::string dim_name, std::string units, std::string calendar = "") {
        REQUIRE_FALSE(built_);
        time_axes_.push_back({std::move(dim_name), std::move(units), std::move(calendar)});
        return *this;
    }

    // A plain dimvar with no special meaning to handle_time_dim() --
    // just a coordinate axis (e.g. "lat"/"lon") filled with 0, 1, 2, ...
    // so dims that need *a* coordinate variable to exist (rather than
    // being purely index-based) have one.
    NcFixture &coord(std::string dim_name) {
        REQUIRE_FALSE(built_);
        coords_.push_back(std::move(dim_name));
        return *this;
    }

    // Defines the data variable over dim_names (in the given order,
    // matching netCDF's own slowest-to-fastest-varying convention), and
    // remembers it as "the last variable" for missing()/fillValue()
    // below.
    NcFixture &var(std::string name, std::vector<std::string> dim_names,
                   std::function<float(size_t)> generator = Ramp{}) {
        REQUIRE_FALSE(built_);
        vars_.push_back({std::move(name), std::move(dim_names), std::move(generator)});
        return *this;
    }

    // Marks the most recently added var() as having `value` as its
    // _FillValue attribute -- matching the attribute name
    // netcdf_fill_value() (file_netcdf.cc) actually looks for.
    NcFixture &missing(float value) {
        REQUIRE_FALSE(built_);
        REQUIRE_FALSE(vars_.empty());
        vars_.back().fill_value = value;
        vars_.back().has_fill_value = true;
        return *this;
    }

    // Materializes the file (if not already done) and returns its path.
    // The file is NOT removed until this NcFixture is destroyed -- a
    // test can keep using the path (e.g. to hand to netcdf_fi_initialize)
    // for as long as the fixture stays in scope.
    const std::string &path() const {
        ensureBuilt();
        return path_;
    }

    // Convenience wrapper for the "hand this path to core" dance every
    // existing test repeats: build a one-shot Stringlist naming the
    // file, run it through determine_file_type() (sets up ncview's
    // internal file-type dispatch), then netcdf_fi_initialize() to get
    // the fileid core's Dataset::addVariable() expects.
    int openForCore() const {
        ensureBuilt();
        Stringlist *files = nullptr;
        stringlist_add_string(&files, path_.c_str());
        determine_file_type(files);
        stringlist_delete_entire_list(files);
        return netcdf_fi_initialize(const_cast<char *>(path_.c_str()));
    }

private:
    struct DimSpec { std::string name; int size; };
    struct TimeAxisSpec { std::string dim_name; std::string units; std::string calendar; };
    struct VarSpec {
        std::string name;
        std::vector<std::string> dim_names;
        std::function<float(size_t)> generator;
        float fill_value = 0.0f;
        bool has_fill_value = false;
    };

    void ensureBuilt() const {
        if (built_) return;
        built_ = true;

        int ncid;
        REQUIRE(nc_create(path_.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);

        std::vector<std::pair<std::string, int>> dim_ids; // name -> netCDF dimid
        for (const auto &d : dims_) {
            int dimid;
            REQUIRE(nc_def_dim(ncid, d.name.c_str(), d.size, &dimid) == NC_NOERR);
            dim_ids.push_back({d.name, dimid});
        }
        auto dim_id_of = [&](const std::string &name) -> int {
            for (const auto &kv : dim_ids) if (kv.first == name) return kv.second;
            FAIL("NcFixture: dim '", name, "' was never declared with .dim()");
            return -1;
        };
        auto dim_size_of = [&](const std::string &name) -> int {
            for (const auto &d : dims_) if (d.name == name) return d.size;
            FAIL("NcFixture: dim '", name, "' was never declared with .dim()");
            return 0;
        };

        std::vector<std::pair<std::string, int>> time_var_ids;
        for (const auto &t : time_axes_) {
            int dimid = dim_id_of(t.dim_name);
            int varid;
            REQUIRE(nc_def_var(ncid, t.dim_name.c_str(), NC_DOUBLE, 1, &dimid, &varid) == NC_NOERR);
            REQUIRE(nc_put_att_text(ncid, varid, "units", t.units.size(), t.units.c_str()) == NC_NOERR);
            if (!t.calendar.empty())
                REQUIRE(nc_put_att_text(ncid, varid, "calendar", t.calendar.size(), t.calendar.c_str()) == NC_NOERR);
            time_var_ids.push_back({t.dim_name, varid});
        }

        std::vector<std::pair<std::string, int>> coord_var_ids;
        for (const auto &c : coords_) {
            int dimid = dim_id_of(c);
            int varid;
            REQUIRE(nc_def_var(ncid, c.c_str(), NC_FLOAT, 1, &dimid, &varid) == NC_NOERR);
            coord_var_ids.push_back({c, varid});
        }

        std::vector<int> data_var_ids(vars_.size());
        for (size_t vi = 0; vi < vars_.size(); vi++) {
            const VarSpec &v = vars_[vi];
            std::vector<int> dimids;
            for (const auto &dn : v.dim_names) dimids.push_back(dim_id_of(dn));
            int varid;
            REQUIRE(nc_def_var(ncid, v.name.c_str(), NC_FLOAT, (int)dimids.size(),
                                dimids.data(), &varid) == NC_NOERR);
            if (v.has_fill_value)
                REQUIRE(nc_put_att_float(ncid, varid, "_FillValue", NC_FLOAT, 1, &v.fill_value) == NC_NOERR);
            data_var_ids[vi] = varid;
        }

        REQUIRE(nc_enddef(ncid) == NC_NOERR);

        for (const auto &t : time_axes_) {
            int n = dim_size_of(t.dim_name);
            std::vector<double> vals(n);
            for (int i = 0; i < n; i++) vals[i] = (double)i;
            int varid = -1;
            for (const auto &kv : time_var_ids) if (kv.first == t.dim_name) varid = kv.second;
            REQUIRE(nc_put_var_double(ncid, varid, vals.data()) == NC_NOERR);
        }
        for (const auto &c : coords_) {
            int n = dim_size_of(c);
            std::vector<float> vals(n);
            for (int i = 0; i < n; i++) vals[i] = (float)i;
            int varid = -1;
            for (const auto &kv : coord_var_ids) if (kv.first == c) varid = kv.second;
            REQUIRE(nc_put_var_float(ncid, varid, vals.data()) == NC_NOERR);
        }
        for (size_t vi = 0; vi < vars_.size(); vi++) {
            const VarSpec &v = vars_[vi];
            size_t total = 1;
            for (const auto &dn : v.dim_names) total *= (size_t)dim_size_of(dn);
            std::vector<float> vals(total);
            for (size_t i = 0; i < total; i++) vals[i] = v.generator(i);
            REQUIRE(nc_put_var_float(ncid, data_var_ids[vi], vals.data()) == NC_NOERR);
        }

        REQUIRE(nc_close(ncid) == NC_NOERR);
    }

    std::string path_;
    std::vector<DimSpec> dims_;
    std::vector<TimeAxisSpec> time_axes_;
    std::vector<std::string> coords_;
    std::vector<VarSpec> vars_;
    mutable bool built_ = false;
};

} // namespace ncview_test
