/*
 * core/include/ncview/viewer_controller.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * Owns the 21 do_*() actions that used to live as free functions in
 * do_buttons.cc, plus the playback state (cur_button, previously a file
 * -static) that drives them. OOP_redesign plan, Step 7 (scoped down --
 * see the commit message this ships with for what was deliberately left
 * for a later pass: the full ViewerUi virtual-interface conversion of
 * ncview/interface.h is NOT part of this step).
 *
 * do_buttons.cc originally kept the do_*() free-function names as thin
 * one-line forwards to g_app.controller, so every existing call site
 * could keep compiling unchanged while the logic moved here. Once those
 * forwards had nothing left in them but the forward, "refine the
 * architecture" plan's Phase 1 deleted them and updated every call site
 * to call the ViewerController method directly instead
 * (g_app.controller.range(modifier), not do_range(modifier)).
 * do_buttons.cc now only keeps which_button_pressed(), in_button_pressed(),
 * and in_colormap_selected() -- the parts with no direct-call equivalent.
 *
 * "Refine the architecture" plan, Phase 2 added a real `ViewerSession
 * &session_` (below) and, with it, the ~9 view.cc entry points that used
 * to stay free functions purely because each carried its own
 * `if (view == NULL) return;` guard. That guard was never really about
 * `View` possibly being null -- it was standing in for a fact only the
 * session can know: "no variable is selected yet". Moving these onto the
 * class that owns the active `View` lets the guard live where the fact
 * it's protecting actually lives, instead of being duplicated at every
 * UI call site. NcviewApp (ncview/app_context.h; AppContext before Phase
 * 11f's rename) now constructs this with a reference to its own
 * `session` member.
 */
#pragma once

#include "ncview/defines.h"
#include "ncview/viewer_session.h"

class ViewerController {
public:
	/* Deliberately does not also take a ViewerUi& -- Phase 11c looked at
	 * adding one (matching the plan's original ask) and found it's not
	 * achievable: this object is a member of the static-duration global
	 * NcviewApp (app_context.h), constructed before main() runs, before
	 * any concrete ViewerUi implementation exists to bind a reference to.
	 * Phase 11f re-examined this at NcviewApp's own level (could the
	 * whole composition root move into main(), making this achievable?)
	 * and confirmed it's a real architectural wall, not a missed trick --
	 * see app_context.h's `ui` member for the full reasoning. Its ~27
	 * seam calls reach g_app.ui-> explicitly instead, the same pattern
	 * Phase 11b used for View for the identical reason. */
	explicit ViewerController( ViewerSession &session ) : session_( session ) {}

	Button whichButtonPressed() const { return cur_button_; }

	/* The former in_button_pressed() switch body. */
	void dispatch( Button button_id, Modifier modifier );

	void range( Modifier modifier );
	void dimset( Modifier modifier );
	void restart( Modifier modifier );
	void rewind( Modifier modifier );
	void quit( Modifier modifier );
	void backwards( Modifier modifier );
	void pause( Modifier modifier );
	void forward( Modifier modifier );
	void fastforward( Modifier modifier );
	void colormapSelect( Modifier modifier );
	/* The former in_colormap_selected()'s body -- direct-pick counterpart
	 * of colormapSelect()'s cycle-by-one-step handling. */
	void colormapSelectByName( const char *name );
	void invertPhysical( Modifier modifier );
	void dataEdit( Modifier modifier );
	void invertColormap( Modifier modifier );
	/* Empty on purpose -- see do_buttons.cc's former do_set_minimum/
	 * do_set_maximum. Kept (rather than dropped, as the plan suggested)
	 * because Button::Minimum/Button::Maximum are still reachable via
	 * interface_fltk.cc's NCVIEW_TEST_BUTTON name table; removing these
	 * would turn that path's outcome from a no-op into dispatch()'s
	 * exit(-1) default case. */
	void setMinimum( Modifier modifier );
	void setMaximum( Modifier modifier );
	void blowup( Modifier modifier );
	void transform( Modifier modifier );
	void blowupType( Modifier modifier );
	void info( Modifier modifier );
	/* Named optionsDialog, not options -- a member named `options` would
	 * shadow the global `extern Options options;` for unqualified lookup
	 * inside this class's other methods. */
	void optionsDialog( Modifier modifier );
	/* Delegates to do_print() (core/src/do_print.cc), which stays a free
	 * function -- its printing logic is a large, separate subsystem that
	 * this scoped-down step doesn't move wholesale. */
	void print();

	/* Phase 2: the view.cc entry points that carried their own
	 * `view == NULL` guard, moved verbatim (bodies unchanged -- each gets
	 * a local `std::unique_ptr<ViewState> &view = session_.activeView();`
	 * alias so the existing `view->...` text needs no further edits). */
	int  draw( int allow_framestore_usage, int force_range_to_frame );
	int  stepView( int delta, int interpretation );
	void changeCurDim( char *dim_name, Modifier modifier );
	void setCurDimIndex( const char *dim_name, long place );
	void setMinFromCurdata();
	void setMaxFromCurdata();
	void plotXY();
	void recomputeColorbar();
	void reportPosition( int x, int y, unsigned int button_mask );

private:
	ViewerSession &session_;
	Button cur_button_ = Button::Pause;
};
