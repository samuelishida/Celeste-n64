#!/usr/bin/env bash
# Build, run, and strictly qualify one deterministic real-map physics route.
# Usage: [wall_seconds] [label] [walljump|mantle|corner]; walljump is default.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WALL_SECONDS="${1:-30}"
PROFILE="${3:-walljump}"
LABEL="${2:-physics-$PROFILE-autoplay}"
case "$PROFILE" in
  walljump)
    ROM="$ROOT/physics_autoplay.z64"
    BUILD_TARGET=physics-autoplay-rom
    ;;
  mantle)
    ROM="$ROOT/physics_mantle_autoplay.z64"
    BUILD_TARGET=physics-mantle-autoplay-rom
    ;;
  corner)
    ROM="$ROOT/physics_corner_autoplay.z64"
    BUILD_TARGET=physics-corner-autoplay-rom
    ;;
  *)
    echo "[physics-autoplay] ERROR: profile must be 'walljump', 'mantle', or 'corner'" >&2
    exit 2
    ;;
esac

if command -v pgrep >/dev/null 2>&1 && pgrep -x ares >/dev/null 2>&1; then
  echo "[physics-autoplay] ERROR: Ares is already running; refusing a second instance" >&2
  pgrep -a -x ares >&2 || true
  exit 1
fi

make -C "$ROOT" "$BUILD_TARGET"
EXPECTED_SHA256="$(sha256sum "$ROM" | awk '{print $1}')"
CAPTURE_OUTPUT="$(bash "$ROOT/tools/capture_baseline.sh" "$ROM" "$WALL_SECONDS" "$LABEL")"
printf '%s\n' "$CAPTURE_OUTPUT"
CAPTURE="$(printf '%s\n' "$CAPTURE_OUTPUT" | sed -n 's/^\[capture_baseline\] wrote //p' | tail -n 1)"
if [[ -z "$CAPTURE" || ! -r "$CAPTURE" ]]; then
  echo "[physics-autoplay] ERROR: capture summary path missing" >&2
  exit 1
fi
python3 "$ROOT/tools/verify_physics_capture.py" "$CAPTURE" \
  --expected-sha256 "$EXPECTED_SHA256" --profile "$PROFILE"
