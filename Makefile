BUILD_DIR=build
# Requires libdragon preview branch: fgeom.h (fm_vec3_t) is not in trunk.
# https://github.com/DragonMinded/libdragon/tree/preview
# Default: /tmp/n64-bootstrap (setup.sh installs here; ext4 supports symlinks)
N64_INST ?= /tmp/n64-bootstrap/opt/libdragon
export N64_INST

# Default goal must be set before including n64.mk, which defines its own
# pattern rules that can shadow our targets.
.DEFAULT_GOAL := all

include $(N64_INST)/include/n64.mk
include $(N64_INST)/include/t3d.mk

N64_CFLAGS += -std=gnu2x -Os
N64_CXXFLAGS += -std=gnu++17 -Os -Isrc/user

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
    filesystem/tex/snow_1.sprite \
    filesystem/tex/rock_2.sprite \
    filesystem/tex/metal_floor_1.sprite \
    filesystem/tex/floor_dirty_concrete.sprite \
    filesystem/tex/TB_empty.sprite

DFS_FNT_FILES := filesystem/fnt/Renogare.font64

# DFS level files
DFS_LVL_FILES := \
	filesystem/lvl/first-room.lvl \
	filesystem/lvl/first-room.manifest \
	filesystem/lvl/first-room.colmesh \
	filesystem/lvl/1-1.lvl \
	filesystem/lvl/1-1.manifest \
	filesystem/lvl/1-1.colmesh

# Map-pack chunks are added dynamically via wildcard
DFS_MAP_PACK_FILES := $(wildcard filesystem/lvl/forsyken-city/*.lvl) \
                      $(wildcard filesystem/lvl/forsyken-city/*.colmesh) \
                      $(wildcard filesystem/lvl/forsyken-city/*.mappack) \
                      $(wildcard filesystem/lvl/forsyken-city/*.json)

# Combine all DFS files
DFS_ALL_FILES := $(DFS_LVL_FILES) $(DFS_MAP_PACK_FILES)

# Create filesystem directories
filesystem/mdl filesystem/tex filesystem/fnt filesystem/lvl:
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
# LVL2 rooms + a map-pack v2 manifest. This is the ONLY source of the
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
	tools/ogworld/parse.py \
	tools/ogworld/geometry.py \
	tools/ogworld/collision.py \
	tools/ogworld/chunking.py \
	tools/writers/colmesh_world_writer.py \
	tools/writers/lvl_world_writer.py \
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
	@# Copy the staged v2 artifacts to filesystem/lvl/forsyken-city/
	cp $(FORSYKEN_CITY_OUT_DIR)/staging/*.lvl filesystem/lvl/forsyken-city/ 2>/dev/null || true
	cp $(FORSYKEN_CITY_OUT_DIR)/staging/*.colmesh filesystem/lvl/forsyken-city/ 2>/dev/null || true
	cp $(FORSYKEN_CITY_OUT_DIR)/staging/*.mappack filesystem/lvl/forsyken-city/ 2>/dev/null || true

filesystem/lvl/forsyken-city:
	mkdir -p $@

# Convenience target to bake the entire map-pack
bake-forsaken-city: filesystem/lvl/forsyken-city/forsyken-city.mappack
	@echo "Forsaken City map-pack baked successfully"

# OG MAP PIPELINE (ogmap_lib → tools/bake.py)
filesystem/lvl/1-1.lvl filesystem/lvl/1-1.manifest filesystem/lvl/1-1.colmesh: \
	assets/og_converted/maps/1-1.map \
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
	python3 tools/bake.py $< --out-dir build/bake-1-1 --scale 0.2 --fixture-manifest tests/fixtures/1-1.manifest
	@# Rename output files to match expected names
	mv build/bake-1-1/output.lvl filesystem/lvl/1-1.lvl
	mv build/bake-1-1/output.manifest filesystem/lvl/1-1.manifest
	mv build/bake-1-1/output.colmesh filesystem/lvl/1-1.colmesh
	mv build/bake-1-1/output.t3dm filesystem/lvl/1-1.t3dm 2>/dev/null || true
	mv build/bake-1-1/output.nav filesystem/lvl/1-1.nav 2>/dev/null || true
	mv build/bake-1-1/output.report.json filesystem/lvl/1-1.report.json 2>/dev/null || true
	rmdir build/bake-1-1 2>/dev/null || true
# Copy font files
filesystem/fnt/%.font64: assets/og_converted/fonts/%.font64 | filesystem/fnt
	cp $< $@

# Build DFS file
madeline_cube_rom.dfs: $(DFS_MDL_FILES) $(DFS_TEX_FILES) $(DFS_FNT_FILES) $(DFS_ALL_FILES)
	$(N64_INST)/bin/mkdfs $@ filesystem/

src = \
	src/user/rom_main.cpp \
	src/user/gameplay/player/player_controller.cpp \
	src/user/gameplay/player/player_motor.cpp \
	src/user/gameplay/player/camera_controller.cpp \
	src/user/gameplay/world/collectible.cpp \
	src/user/gameplay/world/respawn_system.cpp \
	src/user/gameplay/placeholder_catalog.cpp \
	src/user/gameplay/arena.cpp \
	src/user/gameplay/scene/scene_manager.cpp \
	src/user/gameplay/debug_hud.cpp \
	src/user/gameplay/scene/gameplay_scene.cpp \
	src/user/gameplay/scene/title_scene.cpp \
	src/user/gameplay/runtime/timing.cpp \
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
	src/user/gameplay/render/static_room_model.cpp \
	src/user/gameplay/render/texture.cpp \
	src/user/gameplay/render/material_catalog.cpp \
	src/user/gameplay/render/level_renderer.cpp \
	src/user/gameplay/render/lvl_room_renderer.cpp \
	src/user/gameplay/render/chunk_ring_renderer.cpp \
	src/user/gameplay/render/t3dm_room_renderer.cpp \
	src/user/gameplay/world/level_loader.cpp \
	src/user/gameplay/world/mappack_loader.cpp \
	src/user/gameplay/world/map.cpp \
	src/user/gameplay/world/map_runtime.cpp \
	src/user/gameplay/world/artifact_hash.cpp \
	src/user/gameplay/world/entity_dispatch.cpp \
	src/user/gameplay/world/actor_factory.cpp \
	src/user/gameplay/world/actor_world.cpp \
	src/user/gameplay/actor/moving_solid_actor.cpp \
	src/user/gameplay/rom_telemetry.cpp \
	src/user/n64/profiler.cpp

all: madeline_cube_rom.z64

$(BUILD_DIR)/madeline_cube_rom.elf: $(src:%.cpp=$(BUILD_DIR)/%.o) madeline_cube_rom.dfs

# The .dfs must be a prerequisite of the .z64 target so that n64tool
# includes it in the ROM image via $(filter %.dfs,$^).
madeline_cube_rom.z64: madeline_cube_rom.dfs

N64_ROM_TITLE ?= "Madeline Cube ROM"

clean:
	rm -rf $(BUILD_DIR) madeline_cube_rom.z64 madeline_cube_rom.dfs

filesystem/lvl/%.colmesh: filesystem/lvl/%.lvl tools/colmesh_bake.py tools/lvl_format.py
	python3 tools/colmesh_bake.py $< $@

# Bake collision mesh for a single level.
# Usage: make bake-colmesh LEVEL=1-1
#   Reads  filesystem/lvl/$(LEVEL).lvl
#   Writes filesystem/lvl/$(LEVEL).colmesh
LEVEL ?= 1-1
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

-include $(wildcard $(BUILD_DIR)/*.d)

# Bake the whole Forsaken City map-pack (grid-chunked).
# Usage: make bake-forsaken-city
bake-forsaken-city: filesystem/lvl/forsyken-city/forsyken-city.mappack

.PHONY: all clean bake-colmesh bake-colmesh-all bake-forsaken-city
