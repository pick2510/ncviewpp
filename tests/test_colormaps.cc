// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for core/src/ncview.cc's colormap machinery --
// Phase 8 of the "refine the architecture" plan. Both entry points tested
// here (initialize_colormaps(), init_cmap_from_file()) are declared in
// protos.h (external linkage); their helpers (get_cmaps_from_dir(),
// ncview_cmap_suffix(), init_cmap_from_data(), init_cmaps_from_data()) are
// `static` to ncview.cc, so -- matching this plan's established practice
// of testing through public entry points rather than reaching into a
// file's private implementation (do_print.cc's build_print_info(),
// file.cc's netcdf_* internals) -- they're exercised indirectly here via
// initialize_colormaps()'s directory-scanning path, not called directly.
//
// stub_interface.cc's in_create_colormap() override captures each
// (name, r, g, b) triple into g_created_colormaps (see its own comment)
// so tests can assert on actual colormap content, not just "a call
// happened".
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "support/scratch_home.h"
#include "support/session_fixture.h"

namespace {

// initialize_colormaps() unconditionally scans "." (the process's current
// working directory) in addition to $NCVIEWBASE/$HOME, with no way to
// suppress that scan -- so tests that need a deterministic colormap count
// chdir into an empty scratch directory for the duration of the call and
// restore the original cwd on scope exit, the same isolation shape as
// ScratchHome gives $HOME.
struct ScratchCwd {
	std::filesystem::path dir;
	std::filesystem::path old_cwd;

	ScratchCwd() {
		auto tmpl = (std::filesystem::temp_directory_path() / "ncview_cmap_test_XXXXXX").string();
		REQUIRE( mkdtemp( &tmpl[0] ) != nullptr );
		dir = tmpl;
		old_cwd = std::filesystem::current_path();
		std::filesystem::current_path( dir );
	}
	~ScratchCwd() {
		std::error_code ec;
		std::filesystem::current_path( old_cwd, ec );
		std::filesystem::remove_all( dir, ec );
	}

	// Writes dir/name with the given raw content.
	std::string write( const std::string &name, const std::string &content ) {
		auto path = dir / name;
		FILE *f = fopen( path.string().c_str(), "w" );
		REQUIRE( f != nullptr );
		fwrite( content.data(), 1, content.size(), f );
		fclose( f );
		return path.string();
	}
};

// A syntactically valid 256-line ncmap body: r=g=b=i, matching cmap_bw's
// own shape, so an assertion on file-loaded content can reuse the same
// "grayscale ramp" check as the built-in test below.
std::string validGrayscaleNcmapBody() {
	std::string s;
	char line[32];
	for( int i = 0; i < 256; i++ ) {
		snprintf( line, sizeof(line), "%d %d %d\n", i, i, i );
		s += line;
	}
	return s;
}

const CreatedColormap *findCreated( const std::string &name ) {
	for( auto &c : g_created_colormaps )
		if( c.name == name ) return &c;
	return nullptr;
}

} // namespace

TEST_CASE( "initialize_colormaps: loads all 25 built-in colormaps by name" ) {
	ncview_test::SessionFixture fx;
	ncview_test::ScratchHome home;	// isolate $HOME
	ncview_test::unset_env( "NCVIEWBASE" );
	ScratchCwd cwd;	// isolate "." -- empty, contributes nothing

	initialize_colormaps();

	static const char *kBuiltins[] = {
		"3gauss", "detail", "ssec",
		"bright", "banded", "rainbow", "jaisnb", "jaisnc", "jaisnd",
		"blu_red", "manga", "jet", "wheel",
		"viridis", "plasma", "inferno", "magma", "cividis",
		"3saw", "bw", "default", "extrema", "helix", "helix2", "hotres",
	};
	CHECK( g_created_colormaps.size() == 25 );
	for( const char *name : kBuiltins )
		CHECK_MESSAGE( findCreated( name ) != nullptr, "missing built-in: " << name );
}

TEST_CASE( "initialize_colormaps: the built-in 'bw' colormap is an exact grayscale ramp" ) {
	ncview_test::SessionFixture fx;
	ncview_test::ScratchHome home;
	ncview_test::unset_env( "NCVIEWBASE" );
	ScratchCwd cwd;

	initialize_colormaps();

	const CreatedColormap *bw = findCreated( "bw" );
	REQUIRE( bw != nullptr );
	for( int i = 0; i < 256; i++ ) {
		CHECK( bw->r[i] == i );
		CHECK( bw->g[i] == i );
		CHECK( bw->b[i] == i );
	}
}

TEST_CASE( "initialize_colormaps: a .ncmap file in a scanned directory is loaded by its base name" ) {
	ncview_test::SessionFixture fx;
	ncview_test::ScratchHome home;
	ncview_test::unset_env( "NCVIEWBASE" );
	ScratchCwd cwd;
	cwd.write( "mycustom.ncmap", validGrayscaleNcmapBody() );

	initialize_colormaps();

	const CreatedColormap *custom = findCreated( "mycustom" );
	REQUIRE( custom != nullptr );
	CHECK( custom->r[0] == 0 );
	CHECK( custom->r[255] == 255 );
}

TEST_CASE( "initialize_colormaps: a file with a non-colormap extension is skipped" ) {
	ncview_test::SessionFixture fx;
	ncview_test::ScratchHome home;
	ncview_test::unset_env( "NCVIEWBASE" );
	ScratchCwd cwd;
	cwd.write( "notacolormap.txt", validGrayscaleNcmapBody() );

	initialize_colormaps();

	CHECK( findCreated( "notacolormap" ) == nullptr );
	// Confirms the extension filter actually ran, not just "nothing extra
	// happened to be found": count is exactly the 25 built-ins.
	CHECK( g_created_colormaps.size() == 25 );
}

TEST_CASE( "initialize_colormaps: the short .ncm extension is also recognized" ) {
	ncview_test::SessionFixture fx;
	ncview_test::ScratchHome home;
	ncview_test::unset_env( "NCVIEWBASE" );
	ScratchCwd cwd;
	cwd.write( "shortext.ncm", validGrayscaleNcmapBody() );

	initialize_colormaps();

	CHECK( findCreated( "shortext" ) != nullptr );
}

TEST_CASE( "initialize_colormaps: $NCVIEWBASE is scanned when set" ) {
	ncview_test::SessionFixture fx;
	ncview_test::ScratchHome home;	// isolate $HOME so it doesn't ALSO get scanned with stray content
	ScratchCwd cwd;	// isolate "." too

	auto tmpl = (std::filesystem::temp_directory_path() / "ncview_base_test_XXXXXX").string();
	REQUIRE( mkdtemp( &tmpl[0] ) != nullptr );
	std::filesystem::path base_dir = tmpl;
	{
		FILE *f = fopen( (base_dir / "frombase.ncmap").string().c_str(), "w" );
		REQUIRE( f != nullptr );
		std::string body = validGrayscaleNcmapBody();
		fwrite( body.data(), 1, body.size(), f );
		fclose( f );
	}
	ncview_test::set_env( "NCVIEWBASE", base_dir.string() );

	initialize_colormaps();

	CHECK( findCreated( "frombase" ) != nullptr );

	ncview_test::unset_env( "NCVIEWBASE" );
	std::error_code ec;
	std::filesystem::remove_all( base_dir, ec );
}

TEST_CASE( "init_cmap_from_file: a malformed file (too few lines) is skipped, not crashed on" ) {
	ncview_test::SessionFixture fx;
	ScratchCwd cwd;
	cwd.write( "toofew.ncmap", "1 2 3\n4 5 6\n" );	// only 2 of the required 256 lines

	init_cmap_from_file( cwd.dir.string().c_str(), "toofew.ncmap", 6 );

	CHECK( findCreated( "toofew" ) == nullptr );
}

TEST_CASE( "init_cmap_from_file: a line with the wrong number of entries is skipped" ) {
	ncview_test::SessionFixture fx;
	ScratchCwd cwd;
	std::string body = "1 2\n" + validGrayscaleNcmapBody();	// first line has 2 entries, not 3
	cwd.write( "badcount.ncmap", body );

	init_cmap_from_file( cwd.dir.string().c_str(), "badcount.ncmap", 6 );

	CHECK( findCreated( "badcount" ) == nullptr );
}

TEST_CASE( "init_cmap_from_file: an out-of-range component value is skipped" ) {
	ncview_test::SessionFixture fx;
	ScratchCwd cwd;
	std::string body = "999 0 0\n" + validGrayscaleNcmapBody().substr(
		validGrayscaleNcmapBody().find('\n') + 1 );
	cwd.write( "outofrange.ncmap", body );

	init_cmap_from_file( cwd.dir.string().c_str(), "outofrange.ncmap", 6 );

	CHECK( findCreated( "outofrange" ) == nullptr );
}

TEST_CASE( "init_cmap_from_file: a name already reported seen by the UI is not reloaded" ) {
	ncview_test::SessionFixture fx;
	ScratchCwd cwd;
	cwd.write( "dupname.ncmap", validGrayscaleNcmapBody() );
	g_seen_colormap_names.push_back( "dupname" );

	init_cmap_from_file( cwd.dir.string().c_str(), "dupname.ncmap", 6 );

	CHECK( findCreated( "dupname" ) == nullptr );
}

TEST_CASE( "check: clamps to [min,max] via its return value, not by modifying the input" ) {
	CHECK( check( 128, 0, 255 ) >= 0 );
	CHECK( check( -1, 0, 255 ) < 0 );
	CHECK( check( 256, 0, 255 ) < 0 );
	CHECK( check( 0, 0, 255 ) >= 0 );
	CHECK( check( 255, 0, 255 ) >= 0 );
}
