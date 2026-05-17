#!/usr/bin/env bash
set -euo pipefail
: "${N64_INST:?set N64_INST to the libdragon install prefix}"
PATH="$N64_INST/bin:$PATH" make
printf 'ROM ready: madeline_cube_rom.z64\n'
printf 'Run docs/physics_feel_acceptance.md on controller before freezing constants.\n'
