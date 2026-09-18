/*
 * core/include/ncview/frame_renderer.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * Extracted from data_to_pixels() (core/src/util.cc) -- OOP_redesign plan,
 * Step 2. This covers exactly the per-pixel mapping loop: missing-value
 * check, normalize by range, clip, apply the display transform, optional
 * color inversion, quantize to a colormap index, and (for PseudoColor)
 * remap through the pixel_transform table. It is deliberately narrow:
 * blowup expand/contract, overlay application, and the degenerate-range
 * recursion/dialogs all stay in data_to_pixels() -- they involve UI calls,
 * recursion, and shared-state mutation that this class doesn't need and
 * shouldn't take on. FrameRenderer itself reads no globals and mutates
 * nothing it wasn't given -- everything it needs is a parameter.
 */
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

#include "ncview/defines.h"

/* The subset of Options that shapes the pixel-mapping loop -- see the
 * comment above data_to_pixels() in util.cc for how these fields are
 * actually used. */
struct PixelMapSettings {
	Transform	transform;
	bool	invert_colors;
	bool	invert_physical;
	int	n_colors;
	int	n_extra_colors;
	int	display_type;	/* compared against PseudoColor, defines.h */
};

class FrameRenderer {
public:
	/* scaled_data is nx*ny floats (already blowup-expanded/contracted by
	 * the caller); out_pixels must have room for nx*ny ncv_pixel entries.
	 * fill_value marks missing data (-> pixel_transform[0]). user_min/
	 * user_max define the normalization range (data_to_pixels() has
	 * already handled the case where they're equal before calling this).
	 * pixel_transform is the colormap index table; only consulted for a
	 * missing value, or for every pixel when display_type == PseudoColor.
	 */
	static void render(
		const float *scaled_data, size_t nx, size_t ny,
		float fill_value, float user_min, float user_max,
		const PixelMapSettings &settings,
		const std::vector<ncv_pixel> &pixel_transform,
		ncv_pixel *out_pixels );

	/* The single-sample color-index step of render()'s inner loop, pulled
	 * out so ui/'s Colorbar::draw() can share it instead of hand-copying
	 * the transform/invert/scale formula (which had drifted: it hardcoded
	 * 10 in place of n_extra_colors). 'normalized' is expected in [0,1];
	 * render() clip_f()s to [0,.9999] before calling this, matching its
	 * pre-extraction behavior exactly. Not clamped to a pixel/colormap
	 * range here -- callers that don't pre-clip (Colorbar::draw() samples
	 * px/width directly) must clamp the result themselves, as
	 * Colorbar::draw() already did before this extraction.
	 *
	 * Templated on T (render() instantiates T=float, Colorbar::draw()
	 * T=double) so each call site keeps its own original floating-point
	 * precision through this one shared body, rather than forcing both
	 * onto a single type: render()'s float arithmetic must stay
	 * byte-for-byte identical to what test_pixels.cc/ui_smoke.sh's
	 * goldens already pin, and forcing Colorbar's historically
	 * double-precision math down to float shifts its rendered pixels by
	 * one color index at some truncation boundaries (confirmed by
	 * ui_smoke.sh failing when this was tried as a single non-templated
	 * double/float function) -- a real, if tiny, behavior change this
	 * phase isn't meant to introduce. */
	template <typename T>
	static int colorIndex(
		T normalized, Transform transform, bool invert_colors,
		int n_colors, int n_extra_colors )
	{
		const double pi = 3.1415926536;
		T data = normalized;

		switch( transform ) {
			case Transform::None:	break;

			case Transform::Low:	data = std::sqrt( data );
						data = std::sqrt( data );
						break;

			case Transform::Hi:	data = data*data*data*data;     break;

			case Transform::Center:	data = std::atan( (data - 0.5)*8.0 );
						data = data/pi + 0.5;
						break;
			}
		if( invert_colors )
			data = 1. - data;
		return (int)(data * n_colors) + n_extra_colors;
	}

	/* Picks "nice" (1/2/5 x10^n) tick-mark levels covering [mindat, maxdat],
	 * targeting nlevels ticks -- e.g. for labeling a colorbar or axis with
	 * round numbers instead of the two raw endpoints. Pulled out of ui/'s
	 * Colorbar::draw() (Phase 9 of the "refine the architecture" plan),
	 * where it lived as three free functions (cbarNormalize/
	 * cbarNlevFromStep/cbarGenlevs) in an anonymous namespace: pure
	 * arithmetic with no FLTK/widget dependency at all, so ui/'s own unit
	 * test binary (there isn't one -- see PORTING.md's Phase 9 entry) was
	 * never actually required to test it; it belongs here instead, next to
	 * colorIndex() for the same reason. Returns false (leaving the out
	 * parameters untouched) for a degenerate range or fewer than 2
	 * requested levels, matching cbarGenlevs()'s original contract exactly
	 * -- ui/'s Colorbar::draw() already handles a false return by skipping
	 * tick labels for that frame. */
	static bool niceTickLevels(
		double mindat, double maxdat, int nlevels,
		double *start, int *nlevs, double *step );
};
