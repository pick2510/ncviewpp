# M1 checkpoint, part 1: prove every symbol ncview_core needs is resolved by
# nothing but the headless stub_interface.cc implementation of
# ncview/interface.h (no UI, no X11). ncview_core has no `main` of its own
# (M1 renamed upstream's to ncview_main()), so whole-archive-linking it
# into an ordinary executable (pulling in every .o) can't collide with
# anything -- any missing symbol anywhere in ncview_core fails right here
# at link time. Deliberately an executable, not a shared library: a shared
# object requires every static archive linked into it to be PIC, which
# NCVIEW_STATIC_LINK's non-PIC system static libs (e.g. Homebrew's
# libz.a) aren't -- an executable carries no such requirement.
add_executable(ncview_core_linkcheck
    stub_interface.cc
    linkcheck_main.cc
)
# GNU ld/LLD's -Wl,--whole-archive/--no-whole-archive syntax isn't
# understood by Apple's ld64 at all ("unknown options: --whole-archive
# --no-whole-archive"); its equivalent is -force_load taking the archive's
# path directly, with no on/off toggle wrapping other libraries.
if(APPLE)
    target_link_libraries(ncview_core_linkcheck PRIVATE
        -Wl,-force_load,$<TARGET_FILE:ncview_core> ncview_core
    )
else()
    target_link_libraries(ncview_core_linkcheck PRIVATE
        -Wl,--whole-archive ncview_core -Wl,--no-whole-archive
    )
endif()
target_link_libraries(ncview_core_linkcheck PRIVATE ncview_sanitize_flags)

# M1 checkpoint, part 2 (and the general unit test suite): functional
# behavior of core's logic, from pure functions (stringlist, calcalcs,
# util's string/number helpers) up through the file_netcdf.cc I/O boundary
# against a real, synthetically-created netCDF file. Links normally (not
# whole-archive), so only the .o's these tests actually call get pulled in
# -- no main() clash with ncview_core_linkcheck above.
#
# Built on doctest (vendored single header, third_party/doctest) rather
# than a hand-rolled harness or a framework needing its own build step
# (e.g. GoogleTest) -- it's a real, widely-used C++ test framework with
# named test cases, CHECK/REQUIRE assertions, and readable failure output,
# for the cost of one header.
add_executable(ncview_core_tests
    main.cc
    test_stringlist.cc
    test_calendar.cc
    test_util.cc
    test_file_netcdf.cc
    test_netcdf_file.cc
    test_dataset.cc
    test_pixels.cc
    test_shrink.cc
    test_expand.cc
    test_dim_mapping.cc
    test_frame_renderer.cc
    test_frame_cache.cc
    test_rcfile.cc
    test_time_fmt.cc
    test_varlist.cc
    test_view_data_edit.cc
    test_controller_characterization.cc
    test_session_fixture.cc
    test_playback.cc
    test_nc_fixture.cc
    test_button_dispatch.cc
    test_view_null_guards.cc
    test_view_navigation.cc
    test_view_draw.cc
    test_view_dims.cc
    test_view_check_new_data.cc
    test_view_xy_plot.cc
    test_view_1d_guards.cc
    test_view_labels.cc
    test_multifile.cc
    test_do_print.cc
    test_overlay.cc
    test_file_layer.cc
    test_file_metadata.cc
    test_cli_options.cc
    test_colormaps.cc
    stub_interface.cc
)
target_include_directories(ncview_core_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/doctest
    # So test files can #include "support/session_fixture.h",
    # "support/scratch_home.h", etc. -- shared test-only infrastructure
    # under tests/support/ (see the "refine the architecture" plan's
    # Phase 0a), analogous to how test_udunits_helper.h is already
    # included unqualified from tests/ itself.
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(ncview_core_tests PRIVATE ncview_core ncview_sanitize_flags ncview_coverage_flags)
add_dependencies(ncview_core_tests ncview_core_linkcheck)
add_test(NAME ncview_core_tests COMMAND ncview_core_tests)
# Defense in depth against a hang like the HDF5 file-locking one this suite
# hit on Windows CI (see test_file_netcdf.cc): fail fast with a clear CTest
# timeout instead of stalling the whole workflow until the job-level limit
# kills it with no indication of which test or why.
set_tests_properties(ncview_core_tests PROPERTIES TIMEOUT 60)

# docs/modernization.md Phase 0c: end-to-end Xvfb screenshot regression test,
# driving the real `ncview` binary (needs ncview_ui, so only buildable once
# that target exists -- see the top-level CMakeLists.txt's add_subdirectory
# order: core, ui, app, then tests). Off by default (NCVIEW_BUILD_UI_TESTS);
# see that option's own doc string in the top-level CMakeLists.txt for why.
if(NCVIEW_BUILD_UI_TESTS)
    add_test(NAME ncview_ui_smoke
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/ui_smoke.sh $<TARGET_FILE:ncview>)
    set_tests_properties(ncview_ui_smoke PROPERTIES TIMEOUT 120)
endif()
