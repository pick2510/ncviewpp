// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for the ".ncviewrc" state file round-trip
// (core/src/handle_rc_file.cc + the Stringlist file format in
// core/src/stringlist.cc). See modernization.md Phase 0b: Phase 3 replaces
// Stringlist's intrusive linked list with a std::vector-backed container,
// and the one thing that absolutely must not change as a result is the
// on-disk format real users' existing ~/.ncviewrc files are written in --
// PORTING.md's M6 notes describe a real such file, found during that work,
// containing a run of "CMAP_<name> INT 1" lines recording per-colormap
// enabled state.
//
// write_state_to_file()/read_state_from_file() hardcode "$HOME/.ncviewrc"
// -- there is no way to point them at an arbitrary path -- so every test
// here redirects $HOME to a scratch directory for its duration and restores
// it afterward. Never runs against the real developer's ~/.ncviewrc.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "support/scratch_home.h"

using ncview_test::ScratchHome;

namespace {

// handle_rc_file.cc writes via fopen(..., "a") in text mode, so on Windows
// each '\n' comes out as "\r\n"; normalize that away so the hardcoded
// `expected` strings below (and the round-trip comparisons) don't have to
// care which platform wrote the file.
std::string read_file(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string contents = ss.str();
    contents.erase(std::remove(contents.begin(), contents.end(), '\r'), contents.end());
    return contents;
}

// Builds the kind of state list colormap_funcs.c's colormap_options_to_
// stringlist() would have written upstream, and this port's
// get_persistent_X_state() (currently a stub returning nullptr, per
// PORTING.md's M6 notes on the colormap-enable scope gap) will need to
// produce again whenever that feature lands. Three entries is enough to
// exercise ordering and both boolean-ish INT values (0 and 1) real files
// use for "disabled"/"enabled".
Stringlist *make_cmap_state() {
    Stringlist *state = nullptr;
    REQUIRE(stringlist_add_string(&state, "CMAP_3gauss", 1) == 0);
    REQUIRE(stringlist_add_string(&state, "CMAP_bright", 0) == 0);
    REQUIRE(stringlist_add_string(&state, "CMAP_rainbow", 1) == 0);
    return state;
}

} // namespace

TEST_CASE("rc file: write_state_to_file produces the exact on-disk format") {
    ScratchHome home;
    Stringlist *state = make_cmap_state();

    REQUIRE(write_state_to_file(state) == 0);

    // Hand-derived, not captured: stringlist_write_single_element_to_file()
    // writes `<index> "<escaped-name>" <TYPE> <value>\n`, none of our
    // names/values need escaping. There are two nested headers: the
    // stringlist file format's own STRINGLIST_SAVE_FILE_VERSION (added by
    // stringlist_write_to_file() itself, via a direct stringlist_new_sl()
    // call that never updates its index, so it keeps -1), and handle_rc_
    // file.cc's own NCVIEW_STATE_FILE_VERSION (added by write_state_to_
    // file() through the public API, so it gets index 0 as the first real
    // entry). Our 3 entries are then stringlist_cat()'d on afterward, which
    // re-numbers them via stringlist_add_string() starting from index 1
    // (position 1, right after NCVIEW_STATE_FILE_VERSION) -- so 1, 2, 3,
    // not the 0, 1, 2 they had in the list we built above.
    std::string expected =
        "-1 \"STRINGLIST_SAVE_FILE_VERSION\" INT 1\n"
        "0 \"NCVIEW_STATE_FILE_VERSION\" INT 1\n"
        "1 \"CMAP_3gauss\" INT 1\n"
        "2 \"CMAP_bright\" INT 0\n"
        "3 \"CMAP_rainbow\" INT 1\n";
    CHECK(read_file(home.rc_path()) == expected);

    stringlist_delete_entire_list(state);
}

TEST_CASE("rc file: read_state_from_file round-trips names, types, and values") {
    ScratchHome home;
    Stringlist *state = make_cmap_state();
    REQUIRE(write_state_to_file(state) == 0);
    stringlist_delete_entire_list(state);

    Stringlist *read_back = nullptr;
    REQUIRE(read_state_from_file(&read_back) == 0);

    // The STRINGLIST_SAVE_FILE_VERSION header is consumed internally (it's
    // how v1-vs-future-version dispatch works) and never appears in the
    // returned list -- but handle_rc_file.cc's own NCVIEW_STATE_FILE_
    // VERSION entry is ordinary data as far as the Stringlist layer is
    // concerned, so it comes back as the first element (this is exactly
    // what ncview_main() itself gets back into its `read_in_state` global
    // -- see core/src/ncview.cc -- and simply never looks up by that name).
    // Read-back index values are reassigned by position during parsing
    // (see stringlist_line_to_sl(): it calls stringlist_add_string(),
    // which ignores the index field it just parsed out of the line), so
    // this checks name/type/value only.
    REQUIRE(stringlist_len(read_back) == 4);
    REQUIRE(read_back->size() == 4);

    const StringlistEntry &header = (*read_back)[0];
    CHECK(header.string == "NCVIEW_STATE_FILE_VERSION");
    CHECK(std::get<int>(header.aux) == 1);

    const StringlistEntry &e0 = (*read_back)[1];
    CHECK(e0.string == "CMAP_3gauss");
    CHECK(std::holds_alternative<int>(e0.aux));
    CHECK(std::get<int>(e0.aux) == 1);

    const StringlistEntry &e1 = (*read_back)[2];
    CHECK(e1.string == "CMAP_bright");
    CHECK(std::get<int>(e1.aux) == 0);

    const StringlistEntry &e2 = (*read_back)[3];
    CHECK(e2.string == "CMAP_rainbow");
    CHECK(std::get<int>(e2.aux) == 1);

    stringlist_delete_entire_list(read_back);
}

TEST_CASE("rc file: a real upstream file with CMAP_* entries reads back untouched") {
    // A representative copy of the kind of file PORTING.md's M6 notes
    // describe finding on a real machine with an actual upstream ncview
    // install: a header line plus a run of CMAP_<name> INT 1 lines. This
    // guards the "read an existing real-world file" path specifically,
    // separately from the write-then-read round trip above.
    ScratchHome home;
    {
        std::ofstream out(home.rc_path(), std::ios::binary);
        REQUIRE(out.good());
        out << "-1 \"STRINGLIST_SAVE_FILE_VERSION\" INT 1\n"
               "0 \"NCVIEW_STATE_FILE_VERSION\" INT 1\n"
               "1 \"CMAP_3gauss\" INT 1\n"
               "2 \"CMAP_3w_bright\" INT 1\n"
               "3 \"CMAP_3w_gray\" INT 1\n"
               "4 \"CMAP_bright\" INT 1\n"
               "5 \"CMAP_default\" INT 1\n"
               "6 \"CMAP_detail\" INT 1\n";
    }
    std::string original = read_file(home.rc_path());

    Stringlist *read_back = nullptr;
    REQUIRE(read_state_from_file(&read_back) == 0);
    CHECK(stringlist_len(read_back) == 7);
    REQUIRE(read_back->size() == 7);
    CHECK((*read_back)[0].string == "NCVIEW_STATE_FILE_VERSION");
    CHECK((*read_back)[1].string == "CMAP_3gauss");

    // Reading must not touch the file at all.
    CHECK(read_file(home.rc_path()) == original);

    stringlist_delete_entire_list(read_back);
}

TEST_CASE("rc file: first run with no existing file reports failure, not a crash") {
    ScratchHome home; // fresh scratch dir -- guaranteed no .ncviewrc in it
    Stringlist *state = nullptr;
    // Matches ncview_main()'s own contract (core/src/ncview.cc): pass a
    // NULL Stringlist**-target, expect a nonzero (but non-fatal) return
    // when the file simply doesn't exist yet.
    CHECK(read_state_from_file(&state) != 0);
    CHECK(state == nullptr);
}
