// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for core/src/util.cc's data_to_pixels() -- the
// rendering transform at the heart of ncview (data value -> byte pixel,
// applied per-frame to every 2-D field the app ever draws). Before this
// file, no test touched it at all (see modernization.md Phase 0b): this is
// the safety net for the core data-model refactor that follows.
//
// data_to_pixels() reads and writes through several file-scope globals
// (options, pixel_transform) that upstream's own X11 setup path
// (initialize_display_interface() -> initialize_colormaps()) would normally
// populate by touching the filesystem for colormap files. That's more than
// this test needs: it only replicates the two things data_to_pixels()
// actually reads off of them (a handful of Options fields, and an identity
// pixel_transform table -- see core/src/ncview.cc's own comment on why the
// identity table is what a non-indexed-color toolkit like FLTK needs).
#include <cstring>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "test_udunits_helper.h"

namespace {

// data_to_pixels() only ever reads pixel_transform[0] (missing-value
// pixel) and, when options.display_type == PseudoColor, remaps through it
// -- both cases upstream's own TrueColor/FLTK path handles with a plain
// identity table (see initialize_display_interface()'s comment). Backed by
// a static vector, not a raw new[], so nothing to free.
void setup_identity_pixel_transform(int n_colors, int n_extra_colors) {
    pixel_transform.resize(n_colors + n_extra_colors);
    for (size_t i = 0; i < pixel_transform.size(); i++) pixel_transform[i] = (ncv_pixel)i;
}

// A minimal but fully wired View + NCVar over a caller-supplied nx*ny data
// array (row-major, x fastest -- the layout data_to_pixels()/expand_data()
// assume). Every field data_to_pixels() reads is set explicitly; nothing
// else about NCVar/View matters to it.
struct PixelFixture {
    NCVar var{};
    View view{};
    std::vector<float> data;
    size_t nx, ny;

    PixelFixture(size_t nx_, size_t ny_, std::vector<float> values,
                 float fill_value, float user_min, float user_max)
        : nx(nx_), ny(ny_) {
        ensure_ncview_misc_initialized();

        var.size = { ny, nx }; // [0]=y axis, [1]=x axis
        var.fill_value = fill_value;
        var.user_min = user_min;
        var.user_max = user_max;
        var.have_set_range = true;
        var.name = "test_var";

        data = std::move(values);
        REQUIRE(data.size() == nx * ny);

        view.variable = &var;
        view.x_axis_id = 1;
        view.y_axis_id = 0;
        view.data = data;
    }

    // Runs data_to_pixels() with the given options, sizing the pixel
    // buffer for whatever blowup/shrink they imply, and returns the
    // resulting pixel grid (row-major, new_x_size wide).
    std::vector<ncv_pixel> run(Transform transform, BlowupType blowup_type, int blowup,
                                bool invert_colors, int n_colors = 80,
                                int n_extra_colors = 10) {
        options.transform = transform;
        options.blowup_type = blowup_type;
        options.blowup = blowup;
        options.invert_colors = invert_colors;
        options.invert_physical = false;
        options.n_colors = n_colors;
        options.n_extra_colors = n_extra_colors;
        options.display_type = 0; // not PseudoColor -- see setup_identity_pixel_transform
        options.autoscale = false;
        options.debug = false;
        setup_identity_pixel_transform(n_colors, n_extra_colors);

        size_t new_nx, new_ny;
        view_get_scaled_size(blowup, nx, ny, &new_nx, &new_ny);
        view.pixels.assign(new_nx * new_ny, 0);

        REQUIRE(view.dataToPixels() == 0);
        return view.pixels;
    }
};

// Replicates a src_w x src_h grid into a (src_w*factor) x (src_h*factor)
// grid, each source cell becoming a factor x factor block -- the expected
// shape of BlowupType::Replicate's output, computed here rather than hand-typed
// so the test documents the *relationship*, not 36 magic numbers.
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

TEST_CASE("data_to_pixels: Transform::None, blowup=1, exact pixel values with a missing cell") {
    // 3x3 grid, row-major (il fastest): the center cell is the fill value.
    // clang-format off
    std::vector<float> grid = {
        0, 1, 2,
        3, -999, 5,
        6, 7, 8,
    };
    // clang-format on
    PixelFixture f(3, 3, grid, /*fill_value=*/-999, /*user_min=*/0, /*user_max=*/8);

    // n_colors=80 makes rawdata/8*80 land on exact integers for every
    // non-clipped value (rawdata*10), so the expected grid below is exact
    // arithmetic, not a captured/approximate baseline. pix = (uchar)(data_n
    // * 80) + 10; missing cells map to pixel_transform[0] == 0.
    //
    // options.invert_physical defaults to false, which data_to_pixels()
    // takes to mean row 0 of the *pixel* grid is the data's LAST row (see
    // its j2 = new_y_size - j - 1) -- upstream's normal "y increases
    // upward" image convention. That's why the expected grid below is the
    // input grid's rows in reverse order, not a straight copy.
    auto pix = f.run(Transform::None, BlowupType::Replicate, /*blowup=*/1, /*invert_colors=*/false);
    // clang-format off
    std::vector<ncv_pixel> expected = {
        70, 80, 89,   // rawdata 6,7,8 (rawdata=8 clips to 0.9999*80=79 -> +10)
        40,  0, 60,   // rawdata 3,MISSING,5
        10, 20, 30,   // rawdata 0,1,2
    };
    // clang-format on
    CHECK(pix == expected);
}

TEST_CASE("data_to_pixels: Transform::None, blowup=1, invert_colors flips the ramp") {
    std::vector<float> grid = {
        0, 1, 2,
        3, -999, 5,
        6, 7, 8,
    };
    PixelFixture f(3, 3, grid, -999, 0, 8);

    auto pix = f.run(Transform::None, BlowupType::Replicate, 1, /*invert_colors=*/true);
    // clang-format off
    std::vector<ncv_pixel> expected = {
        30, 20, 10,
        60,  0, 40,
        90, 80, 70,
    };
    // clang-format on
    CHECK(pix == expected);
    // The missing-value pixel is untouched by invert_colors either way --
    // it's picked before the invert_colors branch even runs.
    CHECK(pix[4] == 0);
}

TEST_CASE("data_to_pixels: BlowupType::Replicate at blowup=2 replicates each pixel into a 2x2 block") {
    std::vector<float> grid = {
        0, 1, 2,
        3, -999, 5,
        6, 7, 8,
    };
    PixelFixture f(3, 3, grid, -999, 0, 8);

    auto pix1 = f.run(Transform::None, BlowupType::Replicate, 1, false);
    auto pix2 = f.run(Transform::None, BlowupType::Replicate, 2, false);
    CHECK(pix2 == replicate_grid(pix1, 3, 3, 2));
}

TEST_CASE("data_to_pixels: transform functions preserve ordering (monotonic ramp)") {
    // sqrt(sqrt(x)), x^4, and the atan-based center transform are all
    // strictly increasing on [0,1] -- so for any two distinct, non-clipped
    // raw values, the transformed pixel ordering must match the raw
    // ordering, regardless of which specific pixel values a given libm
    // produces (avoids pinning exact bytes to one platform's sqrt/atan
    // rounding).
    std::vector<float> grid = {1, 3, 5, 7}; // 2x2, well clear of the 0/8 clip edges
    PixelFixture f(2, 2, grid, -999, 0, 8);

    for (Transform transform : {Transform::None, Transform::Low, Transform::Hi, Transform::Center}) {
        auto pix = f.run(transform, BlowupType::Replicate, 1, false);
        // Row order is flipped (see the exact-value test above): row 0 of
        // the pixel grid holds rawdata {5,7}, row 1 holds rawdata {1,3}.
        CHECK(pix[2] < pix[3]); // rawdata 1 < rawdata 3
        CHECK(pix[0] < pix[1]); // rawdata 5 < rawdata 7
        CHECK(pix[2] < pix[0]); // rawdata 1 < rawdata 5
    }
}

TEST_CASE("data_to_pixels: BlowupType::Bilinear produces a correctly-sized, in-range grid") {
    // Bilinear interpolation's exact arithmetic is worth less to pin down
    // byte-for-byte than its structural contract: it must not crash, must
    // produce a blowup*blowup-scaled grid, and every interpolated value
    // must fall within the range spanned by the real (non-missing) samples
    // -- interpolation cannot invent a new extreme.
    std::vector<float> grid = {
        0, 4,
        8, 12,
    };
    PixelFixture f(2, 2, grid, -999, 0, 12);

    auto pix = f.run(Transform::None, BlowupType::Bilinear, /*blowup=*/4, false);
    CHECK(pix.size() == 4 * 2 * 4 * 2);
    // pix = (uchar)(data_n * n_colors) + 10, with data_n clipped to
    // [0, 0.9999]: 10 at rawdata==user_min, up to (uchar)(0.9999*80)+10==89
    // at rawdata==user_max, for the default n_colors=80 f.run() uses.
    for (ncv_pixel p : pix) {
        CHECK(p >= 10);
        CHECK(p <= 89);
    }
}
