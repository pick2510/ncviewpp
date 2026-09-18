// Copyright (C) 2026 Dominik Strebel
//
// Unit tests for core/src/stringlist.cc -- the std::vector<StringlistEntry>
// based string-list type used throughout core for variable/dimension name
// lists (docs/modernization.md Phase 3 replaced the original intrusive doubly-
// linked list with this container; see stringlist.h's own comment for why
// most call sites still hold a `Stringlist *`).
#include <cstdio>
#include <cstring>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "ncview/stringlist.h"

TEST_CASE("stringlist: add_string appends in order") {
    Stringlist *sl = nullptr;
    CHECK(stringlist_add_string(&sl, "one") == 0);
    CHECK(stringlist_add_string(&sl, "two") == 0);
    CHECK(stringlist_add_string(&sl, "three") == 0);
    REQUIRE(sl != nullptr);
    REQUIRE(sl->size() == 3);
    CHECK((*sl)[0].string == "one");
    CHECK((*sl)[1].string == "two");
    CHECK((*sl)[2].string == "three");
    CHECK(stringlist_len(sl) == 3);
    stringlist_delete_entire_list(sl);
}

TEST_CASE("stringlist: empty list has zero length") {
    CHECK(stringlist_len(nullptr) == 0);
}

TEST_CASE("stringlist: match_string_exact finds and misses") {
    Stringlist *sl = nullptr;
    stringlist_add_string(&sl, "alpha");
    stringlist_add_string(&sl, "beta");

    StringlistEntry *hit = stringlist_match_string_exact(sl, "beta");
    REQUIRE(hit != nullptr);
    CHECK(hit->string == "beta");

    CHECK(stringlist_match_string_exact(sl, "gamma") == nullptr);
    // Exact match: a substring or differently-cased match must not hit.
    CHECK(stringlist_match_string_exact(sl, "bet") == nullptr);
    CHECK(stringlist_match_string_exact(sl, "BETA") == nullptr);

    stringlist_delete_entire_list(sl);
}

TEST_CASE("stringlist: cat appends second list to first") {
    Stringlist *a = nullptr, *b = nullptr;
    stringlist_add_string(&a, "a1");
    stringlist_add_string(&a, "a2");
    stringlist_add_string(&b, "b1");

    CHECK(stringlist_cat(&a, &b) == 0);
    CHECK(stringlist_len(a) == 3);
    REQUIRE(a->size() == 3);
    CHECK((*a)[2].string == "b1");

    stringlist_delete_entire_list(a);
    stringlist_delete_entire_list(b);
}

TEST_CASE("stringlist: aux data round-trips through the variant") {
    Stringlist *sl = nullptr;
    stringlist_add_string(&sl, "an_int", 42);
    stringlist_add_string(&sl, "a_float", 3.5f);
    stringlist_add_string(&sl, "a_string", std::string("hello"));
    stringlist_add_string(&sl, "a_bool", true);
    stringlist_add_string(&sl, "no_aux");

    REQUIRE(sl->size() == 5);
    CHECK(std::get<int>((*sl)[0].aux) == 42);
    CHECK(std::get<float>((*sl)[1].aux) == doctest::Approx(3.5f));
    CHECK(std::get<std::string>((*sl)[2].aux) == "hello");
    CHECK(std::get<bool>((*sl)[3].aux) == true);
    CHECK(std::holds_alternative<std::monostate>((*sl)[4].aux));

    stringlist_delete_entire_list(sl);
}

TEST_CASE("stringlist: write then read round-trips through a file") {
    Stringlist *sl = nullptr;
    stringlist_add_string(&sl, "round");
    stringlist_add_string(&sl, "trip");

    FILE *f = std::tmpfile();
    REQUIRE(f != nullptr);
    CHECK(stringlist_write_to_file(sl, f) == 0);
    std::rewind(f);

    Stringlist *readback = nullptr;
    CHECK(stringlist_read_from_file(&readback, f) == 0);
    CHECK(stringlist_len(readback) == 2);
    REQUIRE(readback != nullptr);
    REQUIRE(readback->size() == 2);
    CHECK((*readback)[0].string == "round");
    CHECK((*readback)[1].string == "trip");

    std::fclose(f);
    stringlist_delete_entire_list(sl);
    stringlist_delete_entire_list(readback);
}
