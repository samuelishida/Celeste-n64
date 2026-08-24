// Host-side pending-load state machine test (streaming-experience Inc 1,
// Pattern A: header-only, no N64 deps).
//
// Exercises `PendingLoadQueue` (tile_streamer.hpp) — the bounded async-load
// queue the device `TileStreamer` drives with a real `dma_finished`/`dma_read_async`
// behind it. Here a mock "is_finished" callback + a faithful model of the
// device pump (issue → poll → parse → promote) assert the invariants:
//   - issue → poll → parse → promote, at most one parse per pump tick;
//   - no double-parse / no leak (a parsed cell is removed from the queue);
//   - the rapid A→B→A→C transition never double-enqueues a pending/READY cell;
//   - a cell evicted while in-flight is abandoned (its DMA drains harmlessly)
//     and the staging slot frees only when the ticket completes;
//   - a ticket that never completes times out (sync fallback path).
//
// Build:
//   g++ -std=c++17 -Isrc/user tests/pending_load_smoke.cpp
#include <cassert>
#include <cstdio>
#include <cstring>

#include "gameplay/render/tile_streamer.hpp"

using namespace madeline_cube;

// A tiny mock manifest so we have distinct V2RoomSpec objects.
static MapSpecV2 g_spec;
static V2RoomSpec* Cell(int i) { return &g_spec.rooms[i]; }

// Faithful model of the device pump's per-tick work, driven by a mock
// "is_finished" callback. Returns the number of cells parsed this tick.
static int PumpTick(PendingLoadQueue& q, uint32_t frame,
                    bool (*is_finished)(uint64_t), int* parse_count) {
    int parsed = 0;
    const int inflight = q.FindInFlight();
    if (inflight >= 0) {
        PendingLoadQueue::Slot& s = q.slots[inflight];
        const bool done = is_finished(s.ticket);
        const bool timed_out =
            (frame - s.issued_frame) > PendingLoadQueue::kPendingTimeoutFrames;
        if (done || timed_out) {
            if (s.state == PendingLoadQueue::kInFlight && !timed_out) {
                ++*parse_count;  // PromoteStaged (mock: always succeeds)
                ++parsed;
            }
            q.FreeSlot(inflight);
        }
    }
    if (q.FindInFlight() < 0) {
        const int next = q.FirstQueued();
        if (next >= 0) {
            // IssueAsyncRead (mock: always issues a non-zero ticket).
            q.MarkIssued(next, 1000 + next, frame);
        }
    }
    return parsed;
}

int main() {
    g_spec.room_count = 8;
    for (int i = 0; i < 8; ++i) {
        std::snprintf(g_spec.rooms[i].id, V2RoomSpec::kIdLen, "cell_%d_0", i);
        g_spec.rooms[i].cell_ix = i;
        g_spec.rooms[i].cell_iz = 0;
        g_spec.rooms[i].lvl_size = 1000;
    }

    // ── 1. Enqueue up to kMaxPending; the (kMaxPending+1)-th is rejected. ──
    {
        PendingLoadQueue q;
        for (int i = 0; i < PendingLoadQueue::kMaxPending; ++i)
            assert(q.Enqueue(Cell(i)));
        assert(q.count == PendingLoadQueue::kMaxPending);
        // Full: a NEW cell is rejected (caller falls back to sync).
        assert(!q.Enqueue(Cell(PendingLoadQueue::kMaxPending)));
        // A tracked cell is a no-op (dedup), not a rejection.
        assert(q.Enqueue(Cell(0)));
        assert(q.count == PendingLoadQueue::kMaxPending);
        printf("PASS: enqueue caps at kMaxPending; dedup is a no-op\n");
    }

    // ── 2. Issue → poll → parse → promote, one parse per tick, no double. ──
    {
        PendingLoadQueue q;
        q.Enqueue(Cell(0));
        q.Enqueue(Cell(1));
        int parse_count = 0;
        // Tick 1: issues cell 0 (no in-flight yet).
        PumpTick(q, 1, [](uint64_t) { return false; }, &parse_count);
        assert(q.FindInFlight() >= 0);
        assert(parse_count == 0);
        // Tick 2: cell 0's DMA completes → parse it; issue cell 1.
        PumpTick(q, 2, [](uint64_t) { return true; }, &parse_count);
        assert(parse_count == 1);
        assert(q.IndexOf(Cell(0)) == -1);  // parsed cell removed (no double-parse)
        assert(q.IndexOf(Cell(1)) >= 0);   // cell 1 now in flight
        // Tick 3: cell 1 completes → parse it; nothing queued.
        PumpTick(q, 3, [](uint64_t) { return true; }, &parse_count);
        assert(parse_count == 2);
        assert(q.count == 0);
        printf("PASS: issue→poll→parse→promote, one parse per tick, no double-parse\n");
    }

    // ── 3. Rapid A→B→A→C never double-enqueues a pending/READY cell. ──
    {
        PendingLoadQueue q;
        q.Enqueue(Cell(0));  // A
        q.Enqueue(Cell(1));  // B
        // A is still pending (in flight) — re-enqueue is a no-op.
        assert(q.Enqueue(Cell(0)));
        assert(q.IndexOf(Cell(0)) >= 0);
        assert(q.count == 2);
        // C
        assert(q.Enqueue(Cell(2)));
        assert(q.count == 3);
        printf("PASS: rapid A→B→A→C does not double-enqueue a pending cell\n");
    }

    // ── 4. Evict while in-flight → abandoned; slot frees on completion. ──
    {
        PendingLoadQueue q;
        q.Enqueue(Cell(0));
        int parse_count = 0;
        PumpTick(q, 1, [](uint64_t) { return false; }, &parse_count);  // issue A
        const int inflight = q.FindInFlight();
        assert(inflight >= 0);
        // Evict A while in-flight → abandoned (ticket still draining).
        q.DropSpec(Cell(0));
        assert(q.slots[inflight].state == PendingLoadQueue::kAbandoned);
        assert(q.FindInFlight() >= 0);  // still draining the staging buffer
        // The pump must NOT parse an abandoned cell (no double-parse / no leak).
        PumpTick(q, 2, [](uint64_t) { return true; }, &parse_count);
        assert(parse_count == 0);       // abandoned → never parsed
        assert(q.FindInFlight() < 0);   // slot freed once the ticket completed
        assert(q.count == 0);
        printf("PASS: evicted in-flight cell is abandoned, never parsed, slot freed\n");
    }

    // ── 5. A ticket that never completes times out (sync fallback path). ──
    {
        PendingLoadQueue q;
        q.Enqueue(Cell(0));
        int parse_count = 0;
        PumpTick(q, 1, [](uint64_t) { return false; }, &parse_count);  // issue A
        // Simulate many frames with the DMA never completing.
        const uint32_t timeout_frame =
            1 + PendingLoadQueue::kPendingTimeoutFrames + 1;
        PumpTick(q, timeout_frame, [](uint64_t) { return false; }, &parse_count);
        assert(parse_count == 0);        // timed out → not parsed
        assert(q.FindInFlight() < 0);    // slot freed (caller does sync fallback)
        assert(q.count == 0);
        printf("PASS: never-completing ticket times out and frees the slot\n");
    }

    printf("ALL PASS\n");
    return 0;
}
