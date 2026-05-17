# Asset Budget

Milestone 0 intentionally uses placeholders only.

## Placeholder assets

- player: one cube
- collectible: one cube or simple billboard
- level: one platform mesh
- materials: flat colors only

## First real-art targets

| Asset | Budget |
| --- | --- |
| player model | low-poly, readable silhouette first |
| test room | small single-room mesh |
| textures | tiny atlas, few materials |
| animation | idle, run, jump/fall, dash, climb |

## Guardrails

- favor vertex color before texture detail
- keep the first authored room compact
- condition first-room 3D materials to `32x32 RGBA16` before ROM use
- avoid custom shaders or dynamic-lighting dependencies
- add art only after movement is already fun
