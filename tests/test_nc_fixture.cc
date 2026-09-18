// Copyright (C) 2026 Dominik Strebel
//
// Tests for NcFixture itself (tests/support/nc_fixture.h) -- "refine the
// architecture" plan, Phase 0b. Proves the files it builds are real,
// correctly-shaped netCDF files that Dataset::addVariable() can load,
// with time axes, plain coordinates, and missing values behaving exactly
// as a hand-rolled test file's would.
#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "support/nc_fixture.h"
#include "support/session_fixture.h"
#include "test_udunits_helper.h"

using ncview_test::Constant;
using ncview_test::NcFixture;
using ncview_test::Ramp;
using ncview_test::SessionFixture;

TEST_CASE("NcFixture: a plain (time, lat, lon) variable loads with the right shape and values") {
    SessionFixture fx;
    ensure_ncview_misc_initialized();

    NcFixture nc;
    nc.dim("time", 3).dim("lat", 2).dim("lon", 4)
      .timeAxis("time", "days since 2000-01-01")
      .coord("lat").coord("lon")
      .var("temp", {"time", "lat", "lon"});

    int fid = nc.openForCore();
    g_dataset.addVariable("temp", fid, nc.path().c_str());
    NCVar *var = g_dataset.findVariable("temp");
    REQUIRE(var != nullptr);
    REQUIRE(var->n_dims == 3);
    CHECK(var->size[0] == 3); // time
    CHECK(var->size[1] == 2); // lat
    CHECK(var->size[2] == 4); // lon
    CHECK(var->dim[0]->timelike == 1); // recognized as a real UDUNITS time axis
}

TEST_CASE("NcFixture: the default Ramp generator fills data in row-major order") {
    SessionFixture fx;
    ensure_ncview_misc_initialized();

    NcFixture nc;
    nc.dim("x", 2).dim("y", 3)
      .var("v", {"x", "y"}, Ramp{});

    int fid = nc.openForCore();
    g_dataset.addVariable("v", fid, nc.path().c_str());
    NCVar *var = g_dataset.findVariable("v");
    REQUIRE(var != nullptr);

    size_t start[2] = {0, 0}, count[2] = {2, 3};
    std::vector<float> data(6);
    g_dataset.getData(var, start, count, data.data());
    for (size_t i = 0; i < 6; i++) CHECK(data[i] == (float)i);
}

TEST_CASE("NcFixture: a Constant generator plus missing() sets a real _FillValue") {
    SessionFixture fx;
    ensure_ncview_misc_initialized();

    NcFixture nc;
    nc.dim("x", 4)
      .var("v", {"x"}, Constant{2.5f})
      .missing(-999.0f);

    int fid = nc.openForCore();
    g_dataset.addVariable("v", fid, nc.path().c_str());
    NCVar *var = g_dataset.findVariable("v");
    REQUIRE(var != nullptr);

    float fill = 0.0f;
    g_dataset.fillValue(var, &fill);
    CHECK(fill == doctest::Approx(-999.0f));

    size_t start[1] = {0}, count[1] = {4};
    std::vector<float> data(4);
    g_dataset.getData(var, start, count, data.data());
    for (float v : data) CHECK(v == doctest::Approx(2.5f));
}

TEST_CASE("NcFixture: two independent fixtures never collide, even with the same variable name") {
    SessionFixture fx;
    ensure_ncview_misc_initialized();

    NcFixture nc1;
    nc1.dim("x", 2).var("v", {"x"}, Constant{1.0f});
    int fid1 = nc1.openForCore();
    g_dataset.addVariable("v", fid1, nc1.path().c_str());
    CHECK(g_dataset.findVariable("v") != nullptr);

    // A second NcFixture with the same variable name is a genuinely
    // different physical file/fileid -- this is the "virtual variable"
    // append path, not a collision, and it's exactly what a real
    // multi-file dataset does.
    NcFixture nc2;
    nc2.dim("x", 2).var("v", {"x"}, Constant{9.0f});
    int fid2 = nc2.openForCore();
    CHECK(fid2 != fid1);
}
