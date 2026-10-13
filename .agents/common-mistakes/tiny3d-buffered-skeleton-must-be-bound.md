# Bind Tiny3D skeleton before buffered skinned draw

When `T3DSkeleton::bufferCount > 1`, `t3d_model_draw_skinned()` reads joint
matrices through Tiny3D's skeleton segment placeholder. Call
`t3d_skeleton_use(&skeleton)` before every skinned draw so that segment points
at the active display-buffer matrices. Updating the skeleton alone does not
bind its matrices; without the bind, the player can disappear or render with
invalid pose data.

Tiny3D API: https://github.com/HailToDodongo/tiny3d
