#!/usr/bin/env bash
# capture_baseline.sh — capture the ROM's runtime telemetry from Ares.
#
# Greps boot, map/render, physics, and [profiler] / [counters] / [memory]
# telemetry from emulator stdout and writes build/baseline-<date>.txt with ROM
# SHA-256 provenance so every claim identifies exact artifact and runtime state.
#
# Device emulator: Ares (sole validator per AGENTS.md). Ares prints the ROM's
# USB-serial debugf() telemetry to stdout. Ares is GUI-only (opens a window and
# stays running), so we launch it in the background, poll the captured stdout
# for report lines, and kill only our own process group. Existing Ares session
# causes a fail-fast refusal; this script cannot capture another process's log.
#
# Report cadence (src/user/rom_main.cpp):
#   [profiler] / [render-phases] / [counters] every 60 frames
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
# Env overrides: ARES_BIN, ARES_LIBDIR, CAPTURE_OUT_DIR.

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

if [[ ! -f "$ROM" || ! -r "$ROM" ]]; then
  echo "[capture_baseline] ERROR: ROM is missing or unreadable: $ROM" >&2
  exit 1
fi
if ! ROM_SHA256="$(sha256sum "$ROM" | awk '{print $1}')"; then
  echo "[capture_baseline] ERROR: could not hash ROM: $ROM" >&2
  exit 1
fi
ROM_SIZE_BYTES="$(stat -c '%s' -- "$ROM")"

# Never create a second emulator beside the interactive/user session. Its
# stdout cannot be captured by this script, and two Ares instances compete for
# the same controller/device state and make physics evidence ambiguous.
if command -v pgrep >/dev/null 2>&1 && pgrep -x ares >/dev/null 2>&1; then
  echo "[capture_baseline] ERROR: Ares is already running; refusing a second instance" >&2
  pgrep -a -x ares >&2 || true
  exit 1
fi

OUT_DIR="${CAPTURE_OUT_DIR:-$REPO_ROOT/build}"
mkdir -p "$OUT_DIR"
DATE="$(date +%Y%m%d-%H%M%S)"
NAME="baseline${LABEL:+-$LABEL}-$DATE.txt"
OUT="$OUT_DIR/$NAME"
RAW="$OUT_DIR/raw-$NAME.log"

echo "[capture_baseline] rom=$ROM"
echo "[capture_baseline] rom_sha256=$ROM_SHA256 size_bytes=$ROM_SIZE_BYTES"
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
  # Ares may ignore SIGTERM in GUI mode. Bound the grace period, then reap
  # only the process group this script created so captures cannot hang.
  for attempt in 1 2 3 4 5; do
    if ! kill -0 "$PID" 2>/dev/null; then
      break
    fi
    sleep 0.2
  done
  if kill -0 "$PID" 2>/dev/null; then
    kill -KILL -- "-$PGID" 2>/dev/null || true
    kill -KILL "$PID" 2>/dev/null || true
  fi
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

# Extract boot, map/render health, and physics report lines with artifact
# identity. A physics PASS without its ROM/build/world/input context is not a
# valid capture (stale ROMs and stuck shoulder mappings have caused false
# conclusions before).
{
  printf '[artifact] rom=%s sha256=%s size_bytes=%s\n' \
    "$ROM" "$ROM_SHA256" "$ROM_SIZE_BYTES"
  grep -nE '\[(build|init|mappack|map_runtime|matcat|texroom|lvlroom|profiler|render-phases|counters|memory|budget|phys-test|phys|move|climb-window|climb|input-trace|input)\]' "$RAW" || true
} > "$OUT"

echo "[capture_baseline] wrote $OUT"
echo "=== captured report lines ==="
cat "$OUT"
echo "=== end ==="
