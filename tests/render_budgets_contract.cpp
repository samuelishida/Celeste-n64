// Host test for per-frame render budgets (Pattern A: header-only, no N64
// deps). Asserts `BudgetsExceeded` behaves for boundary values (at-cap passes,
// over-cap fails) and that the budget constants are sane.
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/render_budgets_contract.cpp
#include <cstdio>
#include <cstdlib>

#include "gameplay/render/render_budgets.hpp"

using namespace madeline_cube;

static int failures = 0;

static void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    // A zeroed count never exceeds any budget.
    {
        RenderCounts c;
        expect(!BudgetsExceeded(c), "zeroed counts within budget");
    }

    // At-cap on every field passes (boundary).
    {
        RenderCounts c;
        c.visible_cells = kMaxVisibleCells;
        c.triangles = kMaxTrianglesPerFrame;
        c.material_changes = kMaxMaterialChangesPerFrame;
        c.texture_uploads = kMaxTextureUploadsPerFrame;
        c.stream_ops = kMaxStreamOpsPerFrame;
        c.particles = kMaxParticlesPerFrame;
        expect(!BudgetsExceeded(c), "at-cap on all fields passes");
    }

    // Over-cap on ANY single field fails.
    {
        RenderCounts c;
        c.visible_cells = kMaxVisibleCells + 1;
        expect(BudgetsExceeded(c), "visible_cells over cap fails");

        c = RenderCounts{};
        c.triangles = kMaxTrianglesPerFrame + 1;
        expect(BudgetsExceeded(c), "triangles over cap fails");

        c = RenderCounts{};
        c.material_changes = kMaxMaterialChangesPerFrame + 1;
        expect(BudgetsExceeded(c), "material_changes over cap fails");

        c = RenderCounts{};
        c.texture_uploads = kMaxTextureUploadsPerFrame + 1;
        expect(BudgetsExceeded(c), "texture_uploads over cap fails");

        c = RenderCounts{};
        c.stream_ops = kMaxStreamOpsPerFrame + 1;
        expect(BudgetsExceeded(c), "stream_ops over cap fails");

        c = RenderCounts{};
        c.particles = kMaxParticlesPerFrame + 1;
        expect(BudgetsExceeded(c), "particles over cap fails");
    }

    // Budget constants are sane (positive, near ring >= 1).
    expect(kMaxVisibleCells >= 1, "kMaxVisibleCells >= 1");
    expect(kMaxTrianglesPerFrame > 0, "triangle budget positive");
    expect(kMaxStreamOpsPerFrame >= 1, "streaming budget >= 1");

    if (failures == 0) {
        std::printf("render_budgets_contract: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "render_budgets_contract: %d failures\n", failures);
    return 1;
}
