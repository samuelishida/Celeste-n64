BUILD_DIR=build
# Requires libdragon preview branch: fgeom.h (fm_vec3_t) is not in trunk.
# https://github.com/DragonMinded/libdragon/tree/preview
# Toolchain bytes are stored in-repo (.n64-bootstrap) but accessed through a
# space-free symlink (default ~/.n64-bootstrap): libdragon tools (n64sym,
# mkfont) build shell commands from the toolchain path and break if it contains
# spaces, and this repo lives under ".../Celeste 64 Demake". setup.sh creates
# the symlink; set N64_BOOTSTRAP to relocate.
N64_BOOTSTRAP ?= $(HOME)/.n64-bootstrap
N64_INST ?= $(N64_BOOTSTRAP)/opt/libdragon
export N64_INST
ASSET_PYTHON ?= $(N64_BOOTSTRAP)/asset-venv/bin/python

# Default goal must be set before including n64.mk, which defines its own
# pattern rules that can shadow our targets.
.DEFAULT_GOAL := all

include $(N64_INST)/include/n64.mk
include $(N64_INST)/include/t3d.mk

# The boot log's [build] marker uses __DATE__/__TIME__ in rom_main.cpp.
# Re-linking after another translation unit changes must not leave that marker
# frozen at the last time rom_main.o happened to compile. Refresh this one TU
# for every ROM build; gameplay/art objects still use ordinary timestamps.
.PHONY: refresh_rom_build_marker
refresh_rom_build_marker:
$(BUILD_DIR)/src/user/rom_main.o: refresh_rom_build_marker

N64_CFLAGS += -std=gnu2x -Os
N64_CXXFLAGS += -std=gnu++17 -Os -Isrc/user

# Original animation clocks and STEP scales survive Blender/Tiny3D conversion.
PLAYER_ANIMATION_SOURCE := src/user/gameplay/render/player_animation_source.hpp
$(PLAYER_ANIMATION_SOURCE): Celeste64-og/Content/Models/player.glb tools/generate_player_animation_source.py tools/glb_assets.py
	$(ASSET_PYTHON) tools/generate_player_animation_source.py --source $< --output $@
$(BUILD_DIR)/src/user/gameplay/render/animated_model.o: $(PLAYER_ANIMATION_SOURCE)
assets/og_converted/models/player.t3dm: tools/generate_player_animation_source.py

# Manifest is source of truth for OG-derived converted assets.  Keep the rule
# before the generated include so GNU Make can materialize it and restart
# parsing.  Use -include so goals that do not build the ROM (e.g. `make test`)
# do not require the asset venv; the ROM targets hard-require it via an
# order-only prerequisite, so a missing venv still stops a ROM build.
OG_ASSET_MANIFEST := assets/og_converted/asset_manifest.json
OG_ASSET_INCLUDE := $(BUILD_DIR)/og_assets.mk
$(OG_ASSET_INCLUDE): $(OG_ASSET_MANIFEST) tools/asset_manifest.py tools/glb_assets.py tools/asset_toolchain.lock.json
	mkdir -p $(BUILD_DIR)
	$(ASSET_PYTHON) tools/asset_manifest.py --manifest $(OG_ASSET_MANIFEST) --write-make $@
-include $(OG_ASSET_INCLUDE)

# DFS assets for baked level
DFS_MDL_FILES := \
    filesystem/mdl/room_fixture.t3dm \
    filesystem/mdl/strawberry.t3dm \
    filesystem/mdl/tape_1.t3dm \
    filesystem/mdl/coin.t3dm \
    filesystem/mdl/spring_board.t3dm \
    filesystem/mdl/refill_gem.t3dm \
    filesystem/mdl/refill_gem_double.t3dm \
    filesystem/mdl/first-room.t3dm

DFS_TEX_FILES := \
    filesystem/tex/rock_1.sprite \
    filesystem/tex/rock_1_climbable.sprite \
    filesystem/tex/rock_1_ice.sprite \
    filesystem/tex/rock_1_oneway.sprite \
    filesystem/tex/rock_1_death.sprite \
    filesystem/tex/snow_1.sprite \
    filesystem/tex/rock_2.sprite \
    filesystem/tex/metal_floor_1.sprite \
    filesystem/tex/floor_dirty_concrete.sprite \
    filesystem/tex/TB_empty.sprite \
    filesystem/tex/girder.sprite \
    filesystem/tex/girder_16.sprite \
    filesystem/tex/girder_v.sprite \
    filesystem/tex/girder_v_16.sprite \
    filesystem/tex/girder_wood.sprite \
    filesystem/tex/rock_3.sprite \
    filesystem/tex/rock_4.sprite \
    filesystem/tex/rock_5.sprite \
    filesystem/tex/wood_1.sprite \
    filesystem/tex/wood_2.sprite \
    filesystem/tex/wood_3.sprite \
    filesystem/tex/wood_4.sprite \
    filesystem/tex/wood_fragile.sprite \
    filesystem/tex/wall_ruined_1.sprite \
    filesystem/tex/wall_ruined_2.sprite \
    filesystem/tex/wall_ruined_3.sprite \
    filesystem/tex/wall_ruined_4.sprite \
    filesystem/tex/wall_ruined_5.sprite \
    filesystem/tex/wall_ruined_6.sprite \
    filesystem/tex/wall_ruined_7.sprite \
    filesystem/tex/road1.sprite \
    filesystem/tex/glass_1.sprite \
    filesystem/tex/special_floor_1.sprite \
    filesystem/tex/big_decals_1.sprite

DFS_FNT_FILES := filesystem/fnt/Renogare.font64

# DFS level files
DFS_LVL_FILES := \
	filesystem/lvl/first-room.lvl \
	filesystem/lvl/first-room.manifest \
	filesystem/lvl/first-room.colmesh

# Map-pack chunks are added dynamically via wildcard
DFS_MAP_PACK_FILES := $(wildcard filesystem/lvl/forsyken-city/*.lvl) \
                      $(wildcard filesystem/lvl/forsyken-city/*.colmesh) \
                      $(wildcard filesystem/lvl/forsyken-city/*.mappack) \
                      $(wildcard filesystem/lvl/forsyken-city/*.manifest) \
                      $(wildcard filesystem/lvl/forsyken-city/*.matmeta) \
                      $(wildcard filesystem/lvl/forsyken-city/*.json) \
                      $(wildcard filesystem/lvl/forsyken-city/*.dsol)

# Combine all DFS files
DFS_ALL_FILES := $(DFS_LVL_FILES) $(DFS_MAP_PACK_FILES)

# Create filesystem directories
filesystem/mdl filesystem/tex filesystem/fnt filesystem/audio filesystem/lvl:
	mkdir -p $@

# Copy model files
filesystem/mdl/%.t3dm: assets/og_converted/models/%.t3dm | filesystem/mdl
	cp $< $@

# Known-good tiny3d fixture used to prove the static room model path independently
# of the Quake-map baker. Use a project-local untextured T3DM so the diagnostic
# has no hidden dependency on source-tree texture paths from external examples.
filesystem/mdl/room_fixture.t3dm: assets/og_converted/models/car_collider.t3dm | filesystem/mdl
	cp $< $@

filesystem/mdl/first-room.t3dm: assets/rooms/first-room/first-room.t3dm | filesystem/mdl
	cp $< $@

# Copy texture files
filesystem/tex/%.sprite: assets/og_converted/textures/%.sprite | filesystem/tex
	cp $< $@

# Bake level files
filesystem/lvl/first-room.lvl filesystem/lvl/first-room.manifest filesystem/lvl/first-room.colmesh: \
	assets/rooms/first-room/first-room.map \
	tools/bake.py \
	tools/ogmap_lib/__init__.py \
	tools/ogmap_lib/brush_geom.py \
	tools/ogmap_lib/texture_mapping.py \
	tools/writers/colmesh_writer.py \
	tools/writers/lvl_writer.py \
	tools/writers/t3dm_writer.py \
	tools/writers/nav_writer.py \
	tools/lvl_format.py \
	tools/entity_ids.py \
	tools/patch_t3dm_materials.py | filesystem/lvl
	python3 tools/bake.py $< --out-dir build/bake-first-room --scale 0.2
	mv build/bake-first-room/output.lvl filesystem/lvl/first-room.lvl
	mv build/bake-first-room/output.manifest filesystem/lvl/first-room.manifest
	mv build/bake-first-room/output.colmesh filesystem/lvl/first-room.colmesh
	mv build/bake-first-room/output.nav filesystem/lvl/first-room.nav 2>/dev/null || true
	rmdir build/bake-first-room 2>/dev/null || true

# FORSAKEN CITY MAP-PACK (whole interconnected map, v2)
# Bakes main 1.map through the canonical IR into one global CMSH + per-cell
# LVL2 rooms + MPP5 manifest and DSOL sidecar. This is the ONLY source of the
# published Forsaken City directory.
# Output: filesystem/lvl/forsyken-city/<chunk>.lvl, forsyken-city.colmesh,
#         forsyken-city.mappack
FORSYKEN_CITY_MAP ?= assets/og_converted/maps/1.map
FORSYKEN_CITY_CHUNK_SIZE ?= 1200
FORSYKEN_CITY_OUT_DIR ?= build/bake-fc-1200

# Pattern rule for the published artifacts
filesystem/lvl/forsyken-city/%.lvl filesystem/lvl/forsyken-city/%.colmesh filesystem/lvl/forsyken-city/%.mappack: \
	$(FORSYKEN_CITY_MAP) \
	tools/bake_interconnected_map.py \
	tools/ogworld/__init__.py \
	tools/ogworld/model.py \
	tools/ogworld/class_policy.py \
	tools/asset_manifest.py \
	tools/ogworld/parse.py \
	tools/ogworld/geometry.py \
	tools/ogworld/collision.py \
	tools/ogworld/wall_order.py \
	tools/ogworld/chunking.py \
	tools/ogworld/distant_lod.py \
	tools/writers/colmesh_world_writer.py \
	tools/writers/dynamic_solid_writer.py \
	tools/writers/lvl_world_writer.py \
	tools/writers/dlod_writer.py \
	tools/dsol_format.py \
	tools/mappack_format.py \
	tools/artifact_hash.py \
	tools/ogmap_lib/__init__.py \
	tools/ogmap_lib/brush_geom.py \
	tools/ogmap_lib/brush_grid.py \
	tools/ogmap_lib/texture_mapping.py \
	tools/lvl_format.py \
	tools/entity_ids.py | filesystem/lvl/forsyken-city
	python3 tools/bake_interconnected_map.py $(FORSYKEN_CITY_MAP) \
		--out-dir $(FORSYKEN_CITY_OUT_DIR) \
		--chunk-size $(FORSYKEN_CITY_CHUNK_SIZE) \
		--scale 0.2 \
		--mappack-id forsyken-city
	@# Wipe the top-level build dir (except staging/ and the report) so no
	@# stale v1-era sidecars (per-cell .colmesh, top-level .lvl, v1 mappack/
	@# chunks.json) leak into the pack after a chunk-size/axis change.
	find $(FORSYKEN_CITY_OUT_DIR) -maxdepth 1 -type f ! -name 'interconnected_report.json' -delete
	@# Wipe the DFS subdir first so no stale (old-axis) chunk files leak into
	@# the pack after a chunk-size/axis change.
	rm -rf filesystem/lvl/forsyken-city/*
	@# Copy the staged MPP5 artifacts to filesystem/lvl/forsyken-city/
	cp $(FORSYKEN_CITY_OUT_DIR)/staging/*.lvl filesystem/lvl/forsyken-city/ 2>/dev/null || true
	cp $(FORSYKEN_CITY_OUT_DIR)/staging/*.colmesh filesystem/lvl/forsyken-city/ 2>/dev/null || true
	cp $(FORSYKEN_CITY_OUT_DIR)/staging/*.mappack filesystem/lvl/forsyken-city/ 2>/dev/null || true
	cp $(FORSYKEN_CITY_OUT_DIR)/staging/*.manifest filesystem/lvl/forsyken-city/ 2>/dev/null || true
	cp $(FORSYKEN_CITY_OUT_DIR)/staging/*.matmeta filesystem/lvl/forsyken-city/ 2>/dev/null || true
	cp $(FORSYKEN_CITY_OUT_DIR)/staging/*.dlod filesystem/lvl/forsyken-city/ 2>/dev/null || true
	cp $(FORSYKEN_CITY_OUT_DIR)/staging/*.dsol filesystem/lvl/forsyken-city/ 2>/dev/null || true
	@# memory-opt Inc 2: compress the SHIPPED pack copies (staging stays raw —
	@# it is the smoke + rollback source). mkasset writes the level-1 (LZ4)
	@# compressed form to the -o dir (same dir = in-place on the shipped copy);
	@# the runtime reads them through the asset layer when kEnableCompressedCells
	@# is on. Flag and bake MUST flip together (a mismatch fails loud via the
	@# CMSH magic canary).
	@for f in filesystem/lvl/forsyken-city/*.lvl filesystem/lvl/forsyken-city/*.colmesh filesystem/lvl/forsyken-city/*.mappack; do \
		[ -f "$$f" ] && $(N64_INST)/bin/mkasset --compress 1 -o filesystem/lvl/forsyken-city "$$f" || true; \
	done

filesystem/lvl/forsyken-city:
	mkdir -p $@

# Convenience target to bake the entire map-pack
bake-forsaken-city: filesystem/lvl/forsyken-city/forsyken-city.mappack
	@echo "Forsaken City map-pack baked successfully"

# Copy font files
filesystem/fnt/%.font64: assets/og_converted/fonts/%.font64 | filesystem/fnt
	cp $< $@

# Build DFS file
madeline_cube_rom.dfs: $(DFS_MDL_FILES) $(DFS_TEX_FILES) $(DFS_FNT_FILES) $(DFS_ALL_FILES) $(OG_ASSET_PACKAGE_FILES) | $(OG_ASSET_INCLUDE)
	$(N64_INST)/bin/mkdfs $@ filesystem/

src = \
	src/user/rom_main.cpp \
	src/user/gameplay/input/input_system.cpp \
	src/user/gameplay/player/player_controller.cpp \
	src/user/gameplay/player/player_movement.cpp \
	src/user/gameplay/player/camera_controller.cpp \
	src/user/gameplay/world/collectible.cpp \
	src/user/gameplay/world/respawn_system.cpp \
	src/user/gameplay/placeholder_catalog.cpp \
	src/user/gameplay/scene/scene_manager.cpp \
	src/user/gameplay/debug_hud.cpp \
	src/user/gameplay/scene/gameplay_scene.cpp \
	src/user/gameplay/scene/title_scene.cpp \
	src/user/gameplay/runtime/timing.cpp \
	src/user/gameplay/runtime/math.cpp \
	src/user/gameplay/physics/geom.cpp \
	src/user/gameplay/physics/coll_mesh.cpp \
	src/user/gameplay/world/world.cpp \
	src/user/gameplay/world/room_data.cpp \
	src/user/gameplay/save_system.cpp \
	src/user/gameplay/actor/strawberry_actor.cpp \
	src/user/gameplay/actor/cassette_actor.cpp \
	src/user/gameplay/actor/refill_actor.cpp \
	src/user/gameplay/actor/spring_actor.cpp \
	src/user/gameplay/render/model.cpp \
	src/user/gameplay/render/animation_manifest.cpp \
	src/user/gameplay/render/animated_model.cpp \
	src/user/gameplay/render/player_hair_renderer.cpp \
	src/user/gameplay/audio/audio_service.cpp \
	src/user/gameplay/render/static_room_model.cpp \
	src/user/gameplay/render/texture.cpp \
	src/user/gameplay/render/material_catalog.cpp \
	src/user/gameplay/render/level_renderer.cpp \
	src/user/gameplay/render/lvl_room_renderer.cpp \
	src/user/gameplay/render/textured_room_renderer.cpp \
	src/user/gameplay/render/open_world_renderer.cpp \
	src/user/gameplay/render/tile_streamer.cpp \
	src/user/gameplay/render/skybox.cpp \
	src/user/gameplay/render/distant_tier_renderer.cpp \
	src/user/gameplay/render/dlod_loader.cpp \
	src/user/gameplay/world/dynamic_solid_geometry.cpp \
	src/user/gameplay/render/dynamic_solid_renderer.cpp \
	src/user/gameplay/render/t3dm_room_renderer.cpp \
	src/user/gameplay/world/level_loader.cpp \
	src/user/gameplay/world/mappack_loader.cpp \
	src/user/gameplay/world/map.cpp \
	src/user/gameplay/world/map_runtime.cpp \
	src/user/gameplay/world/dynamic_solid_world.cpp \
	src/user/gameplay/world/artifact_hash.cpp \
	src/user/gameplay/world/entity_dispatch.cpp \
	src/user/gameplay/world/actor_factory.cpp \
	src/user/gameplay/world/actor_world.cpp \
	src/user/gameplay/actor/moving_solid_actor.cpp \
	src/user/gameplay/rom_telemetry.cpp \
	src/user/n64/profiler.cpp

# Per-TU optimization overrides (n64-optimization Inc 1). The toolchain base is
# -O2 (n64.mk), but the global `N64_CXXFLAGS += ... -Os` above is appended last
# and wins, so every TU currently compiles at -Os. These target-specific
# CXXFLAGS appends add -O2 *after* the -Os for the hot TUs only, so the last
# -O wins and those TUs compile at -O2 while the rest stay at -Os.
$(BUILD_DIR)/src/user/gameplay/render/tile_streamer.o: CXXFLAGS += -O2
$(BUILD_DIR)/src/user/gameplay/render/textured_room_renderer.o: CXXFLAGS += -O2
$(BUILD_DIR)/src/user/gameplay/render/lvl_room_renderer.o: CXXFLAGS += -O2
$(BUILD_DIR)/src/user/gameplay/render/open_world_renderer.o: CXXFLAGS += -O2
$(BUILD_DIR)/src/user/gameplay/player/camera_controller.o: CXXFLAGS += -O2
$(BUILD_DIR)/src/user/gameplay/player/player_movement.o: CXXFLAGS += -O2
$(BUILD_DIR)/src/user/gameplay/render/animated_model.o: CXXFLAGS += -O2
$(BUILD_DIR)/src/user/gameplay/render/player_hair_renderer.o: CXXFLAGS += -O2
# Collision BVH traversal measured separately from diagnostic target searches.
$(BUILD_DIR)/src/user/gameplay/physics/coll_mesh.o: CXXFLAGS += -O2

all: madeline_cube_rom.z64 | $(OG_ASSET_INCLUDE)

$(BUILD_DIR)/madeline_cube_rom.elf: $(src:%.cpp=$(BUILD_DIR)/%.o) madeline_cube_rom.dfs

# Separate Ares-only movement diagnostics build. Its objects and ROM output
# use a distinct stem/path, so it cannot contaminate the normal ROM build.
physics-fps60-autoplay-rom:
	$(MAKE) BUILD_DIR=build/physics-fps60-autoplay PHYSICS_AUTOPLAY=1 PERF_FPS60=1 physics_fps60_autoplay.z64

physics-autoplay-rom:
	$(MAKE) BUILD_DIR=build/physics-autoplay PHYSICS_AUTOPLAY=1 physics_autoplay.z64

# Separate climb-to-mantle route. Wall-jump route remains independently gated.
physics-mantle-autoplay-rom:
	$(MAKE) BUILD_DIR=build/physics-mantle-autoplay PHYSICS_AUTOPLAY=1 PHYSICS_AUTOPLAY_MANTLE=1 physics_mantle_autoplay.z64

# No-teleport route from Start through a real map climb corner.
physics-corner-autoplay-rom:
	$(MAKE) BUILD_DIR=build/physics-corner-autoplay PHYSICS_AUTOPLAY=1 PHYSICS_AUTOPLAY_CORNER=1 physics_corner_autoplay.z64

# Separate live-controller trace build. Unlike physics-autoplay, it leaves
# input live and enables sampled input/state/contact telemetry.
physics-input-trace-rom:
	$(MAKE) BUILD_DIR=build/physics-input-trace INPUT_TRACE=1 physics_input_trace.z64

ifeq ($(PHYSICS_AUTOPLAY),1)
$(BUILD_DIR)/src/user/gameplay/scene/gameplay_scene.o: CXXFLAGS += -DMC_DEBUG_PHYSICS_AUTOPLAY
endif

ifeq ($(PHYSICS_AUTOPLAY_MANTLE),1)
$(BUILD_DIR)/src/user/gameplay/scene/gameplay_scene.o: CXXFLAGS += -DMC_DEBUG_PHYSICS_AUTOPLAY_MANTLE
endif

ifeq ($(PHYSICS_AUTOPLAY_CORNER),1)
$(BUILD_DIR)/src/user/gameplay/scene/gameplay_scene.o: CXXFLAGS += -DMC_DEBUG_PHYSICS_AUTOPLAY_CORNER
endif

ifeq ($(PERF_FPS60),1)
$(BUILD_DIR)/src/user/rom_main.o: CXXFLAGS += -DMC_PERF_FPS60
endif

ifeq ($(INPUT_TRACE),1)
$(BUILD_DIR)/src/user/gameplay/scene/gameplay_scene.o: CXXFLAGS += -DMC_DEBUG_INPUT_RECORDING
$(BUILD_DIR)/src/user/gameplay/rom_telemetry.o: CXXFLAGS += -DMC_DEBUG_INPUT_RECORDING
endif

physics_fps60_autoplay.z64: N64_ROM_TITLE = "Physics FPS60 Autoplay"
physics_fps60_autoplay.z64: $(BUILD_DIR)/physics_fps60_autoplay.elf madeline_cube_rom.dfs
$(BUILD_DIR)/physics_fps60_autoplay.elf: $(src:%.cpp=$(BUILD_DIR)/%.o)

physics_autoplay.z64: N64_ROM_TITLE = "Physics Autoplay"
physics_autoplay.z64: $(BUILD_DIR)/physics_autoplay.elf madeline_cube_rom.dfs
$(BUILD_DIR)/physics_autoplay.elf: $(src:%.cpp=$(BUILD_DIR)/%.o)

physics_mantle_autoplay.z64: N64_ROM_TITLE = "Physics Mantle Autoplay"
physics_mantle_autoplay.z64: $(BUILD_DIR)/physics_mantle_autoplay.elf madeline_cube_rom.dfs
$(BUILD_DIR)/physics_mantle_autoplay.elf: $(src:%.cpp=$(BUILD_DIR)/%.o)

physics_corner_autoplay.z64: N64_ROM_TITLE = "Phys Corner Autoplay"
physics_corner_autoplay.z64: $(BUILD_DIR)/physics_corner_autoplay.elf madeline_cube_rom.dfs
$(BUILD_DIR)/physics_corner_autoplay.elf: $(src:%.cpp=$(BUILD_DIR)/%.o)

physics_input_trace.z64: N64_ROM_TITLE = "Physics Input Trace"
physics_input_trace.z64: $(BUILD_DIR)/physics_input_trace.elf madeline_cube_rom.dfs
$(BUILD_DIR)/physics_input_trace.elf: $(src:%.cpp=$(BUILD_DIR)/%.o)

# The .dfs must be a prerequisite of the .z64 target so that n64tool
# includes it in the ROM image via $(filter %.dfs,$^).
madeline_cube_rom.z64: madeline_cube_rom.dfs

N64_ROM_TITLE ?= "Madeline Cube ROM"

clean:
	rm -rf $(BUILD_DIR) madeline_cube_rom.z64 madeline_cube_rom.dfs physics_autoplay.z64 physics_mantle_autoplay.z64 physics_input_trace.z64

filesystem/lvl/%.colmesh: filesystem/lvl/%.lvl tools/colmesh_bake.py tools/lvl_format.py
	python3 tools/colmesh_bake.py $< $@

# Bake collision mesh for a single level.
# Usage: make bake-colmesh LEVEL=first-room
#   Reads  filesystem/lvl/$(LEVEL).lvl
#   Writes filesystem/lvl/$(LEVEL).colmesh
LEVEL ?= first-room
bake-colmesh: filesystem/lvl/$(LEVEL).lvl
	python3 tools/colmesh_bake.py filesystem/lvl/$(LEVEL).lvl filesystem/lvl/$(LEVEL).colmesh
	python3 tools/colmesh_dump.py filesystem/lvl/$(LEVEL).colmesh

# Bake collision meshes for all levels listed in DFS_LVL_FILES.
bake-colmesh-all:
	@for lvl_path in $(DFS_LVL_FILES); do \
	  case $$lvl_path in *.lvl) \
	    stem=$$(basename $$lvl_path .lvl); \
	    python3 tools/colmesh_bake.py $$lvl_path filesystem/lvl/$$stem.colmesh && \
	    python3 tools/colmesh_dump.py filesystem/lvl/$$stem.colmesh; \
	  esac \
	done

# Header dependency files are emitted next to each .o under $(BUILD_DIR)/src,
# so the flat wildcard would miss them (causing stale-layout links — a real
# heap-corruption bug class). Find them recursively instead.
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

# Bake the whole Forsaken City map-pack (grid-chunked).
# Usage: make bake-forsaken-city
bake-forsaken-city: filesystem/lvl/forsyken-city/forsyken-city.mappack

# Host-side test runner (Inc 7). Runs all host C++ + Python tests with one
# command. No N64 toolchain needed.
test:
	./tests/run_host_tests.sh

.PHONY: all clean bake-colmesh bake-colmesh-all bake-forsaken-city test physics-autoplay-rom physics-mantle-autoplay-rom physics-corner-autoplay-rom physics-input-trace-rom


.PHONY: physics-fps60-autoplay-rom
