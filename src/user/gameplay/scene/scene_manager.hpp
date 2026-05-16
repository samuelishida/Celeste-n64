#pragma once

#include "gameplay/scene/scene.hpp"

namespace madeline_cube {

// Owns a fixed-size array of scenes and handles transitions.
// Scenes are registered by the caller; the manager does not allocate.
class SceneManager {
public:
    static constexpr int kMaxScenes = 8;

    SceneManager();

    // Register a scene at a given slot. Returns false if slot is full.
    bool Register(int scene_id, Scene* scene);

    // Start with the given scene.
    void Goto(int scene_id);

    // Update the active scene and handle pending transitions.
    void Update(float delta_seconds);

    // Render the active scene.
    void Render();

    int ActiveSceneId() const { return active_scene_id_; }

private:
    Scene* scenes_[kMaxScenes] = {};
    int active_scene_id_ = -1;
};

}  // namespace madeline_cube
