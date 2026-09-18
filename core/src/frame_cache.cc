/*
 * core/src/frame_cache.cc
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * See ncview/frame_cache.h. OOP_redesign plan, Step 3.
 */
#include "ncview/frame_cache.h"

void FrameCache::reset( size_t nt, size_t nx, size_t ny )
{
	frame_.clear();
	frame_.shrink_to_fit();
	frame_valid_.clear();
	frame_valid_.shrink_to_fit();
	valid_ = false;

	nt_ = nt;
	nx_ = nx;
	ny_ = ny;
	size_t storage_size = nx_ * ny_ * nt_;

	try {
		frame_.resize( storage_size );
		frame_valid_.resize( nt_, false );
		valid_ = true;
		}
	catch( const std::bad_alloc & ) {
		frame_.clear();
		frame_.shrink_to_fit();
		frame_valid_.clear();
		frame_valid_.shrink_to_fit();
		valid_ = false;
		throw;
		}
}

void FrameCache::growTo( size_t new_nt )
{
	size_t old_nt = nt_;
	nt_ = new_nt;
	frame_.resize( nx_ * ny_ * nt_ );
	frame_valid_.resize( nt_ );

	/* Initialize to NOT a valid frame for the new frames */
	for( size_t i=old_nt; i<nt_; i++ )
		frame_valid_[i] = false;
}

void FrameCache::invalidateAll()
{
	for( size_t i=0; i<nt_; i++ )
		frame_valid_[i] = false;
}

const ncv_pixel* FrameCache::lookup( size_t frameno ) const
{
	if( ! valid_ )
		return nullptr;
	if( (frameno >= frame_valid_.size()) || ! frame_valid_[frameno] )
		return nullptr;
	return frame_.data() + frameno * (nx_ * ny_);
}

void FrameCache::store( size_t frameno, const ncv_pixel *pixels, size_t count )
{
	/* Phase 12a: match lookup()'s existing discipline -- validate frameno
	 * against capacity and use the cache's own nx_*ny_ as the trusted
	 * stride, rather than writing on the caller-supplied count/frameno
	 * unconditionally. Reachable without any malformed file: toggling
	 * "Save frames in memory" off leaves this cache's geometry stale
	 * (init_saveframes() no-ops on every call while the option is off,
	 * and at least one call site -- view.cc's blowup-change path --
	 * skips calling init_saveframes() at all in that case, resizing
	 * view->pixels without ever touching this cache), so a later draw()
	 * can call store() with a frame that no longer matches nx_*ny_.
	 * A geometry mismatch invalidates the whole cache, not just this
	 * write: every other stored frame was sized for the same stale
	 * geometry, so none of them can be trusted either once the display
	 * has moved on to a different size. */
	if( ! valid_ )
		return;
	if( (frameno >= frame_valid_.size()) || (count != nx_ * ny_) ) {
		valid_ = false;
		return;
		}
	size_t offset = frameno * count;
	for( size_t i=0; i<count; i++ )
		frame_[offset + i] = pixels[i];
	frame_valid_[frameno] = true;
}
