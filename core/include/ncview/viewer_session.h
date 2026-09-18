/*
 * core/include/ncview/viewer_session.h
 *
 * Copyright (C) 2026 Dominik Strebel
 *
 * ViewerSession -- OOP_redesign plan, Step 8/9a. Owns the pieces of runtime
 * state that used to be independent globals: the Dataset (Step 5), the
 * active ViewState (Step 6), the FrameCache (Step 3), and now (Step 9a)
 * the storage behind the global Options struct's ~50 fields, split into
 * the four groups the plan's ViewerSession section describes.
 *
 * A single global ViewerSession instance is constructed in ncview.cc; the
 * legacy globals `g_dataset`, `view`, and `framestore` are references
 * bound to this instance's members (the migration-bridge technique Step 5
 * introduced for `variables`). Options takes the same approach one level
 * down: rather than becoming a reference to a whole ViewerSession member
 * (Options's fields don't live together in one place -- they're spread
 * across all four groups below), the *global Options object itself* is
 * unchanged in name and field list, but every one of its fields is now a
 * reference member bound, in its constructor (defines.h declares it,
 * viewer_session.cc defines it), to the corresponding field in one of the
 * structs below. So `options.debug`, `options.blowup`, etc. keep reading
 * and writing exactly as before at all ~300+ existing call sites -- the
 * storage those references point to now genuinely lives on ViewerSession.
 */
#pragma once

#include <memory>

#include "ncview/dataset.h"
#include "ncview/defines.h"
#include "ncview/frame_cache.h"
#include "ncview/frame_renderer.h"

/* The scaled frame size last reported to the UI via in_set_2d_size(),
 * tracked so ViewerController::draw() only re-reports it on a real
 * change (see ViewerSession::lastFrameSize(), Phase 11h). */
struct LastFrameSize {
	size_t width = 0;
	size_t height = 0;
};

/* Feeds FrameRenderer::PixelMapSettings (via ViewerSession::pixelMapSettings()
 * below) plus the couple of extra render-shaped fields PixelMapSettings
 * doesn't need directly (blowup, min_max_method). */
struct RenderSettings {
	int		blowup = 0;
	BlowupType	blowup_type{};
	ShrinkMethod	shrink_method{};
	Transform	transform{};
	int		invert_colors = 0;
	int		invert_physical = 0;
	int		n_colors = 0;
	int		n_extra_colors = 0;
	int		display_type = 0;
	int		autoscale = 0;
	MinMaxMethod	min_max_method{};
};

/* Playback settings. The playback *state* (formerly do_buttons.cc's
 * file-static cur_button) is ViewerController::cur_button_, not here. */
struct PlaybackSettings {
	float	frame_delay = 0;
	int	delta_step = 0;
	int	beep_on_restart = 0;
	int	stop_on_restart = 0;
};

/* Session-lifetime display preferences. */
struct SessionDisplayPrefs {
	int		save_frames = 0;
	int		missval_r = 0, missval_g = 0, missval_b = 0;
	float		scale = 0, offset = 0;
	std::unique_ptr<OverlayOptions>	overlay;
	std::string	calendar;
};

/* Set once from argv in parse_options()/initialize_misc(), never mutated
 * afterward by the running app. */
struct StartupSettings {
	int		dump_frames = 0;
	int		small = 0;
	int		maxsize_pct = 0;
	int		maxsize_width = 0;
	int		maxsize_height = 0;
	int		private_colormap = 0;
	int		no_1d_vars = 0;
	VarselStyle	varsel_style{};
	int		listsel_max = 0;
	int		enable_group_sel = 0;
	int		no_char_dims = 0;
	int		no_autoflip = 0;
	int		color_by_ndims = 0;
	int		auto_overlay = 0;
	int		want_extra_info = 0;
	int		show_sel = 0;
	int		debug = 0;
	int		t_conv = 0;
	int		blowup_default_size = 0;
};

class ViewerSession {
public:
	ViewerSession() = default;

	Dataset& dataset() { return dataset_; }
	const Dataset& dataset() const { return dataset_; }

	std::unique_ptr<ViewState>& activeView() { return view_; }
	const std::unique_ptr<ViewState>& activeView() const { return view_; }

	FrameCache& frameCache() { return frame_cache_; }
	const FrameCache& frameCache() const { return frame_cache_; }

	/* The active colormap's pixel index table (PseudoColor remap target;
	 * see FrameRenderer). Sized to n_colors+n_extra_colors and rebuilt
	 * whenever a colormap is installed -- see ncview.cc's
	 * in_install_colormap_by_name() and friends. */
	std::vector<ncv_pixel>& pixelTransform() { return pixel_transform_; }
	const std::vector<ncv_pixel>& pixelTransform() const { return pixel_transform_; }

	RenderSettings& renderSettings() { return render_settings_; }
	PlaybackSettings& playbackSettings() { return playback_settings_; }
	SessionDisplayPrefs& sessionDisplayPrefs() { return session_display_prefs_; }
	StartupSettings& startupSettings() { return startup_settings_; }

	/* Formerly a lone `static PrintOptions printopts;` module-static in
	 * do_print.cc with no getter or setter -- print_init() was the only
	 * way to reach it, which is why every test that exercises printing
	 * has to call print_init() by hand before anything else can see sane
	 * defaults ("Refine the architecture" plan, Phase 5b). */
	PrintOptions& printSettings() { return print_settings_; }

	/* Formerly overlay.cc's `static int my_current_overlay;` -- a getter
	 * (overlay_current()) existed, but nothing owned the storage; moved
	 * here (Phase 11f) the same way Phase 5b gave do_print.cc's static an
	 * owner. Default matches overlay_init()'s own reset value. */
	int& currentOverlay() { return current_overlay_; }

	/* Formerly ViewerController::draw()'s own function-local `static
	 * size_t last_x_size, last_y_size` (Phase 11h) -- process-global
	 * state SessionFixture never reset, so a test's very first draw()
	 * could silently skip its in_set_2d_size report if an earlier,
	 * unrelated test's draw() happened to leave the same scaled size
	 * behind. Moving it here means SessionFixture's existing
	 * `g_app.session = ViewerSession()` reset now resets this too, so
	 * every fresh session correctly reports its size on its own first
	 * draw regardless of what ran before it. This was also the real,
	 * previously-undiagnosed cause of test_do_print.cc's long-documented
	 * order-dependent assertion count (known since Phase 7a). */
	LastFrameSize& lastFrameSize() { return last_frame_size_; }

	/* Builds FrameRenderer's settings from the render-shaped field group
	 * (transform, invert_colors, invert_physical, n_colors,
	 * n_extra_colors, display_type). Narrowed from `const Options&` to
	 * `const RenderSettings&` in Phase 11e -- this was the one place in
	 * the tree taking a whole Options&; all six fields it reads already
	 * live directly on RenderSettings, so the narrower type documents
	 * the real dependency instead of the global's full ~50-field shape. */
	PixelMapSettings pixelMapSettings( const RenderSettings &render ) const;

	/* "Refine the architecture" plan, Phase 2: view.cc entry points whose
	 * only reason to stay a free function was a `view == NULL` guard
	 * standing in for "no variable selected yet" -- a session fact, so it
	 * belongs here rather than at every call site. Bodies moved verbatim
	 * (formerly view_current_nt(), view_get_cur_dim_index(),
	 * invalidate_all_saveframes()). */
	long currentNt() const;
	size_t curDimIndex( const char *dim_name ) const;
	void invalidateAllSaveframes();

private:
	Dataset dataset_;
	std::unique_ptr<ViewState> view_;
	FrameCache frame_cache_;
	std::vector<ncv_pixel> pixel_transform_;

	RenderSettings render_settings_;
	PlaybackSettings playback_settings_;
	SessionDisplayPrefs session_display_prefs_;
	StartupSettings startup_settings_;
	PrintOptions print_settings_;
	int current_overlay_ = OVERLAY_NONE;
	LastFrameSize last_frame_size_;
};
