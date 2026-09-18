// Copyright (C) 2026 Dominik Strebel
//
// Direct tests of FrameCache (core/src/frame_cache.cc), extracted from the
// bare FrameStore struct -- OOP_redesign plan, Step 3. Mirrors the field
// manipulations previously done by hand in view.cc's view_draw(),
// view_check_new_data(), init_saveframes(), and invalidate_all_saveframes().
#include <vector>

#include <doctest/doctest.h>

#include "ncview/frame_cache.h"

TEST_CASE("FrameCache starts invalid with zero geometry") {
    FrameCache cache;
    CHECK_FALSE(cache.valid());
    CHECK(cache.nt() == 0);
    CHECK(cache.lookup(0) == nullptr);
}

TEST_CASE("FrameCache::reset allocates storage and becomes valid") {
    FrameCache cache;
    cache.reset(/*nt=*/3, /*nx=*/2, /*ny=*/2);
    CHECK(cache.valid());
    CHECK(cache.nt() == 3);
    CHECK(cache.nx() == 2);
    CHECK(cache.ny() == 2);
    // Freshly reset: every frame is allocated but not yet marked valid.
    CHECK(cache.lookup(0) == nullptr);
    CHECK(cache.lookup(2) == nullptr);
}

TEST_CASE("FrameCache::store then lookup round-trips pixel data") {
    FrameCache cache;
    cache.reset(2, 2, 2);
    std::vector<ncv_pixel> pixels = {10, 20, 30, 40};
    cache.store(1, pixels.data(), pixels.size());

    CHECK(cache.lookup(0) == nullptr); // frame 0 untouched
    const ncv_pixel *got = cache.lookup(1);
    REQUIRE(got != nullptr);
    for (size_t i = 0; i < pixels.size(); i++)
        CHECK(got[i] == pixels[i]);
}

TEST_CASE("FrameCache::invalidateAll clears every stored frame but keeps storage") {
    FrameCache cache;
    cache.reset(2, 2, 2);
    std::vector<ncv_pixel> pixels = {1, 2, 3, 4};
    cache.store(0, pixels.data(), pixels.size());
    cache.store(1, pixels.data(), pixels.size());
    REQUIRE(cache.lookup(0) != nullptr);
    REQUIRE(cache.lookup(1) != nullptr);

    cache.invalidateAll();

    CHECK(cache.valid()); // still valid -- just nothing is cached right now
    CHECK(cache.lookup(0) == nullptr);
    CHECK(cache.lookup(1) == nullptr);
}

TEST_CASE("FrameCache::store refuses a write whose count doesn't match the cache's own geometry") {
    // Regression test (Phase 12a). store() used to trust the caller's
    // `count`/`frameno` unconditionally and write before validating
    // anything -- lookup() already validates frameno and uses the cache's
    // own nx()*ny() as the stride, but store() didn't match that
    // discipline. Reachable in production without any malformed file:
    // toggle "Save frames in memory" off (init_saveframes() then no-ops
    // on every subsequent call, per view.cc's `if (options.save_frames
    // == false) return;` guard) -> increase blowup or select a larger
    // variable (view->pixels DOES resize to the new geometry, since that
    // resize isn't gated on save_frames) -> ViewerController::draw()
    // still sees framestore.valid() == true from before the toggle and
    // calls store() with the new, larger frame against the old, smaller
    // buffer. A too-large `count` used to write straight past frame_'s
    // end; an out-of-range `frameno` used to write at an offset with no
    // corresponding frame_valid_ slot, corrupting a different frame's
    // slot or past the buffer entirely.
    FrameCache cache;
    cache.reset(/*nt=*/2, /*nx=*/2, /*ny=*/2); // 2x2 frames, capacity 2

    std::vector<ncv_pixel> oversized(64, 99); // a much larger "frame" than 2x2

    // A too-large count: must not write past the 2*2*2 = 8-element buffer.
    cache.store(0, oversized.data(), oversized.size());
    CHECK(cache.lookup(0) == nullptr); // refused, not silently accepted

    // An out-of-range frameno with a still-mismatched count: must not
    // write at all, and must not corrupt frame_valid_ bookkeeping for a
    // real frame.
    cache.store(5, oversized.data(), oversized.size());
    CHECK(cache.lookup(1) == nullptr);

    // The cache must not report itself as trustworthy after a rejected
    // write -- its stored geometry no longer matches what callers think
    // they're reading, so every subsequent lookup must miss rather than
    // silently return data at the wrong stride.
    CHECK_FALSE(cache.valid());

    // A correctly-sized, in-range store still works after all of this,
    // confirming the fix doesn't just always refuse.
    FrameCache cache2;
    cache2.reset(2, 2, 2);
    std::vector<ncv_pixel> right_size = {1, 2, 3, 4};
    cache2.store(0, right_size.data(), right_size.size());
    const ncv_pixel *got = cache2.lookup(0);
    REQUIRE(got != nullptr);
    for (size_t i = 0; i < right_size.size(); i++)
        CHECK(got[i] == right_size[i]);
}

TEST_CASE("FrameCache::growTo preserves existing frames and adds invalid new ones") {
    FrameCache cache;
    cache.reset(2, 2, 2);
    std::vector<ncv_pixel> pixels = {5, 6, 7, 8};
    cache.store(0, pixels.data(), pixels.size());

    cache.growTo(5);

    CHECK(cache.nt() == 5);
    const ncv_pixel *frame0 = cache.lookup(0);
    REQUIRE(frame0 != nullptr);
    for (size_t i = 0; i < pixels.size(); i++)
        CHECK(frame0[i] == pixels[i]);
    CHECK(cache.lookup(1) == nullptr);
    CHECK(cache.lookup(4) == nullptr); // one of the newly-added frames
}
