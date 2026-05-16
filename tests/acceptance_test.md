# Acceptance Test Script

## Prerequisites

- ROM built: `madeline_cube_rom.z64`
- Emulator: mupen64plus or Ares
- Controller: N64-compatible gamepad

## Test Cases

### TC1: Boot and Render
1. Launch ROM in emulator
2. **Expected:** Blue sky background, green platform, blue player cube visible
3. **Pass:** Scene renders without crash

### TC2: Movement
1. Move analog stick left/right
2. **Expected:** Player moves in corresponding direction
3. Press A
4. **Expected:** Player jumps
5. Press B in air
6. **Expected:** Player dashes

### TC3: Coyote Time
1. Walk off platform edge
2. Press A immediately after leaving ground
3. **Expected:** Jump still works briefly after leaving platform

### TC4: Jump Buffering
1. Press A just before landing
2. **Expected:** Jump executes on landing

### TC5: Variable Jump
1. Press A and hold
2. **Expected:** Full jump height
3. Press A and release quickly
4. **Expected:** Shorter jump

### TC6: Wall Grab
1. Jump toward left or right wall
2. Hold A while falling past wall
3. **Expected:** Player grabs wall and slides down

### TC7: Wall Jump
1. Grab wall
2. Press A
3. **Expected:** Player launches away from wall

### TC7b: Dedicated Climb
1. Jump toward a wall
2. Hold Z while touching the wall
3. **Expected:** Player enters climb state instead of needing A-held wall slide

### TC8: Collectibles
1. Move player to red cube (strawberry)
2. **Expected:** Cube disappears
3. Move to cyan cube (refill)
4. **Expected:** Cube disappears, dash refills

### TC9: Respawn
1. Walk off platform and fall
2. **Expected:** Player respawns at checkpoint

### TC10: Frame Time
1. Check emulator debug output
2. **Expected:** `[profiler]` lines show ~16.6ms average

### TC11: Memory
1. Check emulator debug output
2. **Expected:** `[memory]` lines show used/free bytes

## Hardware QA

- Run all TC1-TC11 on real N64 hardware (if available)
- Verify no crashes during 10-minute soak test
- Verify save/load works across power cycles

## Known Limitations

- Only one graybox room (Forsaken City start)
- No final art (all placeholder geometry)
- No audio
- No story/cutscenes
- No menus
