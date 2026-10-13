#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

static std::string Read(const char* path) {
    std::ifstream file(path);
    assert(file.good());
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

int main() {
    const std::string scene = Read("src/user/gameplay/scene/gameplay_scene.cpp");
    const std::string makefile = Read("Makefile");

    assert(scene.find("#include \"gameplay/render/model.hpp\"") != std::string::npos);
    assert(scene.find("#include \"gameplay/render/animated_model.hpp\"") != std::string::npos);
    assert(scene.find("#include \"gameplay/player/animation_state.hpp\"") != std::string::npos);
    assert(scene.find("rom:/mdl/player.t3dm") != std::string::npos);
    assert(scene.find("rom:/mdl/player.animmap") != std::string::npos);
    assert(scene.find("rom:/mdl/player.wsk") != std::string::npos);
    assert(scene.find("rom:/mdl/player.anim") != std::string::npos);
    assert(scene.find("rom:/tex/player.sprite") == std::string::npos);
    assert(scene.find("kMadelineAnimationCurvePath") != std::string::npos);
    assert(scene.find("madeline_model.Load(kMadelineModelPath, kMadelineAnimmapPath, kMadelineSkinPath,") != std::string::npos);
    assert(scene.find("AnimatedModel madeline_model") != std::string::npos);
    assert(scene.find("ResolvePlayerAnimState") == std::string::npos);
    assert(scene.find("madeline_model.Configure(impl_->player.animation)") != std::string::npos);
    assert(scene.find("madeline_model.Update(delta_seconds)") == std::string::npos);
    assert(scene.find("madeline_model.Sample(impl_->player.animation)") != std::string::npos);
    assert(scene.find("madeline_model_failed_") == std::string::npos);
    assert(scene.find("constexpr float kMadelineScale = 0.07735f") != std::string::npos);
    assert(scene.find("[player-envelope] scale=") != std::string::npos);
    assert(scene.find("impl_->madeline_model.Draw();") != std::string::npos);

    assert(makefile.find("src/user/gameplay/render/animated_model.cpp") != std::string::npos);
    assert(makefile.find("src/user/gameplay/render/animation_manifest.cpp") != std::string::npos);
    return 0;
}
