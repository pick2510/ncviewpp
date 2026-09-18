/*
 * core/src/view_internal.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * Private to core/src -- NOT part of the public core API (never included
 * from protos.h, never installed). "Refine the architecture" plan,
 * Phase 3e moved ViewerController's/ViewerSession's method bodies out of
 * view.cc into viewer_controller.cc/viewer_session.cc, the files named
 * after their classes. A handful of view.cc-local helpers are called
 * from both the moved code and code that stayed in view.cc (other
 * View:: methods, or -- for invalidate_variable/mouse_xy_to_data_xy/
 * view_data_edit_warn -- both); this header is the seam that lets them
 * stay un-exported to the rest of core/ while still crossing that one
 * new TU boundary. Include after ncview/protos.h in any .cc that needs
 * it.
 */
#pragma once

#include "ncview/viewer_session.h"
#include "ncview/viewer_ui.h"

struct NCVar;

/* Definition (still `int`, not `static`) stays in view.cc, alongside
 * View::changeDat -- its other caller. Guards ViewerController::draw()
 * against re-entrancy from a modal dialog popped up mid-draw. */
extern int lockout_view_changes;

/* Phase 12a: set_scan_variable()/View::changeDat() used to set
 * lockout_view_changes = true by hand immediately before calling
 * dataToPixels(), then reset it to false on the line right after --
 * except dataToPixels() has two real, reachable failure returns (one of
 * them the user pressing Cancel on the "min and max both 0" dialog),
 * and both functions `return` on that failure path before reaching the
 * reset. Left set, ViewerController::draw() (which checks the flag and
 * no-ops if set) can never clear a flag it didn't set itself, so every
 * later draw silently no-ops until the next *successful*
 * set_scan_variable()/changeDat() call. This RAII guard makes the reset
 * unconditional: construct it as the first statement of the scope that
 * used to say `lockout_view_changes = true;`, and every exit from that
 * scope -- success or early return -- clears it. */
struct LockoutViewChangesGuard {
	LockoutViewChangesGuard()  { lockout_view_changes = true; }
	~LockoutViewChangesGuard() { lockout_view_changes = false; }
	LockoutViewChangesGuard( const LockoutViewChangesGuard & ) = delete;
	LockoutViewChangesGuard &operator=( const LockoutViewChangesGuard & ) = delete;
};

/* Definition stays in view.cc (set_scan_variable() and View::changeDat
 * are its other two callers); ViewerController::draw() is the third.
 * Phase 11a threaded ViewerSession&/ViewerUi& through this instead of
 * reading the global `view` alias and calling x_set_var_sensitivity()/
 * set_buttons() through the free-function seam -- every caller still
 * reaches g_app.session/g_app.ui explicitly for now, since none of
 * View/ViewerController hold their own reference yet (that's 11b/11c);
 * the point here is that invalidate_variable() itself no longer touches
 * a global internally. */
void invalidate_variable( NCVar *var, ViewerSession &session, ViewerUi &ui );

/* Definition stays in view.cc (View::setDataeditPlace() is its other
 * caller); ViewerController::reportPosition()/setMinFromCurdata()/
 * setMaxFromCurdata()/plotXY() are the rest. */
void mouse_xy_to_data_xy( int mouse_x, int mouse_y, int blowup, size_t *data_x, size_t *data_y );

/* Definition stays in view.cc (View::setAxis() is its other caller);
 * ViewerController::changeCurDim()/setCurDimIndex() are the rest. Phase
 * 11a threaded ViewerSession&/ViewerUi& through this the same way as
 * invalidate_variable() above, for the same reason. */
void view_data_edit_warn( ViewerSession &session, ViewerUi &ui );
