#pragma once

#include <sprite.h>

namespace madeline_cube {

class SpriteTexture {
public:
    bool Load(const char* dfs_path);
    void Free();
    void DebugBlit(float x, float y) const;
    bool IsLoaded() const { return sprite_ != nullptr; }
    sprite_t* Get() const { return sprite_; }

private:
    sprite_t* sprite_ = nullptr;
};

}  // namespace madeline_cube
