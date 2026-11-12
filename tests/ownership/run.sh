#!/bin/bash
# Builds the synthetic-boundary ownership harness against a source tree with
# ASan/UBSan and runs every test in its own process.
#
#   tests/ownership/run.sh <source-root> <fixed|baseline>
#
# <source-root> provides the plugin sources, the bundled 4D Plugin API and
# the bundled headers (include/). The harness itself always comes from this
# script's directory, so the SAME harness can be run against an immutable
# baseline (git archive of the base SHA) and against the fixed tree.
#
# jsoncpp is built from the pinned 1.9.4 sources (the version of
# include/json, as in scripts/build-macos-deps.sh); set JSONCPP_SRC to an
# existing checkout to skip the fetch. Vosk and PortAudio are stubbed in the
# harness: no model is downloaded and no audio device is opened.
#
# In "fixed" mode every test must pass. In "baseline" mode the tests named
# in EXPECTED_BASELINE_FAILURES must fail and every other test (the
# controls) must pass; anything else is a harness or model error.
set -euo pipefail

SRC=$(cd "$1" && pwd)
MODE=$2
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

EXPECTED_BASELINE_FAILURES="
defect_live_vosk_file_platform_path_double_dispose
defect_live_set_options_folder_path_leaks
defect_live_set_options_wrong_kind_options_not_cleared
defect_live_vosk_object_options_not_released
defect_live_on_data_file_method_result_not_cleared
defect_live_on_data_audio_input_method_result_not_cleared
defect_live_execute_method_callback
defect_live_callback_params_leak_without_model
hardening_live_on_data_method_error_result_untouched
hardening_live_platform_path_error_untouched
hardening_live_file_command_error_untouched
hardening_ob_get_error_result_untouched
defect_dormant_ob_get_b_c_not_cleared
hardening_dormant_ob_stringify_error_untouched
"

JSONCPP_SHA=9059f5cad030ba11d37818847443a53918c327b1 # open-source-parsers/jsoncpp tag 1.9.4
if [ -z "${JSONCPP_SRC:-}" ]; then
    JSONCPP_SRC="$OUT/jsoncpp"
    git init -q "$JSONCPP_SRC"
    git -C "$JSONCPP_SRC" fetch -q --depth 1 https://github.com/open-source-parsers/jsoncpp "$JSONCPP_SHA"
    git -C "$JSONCPP_SRC" checkout -q FETCH_HEAD
fi
[ "$(git -C "$JSONCPP_SRC" rev-parse HEAD)" = "$JSONCPP_SHA" ] || { echo "jsoncpp is not $JSONCPP_SHA"; exit 1; }

SAN="-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined"
# Same header search paths as the Xcode project (HEADER_SEARCH_PATHS = include/**).
INC=(-I "$SRC/4D Plugin API" -I "$SRC/4D Plugin API/Classes" -I "$SRC/support" -I "$SRC"
     -I "$SRC/include" -I "$SRC/include/json")
WARN="-w"

objs=()
cc() { # <lang> <file> [extra flags...]
    local lang=$1 file=$2; shift 2
    local o="$OUT/$(basename "$file").o"
    xcrun clang $SAN $WARN -x "$lang" "${INC[@]}" "$@" -c "$file" -o "$o"
    objs+=("$o")
}

# Exactly the sources the Xcode target and the vcxproj build.
cc c "$SRC/4D Plugin API/4DPluginAPI.c" -Wno-multichar -Wno-error=int-conversion
cc objective-c++ "$SRC/4D Plugin API/Classes/C_TEXT.cpp" -std=c++17 -fno-objc-arc
cc objective-c++ "$SRC/support/4DPlugin-JSON.cpp" -std=c++17 -fno-objc-arc
# C_TYPES.h imports Cocoa, so C++ is built as Objective-C++ (as Xcode does).
# engine_harness.cpp #includes 4DPlugin-vosk.cpp from $SRC.
cc objective-c++ "$HERE/engine_harness.cpp" -std=c++17 -fno-objc-arc
# jsoncpp, compiled against the bundled include/json headers.
for f in json_reader json_value json_writer; do
    cc c++ "$JSONCPP_SRC/src/lib_json/$f.cpp" -std=c++17 -I "$JSONCPP_SRC/src/lib_json"
done

xcrun clang++ $SAN "${objs[@]}" -framework Cocoa -framework Foundation -framework CoreFoundation -o "$OUT/harness"

export ASAN_OPTIONS=abort_on_error=0:halt_on_error=1
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1

bad=0
for t in $("$OUT/harness" --list); do
    if "$OUT/harness" "$t" >"$OUT/$t.log" 2>&1; then got=pass; else got=fail; fi
    expect=pass
    if [ "$MODE" = baseline ] && grep -qx "$t" <<<"$EXPECTED_BASELINE_FAILURES"; then expect=fail; fi
    if [ "$got" = "$expect" ]; then status=OK; else status=UNEXPECTED; bad=1; fi
    printf '%-12s %-5s (expected %-4s) %s\n' "$status" "$got" "$expect" "$t"
    if [ "$got" = fail ]; then sed 's/^/    | /' "$OUT/$t.log"; fi
    # A sanitizer report or harness error is never an expected outcome.
    if grep -qE 'ERROR: AddressSanitizer|runtime error:|HARNESS ERROR' "$OUT/$t.log"; then
        echo "UNEXPECTED   sanitizer/harness error in $t"; bad=1
    fi
done

if [ $bad -ne 0 ]; then echo "RESULT ($MODE): FAILED"; exit 1; fi
echo "RESULT ($MODE): all outcomes as expected"
