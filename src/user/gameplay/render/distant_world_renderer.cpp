#include "gameplay/render/distant_world_renderer.hpp"

#include <rdpq.h>
#include <rdpq_mode.h>

namespace madeline_cube {

void DistantWorldRenderer::UpdateCamera(const Vec3&, const CameraDesc&) {
    // Inc 2 stub. Inc 4 rebases the compressed distant translation.
}

void DistantWorldRenderer::Render(const CameraDesc& cam) {
    // Inc 2 stub: exercise the Z-off/on toggling that the real distant pass
    // uses, so the frame order (and RDP state) is validated end-to-end.
    rdpq_sync_pipe();
    rdpq_mode_zbuf(false, false);
    // (Inc 4 draws the coarse distant LOD cells here, back-to-front.)
    rdpq_sync_pipe();
    rdpq_mode_zbuf(true, true);
    (void)cam;
}

}  // namespace madeline_cube
