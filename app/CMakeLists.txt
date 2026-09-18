add_executable(ncview
    main.cc
)
# Phase 12c: this used to list both archives twice, justified by a comment
# claiming "core calls the in_* contract ncview_ui implements" -- a genuine
# circular static-lib dependency once, before viewer_ui_bridge.cc existed,
# but not since: core reaches the UI only through the abstract ViewerUi
# virtual interface (core/include/ncview/viewer_ui.h) at runtime, which
# creates no undefined symbol and therefore no link-time edge back into
# ncview_ui. Confirmed at the symbol level (nm on the built archives shows
# zero symbols ncview_core needs that ncview_ui provides, 37 the other way)
# and by construction: ncview_core_linkcheck already links every object
# file in ncview_core against nothing but a headless stub, which could not
# succeed if a real cycle existed. The dependency is one-directional --
# ncview_ui depends on ncview_core -- so a single ordering is sufficient.
target_link_libraries(ncview PRIVATE
    ncview_ui ncview_core
    ncview_warning_flags ncview_sanitize_flags
)

if(NCVIEW_STATIC_LINK)
    # The single most common reason a binary built on one machine won't run
    # on an HPC compute/login node: a GCC/libstdc++ version older than the
    # one it was built with. Freezing our own copy of the runtime in avoids
    # that regardless of what's on the target node.
    target_link_options(ncview PRIVATE -static-libgcc -static-libstdc++)
endif()
