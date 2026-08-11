#include "gameplay/render/tile_streamer.hpp"

namespace madeline_cube {

TileStreamer::~TileStreamer() {
    // Inc 3 frees the resident pool; Inc 2 stub owns nothing.
}

void TileStreamer::UpdateCamera(const Vec3&, const Mat4&, float) {
    // Inc 2 stub: no resident pool yet; the orchestrator still exercises the
    // call so the frame order is complete. Inc 3 fills this in.
}

void TileStreamer::DrawLowPriority(const CameraDesc&) {
    // Inc 2 stub. Inc 5 renders water/background with Z off.
}

void TileStreamer::DrawHighPriority(const CameraDesc&) {
    // Inc 2 stub. Inc 3/5 draw resident detailed tiles with Z on.
}

}  // namespace madeline_cube
