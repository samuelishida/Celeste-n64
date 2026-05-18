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
    filesystem/mdl/coin.t3dm \
    filesystem/mdl/spring_board.t3dm \
    filesystem/mdl/refill_gem.t3dm \
    filesystem/mdl/refill_gem_double.t3dm \
    filesystem/mdl/first-room.t3dm

DFS_TEX_FILES := \
    filesystem/tex/bubble.sprite \
    filesystem/tex/rock_1.sprite \
    filesystem/tex/snow_1.sprite \
    filesystem/tex/rock_2.sprite \
    filesystem/tex/metal_floor_1.sprite \
    filesystem/tex/floor_dirty_concrete.sprite \
    filesystem/tex/TB_empty.sprite

DFS_FNT_FILES := filesystem/fnt/Renogare.font64

# DFS level files
DFS_LVL_FILES := \
    filesystem/lvl/1-1.lvl \
    filesystem/lvl/1-1.manifest \
    filesystem/lvl/1-1.colmesh \
    filesystem/lvl/first-room.lvl \
    filesystem/lvl/first-room.manifest \
    filesystem/lvl/first-room.colmesh

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
filesystem/lvl/1-1.lvl filesystem/lvl/1-1.manifest: \
	assets/og_converted/maps/1-1.map | filesystem/lvl
	python3 tools/bake_map.py $< \
		filesystem/lvl/1-1.lvl filesystem/lvl/1-1.manifest

# Copy font files
filesystem/fnt/%.font64: assets/og_converted/fonts/%.font64 | filesystem/fnt
	cp $< $@

# Build DFS file
madeline_cube_rom.dfs: $(DFS_MDL_FILES) $(DFS_TEX_FILES) $(DFS_FNT_FILES) $(DFS_LVL_FILES)
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
	src/user/gameplay/runtime/timing.cpp \
	src/user/gameplay/physics/geom.cpp \
	src/user/gameplay/physics/coll_mesh.cpp \
	src/user/gameplay/world/world.cpp \
	src/user/gameplay/world/room_data.cpp \
	src/user/gameplay/save_system.cpp \
	src/user/gameplay/actor/strawberry_actor.cpp \
	src/user/gameplay/actor/refill_actor.cpp \
	src/user/gameplay/actor/spring_actor.cpp \
	src/user/gameplay/render/model.cpp \
	src/user/gameplay/render/static_room_model.cpp \
	src/user/gameplay/render/texture.cpp \
	src/user/gameplay/render/material_catalog.cpp \
	src/user/gameplay/render/level_renderer.cpp \
	src/user/gameplay/world/level_loader.cpp \
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

.PHONY: all clean bake-colmesh bake-colmesh-all
