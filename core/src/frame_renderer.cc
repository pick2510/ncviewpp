/*
 * core/src/frame_renderer.cc
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * See ncview/frame_renderer.h. Arithmetic here must stay byte-for-byte
 * identical to the loop it was extracted from (data_to_pixels(),
 * core/src/util.cc) -- same clip bounds, same transform formulas, same
 * PseudoColor remap condition -- since tests/test_pixels.cc's existing
 * exact-pixel characterization tests, and tests/ui_smoke.sh's screenshot
 * goldens, both depend on the result matching precisely.
 */
#include "ncview/frame_renderer.h"

#include <cmath>

#include "ncview/includes.h"	/* FILL_FLOAT, an alias netcdf.h provides for NC_FILL_FLOAT */
#include "ncview/protos.h"	/* close_enough(), clip_f() */

void FrameRenderer::render(
	const float *scaled_data, size_t nx, size_t ny,
	float fill_value, float user_min, float user_max,
	const PixelMapSettings &settings,
	const std::vector<ncv_pixel> &pixel_transform,
	ncv_pixel *out_pixels )
{
	const float data_range = user_max - user_min;

	for( size_t j=0; j<ny; j++ ) {

		size_t j2;
		if( settings.invert_physical )
			j2 = j;
		else
			j2 = ny - j - 1;

		for( size_t i=0; i<nx; i++ ) {
			float rawdata = scaled_data[i + j2*nx];
			ncv_pixel pix_val;
			if( close_enough(rawdata, fill_value) || (rawdata == FILL_FLOAT))
				pix_val = pixel_transform[0];
			else
				{
				float data = (rawdata - user_min) / data_range;
				clip_f( &data, 0.0, .9999 );
				pix_val = (ncv_pixel)colorIndex<float>(
					data, settings.transform, settings.invert_colors,
					settings.n_colors, settings.n_extra_colors );
				if( settings.display_type == PseudoColor )
					pix_val = pixel_transform[pix_val];
				}
			out_pixels[i + j*nx] = pix_val;
			}
		}
}

namespace {
void niceNormalize( double value, double *mantissa, double *exponent )
{
	if( value == 0.0 ) { *mantissa = 0.0; *exponent = 0.0; return; }
	double q = std::log10( value );
	*exponent = (double)(int)q;
	*mantissa = value / std::pow( 10.0, *exponent );
	if( q < 0.0 ) { *exponent -= 1.0; *mantissa *= 10.0; }
}

void niceNlevFromStep( double step, double mindat, double maxdat, int *nlev, double *start )
{
	int n0 = (int)(maxdat/step);
	double cursor = (double)n0 * step;
	while( cursor > mindat ) { n0--; cursor = (double)n0 * step; }
	int n1 = (int)(mindat/step);
	cursor = (double)n1 * step;
	while( cursor < maxdat ) { n1++; cursor = (double)n1 * step; }
	*nlev = n1 - n0 + 1;
	*start = (double)n0 * step;
}
} // namespace

bool FrameRenderer::niceTickLevels(
	double mindat, double maxdat, int nlevels,
	double *start, int *nlevs, double *step )
{
	if( nlevels < 2 || maxdat <= mindat ) return false;
	static const int kTrial[4] = { 1, 2, 5, 10 };
	double trialstep = (maxdat - mindat) / (double)(nlevels - 1);
	double mant, expon;
	niceNormalize( trialstep, &mant, &expon );
	double fact = std::pow( 10.0, expon );
	for( int i = 0; i < 3; i++ ) {
		if( mant < kTrial[i] || mant > kTrial[i+1] ) continue;
		double step1 = kTrial[i]*fact, step2 = kTrial[i+1]*fact;
		int n1, n2;
		double start1, start2;
		niceNlevFromStep( step1, mindat, maxdat, &n1, &start1 );
		niceNlevFromStep( step2, mindat, maxdat, &n2, &start2 );
		if( std::abs( n1 - nlevels ) <= std::abs( n2 - nlevels ) ) {
			*step = step1; *nlevs = n1; *start = start1;
		} else {
			*step = step2; *nlevs = n2; *start = start2;
		}
		return true;
	}
	return false;
}
