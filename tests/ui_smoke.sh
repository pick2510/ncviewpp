#!/usr/bin/env bash
# Xvfb screenshot regression harness (modernization.md Phase 0c).
#
# Scripts the headless test hooks ui/src/interface_fltk.cc's in_initialize()
# already provides (NCVIEW_TEST_AUTOSELECT, NCVIEW_TEST_DIALOG,
# NCVIEW_TEST_BUTTON -- see that file's comments) into a repeatable
# screenshot comparison, so a UI-visible regression from the core
# modernization work (Phases 3-6 of modernization.md) is caught
# automatically instead of by manual `import`+eyeballing, which is how
# PORTING.md's own M3/M4/M6 verification was actually done.
#
# Usage:
#   tests/ui_smoke.sh <path-to-ncview-binary> [--update]
#
# Without --update: generates one screenshot per case and compares it,
# pixel-for-pixel, against tests/ui_smoke/golden/<case>.png. Any mismatch
# (or missing golden) fails.
#
# With --update: generates the screenshots and overwrites the goldens
# instead of comparing -- this is how goldens are (re)created, including the
# very first time. Always run this by hand and look at the resulting PNGs
# before committing them; the harness cannot judge whether a screenshot
# looks right, only whether it matches what was last approved.
set -u

NCVIEW_BIN="${1:?usage: $0 <path-to-ncview-binary> [--update]}"
UPDATE=0
[ "${2:-}" = "--update" ] && UPDATE=1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GOLDEN_DIR="$SCRIPT_DIR/ui_smoke/golden"
CDL_FILE="$SCRIPT_DIR/ui_smoke/sample.cdl"
CDL_1D_FILE="$SCRIPT_DIR/ui_smoke/sample_1d.cdl"

command -v Xvfb >/dev/null || { echo "ui_smoke: Xvfb not found, skipping" >&2; exit 77; }
command -v import >/dev/null || { echo "ui_smoke: ImageMagick 'import' not found, skipping" >&2; exit 77; }
command -v compare >/dev/null || { echo "ui_smoke: ImageMagick 'compare' not found, skipping" >&2; exit 77; }
command -v ncgen >/dev/null || { echo "ui_smoke: ncgen not found, skipping" >&2; exit 77; }

WORKDIR="$(mktemp -d)"
XVFB_PID=""
NCVIEW_PID=""

cleanup() {
    [ -n "$NCVIEW_PID" ] && kill -9 "$NCVIEW_PID" >/dev/null 2>&1
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" >/dev/null 2>&1
    rm -rf "$WORKDIR"
}
trap cleanup EXIT

SAMPLE_NC="$WORKDIR/sample.nc"
ncgen -o "$SAMPLE_NC" "$CDL_FILE" || { echo "ui_smoke: ncgen failed"; exit 1; }
SAMPLE_1D_NC="$WORKDIR/sample_1d.nc"
ncgen -o "$SAMPLE_1D_NC" "$CDL_1D_FILE" || { echo "ui_smoke: ncgen (1d) failed"; exit 1; }

# Pick a display number unlikely to collide with a real X server or another
# concurrent run of this script, then start Xvfb on it and wait for its
# socket to actually appear rather than sleeping a guessed delay.
DISPLAY_NUM=$(( (RANDOM % 100) + 150 ))
export DISPLAY=":$DISPLAY_NUM"
Xvfb "$DISPLAY" -screen 0 1024x768x24 >"$WORKDIR/xvfb.log" 2>&1 &
XVFB_PID=$!
for _ in $(seq 1 50); do
    [ -e "/tmp/.X11-unix/X$DISPLAY_NUM" ] && break
    sleep 0.1
done
if [ ! -e "/tmp/.X11-unix/X$DISPLAY_NUM" ]; then
    echo "ui_smoke: Xvfb did not start on display $DISPLAY" >&2
    exit 1
fi

# case_name -> extra environment variable(s) beyond NCVIEW_TEST_AUTOSELECT=1,
# mirroring the vocabulary ui/src/interface_fltk.cc's in_initialize()
# understands. One entry per line: "<case> <ENV_VAR>=<value>".
CASES='
initial
dialog_range NCVIEW_TEST_DIALOG=range
dialog_options NCVIEW_TEST_DIALOG=options
dialog_dimset NCVIEW_TEST_DIALOG=dimset
dialog_info NCVIEW_TEST_DIALOG=info
dialog_dataedit NCVIEW_TEST_DIALOG=dataedit
dialog_plot NCVIEW_TEST_DIALOG=plot
dialog_overlay NCVIEW_TEST_DIALOG=overlay
button_blowup NCVIEW_TEST_BUTTON=blowup
button_blowup_type NCVIEW_TEST_BUTTON=blowup_type
button_transform NCVIEW_TEST_BUTTON=transform
button_invert_colormap NCVIEW_TEST_BUTTON=invert_colormap
button_invert_physical NCVIEW_TEST_BUTTON=invert_physical
button_colormap NCVIEW_TEST_BUTTON=colormap
'

FAILED=0
mkdir -p "$GOLDEN_DIR"

while read -r case_name extra_env; do
    [ -z "$case_name" ] && continue

    shot="$WORKDIR/$case_name.png"
    env -i DISPLAY="$DISPLAY" HOME="$WORKDIR" PATH="$PATH" \
        NCVIEW_TEST_AUTOSELECT=1 ${extra_env:+"$extra_env"} \
        "$NCVIEW_BIN" "$SAMPLE_NC" >"$WORKDIR/$case_name.log" 2>&1 &
    NCVIEW_PID=$!

    # Give the window (and, for dialog/button cases, the modal dialog or
    # button effect) time to actually paint. Modal dialogs never return
    # control to in_initialize()'s caller, so the process must always be
    # killed below -- it is never expected to exit on its own.
    sleep 1.5
    import -window root "$shot" 2>>"$WORKDIR/$case_name.log"

    kill "$NCVIEW_PID" >/dev/null 2>&1
    sleep 0.2
    kill -9 "$NCVIEW_PID" >/dev/null 2>&1
    wait "$NCVIEW_PID" 2>/dev/null
    NCVIEW_PID=""

    if [ "$UPDATE" = "1" ]; then
        cp "$shot" "$GOLDEN_DIR/$case_name.png"
        echo "ui_smoke: updated golden for $case_name"
        continue
    fi

    golden="$GOLDEN_DIR/$case_name.png"
    if [ ! -f "$golden" ]; then
        echo "ui_smoke: FAIL $case_name (no golden at $golden)"
        FAILED=1
        continue
    fi

    # ImageMagick's `compare -metric AE` output format is version-dependent:
    # some versions print a bare pixel count ("0", "123"), others (e.g. IM7)
    # always append a normalized value in parens ("0 (0)", "123 (0.0019)").
    # Only the leading number is the actual differing-pixel count we care
    # about.
    ae=$(compare -metric AE -fuzz 0 "$golden" "$shot" null: 2>&1)
    ae_count="${ae%% *}"
    if [ "$ae_count" != "0" ]; then
        echo "ui_smoke: FAIL $case_name (AE=$ae, differing pixels) -- actual: $shot, expected: $golden"
        FAILED=1
    elif grep -q "got an expose event" "$WORKDIR/$case_name.log"; then
        echo "ui_smoke: FAIL $case_name (stray debug output on stdout/stderr: 'got an expose event') -- log: $WORKDIR/$case_name.log"
        FAILED=1
    else
        echo "ui_smoke: pass $case_name"
    fi
done <<EOF
$CASES
EOF

# var_1d uses its own fixture (sample_1d.nc, a single genuinely 1-D
# variable with no lat/lon) instead of $SAMPLE_NC -- see sample_1d.cdl's
# header comment for why this isn't just a second variable added to the
# shared sample.cdl. Otherwise identical to a loop case: screenshot,
# compare against its golden.
shot="$WORKDIR/var_1d.png"
env -i DISPLAY="$DISPLAY" HOME="$WORKDIR" PATH="$PATH" \
    NCVIEW_TEST_AUTOSELECT=1 \
    "$NCVIEW_BIN" "$SAMPLE_1D_NC" >"$WORKDIR/var_1d.log" 2>&1 &
NCVIEW_PID=$!
sleep 1.5
import -window root "$shot" 2>>"$WORKDIR/var_1d.log"
kill "$NCVIEW_PID" >/dev/null 2>&1
sleep 0.2
kill -9 "$NCVIEW_PID" >/dev/null 2>&1
wait "$NCVIEW_PID" 2>/dev/null
NCVIEW_PID=""

if [ "$UPDATE" = "1" ]; then
    cp "$shot" "$GOLDEN_DIR/var_1d.png"
    echo "ui_smoke: updated golden for var_1d"
else
    golden="$GOLDEN_DIR/var_1d.png"
    if [ ! -f "$golden" ]; then
        echo "ui_smoke: FAIL var_1d (no golden at $golden)"
        FAILED=1
    else
        ae=$(compare -metric AE -fuzz 0 "$golden" "$shot" null: 2>&1)
        ae_count="${ae%% *}"
        if [ "$ae_count" != "0" ]; then
            echo "ui_smoke: FAIL var_1d (AE=$ae, differing pixels) -- actual: $shot, expected: $golden"
            FAILED=1
        elif grep -q "got an expose event" "$WORKDIR/var_1d.log"; then
            echo "ui_smoke: FAIL var_1d (stray debug output on stdout/stderr: 'got an expose event') -- log: $WORKDIR/var_1d.log"
            FAILED=1
        else
            echo "ui_smoke: pass var_1d"
        fi
    fi
fi

# "print" is checked differently from the golden-screenshot cases above: it
# runs do_print() to completion -- both the page-layout dialog and the
# native print dialog are bypassed by NCVIEW_TEST_PRINT_FILE (see
# printer_options()/in_print() in ui/src/interface_fltk.cc) -- and checks
# the resulting PostScript file, rather than a screenshot. Runs regardless
# of --update: there's no golden to (re)generate here, only a real output
# file to sanity-check.
print_ps="$WORKDIR/print.ps"
env -i DISPLAY="$DISPLAY" HOME="$WORKDIR" PATH="$PATH" \
    NCVIEW_TEST_AUTOSELECT=1 NCVIEW_TEST_DIALOG=print NCVIEW_TEST_PRINT_FILE="$print_ps" \
    "$NCVIEW_BIN" "$SAMPLE_NC" >"$WORKDIR/print.log" 2>&1 &
NCVIEW_PID=$!
sleep 1.5
kill "$NCVIEW_PID" >/dev/null 2>&1
sleep 0.2
kill -9 "$NCVIEW_PID" >/dev/null 2>&1
wait "$NCVIEW_PID" 2>/dev/null
NCVIEW_PID=""

if [ ! -s "$print_ps" ]; then
    echo "ui_smoke: FAIL print (no/empty PostScript output at $print_ps) -- log: $WORKDIR/print.log"
    FAILED=1
elif ! head -c 4 "$print_ps" | grep -q '^%!PS'; then
    echo "ui_smoke: FAIL print (output doesn't start with %!PS) -- file: $print_ps"
    FAILED=1
else
    echo "ui_smoke: pass print"
fi

if [ "$FAILED" != "0" ] && [ "$UPDATE" != "1" ]; then
    echo "ui_smoke: one or more cases failed; screenshots left in $WORKDIR" >&2
    trap - EXIT # keep WORKDIR around for inspection
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" >/dev/null 2>&1
    exit 1
fi

exit 0
