
# N64 Celeste64 — Player Movement Controller Specification

## 1. Purpose

Implement a faithful N64-native recreation of the **Celeste 64 player movement controller**.

The objective is **gameplay fidelity**, not source-code fidelity.

The original Celeste64 implementation uses a monolithic player state machine containing:

* Normal movement
* Ground acceleration/deceleration
* Air control
* Jumping
* Variable jump height
* Coyote time
* Wall jumping
* Dashing
* Dash jumping
* Skidding
* Climbing
* Spring launching
* Feather movement
* Collision resolution
* Ground snapping
* Camera-relative movement

The N64 implementation must reproduce the important movement behavior while using an architecture appropriate for Nintendo 64 hardware.

Reference implementation:

`EXOK/Celeste64/Source/Actors/Player.cs`

The original implementation defines movement constants including acceleration, maximum speed, friction, gravity, jump speed, coyote time, wall-jump speed, dash speed and dash duration.

---

# 2. Target Platform

## Hardware

Nintendo 64:

* 93.75 MHz NEC VR4300 CPU
* 8 MB RDRAM (Expansion Pak required — `assert_memory_expanded()` at boot)
* MIPS III
* 64-bit registers but 32-bit game-oriented arithmetic should be preferred
* N64 analog controller
* Fixed 60 Hz target simulation
* No floating-point requirement for gameplay

## Primary requirement

The controller must be deterministic and inexpensive enough to run comfortably at:

```text
60 gameplay updates / second
```

The movement system must not allocate memory during normal gameplay.

---

# 3. Controller Philosophy

The controller should feel like **Celeste**, not like conventional N64 3D movement.

Important characteristics:

1. Immediate analog response.
2. Strong ground acceleration.
3. Limited air control.
4. Momentum preservation.
5. Directional turning at high speed.
6. Variable-height jumping.
7. Coyote time.
8. Dash with very high horizontal/vertical velocity.
9. Wall jump.
10. Climbing.
11. Collision correction rather than physics-engine simulation.
12. Camera-relative movement.

Do NOT implement movement as:

```text
velocity = input * speed
```

Movement must be acceleration-based.

---

# 4. Coordinate System

Use:

```text
X = world horizontal axis
Y = world horizontal axis
Z = vertical axis
```

Player velocity:

```c
typedef struct {
    fx32 x;
    fx32 y;
    fx32 z;
} Vec3Fx;
```

Where `fx32` is the project's fixed-point type.

Recommended initial format:

```text
16.16 signed fixed point
```

Example:

```text
1.0  = 65536
64.0 = 4194304
```

If the existing engine already uses another fixed-point representation, use the existing representation consistently.

Do not introduce floating-point gameplay calculations unless required by existing engine infrastructure.

---

# 5. Fixed Timestep

Movement simulation must run at a fixed timestep.

Target:

```text
60 Hz
```

Conceptually:

```c
void Player_Update(void)
{
    read_controller();

    update_timers();

    update_state();

    apply_velocity();

    resolve_collisions();

    update_ground_state();

    update_animation();
}
```

Do not scale the fundamental movement constants according to arbitrary frame duration.

The N64 version should behave identically regardless of rendering workload.

---

# 6. Player Representation

```c
typedef struct {
    Vec3Fx position;
    Vec3Fx velocity;

    Vec2Fx facing;
    Vec2Fx targetFacing;

    Vec3Fx groundNormal;

    uint8_t onGround;

    uint8_t dashes;

    PlayerState state;

    Fixed tCoyote;
    Fixed tJumpHold;
    Fixed tDash;
    Fixed tDashCooldown;
    Fixed tDashResetCooldown;
    Fixed tClimbCooldown;

    uint8_t autoJump;

    uint8_t wasOnGround;

} Player;
```

Do not store unnecessary rendering state inside the gameplay controller.

Rendering should consume player state.

---

# 7. Player Collision Shape

Use a simple vertical capsule/cylinder approximation.

Recommended gameplay dimensions:

```text
radius = 5 units
height = 10 units
```

Maintain two important collision points:

```text
waist:
    position + (0,0,3)

head:
    position + (0,0,10)
```

The original controller performs ground, ceiling and wall tests around these regions.

For the N64 implementation, use:

```text
GroundCheck()
CeilingCheck()
WallCheck()
```

instead of a general-purpose physics engine.

---

# 8. Player States

Implement the following state machine:

```c
typedef enum {
    PLAYER_NORMAL,
    PLAYER_DASHING,
    PLAYER_SKIDDING,
    PLAYER_CLIMBING,
    PLAYER_DEAD,
    PLAYER_RESPAWNING
} PlayerState;
```

Additional states can be added later:

```text
PLAYER_FEATHER
PLAYER_SPRING
PLAYER_CUTSCENE
PLAYER_BUBBLE
```

Do not implement these until core movement is working.

---

# 9. Core Movement Constants

Initial values should reproduce the original Celeste64 controller.

```c
#define PLAYER_ACCELERATION          500
#define PLAYER_PAST_MAX_DECEL         60

#define PLAYER_AIR_ACCEL_MIN         0.5
#define PLAYER_AIR_ACCEL_MAX         1.0

#define PLAYER_MAX_SPEED              64

#define PLAYER_ROTATE_THRESHOLD      12.8

#define PLAYER_ROTATE_SPEED           1.5 * TAU
#define PLAYER_ROTATE_SPEED_FAST      0.6 * TAU

#define PLAYER_FRICTION               800
#define PLAYER_AIR_FRICTION_MULT      0.1

#define PLAYER_GRAVITY                600
#define PLAYER_MAX_FALL              -120

#define PLAYER_HALF_GRAV_THRESHOLD    100

#define PLAYER_JUMP_HOLD_TIME         0.10
#define PLAYER_JUMP_SPEED             90
#define PLAYER_JUMP_XY_BOOST           10

#define PLAYER_COYOTE_TIME             0.12

#define PLAYER_WALL_JUMP_SPEED        83.2
```

These values come directly from the original Celeste64 player implementation.

Because the N64 implementation may use a different world scale, create a single scale factor:

```c
PLAYER_WORLD_SCALE
```

Do not modify individual constants throughout the code.

---

# 10. Analog Stick Input

The N64 analog stick provides signed X/Y input.

Normalize it into:

```text
[-1.0, +1.0]
```

with a dead zone.

Recommended:

```text
deadzone = 8/80 approximately
```

The exact value should be configurable.

Input structure:

```c
typedef struct {
    int8_t x;
    int8_t y;

    bool jumpPressed;
    bool jumpHeld;

    bool dashPressed;
    bool dashHeld;

    bool climbHeld;
} ControllerInput;
```

---

# 11. Camera-Relative Movement

Movement direction must be relative to the camera.

The original implementation derives the horizontal camera forward vector and transforms controller input into world-space movement.

Implement:

```c
Vec2Fx Player_GetRelativeInput(Player *player)
{
    Vec2Fx forward;
    Vec2Fx right;

    forward = Camera_GetForwardXY();

    right = Perpendicular(forward);

    input = NormalizeAnalogInput();

    return forward * input.y +
           right   * input.x;
}
```

The result should be normalized.

### Important

Do not rotate the player directly according to the controller.

The controller produces:

```text
desired movement direction
```

The player gradually turns toward it.

---

# 12. Ground Movement

When:

```text
onGround == true
```

apply strong acceleration.

### No input

Apply friction:

```text
velocityXY = Approach(
    velocityXY,
    zero,
    FRICTION * dt
)
```

### Input

Calculate:

```text
desiredSpeed = MAX_SPEED * analogMagnitude
```

Then accelerate:

```text
velocityXY = Approach(
    velocityXY,
    inputDirection * desiredSpeed,
    ACCELERATION * dt
)
```

Do not instantly set velocity.

---

# 13. Analog Magnitude

The original controller does not simply use raw stick magnitude.

Use:

```text
magnitude < 0.4
    => approximately 30% movement

magnitude >= 0.92
    => 100% movement
```

Conceptually:

```c
float mag = map(
    stickMagnitude,
    0.4,
    0.92,
    0.3,
    1.0
);

mag = clamp(mag, 0.0, 1.0);
```

Then:

```text
maxSpeed = PLAYER_MAX_SPEED * mag;
```

This preserves analog walking/running behavior from Celeste64.

---

# 14. High-Speed Movement

When:

```text
speed > ROTATE_THRESHOLD
```

do not directly accelerate toward the target vector.

Instead rotate the velocity direction toward the desired direction.

```c
targetFacing =
    RotateToward(
        targetFacing,
        input,
        rotateSpeed * dt
    );
```

Then:

```c
velocityXY =
    targetFacing *
    Approach(
        velocityXY.length,
        targetSpeed,
        acceleration * dt
    );
```

This is critical to the Celeste feel.

---

# 15. Momentum Preservation

If the player is moving faster than normal maximum speed, do not immediately destroy the momentum.

When:

```text
speed >= MAX_SPEED
```

and the player continues moving generally in the same direction:

```text
acceleration = PAST_MAX_DECEL
```

instead of normal acceleration.

This allows:

```text
dash -> landing -> running
```

to preserve momentum.

The original controller explicitly implements this behavior.

---

# 16. Skidding

If the player is moving quickly and pushes strongly in the opposite direction:

```text
dot(inputDirection, velocityDirection) <= -0.7
```

enter:

```text
PLAYER_SKIDDING
```

Do not instantly reverse velocity.

Instead:

```text
1. preserve momentum
2. play skid animation
3. decelerate
4. accelerate toward new direction
5. allow skid jump
```

Constants:

```c
SKID_DOT_THRESHOLD     = -0.7
SKIDDING_START_ACCEL   = 300
SKIDDING_ACCEL         = 500
END_SKID_SPEED         = MAX_SPEED * 0.8
```

---

# 17. Air Movement

Air control is weaker than ground movement.

When airborne:

```text
acceleration = ACCELERATION
```

but multiply it depending on the relationship between:

```text
input direction
target facing
```

Approximate:

```text
opposite direction:
    0.5x

same direction:
    1.0x
```

Then:

```c
velocityXY = Approach(
    velocityXY,
    input * MAX_SPEED,
    acceleration * airMultiplier * dt
);
```

Air friction:

```text
AIR_FRICTION_MULT = 0.1
```

The original implementation deliberately makes air friction much weaker than ground friction.

---

# 18. Gravity

Every airborne frame:

```c
velocity.z -= GRAVITY * dt;
```

Clamp:

```c
velocity.z >= MAX_FALL
```

where:

```text
MAX_FALL = -120
```

Do not allow unlimited falling velocity.

---

# 19. Variable Jump Height

Jumping must not simply apply one fixed impulse.

On jump:

```text
velocity.z = JUMP_SPEED
```

Set:

```text
tJumpHold = 0.10
holdJumpSpeed = JUMP_SPEED
```

While:

```text
jumpHeld == true
```

and:

```text
tJumpHold > 0
```

prevent vertical velocity from falling below the hold speed.

Conceptually:

```c
if (jumpHeld && tJumpHold > 0) {
    if (velocity.z < holdJumpSpeed)
        velocity.z = holdJumpSpeed;
}
```

This produces a stronger/longer jump when the button is held.

---

# 20. Reduced Gravity Near Apex

When the jump button remains held and vertical speed is relatively low:

```text
abs(velocity.z) < HALF_GRAV_THRESHOLD
```

apply:

```text
gravity *= 0.5
```

Otherwise:

```text
gravity *= 1.0
```

This gives the player better control near the jump apex.

---

# 21. Coyote Time

The player should still be able to jump shortly after leaving a platform.

Constant:

```text
COYOTE_TIME = 0.12 seconds
```

Whenever grounded:

```c
tCoyote = COYOTE_TIME;
coyoteZ = position.z;
```

Every frame:

```c
tCoyote -= dt;
```

Jump is allowed while:

```text
tCoyote > 0
```

When the jump happens:

```c
position.z = coyoteZ;
tCoyote = 0;
```

This is explicitly present in the original controller.

---

# 22. Ground Detection

After movement:

```c
bool wasGrounded = player->onGround;

player->onGround =
    GroundCheck(
        player,
        &groundNormal
    );
```

If grounded:

```c
tCoyote = COYOTE_TIME;
coyoteZ = position.z;
```

If the player transitions:

```text
airborne -> grounded
```

trigger:

```text
LAND event
```

Landing should also permit dash refill.

---

# 23. Ground Snap

Small gaps caused by discrete movement should not cause accidental airborne frames.

When:

```text
previously grounded
AND
currently airborne
```

perform a downward raycast.

Recommended distance:

```text
5 units
```

If a valid floor is found:

```c
position = hit.position;
onGround = true;
```

This reproduces the ground-snap behavior of the original controller.

---

# 24. Jump

Normal jump:

```c
position.z = coyoteZ;

velocity.z = JUMP_SPEED;

tJumpHold = JUMP_HOLD_TIME;

tCoyote = 0;
```

If there is directional input:

```c
velocity.xy += inputDirection * JUMP_XY_BOOST;
```

Then:

```text
cancel ground snap
```

The original implementation applies this exact horizontal boost concept.

---

# 25. Jump Facing

When jumping with directional input:

```c
targetFacing = inputDirection;
```

Do not instantly rotate the visual model unless necessary.

The model-facing direction should interpolate toward:

```text
targetFacing
```

---

# 26. Dash System

Player starts with:

```text
1 dash
```

Maximum standard:

```text
1 dash
```

Future support may allow:

```text
2 dashes
```

Dash activation:

```c
if (
    dashes > 0 &&
    tDashCooldown <= 0 &&
    dashPressed
) {
    dashes--;
    state = PLAYER_DASHING;
}
```

Constants:

```c
DASH_SPEED          = 140
DASH_END_SPEED_MULT = 0.75
DASH_TIME           = 0.20
DASH_COOLDOWN       = 0.10
DASH_RESET_COOLDOWN = 0.20
```

These values correspond to the original Celeste64 implementation.

---

# 27. Dash Direction

If directional input exists:

```text
dashDirection = relativeMovementInput
```

Otherwise:

```text
dashDirection = targetFacing
```

Set:

```c
velocity =
    dashDirection * DASH_SPEED;
```

The player should immediately accelerate to dash speed.

---

# 28. Dash Rotation

During dash:

```text
DASH_ROTATE_SPEED = 0.3 * TAU
```

Allow limited directional correction.

Do not allow unlimited instantaneous 180-degree rotation.

```c
targetFacing =
    RotateToward(
        targetFacing,
        input,
        DASH_ROTATE_SPEED * dt
    );
```

Update dash velocity after rotating.

---

# 29. Dash End

After:

```text
DASH_TIME = 0.20
```

leave dash state.

If airborne:

```c
velocity *= 0.75;
```

If grounded, preserve normal ground behavior.

The original implementation applies the speed multiplier only when the player is not grounded.

---

# 30. Dash Refill

When landing:

```c
if (dashResetCooldown <= 0)
    dashes = MAX_DASHES;
```

Do not immediately refill repeatedly while standing on the floor.

Use:

```text
DASH_RESET_COOLDOWN = 0.20
```

after dash.

---

# 31. Dash Jump

If the player dashed while grounded and presses jump during the allowed dash window:

```text
Dash Jump
```

Values:

```c
DASH_JUMP_SPEED       = 40
DASH_JUMP_HOLD_SPEED  = 20
DASH_JUMP_HOLD_TIME   = 0.30
DASH_JUMP_XY_BOOST    = 16
```

Dash jump should:

```text
1. end dash
2. apply vertical velocity
3. preserve horizontal momentum
4. consume/reset dash appropriately
5. enable variable jump behavior
```

---

# 32. Wall Detection

Wall collision should be independent of general floor collision.

Perform tests around:

```text
waist
head
```

Use a small push-out distance:

```text
WALL_PUSHOUT_DIST = 3
```

The original controller uses this same conceptual approach.

---

# 33. Wall Jump

When airborne and touching a valid wall:

```text
WallJumpCheck() == true
```

and jump is pressed:

```text
perform wall jump
```

Vertical:

```text
velocity.z = JUMP_SPEED
```

Horizontal:

```text
velocity.xy =
    wallJumpDirection * WALL_JUMP_SPEED
```

Where:

```text
WALL_JUMP_SPEED = MAX_SPEED * 1.3
```

The original implementation sets the wall-jump horizontal velocity directly toward the wall-opposite/facing direction.

---

# 34. Wall Jump Facing

After wall jump:

```text
targetFacing =
    direction away from wall
```

The player should immediately begin moving away from the wall.

---

# 35. Climbing

Climbing should only be available when:

```text
climb button held
AND
valid climbable wall detected
AND
climb cooldown <= 0
```

Constants:

```c
CLIMB_CHECK_DIST       = 4
CLIMB_SPEED            = 40
CLIMB_HOP_UP_SPEED     = 80
CLIMB_HOP_FORWARD_SPEED = 40
CLIMB_HOP_NO_MOVE_TIME = 0.25
```

The original controller contains these values for its climbing system.

---

# 36. Climb Movement

While climbing:

```text
vertical input > 0:
    velocity.z = CLIMB_SPEED

vertical input < 0:
    velocity.z = -CLIMB_SPEED

no vertical input:
    velocity.z = 0
```

Horizontal velocity should be heavily constrained.

The player should remain attached to the wall.

---

# 37. Climb Jump

Jumping from a wall while climbing should:

```text
velocity.z = CLIMB_HOP_UP_SPEED
velocity.xy = wallNormal * CLIMB_HOP_FORWARD_SPEED
```

Then:

```text
leave climbing state
```

---

# 38. Collision Movement

Do not teleport the player directly:

```c
position += velocity * dt;
```

without collision checking.

Use swept/iterative movement.

The original controller uses a `SweepTestMove()` routine that subdivides movement into small steps and repeatedly pops the player out of solid geometry.

N64 implementation:

```c
void Player_Move(Player *p)
{
    Vec3Fx delta = p->velocity * DT;

    Player_SweepMove(p, delta);
}
```

Recommended maximum step:

```text
2 world units
```

Conceptually:

```c
remaining = length(delta);

while (remaining > 0) {

    step = min(remaining, 2);

    position += direction * step;

    ResolveGround();
    ResolveCeiling();
    ResolveWalls();

    remaining -= step;
}
```

---

# 39. Wall Collision Response

When the player hits a wall:

```text
push player outside wall
```

Then remove velocity directed into the wall.

Conceptually:

```c
dot = dot(velocity, wallNormal);

if (dot < 0)
    velocity -= wallNormal * dot;
```

Do not reflect normal movement off walls.

The original controller removes the velocity component pointing into the wall rather than performing a full bounce.

---

# 40. Ceiling Collision

If the player hits a ceiling:

```c
if (velocity.z > 0)
    velocity.z = 0;
```

Then push the player downward/out of the ceiling.

---

# 41. Ground Collision

When landing:

```c
if (velocity.z < 0)
    velocity.z = 0;
```

Snap the player to the floor.

Update:

```text
onGround = true
groundNormal = floorNormal
coyote timer
dash refill
```

---

# 42. Slopes

Ground movement should account for floor slope.

Use:

```text
groundNormal
```

to modify maximum movement speed.

The original controller modifies maximum speed based on the slope normal and desired movement direction.

For the first N64 implementation:

```text
flat ground:
    1.0x speed

moderate downhill:
    up to approximately 1.25x

moderate uphill:
    down to approximately 0.75x
```

Clamp the final multiplier.

Do not implement physically accurate friction.

Gameplay behavior has priority.

---

# 43. Ledge Assist

The original controller contains special logic to avoid the player awkwardly walking directly off ledges.

For the N64 version implement a simplified version:

Before applying ground movement:

```text
sample floor ahead
```

If:

```text
input direction has no floor
```

slightly rotate desired movement toward the nearest valid floor direction.

Search:

```text
-17 degrees
+17 degrees
```

If a nearby floor exists, steer toward it.

Do not allow this system to move the player without input.

---

# 44. Controller Buffering

Input should be sampled once per frame.

Convert button transitions:

```text
Pressed
Held
Released
```

For example:

```c
bool jumpPressed;
bool jumpHeld;
```

Do not call the N64 controller API repeatedly throughout movement logic.

The controller should receive a single immutable input snapshot:

```c
ControllerInput input;
```

---

# 45. Update Order

This ordering is important.

```text
PLAYER UPDATE

1. Read controller

2. Update timers

3. Determine ground/wall state

4. Process state-specific input

5. Apply horizontal movement

6. Process jump

7. Process dash

8. Apply gravity

9. Calculate movement delta

10. Sweep movement

11. Resolve collision

12. Recalculate ground state

13. Handle coyote time

14. Refill dash if appropriate

15. Update facing

16. Update animation state

17. Update camera target
```

Do not arbitrarily reorder these steps.

---

# 46. State Priority

When multiple actions occur during the same frame, use this priority:

```text
DEATH
↓
CUTSCENE
↓
DASH
↓
CLIMB
↓
JUMP / WALL JUMP
↓
NORMAL MOVEMENT
↓
GRAVITY
```

However, collision resolution always occurs after velocity has been calculated.

---

# 47. Normal State Pseudocode

```c
void Player_UpdateNormal(Player *p, ControllerInput *input)
{
    Vec2Fx move = Player_GetRelativeInput(p);

    if (p->onGround)
        Player_GroundMovement(p, move, input);
    else
        Player_AirMovement(p, move, input);

    if (input->climbHeld &&
        p->climbCooldown <= 0 &&
        Player_TryClimb(p))
    {
        p->state = PLAYER_CLIMBING;
        return;
    }

    if (input->dashPressed &&
        p->dashes > 0 &&
        p->dashCooldown <= 0)
    {
        Player_StartDash(p, move);
        return;
    }

    if (input->jumpPressed) {

        if (p->coyoteTime > 0) {
            Player_Jump(p, move);
            return;
        }

        if (Player_WallJumpCheck(p)) {
            Player_WallJump(p);
            return;
        }
    }

    Player_ApplyGravity(p, input);
}
```

---

# 48. Ground Movement Pseudocode

```c
void Player_GroundMovement(
    Player *p,
    Vec2Fx input,
    ControllerInput *controller)
{
    Vec2Fx velocity = p->velocity.xy;

    if (input == ZERO) {

        ApproachVector(
            &velocity,
            ZERO,
            FRICTION * DT
        );

    } else {

        Fixed speed = MAX_SPEED;

        Fixed analog = GetAnalogMagnitude();

        speed *= analog;

        if (LengthSquared(velocity) >
            MAX_SPEED * MAX_SPEED)
        {
            acceleration = PAST_MAX_DECEL;
        }
        else {
            acceleration = ACCELERATION;
        }

        if (Speed(velocity) > ROTATE_THRESHOLD) {

            if (Dot(
                Normalize(input),
                Normalize(velocity)
            ) <= SKID_DOT_THRESHOLD)
            {
                p->state = PLAYER_SKIDDING;
                return;
            }

            p->targetFacing =
                RotateToward(
                    p->targetFacing,
                    input,
                    ROTATE_SPEED * DT
                );

            velocity =
                p->targetFacing *
                Approach(
                    Speed(velocity),
                    speed,
                    acceleration * DT
                );
        }
        else {

            ApproachVector(
                &velocity,
                input * speed,
                acceleration * DT
            );

            p->targetFacing =
                Normalize(input);
        }
    }

    p->velocity.xy = velocity;
}
```

---

# 49. Air Movement Pseudocode

```c
void Player_AirMovement(
    Player *p,
    Vec2Fx input,
    ControllerInput *controller)
{
    if (input == ZERO)
        return;

    Vec2Fx velocity = p->velocity.xy;

    Fixed accel = ACCELERATION;

    Fixed facingDot =
        Dot(
            Normalize(input),
            p->targetFacing
        );

    Fixed airMultiplier =
        Map(
            facingDot,
            -1,
            1,
            AIR_ACCEL_MULT_MIN,
            AIR_ACCEL_MULT_MAX
        );

    accel *= airMultiplier;

    ApproachVector(
        &velocity,
        input * MAX_SPEED,
        accel * DT
    );

    p->velocity.xy = velocity;
}
```

---

# 50. Jump Pseudocode

```c
void Player_Jump(Player *p, Vec2Fx input)
{
    p->position.z = p->coyoteZ;

    p->velocity.z = JUMP_SPEED;

    p->jumpHoldTimer = JUMP_HOLD_TIME;

    p->coyoteTime = 0;

    if (input != ZERO) {

        input = Normalize(input);

        p->targetFacing = input;

        p->velocity.xy +=
            input * JUMP_XY_BOOST;
    }

    Player_CancelGroundSnap(p);
}
```

---

# 51. Dash Pseudocode

```c
void Player_StartDash(
    Player *p,
    Vec2Fx input)
{
    p->dashes--;

    if (input != ZERO)
        p->targetFacing = Normalize(input);

    p->state = PLAYER_DASHING;

    p->dashTimer = DASH_TIME;

    p->dashResetCooldown =
        DASH_RESET_COOLDOWN;

    p->velocity =
        Vec3Fx(
            p->targetFacing.x * DASH_SPEED,
            p->targetFacing.y * DASH_SPEED,
            0
        );
}
```

---

# 52. Dash Update

```c
void Player_UpdateDash(
    Player *p,
    ControllerInput *input)
{
    p->dashTimer -= DT;

    Vec2Fx move =
        Player_GetRelativeInput(p);

    if (move != ZERO &&
        Dot(move, p->targetFacing) >= -0.2)
    {
        p->targetFacing =
            RotateToward(
                p->targetFacing,
                move,
                DASH_ROTATE_SPEED * DT
            );

        p->velocity.xy =
            p->targetFacing * DASH_SPEED;
    }

    if (p->dashTimer <= 0) {

        if (!p->onGround)
            p->velocity.xy *=
                DASH_END_SPEED_MULT;

        p->dashCooldown =
            DASH_COOLDOWN;

        p->state =
            PLAYER_NORMAL;
    }
}
```

---

# 53. Facing

Maintain:

```text
targetFacing
```

and:

```text
actualFacing
```

The actual player model should smoothly approach the target.

```c
actualFacing =
    RotateToward(
        actualFacing,
        targetFacing,
        FACING_ROTATE_SPEED * DT
    );
```

Do not use instantaneous rotation during normal movement.

During dash:

```text
actualFacing = dashDirection
```

---

# 54. Camera

The movement controller should expose:

```c
Vec2Fx Player_GetFacing();
Vec3Fx Player_GetPosition();
Vec3Fx Player_GetVelocity();
```

The camera system should be separate.

However, movement must use:

```c
Camera_GetForwardXY()
```

for relative input.

Do not put the complete camera implementation inside `player.c`.

---

# 55. Animation Interface

Gameplay controller should expose a simple animation state:

```c
typedef enum {
    ANIM_IDLE,
    ANIM_RUN,
    ANIM_JUMP,
    ANIM_FALL,
    ANIM_DASH,
    ANIM_SKID,
    ANIM_CLIMB
} PlayerAnim;
```

Examples:

```text
ground + speed < threshold
    -> IDLE

ground + speed >= threshold
    -> RUN

air + velocity.z > 0
    -> JUMP

air + velocity.z < 0
    -> FALL

state == DASHING
    -> DASH

state == SKIDDING
    -> SKID

state == CLIMBING
    -> CLIMB
```

---

# 56. N64 Optimization Requirements

## No allocations

The following are forbidden inside the update loop:

```text
malloc
free
new
dynamic arrays
```

Use stack variables and preallocated structures.

---

## Fixed point

Prefer:

```text
fixed-point arithmetic
```

for:

* velocity
* position
* acceleration
* timers
* collision distances

Use lookup tables for expensive trigonometric functions.

---

# 57. Fixed-Point Trigonometry

Do not calculate:

```c
sin()
cos()
atan2()
```

every player frame using expensive floating-point functions.

Use:

```c
SinLUT[]
CosLUT[]
```

or the project's existing N64 math library.

Angles should use integer representations.

Example:

```text
0x0000 = 0°
0x4000 = 90°
0x8000 = 180°
0xC000 = 270°
```

---

# 58. Vector Normalization

Avoid expensive square roots whenever possible.

Use:

```c
LengthSquared()
```

for comparisons.

Only normalize when necessary.

For common movement directions, consider:

```text
fast reciprocal sqrt
```

or a lookup table.

---

# 59. Collision Performance

Do not test the entire world against the player.

Broad phase:

```text
player position
+
collision grid/chunk
```

Retrieve only nearby collision geometry.

Then perform:

```text
ground ray
wall rays
ceiling ray
```

against nearby geometry.

---

# 60. Collision Queries

Minimum required API:

```c
bool World_GroundCheck(
    Vec3Fx position,
    Fixed distance,
    CollisionHit *hit
);

bool World_CeilingCheck(
    Vec3Fx position,
    Fixed distance,
    CollisionHit *hit
);

bool World_WallCheck(
    Vec3Fx position,
    Vec2Fx direction,
    Fixed distance,
    CollisionHit *hit
);
```

`CollisionHit`:

```c
typedef struct {
    Vec3Fx point;
    Vec3Fx normal;

    Fixed distance;

    uint16_t material;
    uint16_t flags;
} CollisionHit;
```

---

# 61. Movement Collision Algorithm

Use iterative sweep.

```c
void Player_Move(Player *p)
{
    Vec3Fx delta =
        p->velocity * DT;

    Fixed remaining =
        Length(delta);

    Vec3Fx direction =
        Normalize(delta);

    while (remaining > 0) {

        Fixed step =
            MIN(remaining, COLLISION_STEP);

        p->position +=
            direction * step;

        Player_ResolveGround(p);
        Player_ResolveCeiling(p);
        Player_ResolveWall(p);

        remaining -= step;
    }
}
```

Recommended:

```text
COLLISION_STEP = 2 units
```

---

# 62. Death

Death occurs when:

```text
player.z < deathPlane
```

or:

```text
player overlaps lethal collision
```

Examples:

```text
spikes
death blocks
```

Transition:

```text
NORMAL
    ↓
DEAD
    ↓
RESPAWN
```

Reset:

```text
velocity = 0
dashes = 1
```

---

# 63. Gameplay Invariants

The following must always hold:

```text
velocity.z >= MAX_FALL

dashes >= 0

dashes <= MAX_DASHES

tCoyote >= 0

tDash >= 0

player is never left inside solid geometry

player cannot jump without:
    coyote time
    ground
    wall jump
    special jump

player cannot dash with zero dashes

player cannot climb without a valid wall
```

---

# 64. Debug Mode

Create a compile-time debug option:

```c
#define PLAYER_DEBUG 1
```

When enabled, render:

```text
player collision radius
ground ray
wall rays
ceiling ray
velocity vector
facing vector
target facing vector
ground normal
```

Also display:

```text
State
Velocity
Speed
Dashes
Coyote Timer
Dash Timer
Grounded
```

Example:

```text
STATE: NORMAL
SPD: 61.2
VEL: 54.3, 28.1, -3.2
DASH: 1
GROUND: YES
COYOTE: 0.120
```

This is extremely important for tuning the N64 implementation.

---

# 65. Test Room

Create a dedicated movement test map.

It must contain:

```text
1. Flat floor
2. Long runway
3. Short platform
4. Gap
5. Small ledge
6. Large ledge
7. Wall
8. Two parallel walls
9. Ceiling
10. Slope
11. Downhill slope
12. Uphill slope
13. Spike pit
14. Dash corridor
15. Climbing wall
16. Vertical wall-jump section
```

---

# 66. Required Test Cases

## Test 1 — Standing

Expected:

```text
velocity = 0
state = NORMAL
```

---

## Test 2 — Full Analog

Hold stick fully forward.

Expected:

```text
speed approaches MAX_SPEED
```

without instantly reaching it.

---

## Test 3 — Release

Release analog stick.

Expected:

```text
player decelerates rapidly on ground
player decelerates slowly in air
```

---

## Test 4 — Direction Change

Run right and immediately press left.

Expected:

```text
skid state
momentum decreases
player changes direction
```

No instant velocity inversion.

---

## Test 5 — Jump

Press jump.

Expected:

```text
velocity.z = JUMP_SPEED
```

---

## Test 6 — Short Jump

Tap jump.

Expected:

```text
lower jump height
```

---

## Test 7 — Full Jump

Hold jump.

Expected:

```text
higher jump
```

---

## Test 8 — Coyote

Walk off platform.

Press jump within:

```text
0.12 seconds
```

Expected:

```text
successful jump
```

---

## Test 9 — Late Jump

Walk off platform.

Wait beyond:

```text
0.12 seconds
```

Expected:

```text
jump fails
```

---

## Test 10 — Dash

Press dash.

Expected:

```text
velocity ≈ DASH_SPEED
state = DASHING
duration ≈ 0.20 sec
```

---

## Test 11 — Dash End

Dash through air.

Expected:

```text
velocity reduced to ≈ 75%
```

after dash.

---

## Test 12 — Dash Refill

Land after spending dash.

Expected:

```text
dashes restored
```

---

## Test 13 — Wall Jump

Jump against wall.

Expected:

```text
wall jump succeeds
horizontal velocity away from wall
vertical velocity = JUMP_SPEED
```

---

## Test 14 — Climb

Hold climb against climbable wall.

Expected:

```text
player attaches
vertical movement controlled by stick
```

---

# 67. Architecture

Recommended files:

```text
src/
    player/
        player.c
        player.h

        player_movement.c
        player_movement.h

        player_collision.c
        player_collision.h

        player_state.c
        player_state.h

        player_input.c
        player_input.h

    collision/
        collision.c
        collision.h

    world/
        world.c
        world.h

    camera/
        camera.c
        camera.h

    input/
        controller.c
        controller.h

    math/
        fixed.c
        fixed.h
        trig.c
        trig.h
```

Do not create a giant 2000-line `player.c`.

The original Celeste64 controller is intentionally monolithic, but that architecture should not be copied onto the N64 version.

---

# 68. Separation of Responsibilities

## `player.c`

Owns:

```text
position
velocity
state
high-level update
```

## `player_movement.c`

Owns:

```text
ground movement
air movement
jump
dash
wall jump
climb
skid
gravity
```

## `player_collision.c`

Owns:

```text
ground detection
wall detection
ceiling detection
sweep movement
pushout
ground snap
```

## `player_input.c`

Owns:

```text
N64 controller
deadzone
button transitions
analog normalization
```

## `camera.c`

Owns:

```text
camera orientation
camera distance
camera smoothing
```

---

# 69. Tuning Strategy

Do not tune every value simultaneously.

Implement in this order:

```text
PHASE 1
    collision
    gravity
    ground detection

PHASE 2
    acceleration
    friction
    air control

PHASE 3
    jump
    variable jump
    coyote time

PHASE 4
    dash

PHASE 5
    wall jump

PHASE 6
    skid

PHASE 7
    climbing

PHASE 8
    slope behavior

PHASE 9
    camera

PHASE 10
    animation/audio polish
```

---

# 70. Acceptance Criteria

The controller is considered complete when:

### Movement

* Player accelerates instead of instantly reaching max speed.
* Player decelerates naturally.
* Ground and air friction differ.
* High-speed momentum is preserved.
* Direction changes produce a skid.

### Jump

* Jump has variable height.
* Coyote time works.
* Horizontal jump boost works.
* Gravity behaves correctly.
* Maximum fall speed is enforced.

### Dash

* Dash consumes a charge.
* Dash direction follows camera-relative input.
* Dash lasts approximately 0.2 seconds.
* Dash can slightly rotate.
* Dash retains appropriate momentum after completion.
* Dash refills on landing.

### Wall

* Walls correctly stop the player.
* Wall jump works.
* Wall jump launches away from wall.
* Climbing attaches correctly.

### Collision

* Player never becomes stuck inside geometry.
* Ground snapping works.
* Ceiling collision works.
* Wall pushout works.
* High-speed movement cannot tunnel through thin walls.

### N64

* No allocations during gameplay update.
* Fixed timestep.
* No expensive floating-point trigonometry in the main movement loop.
* Collision queries are spatially limited.
* Controller update remains comfortably within the 60 Hz frame budget.

---

# 71. Important Implementation Rule

Do **not** simplify the controller into conventional 3D platformer physics.

Bad:

```c
velocity.x = input.x * speed;
velocity.y = input.y * speed;

if (jump)
    velocity.z = jumpSpeed;
```

This will not feel like Celeste.

Correct model:

```text
INPUT
  ↓
camera-relative direction
  ↓
acceleration
  ↓
momentum
  ↓
state machine
  ↓
gravity / jump / dash
  ↓
swept movement
  ↓
collision resolution
  ↓
ground state
  ↓
next frame
```

The key design principle is:

> **The player controller is a momentum controller with collision correction, not a physics simulation.**

---

# 72. Reference Constants

Initial N64 tuning table:

| Parameter               |      Value |
| ----------------------- | ---------: |
| Max Speed               |         64 |
| Ground Acceleration     |        500 |
| High-Speed Deceleration |         60 |
| Ground Friction         |        800 |
| Air Friction Multiplier |        0.1 |
| Gravity                 |        600 |
| Max Fall Speed          |       -120 |
| Jump Speed              |         90 |
| Jump Hold               |     0.10 s |
| Jump XY Boost           |         10 |
| Coyote Time             |     0.12 s |
| Wall Jump Speed         |       83.2 |
| Dash Speed              |        140 |
| Dash Duration           |     0.20 s |
| Dash End Multiplier     |       0.75 |
| Dash Cooldown           |     0.10 s |
| Dash Reset Cooldown     |     0.20 s |
| Dash Rotation Speed     | 0.3 × TAU |
| Dash Jump Speed         |         40 |
| Dash Jump Hold Speed    |         20 |
| Dash Jump Hold Time     |     0.30 s |
| Skid Dot Threshold      |       -0.7 |
| Skid Start Acceleration |        300 |
| Skid Acceleration       |        500 |
| End Skid Speed          |       51.2 |
| Skid Jump Speed         |        120 |
| Wall Pushout            |          3 |
| Climb Check Distance    |          4 |
| Climb Speed             |         40 |
| Climb Hop Up            |         80 |
| Climb Hop Forward       |         40 |

These are based on the values in the original `Player.cs`; scale them globally if the N64 world uses different units.

---

# 73. Final Development Goal

The finished N64 controller should allow the player to perform the following sequence naturally:

```text
RUN
 ↓
ACCELERATE
 ↓
JUMP
 ↓
AIR CONTROL
 ↓
DASH
 ↓
LAND
 ↓
RETAIN MOMENTUM
 ↓
SKID
 ↓
JUMP
 ↓
WALL CONTACT
 ↓
WALL JUMP
 ↓
DASH
 ↓
CLIMB
 ↓
CLIMB JUMP
```

The player should feel that these actions belong to **one continuous momentum system**, rather than independent movement mechanics.

That continuous momentum is the primary gameplay requirement.
