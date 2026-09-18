/*
 * core/include/ncview/app_context.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * The single composition-root global. Earlier OOP_redesign passes replaced
 * ncview's original 5 free-standing globals (options, variables,
 * pixel_transform, framestore, view) with references bridged onto three
 * separately-named singletons (g_viewer_session, g_viewer_controller,
 * g_viewer_ui). Free-function callback seams (interface.h's in_/x_-prefixed
 * contract, do_buttons.cc's do_() actions) are called from many places
 * throughout core/ and ui/ with fixed signatures upstream defined -- a
 * fixed-signature free function can only reach an object instance through
 * global or static state, so eliminating application-level globals
 * entirely is not achievable without rewriting that seam itself (a much
 * larger, separate undertaking). What IS achievable, and what this does:
 * collapse three differently-named globals down to one, so there is a
 * single, clearly-named composition root instead of several pretending to
 * be independently-owned pieces of state. See PORTING.md.
 *
 * Renamed AppContext -> NcviewApp in Phase 11f, matching the object-graph
 * shape originally proposed for this arc (main() -> NcviewApp -> session/
 * controller/ui). The rename is real; one part of the original proposal
 * is not achievable as stated, and is documented on `ui` below rather
 * than silently dropped.
 */
#pragma once

#include "ncview/viewer_controller.h"
#include "ncview/viewer_session.h"
#include "ncview/viewer_ui.h"

struct NcviewApp {
	/* "Refine the architecture" plan, Phase 2 gave ViewerController a
	 * real `ViewerSession &`, which means it can no longer be default
	 * constructed -- NcviewApp needs an explicit constructor to bind it
	 * to `session` below (declaration order controls member-init order,
	 * so session must stay declared first). This makes NcviewApp no
	 * longer an aggregate; nothing brace-initializes it (grepped for
	 * `AppContext{`/`NcviewApp{`/`NcviewApp {` across the tree -- the
	 * only two uses were `AppContext g_app;`, ncview.cc, and the
	 * `extern` declarations of it), so that costs nothing. */
	ViewerSession    session;
	ViewerController controller;
	/* Non-owning: whoever constructs the real ViewerUi (main.cc for the
	 * app, tests/stub_interface.cc for tests) owns its lifetime and
	 * points this at it before anything reaches the interface.h seam.
	 *
	 * Stays a nullable pointer, not a reference bound at construction.
	 * Phase 11c looked at giving ViewerController one directly and found
	 * it can't be done while `g_app` is a static-duration global: the
	 * whole object, `controller` included, is fully constructed before
	 * main() runs, before any concrete ViewerUi exists to bind to.
	 * Phase 11f re-examined this at the top level -- could NcviewApp
	 * itself move into main() instead, so its own `ui` member becomes a
	 * real reference set once at construction? -- and found the same
	 * wall one level up, for a reason that isn't about member layout at
	 * all: core/ deliberately does not depend on which concrete ViewerUi
	 * gets used (ncview_ui::FltkViewerUi for the real app,
	 * RecordingViewerUi for tests -- see ui/ vs tests/stub_interface.cc).
	 * That choice is made at main()'s (or tests/main.cc's) own startup,
	 * not at core's compile time. A reference member must be bound at
	 * its owner's construction; for `g_app` to hold one, `g_app` itself
	 * would have to stop being a global entirely and become a genuine
	 * local in main(), with every one of the ~1,400+ existing `g_app.`
	 * call sites across core/ and ui/ rewritten to receive `NcviewApp&`
	 * as an explicit parameter instead of reaching a global -- in effect
	 * redoing Phase 11a/11b's reference-threading work for the entire
	 * codebase, not just the ~17 free functions and handful of methods
	 * those phases actually touched. That is a real, much larger
	 * project of its own, not a corner this phase cut; see PORTING.md's
	 * Phase 11f entry. What Phase 11f *did* do: every production call
	 * site that reached `g_dataset`/`variables`/`pixel_transform`/
	 * `framestore` as separate migration-bridge globals now goes through
	 * `session` directly, so this pointer is the one remaining
	 * indirection in the production access path, not one of several. */
	ViewerUi        *ui = nullptr;

	NcviewApp() : controller( session ) {}
};

/* Defined once, in ncview.cc. */
extern NcviewApp g_app;
