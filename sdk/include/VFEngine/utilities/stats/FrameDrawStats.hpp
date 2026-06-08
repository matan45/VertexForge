#pragma once
#include <atomic>
#include <cstdint>

namespace render {
    // Per-frame count of CPU-recorded draw commands across the runtime/viewport
    // render passes (VK-1367 follow-up). Each recorded vk::CommandBuffer draw
    // (instanced or indirect batch) counts once — this is the standard "draw calls"
    // metric, not GPU-expanded instance/meshlet counts.
    //
    // Editor-only passes are intentionally NOT instrumented (ImGui, debug gizmos /
    // render/tools/*, IBL bake generators, *Preview* pipelines), so the total stays
    // comparable to what the standalone Runtime would issue.
    //
    // count() is called from many passes, some on parallel secondary command buffers,
    // hence the atomic. beginFrame() must be called once at the start of each frame
    // (before any pass records) — it publishes the just-finished frame into lastFrame
    // and resets the accumulator, so readers always see a complete frame's total.
    struct FrameDrawStats {
        static inline std::atomic<uint32_t> recording{0};
        static inline std::atomic<uint32_t> lastFrame{0};

        static void count(uint32_t n = 1) { recording.fetch_add(n, std::memory_order_relaxed); }

        static void beginFrame() {
            lastFrame.store(recording.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
        }
    };
}
