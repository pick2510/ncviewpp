/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 1993 through 2024  David W. Pierce
 * Modifications Copyright (C) 2026 Dominik Strebel
 *
 * This program  is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License, version 3, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * davidwilliampierce@gmail.com
 */

/*******************************************************************************
 * 	render_pipeline.cc
 *
 *	Data-value -> pixel rendering: View::dataToPixels() (formerly
 *	data_to_pixels(), util.cc) and the shrink/expand steps it dispatches
 *	to. Moved out of util.cc (Phase 4b, "refine the architecture" plan);
 *	dataToPixels()/expandData()/contractData() are "move, don't split"
 *	mechanical method conversions (each already took a View* first).
 *	close_enough/clip_f/util_mean/util_mode/data_has_mv have no natural
 *	View to attach to and stay free functions, moved verbatim.
 *******************************************************************************/

#include <vector>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "ncview/frame_renderer.h"

extern Options   options;

static float util_mean( float *x, size_t n, float fill_value );
static float util_mode( float *x, size_t n, float fill_value );
static int data_has_mv( float *data, size_t n, float fill_value );

/*******************************************************************************
 * Determine whether the data is "close enough" to the fill value
 */
int
close_enough( float data, float fill )
{
	float	criterion, diff;
	int	retval;

	if( fill == 0.0 )
		criterion = 1.0e-5;
	else if( fill < 0.0 )
		criterion = -1.0e-5*fill;
	else
		criterion = 1.0e-5*fill;

	diff = data - fill;
	if( diff < 0.0 )
		diff = -diff;

	if( diff <= criterion )
		retval = 1;
	else
		retval = 0;

 /* printf( "d=%f f=%f c=%f r=%d\n", data, fill, criterion, retval );  */
	return( retval );
}

/******************************************************************************
 * Return 1 if any data value is missing, 0 otherwise
 */
	static int
data_has_mv( float *data, size_t n, float fill_value )
{
	size_t i;

	for( i=0; i<n; i++ )
		if( close_enough( data[i], fill_value ))
			return(1);

	return(0);
}

/******************************************************************************
 * Scale the data, replicate it, and convert to a pixel type array.  I'm afraid
 * that for speed, this considers 'ncv_pixel' to be a single byte value.  Make sure
 * to change it if you change the definition of ncv_pixel!  Returns 0 on
 * success, -1 on failure.
 */
	int
View::dataToPixels()
{
	View *v = this;
	ViewerUi	&ui = *g_app.ui;
	size_t	i;
	size_t	x_size, y_size, new_x_size, new_y_size;
	float	fill_value;
	std::vector<float> scaled_data;
	long	blowup;
	Message	result;
	MinMaxMethod	orig_minmax_method;
	char	error_message[1024];

	/* Make sure the limits have been set on this variable.
	 * They won't always be because an initial expose event can
	 * cause this routine to be executed before the min and
	 * maxes are calcuclated.
	 */
	if( ! v->variable->have_set_range )
		return( -1 );

	/* Phase 13b: last line of defence for the whole render path. The two
	 * size[] reads below drive every buffer size in this function and in
	 * expandData()/contractData(); with an unresolved axis they come out
	 * of heap bytes preceding the vector, which means either a wild
	 * allocation or a silent overwrite. Returning -1 is the established
	 * "this View is not renderable" answer every caller already handles
	 * (see the have_set_range check just above). */
	if( ! v->has2dImage() )
		return( -1 );

	blowup   = options.blowup;	/* NOTE: can be negative if shrinking data! -N means size is 1/Nth */

	x_size     = v->variable->size[v->x_axis_id];
	y_size     = v->variable->size[v->y_axis_id];

	view_get_scaled_size( options.blowup, x_size, y_size, &new_x_size, &new_y_size );

	/* std::vector's own allocator throws std::bad_alloc on failure (there is
	 * no NULL-check equivalent to preserve); everything downstream is
	 * otherwise identical to the old malloc()'d buffer. */
	scaled_data.resize( new_x_size*new_y_size );

	/* If we are doing overlays, implement them */
	if( options.overlay->doit && (! options.overlay->overlay.empty())) {
		for( i=0; i<(x_size*y_size); i++ ) {
			v->data[i] =
			     (float)(1 - options.overlay->overlay[i]) * v->data[i] +
			     (float)(options.overlay->overlay[i]) * v->variable->fill_value;
			}
		}

	fill_value = v->variable->fill_value;

	if( blowup > 0 ) {
		if( options.debug ) printf( "..expanding data, blowup=%ld\n", blowup );
		v->expandData( scaled_data.data(), new_x_size*new_y_size );
		}
	else
		{
		if( options.debug ) printf( "..contracting data, blowup=%ld\n", blowup );
		v->contractData( scaled_data.data(), fill_value );
		}

	if( (v->variable->user_max == 0) &&
	    (v->variable->user_min == 0) &&
	    (! options.autoscale) ) {
		ui.in_set_cursor_normal();
		g_app.controller.pause( Modifier::M1 );	/* pause playback directly -- no need to round-trip through the Button enum */
		if( options.min_max_method == MinMaxMethod::Exhaust ) {
	    		snprintf( error_message, 1022, "min and max both 0 for variable %s (checked all data)\nSetting range to (-1,1)",
								v->variable->name.c_str() );
			in_error( error_message );
			v->variable->user_max = 1;
			v->variable->user_min = -1;
			v->variable->auto_set_no_range = 1;
			return( v->dataToPixels() );
			}
	    	snprintf( error_message, 1022, "min and max both 0 for variable %s.\nI can check ALL the data instead of subsampling if that's OK,\nor just cancel viewing this variable.",
	    				v->variable->name.c_str() );
		result = ui.in_dialog( error_message, true );
		if( result == Message::OK ) {
			orig_minmax_method = options.min_max_method;
			options.min_max_method = MinMaxMethod::Exhaust;
			g_app.session.dataset().initMinMax( v->variable, *g_app.ui );
			options.min_max_method = orig_minmax_method;
			if( (v->variable->user_max == 0) &&
	    		    (v->variable->user_min == 0) ) {
	    			snprintf( error_message, 1022, "min and max both 0 for variable %s (checked all data)\nSetting range to (-1,1)",
								v->variable->name.c_str() );
				in_error( error_message );
				v->variable->user_max = 1;
				v->variable->user_min = -1;
				v->variable->auto_set_no_range = 1;
				return( v->dataToPixels() );
				}
			else
				return( v->dataToPixels() );
			}
		else
			{
			if( ! data_has_mv( v->data.data(), x_size*y_size, fill_value ) )
				return( -1 );
			v->variable->user_max = 1;
			}
	    	}

	if( (v->variable->user_max == v->variable->user_min) && (! options.autoscale) ) {
		ui.in_set_cursor_normal();
	    	snprintf( error_message, 1022, "min and max both %g for variable %s",
	    		v->variable->user_min, v->variable->name.c_str() );
		ui.x_error( error_message );
		if( ! data_has_mv( v->data.data(), x_size*y_size, fill_value ) ) {
			v->variable->user_max += 0.1 * v->variable->user_max;
			v->variable->user_min -= 0.1 * v->variable->user_min;
			v->variable->auto_set_no_range = 1;
			return( v->dataToPixels() );
			}
		/* If we get here, data is all same, but have a missing value,
		 * so let's go ahead and show it
		 */
		if( v->variable->user_max == 0 )
			v->variable->user_max = 1;
		else if( v->variable->user_max > 0 )
			v->variable->user_min = 0;
		else
			v->variable->user_max = 0;
	    	}

	PixelMapSettings pixel_map_settings = g_app.session.pixelMapSettings( g_app.session.renderSettings() );
	FrameRenderer::render( scaled_data.data(), new_x_size, new_y_size,
		fill_value, v->variable->user_min, v->variable->user_max,
		pixel_map_settings, g_app.session.pixelTransform(), v->pixels.data() );

	return( 0 );
}

/******************************************************************************
 * Clip out of range floats
 */
	void
clip_f( float *data, float min, float max )
{
	if( *data < min )
		*data = min;
	if( *data > max )
		*data = max;
}

/******************************************************************************
 * Return the mode (most common value) of passed array "x".  We assume "x"
 * contains the floating point representation of integers.
 */
	static float
util_mode( float *x, size_t n, float fill_value )
{
	long 	i, n_vals;
	long 	ival, max_count;
	std::vector<long> count_vals, unique_vals;
	int	foundval, j, max_index;
	float	retval;

	count_vals.resize( n );
	unique_vals.resize( n );

	n_vals = 0;
	for( i=0L; i<(long)n; i++ ) {
		if( close_enough( x[i], fill_value )) {
			return( fill_value );
			}
		ival = (x[i] > 0.) ? (long)(x[i]+.4) : (long)(x[i]-.4); /* round x[i] to nearest integer */
		foundval = -1;
		for( j=0; j<n_vals; j++ ) {
			if( unique_vals[j] == ival ) {
				foundval = j;
				break;
				}
			}
		if( foundval == -1 ) {
			unique_vals[n_vals] = ival;
			count_vals[n_vals] = 1;
			n_vals++;
			}
		else
			count_vals[foundval]++;
		}

	max_count = -1;
	max_index = -1;
	for( i=0L; i<n_vals; i++ )
		if( count_vals[i] > max_count ) {
			max_count = count_vals[i];
			max_index = i;
			}

	retval = (float)unique_vals[max_index];

	return( retval );
}

/******************************************************************************/
	static float
util_mean( float *x, size_t n, float fill_value )
{
	size_t i;
	double sum;

	sum = 0.0;
	for( i=0L; i<n; i++ ) {
		if( close_enough( x[i], fill_value ))
			return( fill_value );
		sum += x[i];
		}

	sum = sum / (double)n;
	return( sum );
}

/********************************************************************************
 * Actually do the "shrinking" of the FLOATING POINT (not pixel) data, converting
 * it to the small version by either finding the most common value in the square,
 * or by averaging over the square.  Remember that our standard for how to
 * interpret 'options.blowup' is that a value of "-N" means to shrink by a factor
 * of N.  So, blowup == -2 means make it half size, -3 means 1/3 size, etc.
 */
	void
View::contractData( float *small_data, float fill_value )
{
	View *v = this;
	long 	n, ii, jj;
	size_t	i, j, nx, ny;
	size_t	new_nx, new_ny, idx, ioffset, joffset;
	float 	*tmpv;

	if( options.blowup > 0 ) {
		fprintf( stderr, "internal error, contract_data called with a positive blowup factor!\n" );
		exit(-1);
		}

	n = -options.blowup;
	std::vector<float> tmpv_buf( n*n );
	tmpv = tmpv_buf.data();

	/* Get old and new sizes (new size is smaller in this routine) */
	nx   = v->variable->size[v->x_axis_id];
	ny   = v->variable->size[v->y_axis_id];
	view_get_scaled_size( options.blowup, nx, ny, &new_nx, &new_ny );

	for( j=0; j<new_ny; j++ )
	for( i=0; i<new_nx; i++ ) {
		for( jj=0; jj<n; jj++ )
		for( ii=0; ii<n; ii++ ) {
			ioffset = i*n + ii;
			joffset = j*n + jj;
			if( ioffset >= nx )
				ioffset = nx-1;
			if( joffset >= ny )
				joffset = ny-1;
			idx = ioffset + joffset*nx;
			tmpv[ii + jj*n] = v->data[idx];
			}

		if( options.shrink_method == ShrinkMethod::Mean )
			small_data[i + j*new_nx] = util_mean( tmpv, n*n, fill_value );

		else if( options.shrink_method == ShrinkMethod::Mode ) {
			small_data[i + j*new_nx] = util_mode( tmpv, n*n, fill_value );
			}
		else
			{
			fprintf( stderr, "Error in contract_data: unknown value of options.shrink_method!\n" );
			exit( -1 );
			}
		}
}

/******************************************************************************
 * Actually do the "blowup" of the FLOATING POINT (not pixel) data, converting
 * it to the large version by either interpolation or replication.
 * NOTE this routine is only called when options.blowup > 0!
 */
	void
View::expandData( float *big_data, size_t array_size )
{
	View *v = this;
	size_t	idx, nxl, nyl, nxb, nyb;
	long	line, il, jl, i2b, j2b;
	int	blowup, offset_xb, offset_yb, miss_base, miss_right, miss_below;
	float	step, final_est, extrap_fact;
	float	base_val, right_val, below_val, val, bupr;
	float	base_x, base_y, del_x, del_y;
	float	est1, est2, frac_x, frac_y;
	float 	fill_val, cval;

	blowup   = options.blowup;

#ifdef CHECK_MEM
	printf( "...CHECK_MEM is on!!\n" );
#endif

	/*--------------------------------------------------------------------------------
	 * See my notebook entry of 2010-08-23.
	 * In general we draw a distinction between indices that are valid in the
	 * original (little) array, indicazted by a "l" (little) suffix (such as il or jl),
	 * and indices valid in the destination (big) array, which have a suffix of "b".
	 *---------------------------------------------------------------------------------*/
	nxl = v->variable->size[v->x_axis_id];	/* # of X entries in the little array */
	nyl = v->variable->size[v->y_axis_id];	/* # of Y entries in the little array */
	nxb = nxl*blowup;				/* # of X entries in big array */
	nyb = nyl*blowup;				/* # of Y entries in big array */

	fill_val = v->variable->fill_value;

	if( (nxb < (size_t)blowup) || (nxb*nyb < (size_t)blowup) ) {
		fprintf( stderr, "ncview: data_to_pixels: too much magnification\n" );
		fprintf( stderr, "nxb=%zu\n", nxb );
		exit( -1 );
		}

	if( (blowup == 1) || (options.blowup_type == BlowupType::Replicate)) {
		for( jl=0; jl<(long)nyl; jl++ ) {
			for( il=0; il<(long)nxl; il++ )
				for( i2b=0; i2b<blowup; i2b++ ) {
#ifdef CHECK_MEM
					if( il*blowup + jl*nxb*blowup + i2b >= array_size ) { fprintf( stderr, "mem error 001\n" ); exit(-1); }
#endif
					*(big_data + il*blowup + jl*nxb*blowup + i2b) = v->data[il+jl*nxl];
					}
			for( line=1; line<blowup; line++ )
				for( i2b=0; i2b<(long)nxb; i2b++ ) {
#ifdef CHECK_MEM
					if( i2b + jl*nxb*blowup + line*nxb >= array_size ) { fprintf( stderr, "mem error 002\n" ); exit(-1); }
#endif
					*(big_data + i2b + jl*nxb*blowup + line*nxb) =
						*(big_data + i2b + jl*nxb*blowup);
					}
			}
		}

	else 	{ /* BlowupType::Bilinear */
		bupr = 1.0/(float)blowup;

		/* Offset where we will put the center value into the big array. These are offsets
		 * into the big array.
		 */
		offset_xb = (blowup - 1)/2;
		offset_yb = offset_xb;

		/* Horizontal base lines */
		for( jl=0; jl<(long)nyl; jl++ ) {
			for( il=0; il<(long)nxl-1; il++ ) {
				base_val  = v->data[il   + jl*nxl];
				right_val = v->data[il+1 + jl*nxl];

				miss_base  = close_enough(base_val,  fill_val);
				miss_right = close_enough(right_val, fill_val);
				if( miss_base ) {
					if( miss_right ) {
						/* BOTH missing */
						step = 0.0;
						val = base_val;		/* missing value */
						}
					else
						{
						/* base missing, but right is there */
						step = 0.0;
						val = right_val;	/* an OK value */
						}
					}
				else if( miss_right ) {
					/* ONLY right is missing, checked for both missing above */
					val = base_val;
					step = 0.0;
					}
				else
					{
					/* NEITHER missing */
					step = (right_val-base_val)*bupr;
					val = base_val;
					}

				for( i2b=0; i2b < blowup; i2b++ ) {
#ifdef CHECK_MEM
					if( il*blowup+i2b+offset_xb + jl*blowup*nxb + offset_yb*nxb >= array_size ) { fprintf( stderr, "mem error 003\n" ); exit(-1); }
#endif
					*(big_data + il*blowup+i2b+offset_xb + jl*blowup*nxb + offset_yb*nxb ) = val;
					val += step;
					}
				}
			/* Fill in the last center value on the right, which was left unfilled by the above alg */
#ifdef CHECK_MEM
			if( (nxl-1)*blowup+offset_xb + jl*blowup*nxb + offset_yb*nxb >= array_size ) { fprintf( stderr, "mem error 004\n" ); exit(-1); }
#endif
			*(big_data + (nxl-1)*blowup+offset_xb + jl*blowup*nxb + offset_yb*nxb ) = v->data[(nxl-1) + jl*nxl];
			}

		/* Vertical base lines */
		for( jl=0; jl<(long)nyl-1; jl++ )
		for( il=0; il<(long)nxl;   il++ ) {
			base_val  = v->data[il + jl*nxl];
			below_val = v->data[il + (jl+1)*nxl];

			miss_base  = close_enough(base_val,  fill_val);
			miss_below = close_enough(below_val, fill_val);

			if( miss_base ) {
				if( miss_below ) {
					/* BOTH missing */
					step = 0.0;
					val = base_val;		/* missing value */
					}
				else
					{
					/* base missing, but below is there */
					step = 0.0;
					val = below_val;	/* an OK value */
					}
				}
			else if( miss_below ) {
				/* ONLY below is missing, checked for both missing above */
				val = base_val;
				step = 0.0;
				}
			else
				{
				/* NEITHER missing */
				step = (below_val-base_val)*bupr;
				val = base_val;
				}

			for( j2b=0; j2b < blowup; j2b++ ) {
#ifdef CHECK_MEM
			if( il*blowup+offset_xb + jl*blowup*nxb + (j2b+offset_yb)*nxb >= array_size ) { fprintf( stderr, "mem error 005\n" ); exit(-1); }
#endif
				*(big_data + il*blowup+offset_xb + jl*blowup*nxb + (j2b+offset_yb)*nxb ) = val;
				val += step;
				}
			}
		/* Fill in the last center value along the top, which was left unfilled by the above alg */
		for( il=0; il<(long)nxl; il++ ) {
#ifdef CHECK_MEM
			if( il*blowup+offset_xb + (nyl-1)*blowup*nxb + offset_yb*nxb >= array_size ) { fprintf( stderr, "mem error 006\n" ); exit(-1); }
#endif
			*(big_data + il*blowup+offset_xb + (nyl-1)*blowup*nxb + offset_yb*nxb) = v->data[il + (nyl-1)*nxl];
			}

		/* Now, fill in the interior of the interior squares by
		 * interpolating from the horizontal and vertical
		 * base lines.
		 */
		for( jl=0; jl<(long)nyl-1; jl++ )
		for( il=0; il<(long)nxl-1; il++ ) {
			for( j2b=1; j2b<blowup; j2b++ )
			for( i2b=1; i2b<blowup; i2b++ ) {
				frac_x = (float)i2b*bupr;
				frac_y = (float)j2b*bupr;

				base_x    = *(big_data +  il   *blowup+offset_xb + jl*blowup*nxb +(j2b+offset_yb)*nxb);
				right_val = *(big_data + (il+1)*blowup+offset_xb + jl*blowup*nxb+ (j2b+offset_yb)*nxb);
				base_y    = *(big_data + il*blowup+i2b+offset_xb +  jl   *blowup*nxb + offset_yb*nxb);
				below_val = *(big_data + il*blowup+i2b+offset_xb + (jl+1)*blowup*nxb + offset_yb*nxb);

				if( close_enough(base_x,    fill_val) ||
				    close_enough(right_val, fill_val) ||
				    (il == (long)nxl-1) )
					del_x = 0.0;
				else
					del_x  = right_val - base_x;
				if( close_enough(base_y,    fill_val) ||
				    close_enough(below_val, fill_val) ||
				    (jl == (long)nyl-1) )
					del_y = 0.0;
				else
					del_y  = below_val - base_y;
				est1 = frac_x*del_x + base_x;
				est2 = frac_y*del_y + base_y;

				if( close_enough( est1, fill_val )) {
					if( close_enough( est2, fill_val ))
						final_est = fill_val;
					else
						final_est = est2;
					}
				else if( close_enough( est2, fill_val ))
					final_est = est1;
				else
					final_est = (est1 + est2)*.5;

#ifdef CHECK_MEM
				if( il*blowup+i2b+offset_xb + jl*blowup*nxb + (j2b+offset_yb)*nxb >= array_size ) { fprintf( stderr, "mem error 007\n" ); exit(-1); }
#endif
				*(big_data + il*blowup+i2b+offset_xb + jl*blowup*nxb + (j2b+offset_yb)*nxb ) = final_est;
				}
			}

		/* It is a tricky and undetermined question as to whether we want to allow
		 * extrema on the boundaries.  As a complete and total hack, we use only
		 * some fraction of the linear projection when extrapolating out to the
		 * edges.  If this is set to 1, then full linear extrapolation is used;
		 * if set to 0, no extrapolation is done.
		 */
		extrap_fact = 0.2;

		/* Fill in right hand side by extrapolating the gradient from the interior square fill.
		 * This goes from y=the first center point to y=the last center point.
		 */
		il = nxl-1;
		for( j2b=0; j2b<=blowup*(long)(nyl-1); j2b++ ) {
			idx = il*blowup+offset_xb + (j2b+offset_yb)*nxb;
			step = (*(big_data + idx - 1) - *(big_data + idx - 2));
			val  = *(big_data + idx) + step;
			for( i2b=1; i2b<(blowup-offset_xb+1); i2b++ ) {
#ifdef CHECK_MEM
				if( idx + i2b >= array_size ) { fprintf( stderr, "mem error 008\n" ); exit(-1); }
#endif
				*(big_data + idx + i2b) = val;
				val += step*extrap_fact;
				}
			}

		/* Fill in left hand side */
		il = 0;
		for( j2b=0; j2b<=blowup*(long)(nyl-1); j2b++ ) {
			idx = il*blowup+offset_xb + (j2b+offset_yb)*nxb;
			step = (*(big_data + idx + 2) - *(big_data + idx + 1));
			val  = *(big_data + idx) - step;
			for( i2b=1; i2b<=(blowup-1)/2; i2b++ ) {
#ifdef CHECK_MEM
				if( idx - i2b >= array_size ) { fprintf( stderr, "mem error 009\n" ); exit(-1); }
#endif
				*(big_data + idx - i2b) = val;
				val -= step*extrap_fact;
				}
			}

		/* Fill in bottom */
		jl = 0;
		for( i2b=0; i2b<=blowup*(long)(nxl-1); i2b++ ) {
			idx = i2b+offset_xb + jl*blowup*nxb + offset_yb*nxb;
			step = (*(big_data + idx + 2*nxb) - *(big_data + idx + nxb));   /* big(,y+2) - big(,y+1) */
			val  = *(big_data + idx) - step;
			for( j2b=1; j2b<=(blowup-1)/2; j2b++ ) {
#ifdef CHECK_MEM
				if( idx - j2b*nxb >= array_size ) { fprintf( stderr, "mem error 010\n" ); exit(-1); }
#endif
				*(big_data + idx - j2b*nxb) = val;
				val -= step*extrap_fact;
				}
			}

		/* Fill in top */
		jl = nyl-1;
		for( i2b=0; i2b<blowup*(long)(nxl-1); i2b++ ) {
			idx = i2b+offset_xb + jl*blowup*nxb + offset_yb*nxb;
			step = (*(big_data + idx - nxb) - *(big_data + idx - 2*nxb));  /* big(,y-1) - big(,y-2) */
			val  = *(big_data + idx) + step;
			for( j2b=1; j2b<=blowup/2; j2b++ ) {
#ifdef CHECK_MEM
				if( idx + j2b*nxb >= array_size ) { fprintf( stderr, "mem error 011\n" ); exit(-1); }
#endif
				*(big_data + idx + j2b*nxb) = val;
				val += step*extrap_fact;
				}
			}

		/* Still have to fill in the four corners at this point.   Because of the
		 * extrapolation issue noted above, we take a simple approach.  Just fill
		 * in the corner blocks with the center data value.
		 */

		/* Lower left corner */
		il = 0;
		jl = 0;
		cval = v->data[il + jl*nxl];          /* Data value in lower left corner */
		if( ! close_enough( cval, fill_val )) {
			/* Fill in lower left corner */
			for( j2b=0; j2b<=offset_yb; j2b++ )
			for( i2b=0; i2b<=offset_xb; i2b++ ) {
#ifdef CHECK_MEM
				if( i2b + j2b*nxb >= array_size ) { fprintf( stderr, "mem error 012\n" ); exit(-1); }
#endif
				*(big_data + i2b + j2b*nxb) = cval;
				}
			}

		/* Lower right corner */
		il = nxl - 1;
		jl = 0;
		cval = v->data[il + jl*nxl];          /* Data value in lower left corner */
		if( ! close_enough( cval, fill_val )) {
			/* Fill in lower right corner */
			for( j2b=0; j2b<=offset_yb; j2b++ )
			for( i2b=offset_xb; i2b<blowup; i2b++ ) {
#ifdef CHECK_MEM
				if( il*blowup + i2b + j2b*nxb >= array_size ) { fprintf( stderr, "mem error 013\n" ); exit(-1); }
#endif
				*(big_data + il*blowup + i2b + j2b*nxb) = cval;
				}
			}

		/* Upper right corner */
		il = nxl - 1;
		jl = nyl - 1;
		cval = v->data[il + jl*nxl];          /* Data value in lower left corner */
		if( ! close_enough( cval, fill_val )) {
			/* Fill in upper right corner */
			for( j2b=offset_yb; j2b<blowup; j2b++ )
			for( i2b=offset_xb; i2b<blowup; i2b++ ) {
#ifdef CHECK_MEM
				if( il*blowup + i2b + jl*blowup*nxb + j2b*nxb >= array_size ) { fprintf( stderr, "mem error 014\n" ); exit(-1); }
#endif
				*(big_data + il*blowup + i2b + jl*blowup*nxb + j2b*nxb) = cval;
				}
			}

		/* Upper left corner */
		il = 0;
		jl = nyl - 1;
		cval = v->data[il + jl*nxl];          /* Data value in lower left corner */
		if( ! close_enough( cval, fill_val )) {
			/* Fill in upper left corner */
			for( j2b=offset_yb; j2b<blowup; j2b++ )
			for( i2b=0; i2b<=offset_xb; i2b++ ) {
#ifdef CHECK_MEM
				if(  il*blowup + i2b + jl*blowup*nxb + j2b*nxb >= array_size ) { fprintf( stderr, "mem error 015\n" ); exit(-1); }
#endif
				*(big_data + il*blowup + i2b + jl*blowup*nxb + j2b*nxb) = cval;
				}
			}

		/* Paint missing value blocks */
		for( jl=0; jl<(long)nyl; jl++ )
		for( il=0; il<(long)nxl; il++ ) {
			base_val  = v->data[il   + jl*nxl];
			if( close_enough( base_val, fill_val )) {
				for( j2b=0; j2b<blowup; j2b++ )
				for( i2b=0; i2b<blowup; i2b++ ) {
#ifdef CHECK_MEM
					if( il*blowup+i2b + jl*nxb*blowup + j2b*nxb >= array_size ) { fprintf( stderr, "mem error 016\n" ); exit(-1); }
#endif
					*(big_data + il*blowup+i2b + jl*nxb*blowup + j2b*nxb ) = base_val;
					}
				}
			}

		}	/* end of BlowupType::Bilinear case */
}
