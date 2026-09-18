# Changelog

Notable changes to this project, condensed from `PORTING.md`,
`modernization.md`, and the git history. Dates are release-tag dates.
See those two files for full narrative detail and rationale; this is
the short version.

## [Unreleased]

### Added
- `viridis`, `plasma`, `inferno`, `magma`, and `cividis` colormaps
  (matplotlib's perceptually-uniform/colorblind-friendly family),
  alongside the other built-in colormaps.

### Changed
- `core/`'s previously-independent 5 global variables (`options`,
  `variables`, `pixel_transform`, `framestore`, `view`) are now all
  facets of one object, `ViewerSession` -- see `PORTING.md`'s
  "OOP_redesign" section for the full nine-step migration. Also
  introduces `Dataset`, `ViewState`, `FrameCache`, `ViewerController`,
  and a `ViewerUi` virtual interface replacing the old free-function
  toolkit seam. No user-visible behavior change; this is purely an
  internal ownership/structure change, verified at every step against
  the full test suite and the Xvfb screenshot regression harness.
- The root-level `README`/`COPYRIGHT`/`CHANGE_LOG` files (UDUNITS-2's own,
  required at that exact path by its vendored CMake build) are no longer
  committed to the repo -- generated there at configure time instead, from
  the submodule's own copies, and `.gitignore`'d.

### Fixed
- Two memory leaks in variable-switching, found as a side effect of the
  `OOP_redesign` ownership work above: `set_scan_variable()`'s
  early-return path and `invalidate_variable()` both used to
  reassign/reset the global view pointer without deleting what it
  previously pointed to. Now owned via `std::unique_ptr`, which frees the
  old object automatically on both paths.
- Opened netCDF files were never closed for the lifetime of the process
  (`fi_close()` had zero callers anywhere in the codebase) -- fixed as
  part of the same `OOP_redesign` work by introducing a `NetCDFFile` RAII
  wrapper that closes on destruction.
- Info-row label text sitting off-center in its bordered box (previously
  compensated with a manual, environment-specific pixel offset that could
  over- or under-correct depending on the platform's actual font metrics;
  each label now shares its box's exact rect instead, so FLTK's own default
  centering lands correctly everywhere).
- A CF scalar coordinate whose units are a valid UDUNITS time (e.g. WRF's
  "XTIME", `minutes since ...`) is now formatted as a calendar date, the
  same way a real time dimension's current value already was, instead of
  showing the raw `<value> <units>` string. Its label was also visually
  indented/off-center when the adjacent Skip label had nothing to show;
  it now sits flush left in that case.

## [0.3.0] - 2026-09-08

### Changed
- Printing now uses the platform's native print dialog (`Fl_Printer`) in both
  the main window and XY plot windows, instead of ncview's own hand-written
  PostScript generator shelling out to `lpr`. Fixes printing on Windows,
  where `mkstemp`/`lpr` never existed, so it silently produced nothing there.
  ncview's own Printer Options dialog is trimmed to the page-layout settings
  the native dialog doesn't cover (margins, font, and the include-toggles);
  where to print (printer vs file, which printer, paper, orientation,
  copies) is now the native dialog's job.
- "Dump Data" (data-edit grid) and the XY plot window's "Dump" button now
  use the platform's native save-file dialog instead of a plain text-input
  prompt for the output filename.

### Fixed
- Ctrl+click on the Rewind/Backwards/Forward/Fastforward transport buttons
  now accelerates stepping (a percentage of the file's frame count instead
  of one frame at a time), matching upstream. Added a "View/Range (Current
  Frame)" menu entry restoring upstream's right-click-on-Range action (set
  the color range from just the current frame). Both were unreachable since
  the port's button/menu-item callback always used the default action
  regardless of mouse button or modifier keys.

## [0.2.3] - 2026-09-07

### Fixed
Fourth external code-review pass, over `calcalcs.cc`, `utCalendar2_cal.cc`,
and the UI layer (which came back clean):
- `get_day_to_user_converter()` released a UDUNITS2 `ut_unit*` with libc
  `free()` instead of `ut_free()`.
- Three jday→date routines had a misplaced-parenthesis bug turning a real
  error code into a boolean (effectively dead code today, fixed anyway).

### Docs
- README: documented `flex`/`bison`/`texinfo` as build prerequisites for
  the vendored UDUNITS-2 build (always required, never previously listed).
- `PORTING.md`/`modernization.md` updated to reflect the four post-release
  audit rounds and retire two stale claims (the `fill_dim_structs()`
  infinite loop and the file-open-chooser gap, both since fixed/closed).

## [0.2.2] - 2026-09-07

### Fixed
Third external code-review pass, over the remaining `core/src/*.cc` files
not covered by the first two rounds:
- Out-of-bounds array read printing a variable with no scan axis
  (`do_print.cc`).
- Stack buffers sized `20` instead of the real dimension cap `MAX_NC_DIMS`
  (1024) (`file.cc`).
- Missing lower-bound clamp on a month index in the legacy EPIC time
  formatter (`epic_time.cc`).
- Two `ut_unit*` leaked on every return path of `udu_calc_tgran()`.

## [0.2.1] - 2026-09-07

### Fixed
Two external code-review passes covering `view.cc`, `file_netcdf.cc`,
`util.cc`, `ncview.cc`, `stringlist.cc`, `overlay.cc`, and
`handle_rc_file.cc`:
- **Critical**: `fill_dim_structs()`'s multi-file time-units check hung
  forever on any multi-file (virtual) variable with a timelike first
  dimension — a loop cursor was never advanced (pre-existing upstream
  bug, ported verbatim, finally fixed here).
- **Critical**: high `-nc` color counts wrapped the brightest pixel
  values to near-zero color indices instead of the intended color; the
  `-nc` bound itself allowed values past the real palette size.
- **High**: null-pointer crash in `-scale`/`-offset` against a coordinate
  variable with a `missing_value`/`_FillValue` attribute.
- **High**: stack buffer overflow in the data-edit-dump filename prompt
  dialog (`in_dialog()` copied up to 999 bytes into a 132-byte buffer).
- **High**: `-debug` crashed on a variable with a singleton (non-scannable)
  dimension; out-of-bounds indexing navigating a variable with no scan
  axis via the transport buttons.
- **Medium**: malformed numeric netCDF attributes (e.g. `valid_range="abc"`)
  silently accepted as valid; 1-D `NC_CHAR` coordinate variables displayed
  one repeated character instead of their real value; a `View` object
  leaked on every variable switch; a group-name allocation leaked on every
  group visited during netCDF4 group traversal; an uninitialized file
  descriptor could be closed on a failed `nc_open()`.
- **Low**: `.ncviewrc` parsing truncated a valid final line with no
  trailing newline, and mis-detected quote-escaping for values ending in
  an even run of backslashes; malformed overlay-file lines used
  stale/uninitialized coordinates; several CLI options
  (`-cal`/`-scale`/`-offset`/`-listsel_max`/`-missvalrgb`/`-nc`) read past
  the end of `argv` when given with no following argument; a temp-file
  descriptor and file leaked if writing `.ncviewrc` failed partway through.

### Added
- Startup file-open dialog: launching with no input files now pops a
  native `Fl_Native_File_Chooser` (multi-select, covering both a single
  file and a whole one-file-per-timestep series) instead of exiting with
  "no displayable variables found!".

## [0.2.0] - 2026-09-07

The core C++17 modernization (`modernization.md`, Phases 0-9 + U1-U2) and
a large UI polish pass, both landed since 0.1.0.

### Core modernization
- Warnings-clean, `-Werror`-enforced build for `ncview_core` and `ncview_ui`
  on all three CI platforms (previously compiler-default, zero warning
  flags anywhere).
- Characterization tests (`tests/test_pixels.cc`, `test_rcfile.cc`,
  `test_time_fmt.cc`, `test_varlist.cc`) and an Xvfb screenshot regression
  harness (`tests/ui_smoke.sh`) added as a safety net before refactoring.
- An `-fsanitize=address,undefined` CI job added; one real ASan-caught
  heap-buffer-overflow off-by-one fixed (`view_data_edit()`).
- `#define` constant groups converted to `enum class` (`Transform`,
  `Dimension`, `Modifier`, `Button`, `Label`, `TimeStandard`/`TimeGranularity`,
  ...); bounded `snprintf`/equivalents replacing every raw `strcpy`/`strcat`
  in `core/`; `Stringlist`'s intrusive linked list replaced with
  `std::vector`; `char*`-returning core functions converted to `std::string`;
  `NCVar`/`FDBlist`/`NCDim`'s intrusive lists replaced with owning
  `std::vector<std::unique_ptr<...>>` containers; `View`/`FrameStore`/
  `Options`/`PrintOptions`'s raw `malloc`/fixed-buffer state converted to
  RAII containers; the `interface.h`/`protos.h` toolkit seam made
  const-correct. `ui/`'s self-managed raw pointers converted to RAII in a
  parallel pass.
- Deliberately left alone (documented, not oversights): `core/`'s ~180
  raw `exit()` calls (the codebase's uniform error-handling convention)
  and its 5 global variables (the core/UI seam's shared state).
- Fixed along the way: `udu_calc_tgran()` misclassifying every real
  CF-convention time axis as `TGRAN_SEC`; a stray debug `printf` left in
  the render path; `size_t` values printed via `%ld` (UB on Windows LLP64);
  a segfault in `add_var_to_list()` from field-initialization ordering
  (caught by the Xvfb smoke test).

### UI
- Colormap picker changed from a cycle-through-on-click button to a
  combobox showing name + live preview swatch.
- Dimension-row value sliders made draggable (previously step-only via
  prev/next buttons); fixed appearing unresponsive during drag.
- Settings/occasional-action buttons moved into a menu bar (native system
  menu bar on macOS); animation-speed ("Delay") control ported from
  upstream's scrollspeed widget.
- Bottom layout redesigned: dead Min/Max buttons removed, variable/colormap
  selection moved below the dimension rows and centered, the whole lower
  block now sizes to its actual content and shifts to fit however many
  dimensions the active variable has, instead of a fixed placeholder height.
- Fixed: the "Plot Along Dimension" popup rendering embedded inside the
  main window instead of as its own window; the frame/time title label
  going stale when stepping via a dimension row's own arrows; overlapping
  info-row labels/boxes; the scan-axis slider freezing for multi-file
  (virtual) variables (a stale cached dimension size never updated as
  more files were merged in).

### Build & CI
- `NCVIEW_STATIC_LINK` (for HPC clusters without matching shared netCDF/
  HDF5) verified end-to-end, closing three real gaps.
- Cross-platform `-Werror` fallout fixed across Linux/macOS/Windows CI
  (format-string/size_t mismatches, MinGW typedef-for-linkage issues,
  Apple `sprintf` deprecation, `system()` return-value checks, and more).
- `tests/ui_smoke.sh`'s exact-pixel comparison made reproducible in CI
  (grayscale font antialiasing forced to match the goldens' rendering).

## [0.1.0] - 2026-09-05

Initial release of this C++/FLTK/CMake port of upstream ncview 2.1.11
(originally C + X11/Xt/Athena widgets, built with autotools). See
`PORTING.md` for the full milestone-by-milestone history (M0-M6); in
short:

- `core/`: upstream's `src/*.c` ported to C++ (`ncview_core`), behind a
  toolkit-agnostic seam (`interface.h`/`protos.h`) that a headless stub
  (`tests/stub_interface.cc`) proves has no hidden UI dependency.
- `ui/`: a from-scratch FLTK implementation of that seam — main window,
  colorbar, dimension controls, variable selector, range/options/dimset/
  printer-options dialogs, data-edit grid, XY plots, overlay selection —
  replacing upstream's X11/Xt/Athena widgets and two vendored custom Xt
  widgets (`SciPlot.c`, `RadioWidget.c`) entirely.
- FLTK and UDUNITS-2 vendored as git submodules and built from source, so
  the only external dependencies are netCDF and expat (plus flex/bison/
  texinfo to build the vendored UDUNITS-2).
- Continuous scroll-to-zoom and drag-to-pan on the plot, replacing
  upstream's discrete click-to-blowup zoom.
- Cross-platform from day one: Linux, macOS, and Windows, via CI-built
  packaged releases — upstream is X11/Unix-only.
- Real bugs found and fixed during the port (each documented in
  `PORTING.md`'s M5/M6 notes): printed/dumped pixels coming out black
  (a missing 8→16-bit colormap channel scale upstream's PostScript writer
  expected); a crash on any frame with a missing value when overlays were
  in use (`pixel_transform` never allocated outside the X11 colorcell path
  this port doesn't carry over); automatic coastline overlays silently
  never triggering (a stubbed resource-default function always returned
  the "off" default); the on-screen colorbar not reflecting an active
  transform or "Invert Colormap" the way the image itself did.
