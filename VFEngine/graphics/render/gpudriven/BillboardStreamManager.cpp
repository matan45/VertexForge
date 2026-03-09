#include "BillboardStreamManager.hpp"
#include "../../core/Device.hpp"
#include <algorithm>

namespace render::gpudriven
{
    void BillboardStreamManager::init(core::Device& device)
    {
        devicePtr = &device;
        currentMemoryUsage = 0;
        streamedCount = 0;
    }

    void BillboardStreamManager::cleanup()
    {
        devicePtr = nullptr;
        currentMemoryUsage = 0;
        streamedCount = 0;
    }

    void BillboardStreamManager::update(const glm::vec3& cameraPos,
                                          const std::vector<BillboardInstanceGPU>& allInstances,
                                          std::vector<BillboardInstanceGPU>& visibleInstances)
    {
        visibleInstances.clear();

        float loadDistSq = streamConfig.loadDistance * streamConfig.loadDistance;
        float unloadDistSq = streamConfig.unloadDistance * streamConfig.unloadDistance;

        // Distance-based streaming with cached distSq for sorting
        struct InstanceWithDist {
            uint32_t index;
            float distSq;
        };
        std::vector<InstanceWithDist> filtered;
        filtered.reserve(allInstances.size());

        for (uint32_t i = 0; i < static_cast<uint32_t>(allInstances.size()); ++i)
        {
            const auto& instance = allInstances[i];
            glm::vec3 pos(instance.positionAndScale.x, instance.positionAndScale.y, instance.positionAndScale.z);
            glm::vec3 diff = pos - cameraPos;
            float distSq = glm::dot(diff, diff);

            if (distSq <= loadDistSq)
            {
                if (filtered.size() < MAX_GPU_BILLBOARDS)
                {
                    filtered.push_back({i, distSq});
                }
            }
        }

        // Sort by cached distance (closest first)
        std::sort(filtered.begin(), filtered.end(),
                  [](const InstanceWithDist& a, const InstanceWithDist& b)
                  {
                      return a.distSq < b.distSq;
                  });

        visibleInstances.reserve(filtered.size());
        for (const auto& entry : filtered)
        {
            visibleInstances.push_back(allInstances[entry.index]);
        }

        // Apply per-frame upload budget
        size_t maxInstances = static_cast<size_t>(streamConfig.maxUploadsPerFrame) * 8;
        if (visibleInstances.size() > maxInstances)
        {
            visibleInstances.resize(maxInstances);
        }

        streamedCount = static_cast<uint32_t>(visibleInstances.size());
        currentMemoryUsage = streamedCount * sizeof(BillboardInstanceGPU);
    }
}
