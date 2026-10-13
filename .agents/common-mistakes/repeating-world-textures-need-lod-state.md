# Repeating world textures need prefiltered reduction and synchronized LOD state

## Symptom

World textures shimmer or flicker as the camera tilts across large surfaces.

## Cause

High-frequency repeated UVs minify below one texel per pixel. Point sampling
without mip levels aliases. Also, reducing a large source image to the base
32x32 tile with nearest-neighbor bakes alias patterns into the base and every
generated mip. Separately, whole-map UVs are unwrapped and packed into signed
s10.5 coordinates. Int16 overflow preserves repeated sample phase, but
corrupts interpolated gradients used for perspective and mip LOD; a face that
crosses ±1024 texels can therefore shimmer even with valid mip assets. Also,
`rdpq_sprite_upload` in the installed libdragon preview passes the number of
appended mip images to `rdpq_mode_mipmap`, while that API expects the total
level count including the base image. Its internal count-to-max-level conversion
then leaves the deepest uploaded LOD unreachable. Finally, tiny3d snapshots RDP
LOD mode into draw flags, so flags must be reasserted after setting the complete
mipmap count.

## Fix

Reduce opaque repeating world sources with a box prefilter, generate a bounded
box-filter mip chain, and use bilinear filtering. After `rdpq_sprite_upload`,
reapply `rdpq_mode_mipmap` with `sprite_get_lod_count(sprite)` (which includes
the base level), then call `t3d_state_set_drawflags` so tiny3d captures the full
LOD range. Before writing each clipped face, shift repeat-axis UVs by whole
texture periods so s10.5 interpolation stays representable; saturate clamp
axes to the signed range and fail the bake if a repeating face span cannot fit.
Keep each chain within 4 KiB TMEM; do not box-filter or mip straight-alpha
decals without alpha-aware filtering.

## Guardrail

Run `tests/texture_uv_rebase_smoke.py` and
`tests/mipmap_upload_contract.py`: prove packed UV bounds and ensure both
textured paths enable all sprite LOD levels before tiny3d snapshots draw flags.
Inspect packaged LOD dimensions and aggregate TMEM bytes in the material
budget test. Confirm in Ares by rotating the camera over snow and rock surfaces.
