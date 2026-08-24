# Zoom Workspace in Docker

Runs the real Zoom (Workplace) desktop client in a container with:

- **GUI** — X11 passthrough (`/tmp/.X11-unix` + `DISPLAY`)
- **Audio** — PulseAudio and PipeWire sockets forwarded into the container
- **Camera** — `/dev/video0` and `/dev/video1` passed through

## Prerequisites

- Docker with the compose plugin (`docker compose version`)
- An X11 session (this host is X11; Wayland sessions need XWayland running)
- PulseAudio *or* PipeWire on the host (PipeWire provides a PulseAudio socket)

## Build + run

```sh
cd tools/zoom
./run.sh
```

The script computes your UID/GID and the `audio`/`video` group IDs, exports them
as compose variables, builds the image, and launches Zoom.

## Manual commands

```sh
docker compose build
docker compose run --rm zoom
```

## Files

| File | Purpose |
|------|---------|
| `Dockerfile` | Ubuntu 24.04 base, Zoom `.deb`, GUI/audio/GL deps, non-root user |
| `docker-compose.yml` | socket/device mounts, env, user, groups, `ipc: host`, `shm` |
| `run.sh` | computes UID/GID/audio-gid/video-gid and launches |

## Config

Edit `docker-compose.yml` if your distro uses different GIDs than the Debian
defaults (`audio=29`, `video=44`):

```sh
getent group audio   # -> audio:x:29:
getent group video   # -> video:x:44:
```

Set them with `AUDIO_GID` / `VIDEO_GID` env vars, or edit the defaults.

## Notes / troubleshooting

- **No audio in the meeting:** confirm the pulse socket path matches. The
  container expects `PULSE_SERVER=unix:/run/user/<uid>/pulse/native`. If you use
  PipeWire, ensure the PipeWire Pulse module (`pipewire-pulse`) is running on
  the host — it exposes `/run/user/<uid>/pulse/native`.
- **No video:** make sure your user is in the `video` group on the host
  (`groups`), and that `VIDEO_GID` matches (`getent group video`). Zoom may
  also prompt for the camera; pick `/dev/video0`.
- **Black window / compositing issues:** `ipc: host` and `shm_size: 2gb` help
  with GL/Chromium rendering; `QT_X11_NO_MITSHM=1` avoids MIT-SHM shared-memory
  conflicts over the forwarded socket.
- **Wayland only:** run with XWayland. In the future this image could switch to
  a Wayland+VNC arrangement, but for now X11 passthrough is the supported path.

## Security

Runs as a non-root user at the host UID. Still, forwarding X11, the pulse
socket, and the webcam into any container widens the container's access to your
desktop — only run trusted images. See the Docker docs on `--cap-add` and
`--security-opt seccomp:unconfined`; `SYS_PTRACE`/`seccomp:unconfined` are here
for Zoom's DRM/Chromium stack but reduce sandboxing.
