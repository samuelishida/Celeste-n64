# Zoom Workplace desktop client in a container
# GUI via X11 passthrough, audio via PulseAudio/PipeWire, camera via /dev/video*
#
# The container runs as a non-root user whose UID/GID match the host user
# (default 1000:1000) so X11 and the pulse socket permissions line up.
ARG UBUNTU=24.04
FROM ubuntu:${UBUNTU}

ARG UID=1000
ARG GID=1000
ARG USERNAME=zoom

ENV DEBIAN_FRONTEND=noninteractive

# --- Build-time deps: Zoom's GUI, audio, and GL stack -------------------------
RUN apt-get update && apt-get install -y --no-install-recommends \
        curl ca-certificates wget gnupg \
        x11-utils x11-xserver-utils libx11-xcb1 libxkbcommon-x11-0 \
        libgl1 libegl1 libglib2.0-0 libglib2.0-bin libgl1-mesa-dri \
        libasound2t64 libpulse0 pulseaudio-utils \
        libfontconfig1 libfreetype6 libharfbuzz0b \
        libxrender1 libxss1 libxtst6 libxxf86vm1 \
        libqt5gui5 libqt5network5 libqt5widgets5 \
        desktop-file-utils dbus dbus-x11 \
        fonts-dejavu-core fonts-liberation fonts-noto-core \
    && rm -rf /var/lib/apt/lists/*

# --- Zoom Workplace client (latest .deb from Zoom) ---------------------------
RUN wget -q "https://zoom.us/client/latest/zoom_amd64.deb" -O /tmp/zoom.deb \
    && (dpkg -i /tmp/zoom.deb || apt-get install -f -y) \
    && rm -f /tmp/zoom.deb

# --- Non-root user matching the host UID/GID --------------------------------
RUN groupadd --gid ${GID} ${USERNAME} \
    && useradd --uid ${UID} --gid ${GID} --create-home --shell /bin/bash ${USERNAME}

USER ${USERNAME}
ENV HOME=/home/${USERNAME}

# PulseAudio + XDG runtime dir the app expects inside the container.
ENV XDG_RUNTIME_DIR=/tmp
ENV PULSE_SERVER=unix:/tmp/pulse-native

WORKDIR /home/${USERNAME}

CMD ["/usr/bin/zoom"]
