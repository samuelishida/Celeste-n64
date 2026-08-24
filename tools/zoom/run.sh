#!/usr/bin/env bash
# Build and launch Zoom Workplace from a Docker container.
# GUI -> X11 passthrough; audio -> PulseAudio/PipeWire; camera -> /dev/video*.
set -euo pipefail

cd "$(dirname "$0")"

UID="$(id -u)"
GID="$(id -g)"
USER="$(id -un)"
AUDIO_GID="$(getent group audio | cut -d: -f3)"
VIDEO_GID="$(getent group video | cut -d: -f3)"

export UID GID USER AUDIO_GID VIDEO_GID DISPLAY

echo "Launching Zoom (uid=$UID gid=$GID display=$DISPLAY audio_gid=$AUDIO_GID video_gid=$VIDEO_GID)"

# Build the image once, then run. `docker compose up` rebuilds only on Dockerfile change.
docker compose build --quiet
exec docker compose run --rm zoom
