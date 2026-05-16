#include "gameplay/scene/scene_manager.hpp"

namespace madeline_cube {

SceneManager::SceneManager() {
    for (int i = 0; i < kMaxScenes; ++i) {
        scenes_[i] = nullptr;
    }
}

bool SceneManager::Register(int scene_id, Scene* scene) {
    if (scene_id < 0 || scene_id >= kMaxScenes || scenes_[scene_id] != nullptr) {
        return false;
    }
    scenes_[scene_id] = scene;
    return true;
}

void SceneManager::Goto(int scene_id) {
    if (active_scene_id_ >= 0 && active_scene_id_ < kMaxScenes && scenes_[active_scene_id_] != nullptr) {
        scenes_[active_scene_id_]->Shutdown();
    }
    active_scene_id_ = scene_id;
    if (active_scene_id_ >= 0 && active_scene_id_ < kMaxScenes && scenes_[active_scene_id_] != nullptr) {
        scenes_[active_scene_id_]->Init();
    }
}

void SceneManager::Update(float delta_seconds) {
    if (active_scene_id_ < 0 || active_scene_id_ >= kMaxScenes || scenes_[active_scene_id_] == nullptr) {
        return;
    }

    Scene* active = scenes_[active_scene_id_];
    active->Update(delta_seconds);

    if (active->WantsTransition()) {
        const int next = active->NextSceneId();
        if (next >= 0 && next < kMaxScenes && scenes_[next] != nullptr) {
            active->Shutdown();
            active_scene_id_ = next;
            scenes_[active_scene_id_]->Init();
        }
    }
}

void SceneManager::Render() {
    if (active_scene_id_ < 0 || active_scene_id_ >= kMaxScenes || scenes_[active_scene_id_] == nullptr) {
        return;
    }
    scenes_[active_scene_id_]->Render();
}

}  // namespace madeline_cube
