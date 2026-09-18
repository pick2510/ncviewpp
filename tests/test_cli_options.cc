// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for core/src/ncview.cc's parse_options() -- Phase
// 8 of the "refine the architecture" plan. parse_options() had zero direct
// test coverage before this file: it writes into the global `options`
// struct (whose fields are reference members bound to storage inside
// g_app.session -- see viewer_session.h's header comment), so tests here
// assert on options' post-call state, not a return value.
//
// Not covered here, deliberately: every error path that calls exit() --
// a missing required argument, an out-of-range -nc/-maxsize value, -w/-c
// (print_no_warranty/print_copying then exit(0)), or an unrecognized flag
// (useage() then exit(-1)). Calling any of these in-process would kill the
// whole test binary. Same category of gap as Phase 5a's determine_file_type
// rejection path -- documented, not worked around.
#include <cstring>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "support/session_fixture.h"

namespace {

// Builds a mutable argv (parse_options's signature is char *argv[], and
// argv[0] is conventionally the program name, though parse_options never
// reads it -- included anyway to match real invocation shape).
class Argv {
public:
	explicit Argv( std::vector<std::string> args ) {
		storage_.push_back( "ncview" );
		for( auto &a : args ) storage_.push_back( std::move( a ) );
		for( auto &s : storage_ ) ptrs_.push_back( s.data() );
	}
	int argc() { return (int)ptrs_.size(); }
	char **argv() { return ptrs_.data(); }
private:
	std::vector<std::string> storage_;
	std::vector<char*> ptrs_;
};

} // namespace

TEST_CASE( "parse_options: -minmax accepts each recognized method" ) {
	ncview_test::SessionFixture fx;

	{
		Argv a( { "-minmax", "fast" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.min_max_method == MinMaxMethod::Fast );
	}
	{
		Argv a( { "-minmax", "med" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.min_max_method == MinMaxMethod::Med );
	}
	{
		Argv a( { "-minmax", "slow" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.min_max_method == MinMaxMethod::Slow );
	}
	{
		Argv a( { "-minmax", "exh" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.min_max_method == MinMaxMethod::Exhaust );
	}
	{
		// Real, current quirk: "all" is accepted (not rejected as
		// unrecognized) and silently mapped to the *same* enumerator as
		// "exh" -- there is no separate MinMaxMethod for it. Pinned, not
		// "fixed", per this plan's coverage-phase discipline.
		Argv a( { "-minmax", "all" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.min_max_method == MinMaxMethod::Exhaust );
	}
}

TEST_CASE( "parse_options: -cal sets the calendar override string" ) {
	ncview_test::SessionFixture fx;
	Argv a( { "-cal", "365_day" } );
	parse_options( a.argc(), a.argv() );
	CHECK( options.calendar == "365_day" );
}

TEST_CASE( "parse_options: boolean flags each set exactly their own field" ) {
	ncview_test::SessionFixture fx;

	{
		Argv a( { "-private" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.private_colormap == 1 );
		CHECK( options.debug == 0 );
	}
	{
		Argv a( { "-debug" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.debug == 1 );
	}
	{
		Argv a( { "-beep" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.beep_on_restart == 1 );
	}
	{
		Argv a( { "-pause_on_restart" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.stop_on_restart == 1 );
	}
	{
		Argv a( { "-frames" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.dump_frames == 1 );
	}
	{
		Argv a( { "-small" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.small == 1 );
	}
	{
		Argv a( { "-extra" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.want_extra_info == 1 );
	}
	{
		Argv a( { "-noauto" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.no_autoflip == 1 );
	}
	{
		Argv a( { "-no1d" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.no_1d_vars == 1 );
	}
	{
		Argv a( { "-show_sel" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.show_sel == 1 );
	}
	{
		Argv a( { "-no_char_dim" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.no_char_dims == 1 );
	}
	{
		Argv a( { "-notconv" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.t_conv == false );
	}
	{
		Argv a( { "-shrink_mode" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.shrink_method == ShrinkMethod::Mode );
	}
	{
		Argv a( { "-no_color_ndims" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.color_by_ndims == false );
	}
	{
		Argv a( { "-no_auto_overlay" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.auto_overlay == false );
	}
	{
		Argv a( { "-autoscale" } );
		parse_options( a.argc(), a.argv() );
		CHECK( options.autoscale == true );
	}
}

TEST_CASE( "parse_options: -repl sets options.blowup, not options.blowup_type" ) {
	// Pre-existing quirk, preserved and explicitly pinned per the code's own
	// comment at ncview.cc's -repl branch: this sets the blowup *magnitude*
	// to 1, not blowup_type to BlowupType::Replicate, even though 1 happens
	// to be Replicate's numeric value. blowup_type never changes.
	ncview_test::SessionFixture fx;
	BlowupType before = options.blowup_type;
	Argv a( { "-repl" } );
	parse_options( a.argc(), a.argv() );
	CHECK( options.blowup == 1 );
	CHECK( options.blowup_type == before );
}

TEST_CASE( "parse_options: -mtitle is a documented no-op that still consumes its argument" ) {
	ncview_test::SessionFixture fx;
	// If -mtitle's argument were NOT consumed, "somefile.nc" would be
	// mis-parsed as -mtitle's value instead of landing in the returned
	// file list.
	Argv a( { "-mtitle", "My Title", "somefile.nc" } );
	Stringlist *files = parse_options( a.argc(), a.argv() );
	REQUIRE( stringlist_len( files ) == 1 );
	CHECK( (*files)[0].string == "somefile.nc" );
	stringlist_delete_entire_list( files );
}

TEST_CASE( "parse_options: -scale and -offset parse floats" ) {
	ncview_test::SessionFixture fx;
	Argv a( { "-scale", "2.5", "-offset", "-1.5" } );
	parse_options( a.argc(), a.argv() );
	CHECK( options.scale == doctest::Approx( 2.5f ) );
	CHECK( options.offset == doctest::Approx( -1.5f ) );
}

TEST_CASE( "parse_options: -listsel_max parses an integer" ) {
	ncview_test::SessionFixture fx;
	Argv a( { "-listsel_max", "99" } );
	parse_options( a.argc(), a.argv() );
	CHECK( options.listsel_max == 99 );
}

TEST_CASE( "parse_options: -missvalrgb parses three integers" ) {
	ncview_test::SessionFixture fx;
	Argv a( { "-missvalrgb", "10", "20", "30" } );
	parse_options( a.argc(), a.argv() );
	CHECK( options.missval_r == 10 );
	CHECK( options.missval_g == 20 );
	CHECK( options.missval_b == 30 );
}

TEST_CASE( "parse_options: -nc within range sets n_colors" ) {
	ncview_test::SessionFixture fx;
	Argv a( { "-nc", "150" } );
	parse_options( a.argc(), a.argv() );
	CHECK( options.n_colors == 150 );
}

TEST_CASE( "parse_options: -maxsize with a single integer sets maxsize_pct" ) {
	ncview_test::SessionFixture fx;
	Argv a( { "-maxsize", "50" } );
	parse_options( a.argc(), a.argv() );
	CHECK( options.maxsize_pct == 50 );
}

TEST_CASE( "parse_options: -maxsize with a comma pair sets width and height" ) {
	ncview_test::SessionFixture fx;
	Argv a( { "-maxsize", "800,600" } );
	parse_options( a.argc(), a.argv() );
	CHECK( options.maxsize_pct == -1 );
	CHECK( options.maxsize_width == 800 );
	CHECK( options.maxsize_height == 600 );
}

TEST_CASE( "parse_options: non-flag arguments accumulate into the returned file list, in order" ) {
	ncview_test::SessionFixture fx;
	Argv a( { "-debug", "a.nc", "b.nc", "-small", "c.nc" } );
	Stringlist *files = parse_options( a.argc(), a.argv() );
	REQUIRE( stringlist_len( files ) == 3 );
	CHECK( (*files)[0].string == "a.nc" );
	CHECK( (*files)[1].string == "b.nc" );
	CHECK( (*files)[2].string == "c.nc" );
	CHECK( options.debug == 1 );
	CHECK( options.small == 1 );
	stringlist_delete_entire_list( files );
}

TEST_CASE( "parse_options: no arguments returns an empty file list and touches no options" ) {
	ncview_test::SessionFixture fx;
	Argv a( {} );
	Stringlist *files = parse_options( a.argc(), a.argv() );
	CHECK( stringlist_len( files ) == 0 );
}
