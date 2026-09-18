# M3: the FLTK implementation of the ncview/interface.h toolkit seam.
add_library(ncview_ui STATIC
    src/interface_fltk.cc
    src/main_window.cc
    src/main_window_dialogs.cc
    src/plot_window.cc
)
target_include_directories(ncview_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(ncview_ui PUBLIC
    ncview_core
    fltk
    fltk_images
)
target_link_libraries(ncview_ui PRIVATE ncview_warning_flags ncview_sanitize_flags)
# ui-modernization.md Phase U2: ncview_ui already builds warnings-clean at
# -Wall -Wextra, so -Werror is enabled here too (app/tests haven't had their
# own sweep, so they're left alone -- see core/CMakeLists.txt's Phase 9 note).
if(MSVC)
    target_compile_options(ncview_ui PRIVATE /WX)
else()
    target_compile_options(ncview_ui PRIVATE -Werror)
endif()
