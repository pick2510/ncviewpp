# Bring `ui/` up to idiomatic C++17

## Context

`ui/` is the FLTK implementation of the `core/ncview/interface.h` toolkit seam, added fresh during the port (M3) rather than carried over from upstream C the way `core/` was. That shows: at 2699 lines across `interface_fltk.cc`, `main_window.cc`, `plot_window.cc` (plus headers), it already builds **warnings-clean** at `-Wall -Wextra` (0 warnings, confirmed via a full rebuild against the `ncview_warning_flags` target it already links), unlike `core/`'s pre-[[modernization]] 214.

So this isn't a repeat of `core/`'s warnings sweep. What's left is idiom, not hygiene:

| Signal | Count | Note |
|---|---|---|
| `new` sites | 58 | Mostly FLTK widgets (`Fl_Button`, `Fl_Box`, …) constructed and handed to a parent `Fl_Group`/window |
| `delete` sites | 4 | `PlotWindow::closeCallback`, `in_timer_clear`/`timerTrampoline` (×2), one more |
| `char *`/`char **` occurrences | 144 | Overwhelmingly parameter types, not owned storage — see below |
| Compiler warnings at `-Wall -Wextra` | 0 | Already clean |
| `-Werror` on `ncview_ui` | not enabled | Trivial once decided, since already 0 warnings |

Two things make `ui/`'s idiom gap narrower than raw counts suggest, and both shape the scope below:

1. **Most `new` sites are FLTK widgets whose lifetime FLTK itself owns.** `Fl_Group`/`Fl_Window::add()` takes a raw pointer and deletes it when the parent is destroyed — that's the framework's own ownership contract, not a C idiom needing replacement. Wrapping these in `std::unique_ptr` would fight FLTK (double-free unless `.release()` is threaded through every `add()` call) for no behavioral or safety gain. These stay raw `new`, matching FLTK's own examples and every other FLTK codebase.
2. **Most `char *` occurrences are dictated by call sites `ui/` doesn't own**: `core/include/ncview/interface.h`'s `in_*` seam (`core/`'s public contract — changing it is `core/` work, per [[modernization]]'s decision 3, not an `ui/`-only pass) and FLTK's own API (`Fl_Widget` constructors take `const char *`). `ui/`'s own owned state is already `std::string` where it matters (`PlotWindow::title_`/`xlabel_`/`ylabel_`). The real, narrow gap is a handful of raw pointers `ui/` genuinely owns and manages itself.

The intended outcome: `ui/` owns its self-managed state through RAII where FLTK's own model doesn't already dictate otherwise, builds `-Werror` clean like `ncview_core` now does, and behaves **exactly** as it does today.

## Decisions binding this work

1. **Scope is deliberately narrower than `core/`'s.** No blanket "eliminate raw pointers" or "eliminate `char*`" rule — see Context above. Each phase below names the specific sites in scope; anything not named (FLTK-widget `new`, FLTK/seam-dictated `char*` parameters) is explicitly out of scope and stays as-is.
2. **Parity is strict**, exactly as in [[modernization]]: every commit leaves `ctest` output and the Xvfb `ncview_ui_smoke` screenshots byte-identical. Defects found along the way are logged here and fixed in separate, later commits with their own tests.
3. **The `core/` seam is out of scope here.** If a genuine improvement would require changing `interface.h`/`protos.h`, it's noted below as a `core/`-side follow-up, not done in this file's phases.
4. Same verification bar as [[modernization]]: full rebuild, `ctest` 100% (including the Xvfb screenshot test), and an `-DNCVIEW_SANITIZE=address,undefined` Debug build clean, after every phase.

## Phase U1 — Self-managed pointers to RAII (done)

Sites where `ui/` itself is the sole owner and manages lifetime manually (not FLTK's parent-child tree):

- `ui/src/interface_fltk.cc`'s `g_pending_timer` (a `std::function<void()> *`, single-slot by design per its own comment) and the heap `std::function<void()>` allocated in `in_timer_set()`/freed in `timerTrampoline()`/`in_timer_clear()` — convert `g_pending_timer` to `std::unique_ptr<std::function<void()>>`; `timerTrampoline`'s `delete fn` and `in_timer_clear`'s `delete g_pending_timer` become `.reset()`/implicit destruction. FLTK's `Fl::add_timeout` still needs the raw pointer for its `void*` callback data — get it via `.get()`.
- `ui/src/plot_window.cc`'s `g_plot_windows[MAX_PLOT_XY]` array of raw `PlotWindow*`, populated by `PlotWindow::create()` and manually `delete`d in `closeCallback()` — convert to `std::array<std::unique_ptr<PlotWindow>, MAX_PLOT_XY>` (or leave the slot search as-is and just change the element type). `PlotWindow::create()`'s returned raw pointer and the `void*` passed to FLTK callbacks are unaffected (still the raw address, via `.get()`); only who calls `delete` changes.
- `ui/src/main_window.cc:411`'s function-local `static MainWindow *w = new MainWindow();` (Meyers-singleton-shaped, intentionally never freed for the process lifetime) — leave as-is; there's no owner to convert to (a `static MainWindow w;` would change construction-order/exception-safety guarantees the current code doesn't rely on either way, and the leak is deliberate and harmless at process exit). Note this explicitly rather than silently skipping it.

**Verification**: full rebuild + `ctest` (incl. `ncview_ui_smoke`) + ASan/UBSan Debug build, all clean, byte-identical screenshots.

**Done.** `g_pending_timer` and `g_plot_windows[]` converted to `std::unique_ptr`/`std::array<std::unique_ptr<...>>`, preserving exact destruction order at each call site (invoke the timer callback before freeing its closure; hide/delete the FLTK window before freeing the `PlotWindow`). `main_window.cc:411`'s Meyers-singleton-shaped `static MainWindow *w = new MainWindow()` left as-is, per the note above. Verified: full rebuild clean, `ctest` 100% (incl. the byte-identical Xvfb `ncview_ui_smoke` screenshot test), `-DNCVIEW_SANITIZE=address,undefined` Debug build clean.

## Phase U2 — Enable `-Werror` for `ncview_ui` (done)

`ncview_ui` already builds with 0 warnings at `-Wall -Wextra` (confirmed before writing this plan). Mirror `core/CMakeLists.txt`'s Phase 9 pattern in `ui/CMakeLists.txt`:

```cmake
if(MSVC)
    target_compile_options(ncview_ui PRIVATE /WX)
else()
    target_compile_options(ncview_ui PRIVATE -Werror)
endif()
```

No source changes expected; this phase is the CMake change plus a full rebuild to confirm it's still clean immediately before and after.

**Done.** No source changes needed. Verified: full rebuild clean with `-Werror` on for `ncview_ui`, `ctest` 100% (incl. `ncview_ui_smoke`), and an `-DNCVIEW_SANITIZE=address,undefined` Debug build clean with `-Werror` on.

**This completes ui-modernization.md's scoped work.**

## Follow-up notes (not phases — `core/`-side or out of scope)

- `interface.h`'s `in_*` seam still passes `char*`/`char**` in several places `ui/` implements against (e.g. `in_dialog`'s `char *ret_string`, `in_parse_args`'s `char **argv`). Narrowing these to `std::string&`/`std::span<char*>` where safe is `core/` seam work per decision 3 above, not this file's scope — noted here so it isn't lost, not scheduled.
