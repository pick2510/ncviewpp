cmake_minimum_required(VERSION 3.21)  # install(RUNTIME_DEPENDENCY_SET ...) below needs 3.21+
project(ncviewpp VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Linuxbrew isolation (Linux only -- see below for why macOS is excluded).
#
# A brew install puts a full parallel stack of libraries, headers and
# pkg-config files on the machine and puts its own bin/ ahead of the system
# one on PATH. From there:
#   - CMake's find_package()/find_library()/find_path() derive extra search
#     prefixes from PATH (stripping a trailing /bin or /sbin off each entry)
#     and from CMAKE_SYSTEM_PREFIX_PATH;
#   - pkg-config honors PKG_CONFIG_PATH;
#   - gcc/clang honor CPATH/C_INCLUDE_PATH/CPLUS_INCLUDE_PATH/LIBRARY_PATH
#     directly, independent of any CMake variable.
# Any of these can resolve one dependency (e.g. pangocairo) against brew's
# copy and another (e.g. pangoxft, which brew's pango doesn't ship, or a
# bare -lfoo the linker resolves via its own search order) against the
# system's -- silently mixing two incompatible library stacks and producing
# undefined-reference link errors or subtly wrong runtime behavior. Detect
# the brew prefix once, up front, and exclude it from every one of those
# lookup mechanisms, for this project and for the third_party/fltk and
# third_party/udunits2 subprojects it configures below.
#
# Linux only: on Linux, the system already has its own package manager and
# its own copy of these libraries, so a Linuxbrew install is a genuinely
# foreign, optional add-on worth isolating against. On macOS there is no
# separate "system stack" to prefer instead -- Homebrew IS how this project's
# dependencies (netCDF in particular, see the `brew install` step in CI) are
# installed, so excluding it would break discovery, not protect it: this
# used to run unconditionally and did fire on macOS CI, but only "worked" by
# accident because pkg-config's own Homebrew build carries /opt/homebrew in
# its compiled-in default search path regardless of PKG_CONFIG_PATH --
# find_package(netCDF)'s CMake-native search was silently excluded and
# quietly fell back to that pkg-config path every time. Restricted to Linux
# to stop fighting the platform it's meant to help.
if(UNIX AND NOT APPLE)
find_program(_ncview_brew_executable brew)
if(_ncview_brew_executable)
    execute_process(
        COMMAND "${_ncview_brew_executable}" --prefix
        OUTPUT_VARIABLE _ncview_brew_prefix
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
if(NOT _ncview_brew_prefix)
    # No `brew` on PATH to ask (e.g. a PATH that carries brew's lib/include
    # via some other inherited *_PATH variable but not its bin/) -- fall
    # back to the well-known install location.
    if(IS_DIRECTORY /home/linuxbrew/.linuxbrew)
        set(_ncview_brew_prefix /home/linuxbrew/.linuxbrew)
    endif()
endif()
unset(_ncview_brew_executable CACHE)

if(_ncview_brew_prefix)
    get_filename_component(_ncview_brew_prefix "${_ncview_brew_prefix}" ABSOLUTE)
    message(STATUS "Detected Linuxbrew at ${_ncview_brew_prefix} -- excluding it from library/header search")

    # Covers CMake's own prefix-based find_*() search. Needs CMake 3.23; a
    # no-op on this project's older-supported 3.21/3.22, where the
    # environment-variable scrubbing below is what carries the fix.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23)
        list(APPEND CMAKE_IGNORE_PREFIX_PATH "${_ncview_brew_prefix}")
    endif()

    # Covers what find_*() can't: pkg-config and the compiler driver's own
    # search, both consulted straight from the environment. Strip only the
    # brew-prefixed entries out of each -- anything else already on these
    # (e.g. a project-specific vcpkg toolchain dir) is left alone.
    function(_ncview_scrub_env_path_var _varname _prefix)
        if(NOT DEFINED ENV{${_varname}})
            return()
        endif()
        string(REPLACE ":" ";" _entries "$ENV{${_varname}}")
        string(LENGTH "${_prefix}" _prefix_len)
        set(_kept "")
        foreach(_entry IN LISTS _entries)
            set(_under_prefix FALSE)
            string(LENGTH "${_entry}" _entry_len)
            if(_entry_len GREATER_EQUAL _prefix_len)
                string(SUBSTRING "${_entry}" 0 ${_prefix_len} _head)
                if(_head STREQUAL _prefix)
                    if(_entry_len EQUAL _prefix_len)
                        set(_under_prefix TRUE)
                    else()
                        string(SUBSTRING "${_entry}" ${_prefix_len} 1 _next_char)
                        if(_next_char STREQUAL "/")
                            set(_under_prefix TRUE)
                        endif()
                    endif()
                endif()
            endif()
            if(NOT _under_prefix)
                list(APPEND _kept "${_entry}")
            endif()
        endforeach()
        list(JOIN _kept ":" _joined)
        set(ENV{${_varname}} "${_joined}")
    endfunction()

    foreach(_ncview_env_var PKG_CONFIG_PATH CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH)
        _ncview_scrub_env_path_var("${_ncview_env_var}" "${_ncview_brew_prefix}")
    endforeach()
    unset(_ncview_env_var)

    # Belt-and-suspenders for pkg-config specifically: make sure the
    # system's own pkgconfig directories are on PKG_CONFIG_PATH even if
    # scrubbing left it empty (pkg-config's compiled-in default search path
    # normally covers this already, but not on every distro/packaging).
    foreach(_ncview_sys_pkgconfig_dir
            /usr/lib64/pkgconfig
            /usr/lib/x86_64-linux-gnu/pkgconfig
            /usr/lib/pkgconfig
            /usr/share/pkgconfig)
        if(IS_DIRECTORY "${_ncview_sys_pkgconfig_dir}")
            list(APPEND _ncview_sys_pkgconfig_dirs "${_ncview_sys_pkgconfig_dir}")
        endif()
    endforeach()
    if(_ncview_sys_pkgconfig_dirs)
        list(JOIN _ncview_sys_pkgconfig_dirs ":" _ncview_sys_pkgconfig_path)
        set(ENV{PKG_CONFIG_PATH} "${_ncview_sys_pkgconfig_path}:$ENV{PKG_CONFIG_PATH}")
    endif()
    unset(_ncview_sys_pkgconfig_dir)
    unset(_ncview_sys_pkgconfig_dirs)
    unset(_ncview_sys_pkgconfig_path)
endif()
unset(_ncview_brew_prefix)
endif()

# core/modernization.md Phase 0a: surface every warning the compiler already
# has an opinion about, without failing the build on them yet (see
# core/WARNINGS.md for the baseline this produced and the plan for clearing
# it module by module). Only applied to our own targets -- third_party/fltk
# and third_party/udunits2 are vendored and not ours to clean up.
add_library(ncview_warning_flags INTERFACE)
if(MSVC)
    target_compile_options(ncview_warning_flags INTERFACE /W4)
else()
    target_compile_options(ncview_warning_flags INTERFACE -Wall -Wextra -Wno-unused-parameter)
endif()

# tests/ whole-archive-links ncview_core (and transitively libudunits2) into
# an executable, ncview_core_linkcheck, to force full symbol resolution --
# deliberately an executable rather than a shared library (see
# tests/CMakeLists.txt for why); that still requires every static lib
# involved to be PIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(NCVIEW_BUILD_TESTS "Build ncview_core headless tests" ON)

# docs/modernization.md Phase 0c: an end-to-end screenshot regression harness
# (tests/ui_smoke.sh) that drives the real ncview binary under Xvfb via the
# NCVIEW_TEST_AUTOSELECT/NCVIEW_TEST_DIALOG/NCVIEW_TEST_BUTTON hooks already
# in ui/src/interface_fltk.cc and compares screenshots pixel-for-pixel
# against tests/ui_smoke/golden/*.png. Off by default: it needs Xvfb +
# ImageMagick + ncgen on PATH (Linux/X11 only -- this project always forces
# FLTK_BACKEND=x11, see README), none of which every dev machine or CI
# platform has.
option(NCVIEW_BUILD_UI_TESTS "Build the Xvfb screenshot regression test (needs Xvfb, ImageMagick, ncgen)" OFF)

# docs/modernization.md Phase 0d: build with -fsanitize=<value>, e.g.
# "address,undefined", applied to every target of ours (not the vendored
# third_party/fltk or third_party/udunits2). docs/PORTING.md's own verification
# step 5 calls for exactly this pass over the headless tests; empty (the
# default) means sanitizers are off entirely, matching every build before
# this option existed.
set(NCVIEW_SANITIZE "" CACHE STRING "Sanitizer(s) to build with, e.g. address,undefined (empty disables)")
add_library(ncview_sanitize_flags INTERFACE)
if(NCVIEW_SANITIZE)
    target_compile_options(ncview_sanitize_flags INTERFACE -fsanitize=${NCVIEW_SANITIZE} -fno-omit-frame-pointer)
    target_link_options(ncview_sanitize_flags INTERFACE -fsanitize=${NCVIEW_SANITIZE})
endif()

# docs/modernization.md Phase 0c/10: `--coverage` (GCC/gcov's combined
# -fprofile-arcs -ftest-coverage compile flag plus the matching link flag
# that pulls in libgcov) applied to our own targets only, same
# opt-in-INTERFACE-library pattern as NCVIEW_SANITIZE just above. GCC/gcov
# rather than Clang/llvm-cov because every CI job that could plausibly run
# this (linux, sanitize) already builds with GCC (ubuntu-latest's default
# `cc`/`c++`) -- see scripts/coverage.sh, which drives a full
# configure-build-test-report cycle and is what both a developer and CI
# actually invoke.
option(NCVIEW_COVERAGE "Build with GCC/gcov coverage instrumentation (see scripts/coverage.sh)" OFF)
add_library(ncview_coverage_flags INTERFACE)
if(NCVIEW_COVERAGE)
    target_compile_options(ncview_coverage_flags INTERFACE --coverage -O0)
    target_link_options(ncview_coverage_flags INTERFACE --coverage)
    # gcov's counters are normally flushed to .gcda files by an atexit()
    # handler libgcov registers -- but tests/fast_exit.h deliberately calls
    # std::_Exit()/TerminateProcess() to skip exit-time teardown (a Windows
    # CI hang, see that file's own comment), which skips atexit() too and
    # silently produces zero .gcda files / 0% coverage with no error at all.
    # This define tells fast_exit.h to call __gcov_dump() itself first,
    # only when this option is actually on.
    target_compile_definitions(ncview_coverage_flags INTERFACE NCVIEW_COVERAGE_BUILD)
endif()

# HPC clusters routinely lack (or have mismatched-version) shared libraries
# for anything outside their own module system -- this pulls netCDF/HDF5 and
# the C++ runtime in statically so the binary doesn't touch either at
# runtime, and disables FLTK's Wayland backend, which this project never
# uses at runtime anyway (main.cc/README always run with FLTK_BACKEND=x11)
# but which otherwise drags in ~80 transitive shared libraries (GTK, Mesa,
# EGL, D-Bus, at-spi, LLVM...) purely to sit unused. Deliberately NOT static:
# curl/xml2 (netCDF's optional, unused-for-local-files OPeNDAP/remote-access
# dependency, pulling in OpenSSL/Kerberos/LDAP -- both unnecessary here and
# inadvisable to freeze via static linking) and the core X11/Xft stack
# (stable, always-present OS-level ABI on any cluster reachable via
# `ssh -X`). Xinerama and Xcursor are the exception within that stack: unlike
# libX11/libXext/libXft etc., testing on a minimal RHEL8-family image (a
# stand-in for a real HPC login/compute node) showed these two -- multi-
# monitor geometry queries and themed-cursor loading, both FLTK features
# ncview doesn't rely on for correctness -- are NOT reliably preinstalled.
# Rather than fight static/dynamic link ordering for two optional features,
# just turn them off for this build variant (matching the Wayland precedent
# right below).
option(NCVIEW_STATIC_LINK
    "Statically link netCDF/HDF5 and the C++ runtime, and disable FLTK's unused Wayland backend, for portability to systems (e.g. HPC clusters) without matching shared libraries"
    OFF)

if(NCVIEW_STATIC_LINK)
    set(FLTK_BACKEND_WAYLAND OFF CACHE BOOL "" FORCE)
    set(FLTK_USE_XINERAMA OFF CACHE BOOL "" FORCE)
    set(FLTK_USE_XCURSOR OFF CACHE BOOL "" FORCE)
endif()

# --------------------------------------------------------------------------
# netCDF (system dependency)
# --------------------------------------------------------------------------
find_package(netCDF QUIET)
if(NOT netCDF_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NETCDF REQUIRED IMPORTED_TARGET netcdf)
    add_library(netCDF::netcdf ALIAS PkgConfig::NETCDF)
endif()

if(NCVIEW_STATIC_LINK)
    # netCDF's own numeric/storage chain -- swap netCDF::netcdf's dynamic
    # .so for its .a, plus the .a of everything it in turn needs (found the
    # same way autotools/CMake would: by name, via the normal library search
    # path), so the ncview_core -> netCDF::netcdf PUBLIC link (core/CMakeLists.txt)
    # picks it up transitively with no changes needed there.
    # netCDF_DIR (set by find_package(netCDF)'s own CMake config, e.g.
    # <prefix>/lib/cmake/netCDF) tells us which prefix's lib/ directory to
    # also search -- covers non-system installs (Homebrew/Linuxbrew, a
    # module-system path, spack, ...) without hardcoding one.
    set(_ncview_static_lib_hints)
    if(netCDF_DIR)
        get_filename_component(_ncview_static_lib_hints "${netCDF_DIR}/../.." ABSOLUTE)
    endif()

    set(_ncview_static_netcdf_chain netcdf hdf5_hl hdf5 sz zstd bz2 z)
    set(_ncview_static_netcdf_libs "")
    foreach(_lib ${_ncview_static_netcdf_chain})
        find_library(_ncview_static_${_lib} NAMES lib${_lib}.a HINTS ${_ncview_static_lib_hints})
        if(NOT _ncview_static_${_lib})
            message(FATAL_ERROR
                "NCVIEW_STATIC_LINK is ON but lib${_lib}.a was not found. "
                "Install a static build of ${_lib} (e.g. via your package "
                "manager's -static/-devel package) or turn NCVIEW_STATIC_LINK off.")
        endif()
        list(APPEND _ncview_static_netcdf_libs "${_ncview_static_${_lib}}")
    endforeach()
    # libaec provides the szip *algorithm* libsz.a itself just re-exports
    # (e.g. Debian/Ubuntu's libsz2/libaec split); not every static-szip
    # build is split this way, so unlike the chain above this is opportunistic
    # -- add it if present, don't require it.
    find_library(_ncview_static_aec NAMES libaec.a HINTS ${_ncview_static_lib_hints})
    if(_ncview_static_aec)
        list(APPEND _ncview_static_netcdf_libs "${_ncview_static_aec}")
    endif()
    list(POP_FRONT _ncview_static_netcdf_libs _ncview_static_netcdf_a)
    # netCDF's own exported target declares per-configuration locations
    # (IMPORTED_LOCATION_RELEASE, etc. -- see IMPORTED_CONFIGURATIONS) that
    # CMake consults *before* the plain IMPORTED_LOCATION, so overriding
    # only the latter silently has no effect on the actual link line;
    # every configuration CMake might map our build type to needs the
    # override too.
    set_target_properties(netCDF::netcdf PROPERTIES
        IMPORTED_LOCATION "${_ncview_static_netcdf_a}"
        IMPORTED_LOCATION_RELEASE "${_ncview_static_netcdf_a}"
        IMPORTED_LOCATION_DEBUG "${_ncview_static_netcdf_a}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${_ncview_static_netcdf_a}"
        IMPORTED_LOCATION_MINSIZEREL "${_ncview_static_netcdf_a}"
        IMPORTED_LOCATION_NOCONFIG "${_ncview_static_netcdf_a}"
        # Static libhdf5.a references pthread symbols (H5.o's use of
        # pthread_once) that its dynamic .so counterpart resolves silently
        # via its own DT_NEEDED entry; a static archive carries no such
        # linkage information, so the final link needs -lpthread spelled
        # out explicitly or it fails with "DSO missing from command line".
        INTERFACE_LINK_LIBRARIES "${_ncview_static_netcdf_libs};curl;xml2;m;pthread"
    )
endif()

# M0's plan called for an NCVIEW_ENABLE_PNG option guarding a system libpng
# dependency for the "-frames" PNG dump. M5 implemented that dump with
# FLTK's own fl_write_png instead (ui/src/interface_fltk.cc) -- FLTK already
# bundles libpng for its own image support, so PNG output is unconditional
# and there is no separate libpng dependency to gate.

# --------------------------------------------------------------------------
# Vendored third-party libraries (submodules)
# --------------------------------------------------------------------------
set(FLTK_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLUID OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLTK_OPTIONS OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FORMS OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/fltk)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# UDUNITS-2's own CMakeLists.txt (lib/CMakeLists.txt) only regenerates
# parser.c/scanner.c from their bison/flex sources "IF(UNIX)" -- on Windows
# (MinGW) that's false regardless of whether flex/bison are installed, so
# its add_library() a few lines later fails outright with "Cannot find
# source file: parser.c" (neither generated file is committed to the
# submodule, only their .y/.l sources are). Generate both ourselves,
# unconditionally, so they simply already exist by the time UDUNITS-2's own
# CMakeLists.txt looks for them -- fixes Windows without touching the
# vendored submodule, and is a no-op on Unix where its own custom command
# would just regenerate the same files anyway.
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)
set(_udunits2_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/udunits2/lib")
if(NOT EXISTS "${_udunits2_lib_dir}/scanner.c")
    execute_process(
        COMMAND "${FLEX_EXECUTABLE}" -d -Put -o scanner.c scanner.l
        WORKING_DIRECTORY "${_udunits2_lib_dir}"
        RESULT_VARIABLE _flex_result
    )
    if(NOT _flex_result EQUAL 0)
        message(FATAL_ERROR "Failed to generate UDUNITS-2's scanner.c via flex")
    endif()
endif()
if(NOT EXISTS "${_udunits2_lib_dir}/parser.c")
    execute_process(
        COMMAND "${BISON_EXECUTABLE}" -t -p ut -o parser.c parser.y
        WORKING_DIRECTORY "${_udunits2_lib_dir}"
        RESULT_VARIABLE _bison_result
    )
    if(NOT _bison_result EQUAL 0)
        message(FATAL_ERROR "Failed to generate UDUNITS-2's parser.c via bison")
    endif()
endif()

if(WIN32)
    # UDUNITS-2's texi_doc() (called three times: its own CMakeLists.txt,
    # lib/, and prog/) unconditionally depends on generated .info/.html
    # files once a makeinfo is found (the same "ALL target depends on a
    # file nothing generates" shape as the flex/bison fix above). On
    # Windows, MSYS2's `texinfo` package's makeinfo is a Perl script under
    # its POSIX layer (/usr/bin), not a native .exe -- ninja (itself a
    # native process, invoked outside the MSYS2 shell) can't run it at all
    # ("is not recognized as an internal or external command").
    #
    # ncview has no use for UDUNITS-2's own documentation, so rather than
    # chase down a native makeinfo: the CI workflow simply doesn't install
    # the `texinfo` MSYS2 package on Windows, so find_program(MAKEINFO
    # makeinfo) genuinely finds nothing and texi_doc() takes its own "not
    # found" branch, never defining a command to (re)generate these files
    # at all. (Pre-setting the MAKEINFO cache variable to a NOTFOUND-
    # suffixed value here does *not* work as a substitute for that -- CMake
    # specifically treats an existing "<var>-NOTFOUND" cache value as "not
    # found *yet*, keep searching" and overwrites it with a fresh, real
    # result if one exists on PATH, confirmed locally.) What's still needed
    # regardless of *how* it ends up not found: pre-create empty
    # placeholders at the exact paths its add_custom_target(... ALL
    # DEPENDS ...) unconditionally references, so each is a plain file
    # with no producer rule -- permanently satisfied, not a missing target.
    foreach(_doc_output
        third_party/udunits2/udunits2.info
        third_party/udunits2/udunits2.html
        third_party/udunits2/lib/udunits2lib.info
        third_party/udunits2/lib/udunits2lib.html
        third_party/udunits2/prog/udunits2prog.info
        third_party/udunits2/prog/udunits2prog.html
    )
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_doc_output}")
            file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/${_doc_output}" "")
        endif()
    endforeach()
endif()

# Upstream's CMakeLists.txt assumes it is the top-level project: its CPack
# block and its `install(FILES "${CMAKE_SOURCE_DIR}/COPYRIGHT" ...)` resolve
# CMAKE_SOURCE_DIR to *our* repo root when add_subdirectory'd, so those
# paths need a README/COPYRIGHT/CHANGE_LOG to exist there. Rather than
# committing copies of udunits2's own files at our root (where they read as
# this project's own and clutter the repo listing), generate them here at
# configure time from the submodule's real copies -- .gitignore'd, not
# tracked. Our actual project readme is README.md.
foreach(_udunits2_legal_file README COPYRIGHT CHANGE_LOG)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/udunits2/${_udunits2_legal_file}"
        "${CMAKE_CURRENT_SOURCE_DIR}/${_udunits2_legal_file}"
        COPYONLY
    )
endforeach()

add_subdirectory(third_party/udunits2)

# UDUNITS-2's own CMakeLists.txt only escapes backslashes in the path it
# bakes into config.h's DEFAULT_UDUNITS2_XML_PATH "IF(MSVC)" -- on Windows/
# MinGW (CMAKE_CXX_COMPILER_ID GNU, not MSVC, which is how this project
# builds on Windows -- see the flex/bison note above and the CI workflow)
# that guard is false, leaving raw Windows backslashes sitting in a C
# string literal, which the compiler then tries to parse as escape
# sequences and fails outright ("incomplete universal character name
# \u..."). Windows' own path APIs (and ut_read_xml()) accept forward
# slashes identically to backslashes, so normalize the generated header
# ourselves after the fact rather than patch the vendored submodule.
if(WIN32)
    set(_udunits2_config_h "${CMAKE_BINARY_DIR}/third_party/udunits2/config.h")
    if(EXISTS "${_udunits2_config_h}")
        file(READ "${_udunits2_config_h}" _udunits2_config_h_content)
        string(REPLACE "\\" "/" _udunits2_config_h_content "${_udunits2_config_h_content}")
        file(WRITE "${_udunits2_config_h}" "${_udunits2_config_h_content}")
    endif()
endif()

# --------------------------------------------------------------------------
# ncview
# --------------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(ui)
add_subdirectory(app)

if(NCVIEW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --------------------------------------------------------------------------
# Install rules
# --------------------------------------------------------------------------
install(TARGETS ncview
    RUNTIME_DEPENDENCY_SET ncview_runtime_deps
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY data/colormaps/ DESTINATION ${CMAKE_INSTALL_DATADIR}/ncview++/colormaps)
install(FILES "data/ncview++.1" DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

# Bundle every non-system shared library ncview actually needs at runtime
# (netCDF, HDF5, X11, curl, ...) next to the binary, so a packaged archive
# is self-contained and doesn't require any of that separately installed on
# the machine that unpacks it. Deliberately excludes the OS's own core
# runtime (libc, the dynamic loader, kernel/GPU-driver-tied libraries on
# Linux, Windows' universal-CRT split DLLs) -- bundling those would be
# actively harmful (ABI/driver mismatches against the host), not helpful.
if(WIN32)
    # DLLs are searched for next to the .exe -- no rpath mechanism to set up.
    set(_ncview_bundle_dir ${CMAKE_INSTALL_BINDIR})
    set(_ncview_bundle_pre_exclude "api-ms-.*" "ext-ms-.*")
    set(_ncview_bundle_post_exclude ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss]y[Ss]tem32.*")
    # install(RUNTIME_DEPENDENCY_SET)'s resolver finds *names* of needed
    # DLLs (libstdc++-6.dll, libnetcdf.dll, ...) via objdump fine on its
    # own, but doesn't know where any of them actually live on disk unless
    # told -- MinGW/MSYS2 keeps every package's DLLs (the compiler's own
    # runtime included) in one shared bin/ next to the compiler itself, so
    # deriving that directory from CMAKE_CXX_COMPILER covers all of them
    # without hardcoding a path.
    get_filename_component(_ncview_mingw_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_ncview_bundle_search_dirs "${_ncview_mingw_bin_dir}")
elseif(APPLE)
    set(_ncview_bundle_dir ${CMAKE_INSTALL_LIBDIR})
    set(_ncview_bundle_pre_exclude)
    set(_ncview_bundle_post_exclude "^/usr/lib/.*" "^/System/Library/.*")
    set(_ncview_bundle_search_dirs)
else()
    set(_ncview_bundle_dir ${CMAKE_INSTALL_LIBDIR})
    set(_ncview_bundle_pre_exclude)
    set(_ncview_bundle_post_exclude
        "^linux-vdso.*"
        "^/lib/ld-linux.*" "^/lib64/ld-linux.*" "^/lib/x86_64-linux-gnu/ld-.*"
        ".*/libc\\.so.*" ".*/libm\\.so.*" ".*/libdl\\.so.*"
        ".*/libpthread\\.so.*" ".*/librt\\.so.*" ".*/libresolv\\.so.*"
        ".*/libGL\\.so.*" ".*/libEGL\\.so.*" ".*/libgbm\\.so.*" ".*/libdrm.*"
        ".*/dri/.*"
    )
    set(_ncview_bundle_search_dirs)
endif()

install(RUNTIME_DEPENDENCY_SET ncview_runtime_deps
    DESTINATION ${_ncview_bundle_dir}
    PRE_EXCLUDE_REGEXES ${_ncview_bundle_pre_exclude}
    POST_EXCLUDE_REGEXES ${_ncview_bundle_post_exclude}
    DIRECTORIES ${_ncview_bundle_search_dirs}
)

if(UNIX AND NOT APPLE)
    set_target_properties(ncview PROPERTIES
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
        BUILD_WITH_INSTALL_RPATH FALSE
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
elseif(APPLE)
    set_target_properties(ncview PROPERTIES
        INSTALL_RPATH "@executable_path/../${CMAKE_INSTALL_LIBDIR}"
        BUILD_WITH_INSTALL_RPATH FALSE
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
endif()

# The two rpath fixes above only make ncview's own *direct* dependencies
# resolve against the bundle. Neither ELF RUNPATH (Linux) nor an
# @executable_path LC_RPATH (macOS) is transitive/consulted for a
# dependency referenced by its original absolute/non-@rpath path, so
# without this, a bundled library's *own* dependencies (e.g. libnetcdf.so
# needing libhdf5.so, in turn needing libcurl.so) are never found in the
# bundle at all -- confirmed with a real built package on both platforms
# (ldd reporting bundled-but-still-"not found" libraries on Linux; ncview
# itself referencing netcdf by its literal build-time Homebrew path on
# macOS). See the two scripts themselves for the full explanation.
if(UNIX AND NOT APPLE)
    install(CODE "set(NCVIEW_BUNDLE_LIBDIR \"${CMAKE_INSTALL_LIBDIR}\")")
    install(SCRIPT "${CMAKE_SOURCE_DIR}/cmake/fixup_linux_bundle.cmake")
elseif(APPLE)
    install(CODE "set(NCVIEW_BUNDLE_LIBDIR \"${CMAKE_INSTALL_LIBDIR}\")")
    install(SCRIPT "${CMAKE_SOURCE_DIR}/cmake/fixup_macos_bundle.cmake")
endif()

# --------------------------------------------------------------------------
# CPack (source/binary tarball)
# --------------------------------------------------------------------------
# "ncviewpp", not "ncview++": the name ends up in release-asset URLs, and
# GitHub rewrites special characters in asset file names.
set(CPACK_PACKAGE_NAME "ncviewpp")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A visual netCDF file browser (C++/FLTK port)")
# ZIP on Windows (no tar/gzip on a bare runner, and it's the platform-native
# archive format there); TGZ everywhere else.
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_SOURCE_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_SOURCE_GENERATOR "TGZ")
endif()
set(CPACK_SOURCE_IGNORE_FILES "/build/;/build-.*/;/\\\\.git/")
include(CPack)
