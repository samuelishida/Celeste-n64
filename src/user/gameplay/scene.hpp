#pragma once

namespace madeline_cube {

// Base class for game scenes (title, gameplay, pause, etc.).
// The scene manager owns the lifetime: Init is called once on entry,
// Update/Render every frame, Shutdown once on exit.
class Scene {
public:
    virtual ~Scene() = default;

    virtual void Init() {}
    virtual void Shutdown() {}

    virtual void Update(float delta_seconds) = 0;
    virtual void Render() = 0;

    // Return true to request a scene transition.
    // The manager reads this and calls Shutdown/Init on the new scene.
    virtual bool WantsTransition() const { return wants_transition_; }
    virtual int NextSceneId() const { return next_scene_id_; }

protected:
    void RequestTransition(int scene_id) {
        wants_transition_ = true;
        next_scene_id_ = scene_id;
    }

    void ClearTransition() {
        wants_transition_ = false;
        next_scene_id_ = -1;
    }

private:
    bool wants_transition_ = false;
    int next_scene_id_ = -1;
};

}  // namespace madeline_cube
