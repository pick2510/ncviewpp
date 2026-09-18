// Copyright (C) 2026 Dominik Strebel
//
// Originally a regression test for the heap-buffer-overflow found in
// View::dataEdit() during Phase 0d's ASan sweep (it allocated exactly
// n_entries char* slots but then wrote a terminating NULL at
// line_array[n_entries], one past the end). Phase 12b replaced the manual
// char**/NULL-terminator scheme with a std::vector<std::string> the
// caller owns outright -- that specific off-by-one class of bug can no
// longer occur (there's no separate terminator slot to size correctly),
// so this test now just confirms the values/count/ordering are still
// exactly right under the new type.
#include <cstdlib>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "test_udunits_helper.h"

extern std::unique_ptr<ViewState> &view;
extern std::vector<std::string> g_last_dataedit_lines;
extern int g_last_dataedit_nx;

TEST_CASE("view_data_edit: allocates exactly n_entries+1 slots and fills them correctly") {
    ensure_ncview_misc_initialized();

    const size_t nx = 4, ny = 3;
    std::vector<float> data = {
        0.f,  1.f,  2.f,  3.f,
        10.f, 11.f, 12.f, 13.f,
        20.f, 21.f, 22.f, 23.f,
    };

    NCVar var{};
    var.size = { ny, nx }; // dim 0 = y, dim 1 = x -- see PixelFixture in test_pixels.cc
    var.name = "test_var";

    // view is a unique_ptr now (Step 6), so it must own a heap object
    // rather than point at a stack one -- view.reset() below deletes
    // whatever it owns, which would be undefined behavior on a stack
    // address.
    view = std::make_unique<View>();
    view->variable   = &var;
    view->x_axis_id  = 1;
    view->y_axis_id  = 0;
    view->data       = data;

    options.invert_physical = true; // so row j in line_array matches row j in `data` directly

    g_last_dataedit_lines.clear();
    g_last_dataedit_nx    = 0;

    view->dataEdit();

    REQUIRE(g_last_dataedit_lines.size() == nx * ny);
    CHECK(g_last_dataedit_nx == (int)nx);

    // Every one of the nx*ny data values must have been formatted, in
    // row-major (x fastest) order. (No separate NULL-terminator slot to
    // check any more -- the vector's own size() is the authoritative
    // count, which is the point of Phase 12b's fix.)
    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            size_t idx = j * nx + i;
            CHECK(std::strtof(g_last_dataedit_lines[idx].c_str(), nullptr) == doctest::Approx(data[idx]));
        }
    }

    g_last_dataedit_lines.clear();
    view.reset();
}
