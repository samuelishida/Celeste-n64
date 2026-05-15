BUILD_DIR=build
include $(N64_INST)/include/n64.mk
include $(N64_INST)/include/t3d.mk

N64_CFLAGS += -std=gnu2x -Os
N64_CXXFLAGS += -std=gnu++17 -Os -Isrc/user

src = \
	src/user/rom_main.cpp \
	src/user/gameplay/player_controller.cpp \
	src/user/gameplay/camera_controller.cpp \
	src/user/gameplay/collectible.cpp \
	src/user/gameplay/respawn_system.cpp \
	src/user/gameplay/placeholder_catalog.cpp \
	src/user/gameplay/arena.cpp \
	src/user/gameplay/scene_manager.cpp \
	src/user/gameplay/debug_hud.cpp \
	src/user/gameplay/gameplay_scene.cpp \
	src/user/gameplay/world.cpp \
	src/user/gameplay/room_data.cpp \
	src/user/gameplay/save_system.cpp \
	src/user/n64/profiler.cpp

all: madeline_cube_rom.z64

$(BUILD_DIR)/madeline_cube_rom.elf: $(src:%.cpp=$(BUILD_DIR)/%.o)

madeline_cube_rom.z64: N64_ROM_TITLE="Madeline Cube ROM"

clean:
	rm -rf $(BUILD_DIR) madeline_cube_rom.z64

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean

