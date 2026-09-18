/*
 * core/include/ncview/frame_cache.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * Replaces the bare FrameStore struct that used to live here (frame pixel
 * cache keyed purely by scan-axis frame index -- see view.cc's view_draw(),
 * view_check_new_data(), init_saveframes(), invalidate_all_saveframes(),
 * the only 4 functions that ever touched it). OOP_redesign plan, Step 3.
 */
#pragma once

#include <cstddef>
#include <vector>

#include "ncview/defines.h"

class FrameCache {
public:
	bool valid() const { return valid_; }
	size_t nt() const { return nt_; }
	size_t nx() const { return nx_; }
	size_t ny() const { return ny_; }

	/* Clears any existing storage and (re)allocates for nt frames of
	 * nx*ny pixels each. Throws std::bad_alloc on allocation failure,
	 * leaving the cache cleared and invalid() (mirrors init_saveframes()'s
	 * old std::bad_alloc catch, which callers still perform themselves so
	 * they can report the failure through in_error()). */
	void reset( size_t nt, size_t nx, size_t ny );

	/* Grows frame storage to new_nt frames (nx/ny unchanged), preserving
	 * the validity and contents of existing frames; new frames start
	 * invalid. Caller (view_check_new_data()) only ever calls this with
	 * new_nt >= nt(). */
	void growTo( size_t new_nt );

	void invalidateAll();

	/* Returns a pointer to frameno's cached pixels, or nullptr if the
	 * cache is invalid or that frame isn't cached. */
	const ncv_pixel* lookup( size_t frameno ) const;

	/* Copies count pixels into frameno's slot and marks it valid. */
	void store( size_t frameno, const ncv_pixel *pixels, size_t count );

private:
	bool valid_ = false;
	size_t nt_ = 0, nx_ = 0, ny_ = 0;
	std::vector<ncv_pixel> frame_;
	std::vector<int> frame_valid_;
};
