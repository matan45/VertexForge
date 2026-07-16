#include "MaterialPipelineWarmup.hpp"

#include "../material/MaterialShaderCache.hpp"
#include "MaterialCacheManager.hpp"

#include "material/PipelineWarmupList.hpp"
#include "material/MaterialTypes.hpp"
#include "scene/EntityRegistry.hpp"
#include "print/Log.hpp"

#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>

namespace render::mesh
{
    MaterialPipelineWarmup::MaterialPipelineWarmup(MaterialShaderCache& shaderCache,
                                                   MaterialCacheManager& materialCache)
        : shaderCache(shaderCache), materialCache(materialCache)
    {
    }

    MaterialPipelineWarmup::~MaterialPipelineWarmup()
    {
        // Block until every scheduled warm-up job has finished so no worker lambda uses this
        // object (or the caches it references) after destruction.
        stop();
    }

    void MaterialPipelineWarmup::stop()
    {
        for (auto& handle : completionJobs)
        {
            handle.wait();
        }
        completionJobs.clear();
        active.store(false, std::memory_order_release);
    }

    void MaterialPipelineWarmup::begin(std::vector<std::string> extraPaths)
    {
        // New generation: any still-pending whenAll continuation from an earlier begin() will
        // see a mismatched generation and leave this run's flags alone.
        const uint32_t gen = generation.fetch_add(1, std::memory_order_acq_rel) + 1;

        // Drop already-finished completion handles from earlier scene loads.
        std::erase_if(completionJobs, [](const threading::JobHandle& h) { return h.isComplete(); });

        std::vector<std::string> paths =
            material::collectMaterialPathsForWarmup(scene::EntityRegistry::getRegistry());
        if (!extraPaths.empty())
        {
            paths.insert(paths.end(),
                         std::make_move_iterator(extraPaths.begin()),
                         std::make_move_iterator(extraPaths.end()));
            paths = material::dedupeWarmupPaths(std::move(paths));
        }

        // Pre-resolve MaterialData on the main thread. MaterialCacheManager::getMaterial loads
        // via ResourceManager on a miss, so keeping it single-threaded avoids racing the loader.
        // Only warm materials that carry shader source and aren't already cached.
        std::vector<std::string> pendingPaths;
        std::vector<std::shared_ptr<material::MaterialData>> pendingData;
        pendingPaths.reserve(paths.size());
        pendingData.reserve(paths.size());
        for (auto& path : paths)
        {
            if (shaderCache.hasPipeline(path))
            {
                continue;
            }
            auto data = materialCache.getMaterial(path);
            if (!data || data->cachedVertexShader.empty() || data->cachedFragmentShader.empty())
            {
                continue;
            }
            pendingPaths.push_back(path);
            pendingData.push_back(std::move(data));
        }

        completed.store(0, std::memory_order_release);
        total.store(static_cast<uint32_t>(pendingPaths.size()), std::memory_order_release);

        if (pendingPaths.empty())
        {
            active.store(false, std::memory_order_release);
            return;
        }

        active.store(true, std::memory_order_release);

        std::vector<threading::JobHandle> jobs;
        jobs.reserve(pendingPaths.size());
        for (size_t i = 0; i < pendingPaths.size(); ++i)
        {
            jobs.push_back(threading::JobSystem::instance().submitJob(
                [this, path = std::move(pendingPaths[i]), data = std::move(pendingData[i])]()
                {
                    shaderCache.warmPipeline(path, *data);
                    completed.fetch_add(1, std::memory_order_acq_rel);
                },
                threading::JobPriority::LOW));
        }

        completionJobs.push_back(threading::JobSystem::instance().whenAll(
            jobs,
            [this, gen]()
            {
                if (generation.load(std::memory_order_acquire) == gen)
                {
                    active.store(false, std::memory_order_release);
                    vfLogInfo("VK-1532: pipeline warm-up complete ({} pipelines)",
                              total.load(std::memory_order_acquire));
                }
            }));

        vfLogInfo("VK-1532: warming {} material pipelines in background", pendingPaths.size());
    }

    services::PipelineWarmupStats MaterialPipelineWarmup::getStats() const
    {
        services::PipelineWarmupStats stats;
        stats.total = total.load(std::memory_order_acquire);
        stats.completed = completed.load(std::memory_order_acquire);
        stats.active = active.load(std::memory_order_acquire);
        return stats;
    }
}
