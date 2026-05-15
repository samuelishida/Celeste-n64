#include <cassert>
#include <cstdio>
#include <cstring>

#include "arena.hpp"
#include "scene.hpp"
#include "scene_manager.hpp"

using namespace madeline_cube;

// Test arena basics
void TestArena() {
    alignas(64) uint8_t buffer[1024];
    Arena arena(buffer, sizeof(buffer));

    assert(arena.Capacity() == 1024);
    assert(arena.Used() == 0);
    assert(arena.Available() == 1024);

    void* p1 = arena.Alloc(100, 8);
    assert(p1 != nullptr);
    assert(arena.Used() >= 100);

    void* p2 = arena.Alloc(200, 16);
    assert(p2 != nullptr);
    assert(reinterpret_cast<uintptr_t>(p2) % 16 == 0);

    arena.Reset();
    assert(arena.Used() == 0);
    assert(arena.Available() == 1024);

    // Exhaust the arena
    void* big = arena.Alloc(2000, 8);
    assert(big == nullptr);

    printf("TestArena: passed\n");
}

// Mock scene for testing transitions
class MockScene : public Scene {
public:
    int id = 0;
    int init_count = 0;
    int shutdown_count = 0;
    int update_count = 0;
    int render_count = 0;

    void Init() override { ++init_count; }
    void Shutdown() override { ++shutdown_count; }

    void Update(float) override { ++update_count; }
    void Render() override { ++render_count; }

    void GoTo(int target) { RequestTransition(target); }
};

void TestSceneManager() {
    SceneManager mgr;
    MockScene sceneA;
    MockScene sceneB;
    sceneA.id = 0;
    sceneB.id = 1;

    assert(mgr.Register(0, &sceneA));
    assert(mgr.Register(1, &sceneB));
    assert(!mgr.Register(0, &sceneA));  // duplicate
    assert(!mgr.Register(99, &sceneA)); // out of range

    mgr.Goto(0);
    assert(sceneA.init_count == 1);
    assert(mgr.ActiveSceneId() == 0);

    mgr.Update(1.0f / 60.0f);
    assert(sceneA.update_count == 1);
    mgr.Render();
    assert(sceneA.render_count == 1);

    // Trigger transition A -> B
    sceneA.GoTo(1);
    mgr.Update(1.0f / 60.0f);
    assert(sceneA.shutdown_count == 1);
    assert(sceneB.init_count == 1);
    assert(mgr.ActiveSceneId() == 1);

    mgr.Update(1.0f / 60.0f);
    assert(sceneB.update_count == 1);

    printf("TestSceneManager: passed\n");
}

int main() {
    TestArena();
    TestSceneManager();
    printf("runtime_smoke: all checks passed\n");
    return 0;
}
