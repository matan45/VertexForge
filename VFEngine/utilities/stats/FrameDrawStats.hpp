#pragma once
#include <array>
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
    // VK-1370 adds a per-category dimension: every draw site tags its count() with the
    // DrawCategory of the pass it belongs to. The grand total (total()) is the sum of
    // all categories, so the single "Draw Calls" readout from VK-1368 is unchanged.

    // Render-pass categories for the per-frame draw-call breakdown. Folded set (VK-1370):
    // extra passes (billboards, clouds, volumetric, transparency, SSR/SSGI, depth-prepass)
    // map into these buckets at their draw sites.
    enum class DrawCategory : uint8_t {
        Meshes,       // static/skinned meshes, GPU-driven main + depth prepass, impostor billboards
        Terrain,      // terrain mesh-shader rendering
        Grass,        // grass / vegetation mesh-shader rendering
        Shadows,      // mesh + terrain shadow passes
        Sky,          // skybox, atmosphere, clouds
        Water,        // water surface rendering
        VFX,          // particle systems (billboard/ribbon/mesh/scene/distortion)
        Decals,       // deferred decals
        PostProcess,  // SSR, SSGI, bloom, tonemap, DoF, SSAO, volumetric fog, WBOIT composite, ...
        UI,           // UI rects, UI text, world text
        Custom,       // custom / plugin pipelines
        Count
    };

    inline const char* drawCategoryName(DrawCategory c) {
        switch (c) {
            case DrawCategory::Meshes:      return "Meshes";
            case DrawCategory::Terrain:     return "Terrain";
            case DrawCategory::Grass:       return "Grass";
            case DrawCategory::Shadows:     return "Shadows";
            case DrawCategory::Sky:         return "Sky / Atmosphere";
            case DrawCategory::Water:       return "Water";
            case DrawCategory::VFX:         return "VFX";
            case DrawCategory::Decals:      return "Decals";
            case DrawCategory::PostProcess: return "Post-process";
            case DrawCategory::UI:          return "UI";
            case DrawCategory::Custom:      return "Custom / plugin";
            default:                        return "?";
        }
    }

    // count() is called from many passes, some on parallel secondary command buffers,
    // hence the atomics. beginFrame() must be called once at the start of each frame
    // (before any pass records) — it publishes the just-finished frame into lastFrame
    // and resets the accumulators, so readers always see a complete frame's totals.
    struct FrameDrawStats {
        static constexpr size_t kCount = static_cast<size_t>(DrawCategory::Count);

        static inline std::array<std::atomic<uint32_t>, kCount> recording{};
        static inline std::array<std::atomic<uint32_t>, kCount> lastFrame{};

        static void count(DrawCategory cat, uint32_t n = 1) {
            recording[static_cast<size_t>(cat)].fetch_add(n, std::memory_order_relaxed);
        }

        static void beginFrame() {
            for (size_t i = 0; i < kCount; ++i)
                lastFrame[i].store(recording[i].exchange(0, std::memory_order_relaxed),
                                   std::memory_order_relaxed);
        }

        // Grand total across all categories from the last completed frame.
        static uint32_t total() {
            uint32_t t = 0;
            for (auto& c : lastFrame) t += c.load(std::memory_order_relaxed);
            return t;
        }
    };
}
