// Host test for the renderer pool (memory-opt Inc 4). Pattern A: header-only,
// no N64 deps. Runs in `make test`.
//
// Asserts:
//   (a) Acquire returns distinct objects with stable indices; At/InUse agree;
//   (b) Release runs the destructor and frees the slot (re-acquire reuses it);
//   (c) PeakHighWater tracks the max concurrent live objects;
//   (d) SlotBuffer returns 16-aligned pointers into the per-slot block;
//   (e) ScratchPtr is 16-aligned and shared (same base for every slot);
//   (f) exhaustion: acquiring past Capacity returns nullptr (index -1);
//   (g) the pool's destructor releases all live objects (no leak).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/renderer_pool_smoke.cpp
#include <cstdint>
#include <cstdio>

#include "gameplay/render/renderer_pool.hpp"

using namespace madeline_cube;

namespace {

int g_failures = 0;

void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

struct Dummy {
    Dummy() { ++live; }
    ~Dummy() { --live; }
    int tag = 0;
    static int live;
};
int Dummy::live = 0;

using Pool = RendererPool<Dummy, 4, 64, 128>;

void TestAcquireRelease() {
    std::printf("[renderer_pool_smoke] acquire/release\n");
    {
        Pool pool;
        PoolIndex a = -1, b = -1, c = -1, d = -1;
        Dummy* da = pool.Acquire(&a);
        Dummy* db = pool.Acquire(&b);
        Dummy* dc = pool.Acquire(&c);
        Dummy* dd = pool.Acquire(&d);
        expect(da && db && dc && dd, "4 acquires succeed");
        expect(a != b && a != c && a != d && b != c && b != d && c != d,
               "distinct indices");
        expect(pool.At(a) == da && pool.At(b) == db, "At matches Acquire");
        expect(pool.InUse(a) && pool.InUse(b) && pool.InUse(c) && pool.InUse(d),
               "all in use");
        expect(Dummy::live == 4, "4 live objects");
        expect(pool.PeakHighWater() == 4, "high water 4");

        // Release b, then re-acquire — the slot is reused.
        pool.Release(b);
        expect(!pool.InUse(b), "released slot not in use");
        expect(Dummy::live == 3, "destructor ran on release");
        PoolIndex e = -1;
        Dummy* de = pool.Acquire(&e);
        expect(de != nullptr && e == b, "re-acquire reuses the freed slot");
        expect(Dummy::live == 4, "4 live again");
        expect(pool.PeakHighWater() == 4, "high water stays 4");
    }
    expect(Dummy::live == 0, "pool destructor released everything");
}

void TestExhaustion() {
    std::printf("[renderer_pool_smoke] exhaustion\n");
    Pool pool;
    PoolIndex idx = -1;
    for (int i = 0; i < 4; ++i) {
        expect(pool.Acquire(&idx) != nullptr, "acquire within capacity");
    }
    PoolIndex over = 0;
    expect(pool.Acquire(&over) == nullptr && over == -1,
           "acquire past capacity returns nullptr/-1");
}

void TestBuffers() {
    std::printf("[renderer_pool_smoke] slot buffers + scratch\n");
    Pool pool;
    PoolIndex a = -1, b = -1;
    pool.Acquire(&a);
    pool.Acquire(&b);
    uint8_t* sa = pool.SlotBuffer(a, 16);
    uint8_t* sb = pool.SlotBuffer(b, 16);
    expect(sa != sb, "per-slot buffers are distinct");
    expect((reinterpret_cast<uintptr_t>(sa) & 0xF) == 0, "slot buffer 16-aligned");
    expect((reinterpret_cast<uintptr_t>(sb) & 0xF) == 0, "slot buffer 16-aligned");
    uint8_t* sc = pool.ScratchPtr();
    expect((reinterpret_cast<uintptr_t>(sc) & 0xF) == 0, "scratch 16-aligned");
    // The scratch is shared: the same base for every slot.
    expect(pool.ScratchPtr() == sc, "scratch base is stable");
}

}  // namespace

int main() {
    TestAcquireRelease();
    TestExhaustion();
    TestBuffers();
    if (g_failures == 0) {
        std::printf("PASS: renderer_pool_smoke\n");
        return 0;
    }
    std::printf("FAILED: renderer_pool_smoke (%d)\n", g_failures);
    return 1;
}
