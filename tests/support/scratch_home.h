// Copyright (C) 2026 Dominik Strebel
//
// Shared with test_rcfile.cc (which introduced this originally) and any
// other test that touches handle_rc_file.cc's hardcoded "$HOME/.ncviewrc"
// path. Moved here (OOP_redesign "refine the architecture" plan, Phase 0a)
// so SessionFixture (session_fixture.h) can compose it without duplicating
// it, now that more test files need $HOME redirected.
#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include <doctest/doctest.h>

namespace ncview_test {

// setenv/unsetenv are POSIX; MinGW's runtime doesn't declare them even
// under -std=gnu++17. _putenv_s is the MSVCRT equivalent for both set and
// unset (an empty value removes the variable).
inline void set_env(const char *name, const std::string &value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}
inline void unset_env(const char *name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// RAII: creates a scratch directory, points $HOME at it, and restores the
// real $HOME (or unsets it, matching whatever state it found) on
// destruction -- so a failing REQUIRE partway through a test still leaves
// $HOME as it was, and two tests using this never see each other's
// .ncviewrc regardless of run order.
struct ScratchHome {
    std::filesystem::path dir;
    std::string old_home;
    bool had_home;

    ScratchHome() {
        auto tmpl = (std::filesystem::temp_directory_path() / "ncview_rc_test_XXXXXX").string();
        REQUIRE(mkdtemp(&tmpl[0]) != nullptr);
        dir = tmpl;

        const char *old = getenv("HOME");
        had_home = (old != nullptr);
        if (had_home) old_home = old;
        set_env("HOME", dir.string());
    }
    ~ScratchHome() {
        if (had_home)
            set_env("HOME", old_home);
        else
            unset_env("HOME");
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    std::string rc_path() const { return (dir / ".ncviewrc").string(); }
};

} // namespace ncview_test
