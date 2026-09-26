# Sweep contact flags belong to whole move, not each repair substep

`SweepMove` divides one tick's displacement into small steps and calls
`Popout` after every step. `Popout` is a collision repair, not a new tick:
clearing `death_contact` or `ceiling_contact` there erases an earlier hit when
a later substep ends on safe geometry. Reset per-move flags once at
`SweepMove`/`SweepMoveClimb` entry, then only set them during substeps. Keep
death-contact writes in step-up and ground-snap paths OR-only too.

Regression: sweep across a narrow `MAT_DEATH` floor strip, then end on normal
floor. Assert the mover clears the strip and still reports `death_contact` for
that move. Focused test: `move_service_smoke`.
