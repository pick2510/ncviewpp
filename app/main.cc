// Copyright (C) 2026 Dominik Strebel
//
// The real ncview entry point. Everything else -- reading the state file,
// parsing args, opening the netCDF files, building the display, and running
// the event loop -- happens in ncview_main() (core/src/ncview.cc), which
// drives the UI purely through the ncview/interface.h seam that ncview_ui
// implements (ui/src/interface_fltk.cc, ui/src/main_window.cc).
#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"

#include "ncview_ui/fltk_viewer_ui.h"

int main( int argc, char **argv )
{
	// OOP_redesign plan, Step 9b: g_app.ui must be set before
	// ncview_main() runs, since that's the first thing that reaches the
	// interface.h seam (core/src/viewer_ui_bridge.cc's forwarders
	// dereference it unconditionally).
	ncview_ui::FltkViewerUi fltk_viewer_ui;
	g_app.ui = &fltk_viewer_ui;

	return ncview_main( argc, argv, fltk_viewer_ui );
}
