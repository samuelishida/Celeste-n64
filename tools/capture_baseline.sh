#!/usr/bin/env bash
# capture_baseline.sh — capture the ROM's on-device telemetry at map center.
#
# Greps the [profiler] / [render-phases] / [counters] / [memory] report
# lines from emulator stdout and writes them to build/baseline-<date>.txt so
# the before/after comparison is reproducible.
#
# Device emulator: Ares (sole validator per AGENTS.md). Ares prints the ROM's
# USB-serial debugf() telemetry to stdout. Ares is GUI-only (opens a window and
# stays running), so we launch it in the background, poll the captured stdout
# for report lines, and kill it once we have enough or the wall budget elapses.
#
# Report cadence (src/user/rom_main.cpp):
#   [profiler] / [render-phases] / [counters] / [distant-cells]  every 60 frames
#   [memory]                                                     every 3600 frames
# Ares runs at ~60 fps in the emulator, so 60-frame reports land ~1 s apart and
# a [memory] line lands ~60 s apart. A run must cross 3600 frames to capture a
# [memory] line, hence the default wall budget below.
#
# NOTE: absolute timing is a proxy, not proof (see docs/perf_budget.md). The
# relative before/after deltas are what the plan compares.
#
# Usage:
#   tools/capture_baseline.sh [rom] [wall_seconds] [label]
#
#   rom           ROM to run (default: madeline_cube_rom.z64 at repo root)
#   wall_seconds  how long to let the emulator run before killing it
#                 (default: 120 = 2 min; enough for ~2 [memory] lines at ~60 fps)
#   label         optional label embedded in the output filename
#
# Env overrides: ARES_BIN, ARES_LIBDIR.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROM="${1:-$REPO_ROOT/madeline_cube_rom.z64}"
WALL_SECONDS="${2:-120}"
LABEL="${3:-}"

# Ares is a snap; the raw binary needs its bundled lib dir on LD_LIBRARY_PATH
# (librashader.so lives there) or it fails with "cannot open shared object
# file". /snap/ares-emulator/current is a symlink to the active revision, so
# this survives snap updates.
ARES_BIN="${ARES_BIN:-/snap/ares-emulator/current/usr/bin/ares}"
ARES_LIBDIR="${ARES_LIBDIR:-/snap/ares-emulator/current/usr/lib/x86_64-linux-gnu}"

OUT_DIR="$REPO_ROOT/build"
mkdir -p "$OUT_DIR"
DATE="$(date +%Y%m%d-%H%M%S)"
NAME="baseline${LABEL:+-$LABEL}-$DATE.txt"
OUT="$OUT_DIR/$NAME"
RAW="$OUT_DIR/raw-$NAME.log"

echo "[capture_baseline] rom=$ROM"
echo "[capture_baseline] wall_seconds=$WALL_SECONDS"
echo "[capture_baseline] ares=$ARES_BIN"
echo "[capture_baseline] out=$OUT"

if [[ ! -x "$ARES_BIN" ]]; then
  echo "[capture_baseline] ERROR: Ares binary not found at $ARES_BIN" >&2
  echo "[capture_baseline] (is the ares-emulator snap installed?)" >&2
  exit 1
fi

# Launch Ares in the background in its own process group (setsid) so we can
# kill the whole tree on cleanup. stdout+stderr -> RAW. stdbuf forces line
# buffering so report lines appear in RAW promptly (Ares block-buffers stdout
# when redirected to a file).
#
# --no-file-prompt skips the file-open dialog so the ROM boots immediately.
LD_LIBRARY_PATH="$ARES_LIBDIR" stdbuf -oL -eL setsid "$ARES_BIN" \
  --no-file-prompt "$ROM" > "$RAW" 2>&1 &
PID=$!
PGID=$PID

cleanup() {
  # Kill the whole process group, then the PID as a fallback.
  kill -- "-$PGID" 2>/dev/null || true
  kill "$PID" 2>/dev/null || true
  wait "$PID" 2>/dev/null || true
}
trap cleanup EXIT

# Poll for report lines until we have at least one [memory] line AND a handful
# of [profiler] lines, or the wall budget elapses. Ares is ~60 fps, so a
# [memory] line takes ~60 s; poll every 2 s.
deadline=$(( $(date +%s) + WALL_SECONDS ))
while (( $(date +%s) < deadline )); do
  if ! kill -0 "$PID" 2>/dev/null; then
    echo "[capture_baseline] Ares exited early (see $RAW)"
    break
  fi
  n_mem=$(grep -cE '\[memory\]' "$RAW" 2>/dev/null || true)
  n_prof=$(grep -cE '\[profiler\]' "$RAW" 2>/dev/null || true)
  if (( n_mem >= 1 && n_prof >= 5 )); then
    echo "[capture_baseline] captured $n_mem [memory] + $n_prof [profiler] lines — stopping"
    break
  fi
  sleep 2
done

cleanup
trap - EXIT

# Extract the report lines, preserving order.
grep -nE '\[(profiler|render-phases|counters|distant-cells|memory)\]' "$RAW" > "$OUT" || true

echo "[capture_baseline] wrote $OUT"
echo "=== captured report lines ==="
cat "$OUT"
echo "=== end ==="
