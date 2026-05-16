#pragma once
#include <cstdint>
namespace madeline_cube {
class StateMachine {
public:
    explicit StateMachine(int16_t initial = 0) : state_(initial) {}
    int16_t State() const { return state_; }
    int16_t Previous() const { return previous_; }
    float TimeInState() const { return time_in_state_; }
    bool ChangedThisFrame() const { return changed_this_frame_; }
    void SetState(int16_t next) {
        if (next == state_) return;
        previous_ = state_;
        state_ = next;
        time_in_state_ = 0.0f;
        changed_this_frame_ = true;
    }
    void Step(float delta_seconds) {
        if (delta_seconds > 0.0f) time_in_state_ += delta_seconds;
        changed_this_frame_ = false;
    }
private:
    int16_t state_ = 0;
    int16_t previous_ = 0;
    float time_in_state_ = 0.0f;
    bool changed_this_frame_ = false;
};
}
