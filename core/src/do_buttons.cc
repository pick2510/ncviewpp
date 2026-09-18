/*
 * Ncview by David W. Pierce.  A visual netCDF file viewer.
 * Copyright (C) 1993 through 2024 David W. Pierce
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
 * David W. Pierce
 * davidwilliampierce@gmail.com
 */

/*************************************************************************
 * which_button_pressed(), in_button_pressed(), and in_colormap_selected()
 * -- the parts of this file with no ViewerController equivalent to call
 * directly. The 21 do_*() action functions that used to live here (each
 * a one-line forward onto g_app.controller, added at OOP_redesign plan
 * Step 7) are gone: "refine the architecture" plan, Phase 1 -- every
 * call site now calls the corresponding ViewerController method
 * directly (g_app.controller.range(modifier), etc.) instead of
 * round-tripping through a same-named free function with nothing left
 * in its body but the forward. See ncview/protos.h's "in do_buttons.c"
 * comment.
 *************************************************************************/

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "ncview/app_context.h"

	Button
which_button_pressed( void )
{
	return( g_app.controller.whichButtonPressed() );
}

/*****************************************************************************
 * Called when a button is pressed (by the UI's widget callbacks, or by
 * core itself -- e.g. util.cc pauses playback on an error by calling
 * do_pause() directly).  Argument 'button_id' indicates which button was
 * pressed.  Argument modifier should ideally take on one of 4 values:
 * Modifier::M1, Modifier::M2, Modifier::M3, and Modifier::M4, used in a
 * generalized sense to mean "normal action", "accelerated action",
 * "backwards action", and "accelerated backwards action". If these are
 * not available, just always use Modifier::M1.
 */
void
in_button_pressed( Button button_id, Modifier modifier )
{
	g_app.controller.dispatch( button_id, modifier );
}

/*****************************************************************************
 * Vector through this routine when a colormap has been picked directly (by
 * name) from the UI's colormap combobox -- the direct-pick counterpart of
 * ViewerController::colormapSelect()'s cycle-by-one-step
 * BUTTON_COLORMAP_SELECT handling (viewer_controller.cc), which this
 * deliberately mirrors (same in_install_..() then
 * view_draw()/view_recompute_colorbar() tail) so a picked colormap repaints
 * exactly like a cycled one does.
 */
void
in_colormap_selected( const char *name )
{
	g_app.controller.colormapSelectByName( name );
}
