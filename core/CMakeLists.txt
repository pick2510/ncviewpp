# M1: core/*.cc is upstream src/*.c, mechanically ported to compile as C++.
# No UI symbols reach this library; it talks to ncview_ui only through the
# ncview/interface.h seam. See PORTING.md.
add_library(ncview_core STATIC
    src/calcalcs.cc
    src/dataset.cc
    src/do_buttons.cc
    src/do_print.cc
    src/epic_time.cc
    src/cli_options.cc
    src/colormap_library.cc
    src/file.cc
    src/file_netcdf.cc
    src/frame_cache.cc
    src/frame_renderer.cc
    src/handle_rc_file.cc
    src/ncview.cc
    src/overlay.cc
    src/render_pipeline.cc
    src/stringlist.cc
    src/udu.cc
    src/utCalendar2_cal.cc
    src/legal_text.cc
    src/var_metadata.cc
    src/varname_utils.cc
    src/view.cc
    src/viewer_controller.cc
    src/viewer_session.cc
    src/viewer_ui_bridge.cc
)
set_target_properties(ncview_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(ncview_core PRIVATE ncview_warning_flags ncview_sanitize_flags ncview_coverage_flags)
# modernization.md Phase 9: core/WARNINGS.md is empty as of this phase, so
# -Werror is enabled here -- only for ncview_core, not ncview_ui/app/tests,
# which haven't had the same warning sweep done on their own sources.
if(MSVC)
    target_compile_options(ncview_core PRIVATE /WX)
else()
    target_compile_options(ncview_core PRIVATE -Werror)
endif()
target_include_directories(ncview_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(ncview_core PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/udunits2/lib
)
target_link_libraries(ncview_core PUBLIC
    netCDF::netcdf
    libudunits2
)
# This port always builds against the vendored UDUNITS-2 (see udu.cc), so
# units support is unconditional rather than an autotools-detected option.
target_compile_definitions(ncview_core PUBLIC HAVE_UDUNITS2)

# Last-resort udunits2.xml location for running straight out of an
# uninstalled build tree (see udu.cc:udu_utinit). $UDUNITS2_XML_PATH and the
# installed-prefix default (set by third_party/udunits2's own CMakeLists)
# both take priority over this.
target_compile_definitions(ncview_core PRIVATE
    NCVIEW_BUILD_TREE_UDUNITS2_XML="${CMAKE_SOURCE_DIR}/third_party/udunits2/lib/udunits2.xml"
)

# M6: the (optional -- built-in colormaps already cover normal use)
# supplementary *.ncmap colormap files this project installs alongside the
# binary (see top-level CMakeLists.txt's install(DIRECTORY data/colormaps ...)).
# ncview.cc's init_display_interface() searches this directory in addition to
# $NCVIEWBASE/$HOME and ".", exactly like upstream's NCVIEW_LIB_DIR.
target_compile_definitions(ncview_core PRIVATE
    NCVIEW_LIB_DIR="${CMAKE_INSTALL_FULL_DATADIR}/ncview/colormaps"
)
