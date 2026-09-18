// Copyright (C) 2026 Dominik Strebel
//
// Direct tests of FrameRenderer (core/src/frame_renderer.cc), extracted
// from data_to_pixels()'s pixel-mapping loop -- OOP_redesign plan, Step 2.
// tests/test_pixels.cc already exercises this loop indirectly through the
// full data_to_pixels() pipeline (View/NCVar/blowup and all), and those
// tests still pass unchanged after the extraction -- the real regression
// net for "did the extraction change any arithmetic". This file instead
// covers what test_pixels.cc's identity-pixel_transform,
// invert_physical==false fixture never could: invert_physical==true (the
// row-flip branch) and a non-identity pixel_transform table under
// display_type==PseudoColor (the remap branch at the end of the loop).
#include <cmath>
#include <vector>

#include <doctest/doctest.h>

#include "ncview/frame_renderer.h"

namespace {

PixelMapSettings make_settings(Transform t, bool invert_colors, bool invert_physical,
                                int n_colors, int n_extra_colors, int display_type) {
    return PixelMapSettings{ t, invert_colors, invert_physical, n_colors, n_extra_colors, display_type };
}

} // namespace

TEST_CASE("FrameRenderer::render matches data_to_pixels()'s known-exact grid (sanity/direct-call form)") {
    // Same 3x3 grid and expectation as test_pixels.cc's first TEST_CASE,
    // called directly instead of through View/NCVar/data_to_pixels().
    std::vector<float> grid = {
        0, 1, 2,
        3, -999, 5,
        6, 7, 8,
    };
    std::vector<ncv_pixel> pixel_transform(90);
    for (size_t i = 0; i < pixel_transform.size(); i++) pixel_transform[i] = (ncv_pixel)i;

    auto settings = make_settings(Transform::None, false, /*invert_physical=*/false, 80, 10, /*display_type=*/0);
    std::vector<ncv_pixel> pix(9);
    FrameRenderer::render(grid.data(), 3, 3, /*fill_value=*/-999, /*user_min=*/0, /*user_max=*/8,
                           settings, pixel_transform, pix.data());

    std::vector<ncv_pixel> expected = {
        70, 80, 89,
        40,  0, 60,
        10, 20, 30,
    };
    CHECK(pix == expected);
}

TEST_CASE("FrameRenderer::render: invert_physical=true skips the row flip") {
    // Same grid/settings as above except invert_physical=true -- row 0 of
    // the output should now be row 0 of the input (rawdata {0,1,2}), not
    // the last row, unlike every existing test_pixels.cc case (all of
    // which leave invert_physical at its default, false).
    std::vector<float> grid = {
        0, 1, 2,
        3, -999, 5,
        6, 7, 8,
    };
    std::vector<ncv_pixel> pixel_transform(90);
    for (size_t i = 0; i < pixel_transform.size(); i++) pixel_transform[i] = (ncv_pixel)i;

    auto settings = make_settings(Transform::None, false, /*invert_physical=*/true, 80, 10, 0);
    std::vector<ncv_pixel> pix(9);
    FrameRenderer::render(grid.data(), 3, 3, -999, 0, 8, settings, pixel_transform, pix.data());

    std::vector<ncv_pixel> expected = {
        10, 20, 30,   // rawdata 0,1,2 -- straight copy, no flip
        40,  0, 60,   // rawdata 3,MISSING,5
        70, 80, 89,   // rawdata 6,7,8
    };
    CHECK(pix == expected);
}

TEST_CASE("FrameRenderer::render: PseudoColor display_type remaps every pixel through pixel_transform") {
    // A single-cell field so the remap is unambiguous: rawdata==user_min
    // maps to color index n_extra_colors (10 here, since data clips to 0),
    // and a non-identity pixel_transform must be visible in the output --
    // every existing test uses an identity table, so this branch
    // (util.cc's old `if (options.display_type == PseudoColor)
    // pix_val = pixel_transform[pix_val];`) was previously unexercised.
    std::vector<float> grid = { 0.0f };
    std::vector<ncv_pixel> pixel_transform(20, 0);
    pixel_transform[10] = 200; // non-identity: index 10 maps to 200, not 10

    auto settings = make_settings(Transform::None, false, false, 5, 10, /*display_type=*/PseudoColor);
    std::vector<ncv_pixel> pix(1);
    FrameRenderer::render(grid.data(), 1, 1, /*fill_value=*/-999, /*user_min=*/0, /*user_max=*/1,
                           settings, pixel_transform, pix.data());

    CHECK(pix[0] == 200);
}

TEST_CASE("FrameRenderer::render: a missing value always maps to pixel_transform[0], regardless of display_type") {
    std::vector<float> grid = { -999.0f };
    std::vector<ncv_pixel> pixel_transform(20, 0);
    pixel_transform[0] = 42; // distinctive, so this test fails loudly if the missing-value path breaks

    auto settings = make_settings(Transform::None, false, false, 5, 10, PseudoColor);
    std::vector<ncv_pixel> pix(1);
    FrameRenderer::render(grid.data(), 1, 1, /*fill_value=*/-999, /*user_min=*/0, /*user_max=*/1,
                           settings, pixel_transform, pix.data());

    CHECK(pix[0] == 42);
}

TEST_CASE("FrameRenderer::colorIndex<float>: render() delegates to the shared step correctly") {
    // Confirms render() actually calls through to colorIndex<float>()
    // rather than having drifted back to an inline copy: a single-pixel
    // grid whose rawdata normalizes to exactly 'normalized' within
    // [0, user_max], with an identity pixel_transform so the returned
    // pixel *is* the index, must equal calling colorIndex<float>()
    // directly with the same normalized value.
    std::vector<ncv_pixel> pixel_transform(90);
    for (size_t i = 0; i < pixel_transform.size(); i++) pixel_transform[i] = (ncv_pixel)i;

    const int n_colors = 80;
    const int n_extra_colors = 10;

    for (Transform t : {Transform::None, Transform::Low, Transform::Hi, Transform::Center}) {
        for (bool invert : {false, true}) {
            for (float normalized : {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 0.9999f}) {
                CAPTURE(t);
                CAPTURE(invert);
                CAPTURE(normalized);

                int direct_idx = FrameRenderer::colorIndex<float>(normalized, t, invert, n_colors, n_extra_colors);

                auto settings = make_settings(t, invert, /*invert_physical=*/false,
                                               n_colors, n_extra_colors, /*display_type=*/0);
                std::vector<float> grid = { normalized };
                std::vector<ncv_pixel> pix(1);
                FrameRenderer::render(grid.data(), 1, 1, /*fill_value=*/-999,
                                       /*user_min=*/0, /*user_max=*/1, settings, pixel_transform, pix.data());

                CHECK((int)pix[0] == direct_idx);
            }
        }
    }
}

TEST_CASE("FrameRenderer::colorIndex<double>: matches Colorbar::draw()'s pre-Phase-5c inline formula") {
    // Colorbar::draw() computed this in double precision, with no
    // intermediate narrowing (unlike render()'s float 'data'). colorIndex
    // is a template specifically so each call site keeps its own original
    // precision through one shared body -- verified here by reproducing
    // Colorbar's exact pre-extraction computation independently and
    // checking colorIndex<double>() agrees at every input, not just most.
    // (An earlier attempt shared a single non-templated function and hit
    // exactly the failure this test would have caught: forcing Colorbar
    // onto float precision changed one pixel column in ui_smoke.sh's
    // "initial" golden, at Transform::Low/invert=true/normalized=0.9 --
    // this test exists so that regression can't come back silently.)
    const int n_colors = 80;
    const int n_extra_colors = 10;

    for (Transform t : {Transform::None, Transform::Low, Transform::Hi, Transform::Center}) {
        for (bool invert : {false, true}) {
            for (double normalized : {0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 0.9999}) {
                CAPTURE(t);
                CAPTURE(invert);
                CAPTURE(normalized);

                double reference = normalized;
                switch (t) {
                    case Transform::Hi:     reference = reference*reference*reference*reference; break;
                    case Transform::Low:    reference = sqrt(sqrt(reference)); break;
                    case Transform::Center: reference = atan((reference-0.5)*8.0)/3.1415926536 + 0.5; break;
                    default: break;
                }
                if (invert) reference = 1.0 - reference;
                int reference_idx = n_extra_colors + (int)(reference * n_colors);

                CHECK(FrameRenderer::colorIndex<double>(normalized, t, invert, n_colors, n_extra_colors)
                      == reference_idx);
            }
        }
    }
}

TEST_CASE("FrameRenderer::niceTickLevels: degenerate inputs return false") {
    // Extracted from ui/'s Colorbar::draw() (Phase 9), which previously had
    // no test at all -- ui/ has no unit test binary, only tests/ui_smoke.sh's
    // screenshot goldens, which show tick labels too small/few to catch a
    // subtle stepping regression. Pure arithmetic, no FLTK dependency, so it
    // belongs in core the same way colorIndex() does.
    double start, step;
    int nlevs;

    CHECK_FALSE(FrameRenderer::niceTickLevels(0.0, 10.0, 1, &start, &nlevs, &step));   // nlevels < 2
    CHECK_FALSE(FrameRenderer::niceTickLevels(10.0, 10.0, 5, &start, &nlevs, &step));  // maxdat == mindat
    CHECK_FALSE(FrameRenderer::niceTickLevels(10.0, 0.0, 5, &start, &nlevs, &step));   // maxdat < mindat
}

TEST_CASE("FrameRenderer::niceTickLevels: picks 1/2/5 x10^n steps covering the range") {
    struct Case { double mindat, maxdat; int target; };
    for (const Case &c : {
             Case{0.0, 100.0, 5}, Case{0.0, 1.0, 4}, Case{260.9, 295.0, 5},
             Case{-50.0, 50.0, 6}, Case{0.001, 0.009, 4}, Case{1.0, 1000.0, 3},
         }) {
        CAPTURE(c.mindat);
        CAPTURE(c.maxdat);
        CAPTURE(c.target);

        double start, step;
        int nlevs;
        REQUIRE(FrameRenderer::niceTickLevels(c.mindat, c.maxdat, c.target, &start, &nlevs, &step));

        // step must be exactly 1, 2, or 5 times a power of 10 (matching
        // kTrial in the implementation) -- not just "some positive number".
        double mant = step / std::pow(10.0, std::floor(std::log10(step) + 1e-9));
        bool is_nice_mantissa = std::fabs(mant - 1.0) < 1e-9 || std::fabs(mant - 2.0) < 1e-9
                                 || std::fabs(mant - 5.0) < 1e-9;
        CHECK(is_nice_mantissa);

        // The generated levels must actually cover [mindat, maxdat]: start
        // <= mindat, and start + (nlevs-1)*step >= maxdat.
        CHECK(start <= c.mindat + 1e-9);
        CHECK(start + (nlevs - 1) * step >= c.maxdat - 1e-9);
        CHECK(nlevs >= 2);
    }
}
