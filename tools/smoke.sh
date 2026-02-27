#!/bin/sh
# Headless smoke test for every example (docs/release.md).
#
#   tools/smoke.sh [build-dir] [snapshot-dir]
#
# Runs each examples/*.lcl for two seconds under the SDL offscreen
# video driver and fails on any "Frame error" / "Error" line (a view or
# theme that does not evaluate), then runs the Widget Tour in --snap
# mode and checks that all six page PNGs were written.  Needs
# lcl_lk_main (LK_BUILD_LCL=ON + SDL3); a display is not needed.
set -u
BUILD=${1:-build}
SNAP=${2:-${TMPDIR:-/tmp}/lk-tour}
RUNNER=$BUILD/lcl_lk_main
export SDL_VIDEODRIVER=offscreen
fail=0

if [ ! -x "$RUNNER" ]; then
  echo "smoke: $RUNNER not built (need LK_BUILD_LCL=ON and SDL3)" >&2
  exit 2
fi

for ex in examples/*.lcl; do
  case "$ex" in examples/tour-dsl.lcl) continue ;; esac
  if [ ! -s "$ex" ]; then
    echo "FAIL $ex"; echo "    empty file (an empty script runs without error)"; fail=1; continue
  fi
  out=$(timeout 2 "$RUNNER" "$ex" 2>&1 | grep -v '^Terminated$' | grep -E 'rror' | sort -u | head -3)
  if [ -n "$out" ]; then
    echo "FAIL $ex"; echo "$out" | sed 's/^/    /'; fail=1
  else
    echo "ok   $ex"
  fi
done

rm -rf "$SNAP"; mkdir -p "$SNAP"
out=$(timeout 30 "$RUNNER" examples/tour-dsl.lcl --snap "$SNAP" 2>&1 | grep -E 'rror' | sort -u | head -3)
n=$(ls "$SNAP"/tour-*.png 2>/dev/null | wc -l)
if [ -n "$out" ] || [ "$n" -ne 6 ]; then
  echo "FAIL examples/tour-dsl.lcl --snap ($n/6 pages)"; echo "$out" | sed 's/^/    /'; fail=1
else
  echo "ok   examples/tour-dsl.lcl --snap -> $SNAP (6 pages)"
fi

exit $fail
