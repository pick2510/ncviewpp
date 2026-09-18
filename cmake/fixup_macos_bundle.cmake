# Run at install time (via install(SCRIPT ...) in the top-level
# CMakeLists.txt), after install(RUNTIME_DEPENDENCY_SET) has copied every
# bundled dylib into lib/ alongside the ncview binary in bin/.
#
# Homebrew builds its dylibs with absolute (not @rpath-relative) install
# names and dependency references. install(RUNTIME_DEPENDENCY_SET) only
# *copies* the files it resolves -- it never rewrites any load-command
# reference -- so both ncview itself and every bundled dylib still
# reference the *original* absolute Homebrew paths (e.g.
# "/opt/homebrew/opt/netcdf/lib/libnetcdf.22.dylib") for whatever they
# depend on. ncview's own LC_RPATH (@executable_path/../lib, set via
# INSTALL_RPATH in the top-level CMakeLists.txt) is never even consulted
# for those, since @rpath is only consulted for dependencies that are
# themselves *named* "@rpath/...". Confirmed with a real built package
# via `otool -l`/`otool -L`: the bundle only actually works on a machine
# with an identical Homebrew layout already installed -- e.g. the CI
# runner that built it, which still has the same packages from the
# `brew install` step earlier in the same job -- defeating the entire
# purpose of bundling for any other machine.
#
# Fix: for every bundled dylib (and the main executable), rewrite its own
# ID (dylibs only) to @rpath/<filename>, and rewrite any of its
# dependency references that point at another *bundled* library (matched
# by filename, regardless of the absolute prefix baked in when it was
# originally built) to @rpath/<filename> too -- so the whole dependency
# graph resolves against the bundle's own lib/ via ncview's existing
# @executable_path/../lib rpath, on a machine with none of this
# installed.

find_program(NCVIEW_INSTALL_NAME_TOOL install_name_tool)
if(NOT NCVIEW_INSTALL_NAME_TOOL)
    message(FATAL_ERROR "install_name_tool is required to fix up the bundled package's dylib references but was not found")
endif()
find_program(NCVIEW_OTOOL otool)
if(NOT NCVIEW_OTOOL)
    message(FATAL_ERROR "otool is required to fix up the bundled package's dylib references but was not found")
endif()

set(_ncview_lib_dir "${CMAKE_INSTALL_PREFIX}/${NCVIEW_BUNDLE_LIBDIR}")
set(_ncview_bin "${CMAKE_INSTALL_PREFIX}/bin/ncview++")

file(GLOB _ncview_bundled_dylibs "${_ncview_lib_dir}/*.dylib")

# Filenames of everything we actually bundled, so only references to
# those (not system/framework libraries, which should stay exactly as
# they are) get rewritten.
set(_ncview_bundled_names)
foreach(_ncview_dylib ${_ncview_bundled_dylibs})
    get_filename_component(_ncview_name "${_ncview_dylib}" NAME)
    list(APPEND _ncview_bundled_names "${_ncview_name}")
endforeach()

function(ncview_fixup_macho _file _is_dylib)
    if(_is_dylib)
        get_filename_component(_name "${_file}" NAME)
        execute_process(COMMAND "${NCVIEW_INSTALL_NAME_TOOL}" -id "@rpath/${_name}" "${_file}")
    endif()

    execute_process(
        COMMAND "${NCVIEW_OTOOL}" -L "${_file}"
        OUTPUT_VARIABLE _otool_output
    )
    string(REPLACE "\n" ";" _otool_lines "${_otool_output}")
    foreach(_line ${_otool_lines})
        string(REGEX MATCH "^[ \t]+([^ \t]+) \\(compat" _matched "${_line}")
        if(_matched)
            set(_dep "${CMAKE_MATCH_1}")
            get_filename_component(_dep_name "${_dep}" NAME)
            list(FIND _ncview_bundled_names "${_dep_name}" _idx)
            if(_idx GREATER -1 AND NOT _dep STREQUAL "@rpath/${_dep_name}")
                execute_process(COMMAND "${NCVIEW_INSTALL_NAME_TOOL}" -change "${_dep}" "@rpath/${_dep_name}" "${_file}")
            endif()
        endif()
    endforeach()
endfunction()

foreach(_ncview_dylib ${_ncview_bundled_dylibs})
    ncview_fixup_macho("${_ncview_dylib}" TRUE)
endforeach()
ncview_fixup_macho("${_ncview_bin}" FALSE)
