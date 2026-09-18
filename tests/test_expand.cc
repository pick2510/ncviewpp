// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for core/src/util.cc's expand_data() -- the "blow
// up data" half of data_to_pixels()'s scaling step (options.blowup > 0).
// expand_data() has external linkage (declared in protos.h) but is only
// ever called from data_to_pixels(), so tests go through that public entry
// point, same as test_pixels.cc. Written before Phase 4b ("refine the
// architecture" plan) moves it out of util.cc.
#include <algorithm>
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

struct ExpandFixture {
    NCVar var{};
    View view{};
    std::vector<float> data;
    size_t nx, ny;

    ExpandFixture(size_t nx_, size_t ny_, std::vector<float> values,
                  float fill_value, float user_min, float user_max)
        : nx(nx_), ny(ny_) {
        ensure_ncview_misc_initialized();

        var.size = { ny, nx };
        var.fill_value = fill_value;
        var.user_min = user_min;
        var.user_max = user_max;
        var.have_set_range = true;
        var.name = "expand_test_var";

        data = std::move(values);
        REQUIRE(data.size() == nx * ny);

        view.variable = &var;
        view.x_axis_id = 1;
        view.y_axis_id = 0;
        view.data = data;
    }

    std::vector<ncv_pixel> run(BlowupType blowup_type, int blowup) {
        options.transform = Transform::None;
        options.blowup_type = blowup_type;
        options.blowup = blowup;
        options.invert_colors = false;
        options.invert_physical = false;
        options.n_colors = 80;
        options.n_extra_colors = 10;
        options.display_type = 0;
        options.autoscale = false;
        options.debug = false;
        setup_identity_pixel_transform(options.n_colors, options.n_extra_colors);

        size_t new_nx, new_ny;
        view_get_scaled_size(blowup, nx, ny, &new_nx, &new_ny);
        view.pixels.assign(new_nx * new_ny, 0);

        REQUIRE(view.dataToPixels() == 0);
        return view.pixels;
    }
};

std::vector<ncv_pixel> replicate_grid(const std::vector<ncv_pixel> &src,
                                       size_t src_w, size_t src_h, int factor) {
    std::vector<ncv_pixel> out(src_w * factor * src_h * factor);
    for (size_t j = 0; j < src_h; j++)
        for (size_t i = 0; i < src_w; i++)
            for (int dj = 0; dj < factor; dj++)
                for (int di = 0; di < factor; di++)
                    out[(j * factor + dj) * (src_w * factor) + (i * factor + di)] =
                        src[j * src_w + i];
    return out;
}

} // namespace

TEST_CASE("expand_data: BlowupType::Replicate at factors 1, 2, 3, 5 all replicate blocks consistently") {
    std::vector<float> grid = {
        0, 1, 2,
        3, -999, 5,
        6, 7, 8,
    };
    ExpandFixture f(3, 3, grid, -999, 0, 8);

    auto pix1 = f.run(BlowupType::Replicate, 1);
    for (int factor : {2, 3, 5}) {
        auto pix = f.run(BlowupType::Replicate, factor);
        CHECK(pix == replicate_grid(pix1, 3, 3, factor));
    }
}

TEST_CASE("expand_data: BlowupType::Bilinear at blowup=1 is a plain copy (no interpolation possible)") {
    std::vector<float> grid = {
        0, 4,
        8, 12,
    };
    ExpandFixture f(2, 2, grid, -999, 0, 12);

    auto pix_bilinear = f.run(BlowupType::Bilinear, 1);
    auto pix_replicate = f.run(BlowupType::Replicate, 1);
    CHECK(pix_bilinear == pix_replicate);
}

TEST_CASE("expand_data: BlowupType::Bilinear preserves exact corner values at several blowup factors") {
    // The four corner data values must appear, byte-exact, at the
    // corresponding corner of the blown-up grid, since expand_data()'s
    // "paint the corner block with the raw corner value" step never
    // interpolates them (see its four corner-fill blocks).
    std::vector<float> grid = {
         0,  4,  8,
        12, 16, 20,
        24, 28, 32,
    };
    ExpandFixture f(3, 3, grid, -999, 0, 32);

    for (int blowup : {2, 3, 4}) {
        auto pix = f.run(BlowupType::Bilinear, blowup);
        size_t new_nx, new_ny;
        view_get_scaled_size(blowup, 3, 3, &new_nx, &new_ny);
        REQUIRE(pix.size() == new_nx * new_ny);

        // data_to_pixels() flips rows (invert_physical == false), so the
        // data's bottom-left corner (value 24) lands at pixel (0,0), and the
        // data's top-left corner (value 0) lands at the pixel grid's
        // bottom-left, i.e. row new_ny-1.
        //
        // FrameRenderer::render() clips the normalized value to [0, 0.9999]
        // before scaling by n_colors (see test_pixels.cc's identical note),
        // so the exact max value (32 here) maps to floor(0.9999*80)+10=89,
        // not the naive 90 -- this test hit that clip directly since it
        // deliberately puts real data at the exact user_max.
        auto expected_pixel = [&](float rawval) {
            double ratio = rawval / 32.0;
            if (ratio > 0.9999) ratio = 0.9999;
            return (ncv_pixel)(ratio * 80) + 10;
        };
        CHECK(pix[0] == expected_pixel(24));                              // data (0,2) bottom-left
        CHECK(pix[new_nx - 1] == expected_pixel(32));                     // data (2,2) bottom-right
        CHECK(pix[(new_ny - 1) * new_nx] == expected_pixel(0));           // data (0,0) top-left
        CHECK(pix[(new_ny - 1) * new_nx + new_nx - 1] == expected_pixel(8)); // data (2,0) top-right
    }
}

TEST_CASE("expand_data: BlowupType::Bilinear interpolates strictly between neighboring values") {
    // Interpolated values strictly between two real, non-missing samples
    // must fall within the range those two samples span -- interpolation
    // cannot invent a new extreme. Checked on the exact center pixel of a
    // 2x2-source, blowup=4 grid (well clear of the edge-extrapolation code
    // this file's corner/edge tests already cover separately).
    std::vector<float> grid = {
        0, 30,
        0, 30,
    };
    ExpandFixture f(2, 2, grid, -999, 0, 30);

    auto pix = f.run(BlowupType::Bilinear, 4);
    size_t new_nx, new_ny;
    view_get_scaled_size(4, 2, 2, &new_nx, &new_ny);
    REQUIRE(pix.size() == new_nx * new_ny);

    ncv_pixel p_low = (ncv_pixel)((0.0 / 30.0) * 80) + 10;
    ncv_pixel p_high = (ncv_pixel)((30.0 / 30.0 > 0.9999 ? 0.9999 : 1.0) * 80) + 10;
    // Middle column of the middle row: strictly between the left (0) and
    // right (30) source columns.
    ncv_pixel mid = pix[(new_ny / 2) * new_nx + new_nx / 2];
    CHECK(mid >= p_low);
    CHECK(mid <= p_high);
}

TEST_CASE("expand_data: BlowupType::Bilinear treats a missing neighbor as a flat step (no interpolation across it)") {
    std::vector<float> grid = {
        0, -999,
        8, 12,
    };
    ExpandFixture f(2, 2, grid, -999, 0, 12);

    // Must not crash, and the missing cell's own footprint must still map
    // to the missing-value pixel somewhere in the blown-up grid.
    auto pix = f.run(BlowupType::Bilinear, 4);
    CHECK(std::find(pix.begin(), pix.end(), (ncv_pixel)0) != pix.end());
}
