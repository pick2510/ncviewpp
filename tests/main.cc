// Copyright (C) 2026 Dominik Strebel
//
// Custom main() (DOCTEST_CONFIG_IMPLEMENT, not _WITH_MAIN) so the process
// can exit immediately after doctest reports its result. Every other
// tests/test_*.cc file just #includes doctest.h (without either define) and
// registers its TEST_CASEs into the same global registry -- doctest links
// all of it into one binary and one aggregate report.
//
// See fast_exit.h for why this needs TerminateProcess() specifically on
// Windows, not std::_Exit(): confirmed on CI that this suite's doctest run
// completes and prints "Status: SUCCESS!" (all cases, all assertions) and
// then the *process* hangs afterwards during exit-time teardown -- CTest
// reports a Timeout despite every test having actually passed.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "fast_exit.h"

// Defined in stub_interface.cc; must run before any test reaches the
// interface.h seam. Called explicitly here, rather than via a static
// initializer in stub_interface.cc, because g_app (ncview/app_context.h)
// requires dynamic initialization itself -- see installRecordingViewerUi()'s
// own comment for why that makes static-initializer ordering unsafe.
void installRecordingViewerUi();

int main(int argc, char **argv) {
    installRecordingViewerUi();
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    int res = context.run();
    ncviewTestsFastExit(res);
}
