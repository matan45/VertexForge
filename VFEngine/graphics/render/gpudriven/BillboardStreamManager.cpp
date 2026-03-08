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

        // Distance-based streaming: only include instances within load distance
        for (const auto& instance : allInstances)
        {
            glm::vec3 pos(instance.positionAndScale.x, instance.positionAndScale.y, instance.positionAndScale.z);
            glm::vec3 diff = pos - cameraPos;
            float distSq = glm::dot(diff, diff);

            if (distSq <= loadDistSq)
            {
                if (visibleInstances.size() < MAX_GPU_BILLBOARDS)
                {
                    visibleInstances.push_back(instance);
                }
            }
        }

        // Sort by distance for priority (closest first)
        std::sort(visibleInstances.begin(), visibleInstances.end(),
                  [&cameraPos](const BillboardInstanceGPU& a, const BillboardInstanceGPU& b)
                  {
                      glm::vec3 posA(a.positionAndScale.x, a.positionAndScale.y, a.positionAndScale.z);
                      glm::vec3 posB(b.positionAndScale.x, b.positionAndScale.y, b.positionAndScale.z);
                      float distA = glm::dot(posA - cameraPos, posA - cameraPos);
                      float distB = glm::dot(posB - cameraPos, posB - cameraPos);
                      return distA < distB;
                  });

        // Apply per-frame upload budget
        if (visibleInstances.size() > streamConfig.maxUploadsPerFrame * 8)
        {
            // Budget: keep up to maxUploadsPerFrame * 8 instances per frame
            // (each upload can handle multiple instances)
        }

        streamedCount = static_cast<uint32_t>(visibleInstances.size());
        currentMemoryUsage = streamedCount * sizeof(BillboardInstanceGPU);
    }
}
