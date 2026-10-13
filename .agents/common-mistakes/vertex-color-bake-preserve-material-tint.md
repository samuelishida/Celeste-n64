# Vertex-color bakes must preserve material tint

When converting a textured model to flat per-face vertex colors, do not assume
the sampled image already contains the final visible color. Grayscale material
atlases can encode shading while the material supplies the hue. Baking those
texels without the hue makes colored parts render gray or white.

Inspect source materials and representative UV samples before removing the
texture. Apply the source material tint during the bake, keep its luminance
variation, then inspect the generated model in the runtime renderer.
