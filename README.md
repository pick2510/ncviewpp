# Ncview++

Ncview++ is a from-scratch C++/FLTK/CMake port of [ncview](http://cirrus.ucsd.edu/ncview/),
David W. Pierce's netCDF visual browser. The original is C + X11/Xt/Athena
widgets built with autotools; this port replaces the toolkit with
[FLTK](https://www.fltk.org/) and the build with CMake. FLTK and
[UDUNITS-2](https://www.unidata.ucar.edu/software/udunits/) are vendored as
git submodules and built from source, so there's no separate FLTK/
UDUNITS-2 install step -- see Prerequisites below for what those
submodule builds and the rest of the project actually need on the
build machine (netCDF and expat as libraries; flex, bison, and
texinfo as build tools).

See [`CHANGELOG.md`](CHANGELOG.md) for what's changed release-to-release,
and [`PORTING.md`](PORTING.md) for the porting plan and design rationale.

### Names

The command is `ncview++`, and it installs its colormaps and man page
under `ncview++` names, so it can be installed alongside the original
`ncview`. Where `+` isn't allowed or safe (the GitHub repository,
release archives, the CMake project name) the name is spelled `ncviewpp`.
Settings are still kept in `~/.ncviewrc`, and extra colormaps are still
found via `$NCVIEWBASE`, same as with the original.

## License

GNU General Public License, version 3 -- see [`LICENSE`](LICENSE). Ncview
itself is Copyright (C) 1993 through 2024, David W. Pierce; Ncview++ is
Copyright (C) 2026 Dominik Strebel.

## Installing

Pre-built packages for Linux, macOS, and Windows are on the
[Releases](../../releases) page.

## Building from source

**Prerequisites**: a C++17 compiler, CMake >= 3.21, netCDF (C library +
headers), expat, and — on Linux/BSD — X11 dev headers (`libx11-dev
libxext-dev libxft-dev libxinerama-dev libxcursor-dev libxrender-dev
libxfixes-dev` on Debian/Ubuntu). FLTK and UDUNITS-2 are built from the
vendored submodules, but the vendored UDUNITS-2 build itself needs
**flex**, **bison**, and **texinfo** on the build machine (on Unix it
unconditionally regenerates its lexer/parser from source with flex/
bison, and generates its documentation with texinfo's `makeinfo`/
`install-info`) — `flex bison texinfo` on Debian/Ubuntu.

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build
```

```sh
./build/app/ncview++ some_file.nc
```

To install:

```sh
cmake --install build --prefix /usr/local
```

This bundles every non-system shared library the binary needs (netCDF,
HDF5, X11, ...) alongside it, so the install (or the `.tar.gz`/`.zip` from
`cmake --build build --target package`) is self-contained. Always pass
`--prefix` explicitly — the default resolves to this repo's parent
directory, a quirk of the vendored UDUNITS-2 build.

### Static linking (HPC clusters etc.)

`-DNCVIEW_STATIC_LINK=ON` statically links netCDF/HDF5/zstd/bz2/sz/zlib and
the C++ runtime, and drops FLTK's Wayland backend (unused at runtime, but
otherwise pulls in ~80 transitive shared libraries). Requires static (`.a`)
builds of those libraries to be findable. See `PORTING.md` for what's
deliberately left dynamic and why.

```sh
cmake -S . -B build-static -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNCVIEW_STATIC_LINK=ON
cmake --build build-static -j
ldd build-static/app/ncview++   # confirm netcdf/hdf5 are no longer listed
```

## Releasing

Tag a commit `vX.Y.Z` and push the tag — `.github/workflows/release.yml`
builds all three platforms and publishes the archives to a GitHub Release.
Keep the tag in sync with `CPACK_PACKAGE_VERSION` in the top-level
`CMakeLists.txt`.

```sh
git tag v0.3.0
git push origin v0.3.0
```

To re-publish an existing tag (e.g. after a CI flake), run `release.yml`
manually from the Actions tab with that tag name.

---

Note: after configuring, you may see root-level `README`, `COPYRIGHT`, and
`CHANGE_LOG` files (no `.md` suffix) appear. Those are UDUNITS-2's own
files, generated there at configure time (from its own copies) because its
vendored CMake build hardcodes those paths at the repo root -- not tracked,
not this project's own files.
