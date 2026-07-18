#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "threading/JobSystem.hpp"
#include "../../../services/data/PipelineWarmupTypes.hpp"

namespace render::mesh
{
    class MaterialShaderCache;
    class MaterialCacheManager;

    // VK-1532: pre-creates the graphics pipelines a loaded scene's materials will need, on
    // JobSystem worker threads, so they already live in MaterialShaderCache before the first
    // draw and never hitch the render thread. Owned by the scene StaticMeshPipeline instance.
    //
    // Threading: begin() runs on the main thread. It resolves MaterialData on the main thread
    // (MaterialCacheManager::getMaterial loads on a miss) and only the heavy pipeline build is
    // scheduled off-thread via MaterialShaderCache::warmPipeline (which is itself thread-safe).
    class MaterialPipelineWarmup
    {
    public:
        MaterialPipelineWarmup(MaterialShaderCache& shaderCache, MaterialCacheManager& materialCache);
        ~MaterialPipelineWarmup();

        MaterialPipelineWarmup(const MaterialPipelineWarmup&) = delete;
        MaterialPipelineWarmup& operator=(const MaterialPipelineWarmup&) = delete;

        // Enumerate the loaded scene's materials (plus any extraPaths from a Phase-2 PSO
        // manifest), resolve their MaterialData, and schedule off-thread pipeline builds for
        // those not yet warm. Call on the main thread after a scene finishes loading.
        void begin(std::vector<std::string> extraPaths = {});

        // Block until all in-flight warm-up jobs finish, then mark inactive. MUST be called on
        // the main thread before anything mutates or destroys the MaterialShaderCache config
        // (pipeline layout / formats) the warm workers read — e.g. pipeline reinit or teardown.
        // waitIdle() only drains the GPU, not these CPU jobs. Idempotent / no-op when idle.
        void stop();

        // True while warm-up jobs from the most recent begin() are still running.
        bool isActive() const { return active.load(std::memory_order_acquire); }

        services::PipelineWarmupStats getStats() const;

    private:
        MaterialShaderCache& shaderCache;
        MaterialCacheManager& materialCache;

        std::atomic<uint32_t> total{0};
        std::atomic<uint32_t> completed{0};
        std::atomic<bool> active{false};
        // Bumped each begin() so a stale whenAll continuation from a previous run cannot clear
        // a newer run's active flag.
        std::atomic<uint32_t> generation{0};

        // Completion handles (one per begin()); waited on in the destructor so no worker job
        // touches this object after it is gone. Accessed only on the main thread.
        std::vector<threading::JobHandle> completionJobs;
    };
}
