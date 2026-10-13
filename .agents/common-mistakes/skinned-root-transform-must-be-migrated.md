# Skinned root transforms must survive normalization

## Symptom
An animated model loads and its clips play, but every skin pose is displaced
from the source bind/model space after asset preparation.

## Cause
Removing a transformed ancestor above the skin joints changes every joint's
model matrix. Retaining inverse-bind matrices does not restore the lost common
ancestor. Root-joint animation channels also replace bind translation, so
adjusting only the bind node still loses the offset while clips play.

## Fix
For the supported translation-only case, migrate ancestor translation into
each skin-root bind translation and every root translation animation output.
Fail closed on ancestor rotation/scale/matrix transforms or shared output
accessors that cannot be adjusted unambiguously. Regression-test both bind and
animated poses, then regenerate T3DM and qualify the ROM. Tiny3D rebuilds
inverse-bind matrices and drops scene ancestors, so also restore the source
model's actor-space origin offset in the outer render matrix. Keep that offset
separate from collision physics, and verify its local-right basis after facing
rotation. This does not prove weighted-skin parity; audit vertex influences
separately.

## Source
Pinned SharpGLTF `SkinnedTransform` forms skin matrices from inverse-bind and
joint model matrices; `SkinnedDrawableTemplate` supplies the joint's model
matrix: [MeshTransforms.cs](https://github.com/vpenades/SharpGLTF/blob/5a33d5452528d827ac55727f302d8a91a75c4186/src/SharpGLTF.Core/Transforms/MeshTransforms.cs), [DrawableTemplate.cs](https://github.com/vpenades/SharpGLTF/blob/5a33d5452528d827ac55727f302d8a91a75c4186/src/SharpGLTF.Runtime/Runtime/DrawableTemplate.cs).
