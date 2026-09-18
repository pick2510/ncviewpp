// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for core/src/util.cc's contract_data()/util_mean()/
// util_mode() -- the "shrink data" half of data_to_pixels()'s scaling step
// (options.blowup < 0), exercised through data_to_pixels() since all three
// are file-static helpers with no direct external linkage. Written before
// Phase 4b ("refine the architecture" plan) moves them out of util.cc, to
// pin util_mode()'s tie-breaking rule (first-encountered value wins ties,
// per its `>` not `>=` comparison) byte-for-byte before the move.
#include <vector>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "test_udunits_helper.h"

namespace {

void setup_identity_pixel_transform(int n_colors, int n_extra_colors) {
    pixel_transform.resize(n_colors + n_extra_colors);
    for (size_t i = 0; i < pixel_transform.size(); i++) pixel_transform[i] = (ncv_pixel)i;
}

// Same shape as test_pixels.cc's PixelFixture, but exposes options.shrink_method
// since that's the axis this file cares about (data_to_pixels() only calls
// contract_data() when options.blowup < 0).
struct ShrinkFixture {
    NCVar var{};
    View view{};
    std::vector<float> data;
    size_t nx, ny;

    ShrinkFixture(size_t nx_, size_t ny_, std::vector<float> values,
                  float fill_value, float user_min, float user_max)
        : nx(nx_), ny(ny_) {
        ensure_ncview_misc_initialized();

        var.size = { ny, nx };
        var.fill_value = fill_value;
        var.user_min = user_min;
        var.user_max = user_max;
        var.have_set_range = true;
        var.name = "shrink_test_var";

        data = std::move(values);
        REQUIRE(data.size() == nx * ny);

        view.variable = &var;
        view.x_axis_id = 1;
        view.y_axis_id = 0;
        view.data = data;
    }

    std::vector<ncv_pixel> run(ShrinkMethod method, int shrink_factor) {
        options.transform = Transform::None;
        options.shrink_method = method;
        options.blowup = -shrink_factor; // negative blowup means "shrink by this factor"
        options.invert_colors = false;
        options.invert_physical = false;
        options.n_colors = 80;
        options.n_extra_colors = 10;
        options.display_type = 0;
        options.autoscale = false;
        options.debug = false;
        setup_identity_pixel_transform(options.n_colors, options.n_extra_colors);

        size_t new_nx, new_ny;
        view_get_scaled_size(options.blowup, nx, ny, &new_nx, &new_ny);
        view.pixels.assign(new_nx * new_ny, 0);

        REQUIRE(view.dataToPixels() == 0);
        return view.pixels;
    }
};

} // namespace

TEST_CASE("contract_data: ShrinkMethod::Mean averages each factor x factor window") {
    // 4x4 grid of a simple ramp, shrunk by factor 2 -> each 2x2 window
    // averages to a clean integer, so the expected pixel values are exact.
    // clang-format off
    std::vector<float> grid = {
         0,  2,  4,  6,
         8, 10, 12, 14,
        16, 18, 20, 22,
        24, 26, 28, 30,
    };
    // clang-format on
    ShrinkFixture f(4, 4, grid, /*fill_value=*/-999, /*user_min=*/0, /*user_max=*/30);

    auto pix = f.run(ShrinkMethod::Mean, /*shrink_factor=*/2);
    // Window means (row-major, il fastest, before the y-flip data_to_pixels
    // applies): top-left(0,2,8,10)=5, top-right(4,6,12,14)=9,
    // bottom-left(16,18,24,26)=21, bottom-right(20,22,28,30)=25.
    // pix = (uchar)(data_n * 80) + 10, data_n = mean/30.
    ncv_pixel p5  = (ncv_pixel)((5.0/30.0)  * 80) + 10;
    ncv_pixel p9  = (ncv_pixel)((9.0/30.0)  * 80) + 10;
    ncv_pixel p21 = (ncv_pixel)((21.0/30.0) * 80) + 10;
    ncv_pixel p25 = (ncv_pixel)((25.0/30.0) * 80) + 10;
    // data_to_pixels() flips rows (invert_physical == false): pixel row 0 is
    // data row 1 (the bottom-most in the *file's* index order).
    std::vector<ncv_pixel> expected = { p21, p25, p5, p9 };
    CHECK(pix == expected);
}

TEST_CASE("contract_data: ShrinkMethod::Mean returns fill_value if any sample in the window is missing") {
    std::vector<float> grid = {
         0,  2,  4,  6,
         8, -999, 12, 14,
        16, 18, 20, 22,
        24, 26, 28, 30,
    };
    ShrinkFixture f(4, 4, grid, -999, 0, 30);

    auto pix = f.run(ShrinkMethod::Mean, 2);
    // Top-left window (0,2,8,MISSING) contains a missing value -> the whole
    // window's mean is the fill value, which maps to pixel_transform[0] == 0
    // (the missing-value pixel, picked before the transform ramp).
    CHECK(pix[2] == 0); // pixel row 1, col 0 == data's top-left window post-flip
}

TEST_CASE("contract_data: ShrinkMethod::Mode picks the most common rounded value") {
    // A 4x4 window (shrink factor 4 on a 4x4 grid -> single output pixel)
    // where value 5 appears 3 times and 7 appears 13 times: mode must be 7.
    std::vector<float> grid(16, 7.0f);
    grid[0] = 5.0f;
    grid[1] = 5.0f;
    grid[2] = 5.0f;
    ShrinkFixture f(4, 4, grid, -999, 0, 10);

    auto pix = f.run(ShrinkMethod::Mode, 4);
    REQUIRE(pix.size() == 1);
    ncv_pixel expected = (ncv_pixel)((7.0/10.0) * 80) + 10;
    CHECK(pix[0] == expected);
}

TEST_CASE("contract_data: ShrinkMethod::Mode breaks ties in favor of the first-encountered value") {
    // A 2x2 window with two values, each appearing twice: 3 and 9, with 3
    // encountered first (row-major scan order). util_mode()'s tie-break is
    // `count_vals[i] > max_count`, strictly greater -- so the FIRST value to
    // reach the max count wins, not the last. This is the exact behavior
    // this test pins before Phase 4b moves the function.
    std::vector<float> grid = {
        3, 9,
        9, 3,
    };
    ShrinkFixture f(2, 2, grid, -999, 0, 10);

    auto pix = f.run(ShrinkMethod::Mode, 2);
    REQUIRE(pix.size() == 1);
    ncv_pixel expected = (ncv_pixel)((3.0/10.0) * 80) + 10;
    CHECK(pix[0] == expected);
}

TEST_CASE("contract_data: ShrinkMethod::Mode returns fill_value immediately on encountering a missing sample") {
    std::vector<float> grid = {
        -999, 9,
        9, 9,
    };
    ShrinkFixture f(2, 2, grid, -999, 0, 10);

    auto pix = f.run(ShrinkMethod::Mode, 2);
    REQUIRE(pix.size() == 1);
    CHECK(pix[0] == 0); // missing-value pixel
}

TEST_CASE("contract_data: non-integer shrink factor clamps window reads to the last valid row/col") {
    // 5x5 grid shrunk by factor 2 (5/2 -> new size 2x2, per view_get_scaled_size's
    // integer floor): contract_data()'s window-index clamp (ioffset/joffset
    // clamped to nx-1/ny-1) means the rightmost/bottommost source column/row
    // gets reused rather than reading out of bounds. This exercises that
    // clamp directly, which is otherwise invisible with a factor that
    // divides evenly.
    std::vector<float> grid = {
         1,  2,  3,  4,  5,
         6,  7,  8,  9, 10,
        11, 12, 13, 14, 15,
        16, 17, 18, 19, 20,
        21, 22, 23, 24, 25,
    };
    ShrinkFixture f(5, 5, grid, -999, 0, 25);

    // Must not crash (the clamp is exactly what prevents an out-of-bounds
    // read here) and must produce the size view_get_scaled_size() promises.
    auto pix = f.run(ShrinkMethod::Mean, 2);
    size_t new_nx, new_ny;
    view_get_scaled_size(-2, 5, 5, &new_nx, &new_ny);
    CHECK(pix.size() == new_nx * new_ny);
}
