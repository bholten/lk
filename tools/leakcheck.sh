#!/bin/sh
# Every example headless under an ASan/LSan build (docs/release.md).
#
#   tools/leakcheck.sh [asan-build-dir]
#
# Runs each examples/*.lcl through tools/autoquit.lcl (the app stops
# after 30 frames and exits normally, so LeakSanitizer reports) and the
# Widget Tour in --snap mode, then fails on any leak whose stack has an
# lk or lcl frame.  Leaks with no such frame (the offscreen video
# driver's libdrm allocations) are counted but tolerated.  Needs an
# ASan-configured lcl_lk_main (LK_BUILD_LCL=ON + SDL3), e.g.
#   cmake -B build-asan -DLK_BUILD_LCL=ON -DCMAKE_BUILD_TYPE=Debug \
#     -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -O1" \
#     -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
set -u
BUILD=${1:-build-asan}
RUNNER=$BUILD/lcl_lk_main
TMP=${TMPDIR:-/tmp}/lk-leakcheck.$$
export SDL_VIDEODRIVER=offscreen
export ASAN_OPTIONS=detect_leaks=1
fail=0

if [ ! -x "$RUNNER" ]; then
  echo "leakcheck: $RUNNER not built (need LK_BUILD_LCL=ON and SDL3)" >&2
  exit 2
fi

mkdir -p "$TMP"

check() {
  name=$1; log=$2
  total=$(grep -c '^\(Direct\|Indirect\) leak' "$log")
  ours=$(grep -A4 '^\(Direct\|Indirect\) leak' "$log" | grep -c 'lcl-lk\|lcl-src\|src/core\|src/editor\|src/sdl')
  err=$(grep -m1 '^Error\|Frame error\|^==[0-9]*==ERROR: AddressSanitizer: [a-z-]*-\(overflow\|use\|free\)' "$log")
  if [ -n "$err" ] || [ "$ours" -ne 0 ]; then
    echo "FAIL $name ($ours lk/lcl-framed leak frames, $total leaks total)"
    [ -n "$err" ] && echo "    $err"
    grep -A6 '^\(Direct\|Indirect\) leak' "$log" | grep -B2 -A4 'lcl-lk\|lcl-src\|src/core\|src/editor\|src/sdl' | head -30 | sed 's/^/    /'
    fail=1
  else
    echo "ok   $name ($total foreign-frame leaks tolerated)"
  fi
}

for ex in examples/*.lcl; do
  case "$ex" in examples/tour-dsl.lcl) continue ;; esac
  log=$TMP/$(basename "$ex" .lcl).log
  timeout 120 "$RUNNER" tools/autoquit.lcl "$ex" > "$log" 2>&1
  check "$ex" "$log"
done

log=$TMP/tour.log
timeout 120 "$RUNNER" examples/tour-dsl.lcl --snap "$TMP/tour" > "$log" 2>&1
check "examples/tour-dsl.lcl --snap" "$log"

rm -rf "$TMP"
exit $fail
