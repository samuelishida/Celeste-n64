# Convert OG ceiling feet target to demake center position

OG `Player.Position` is feet-anchored. In the N64 demake,
`PlayerState.position` is center-anchored and feet are
`position.y - half_height`.

For ceiling contact, OG target is `feet_y = hit.y - ceiling_height`. Do not
assign that directly to `PlayerState.position.y`; convert it back:
`center_y = hit.y - ceiling_height + half_height`. Otherwise every ceiling hit
teleports Madeline downward by one full half-height even though velocity is
correctly canceled.

Regression test: ceiling-only mesh, known foot height, assert post-popout feet
remain exactly `hit.y - ceiling_height`.
