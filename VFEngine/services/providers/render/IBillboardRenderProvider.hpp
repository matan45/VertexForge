#pragma once

#include <string>
#include <cstdint>
#include <glm/glm.hpp>

namespace services
{
    struct BillboardRenderStats
    {
        uint32_t totalInstances = 0;
        uint32_t visibleInstances = 0;
        uint32_t culledByFrustum = 0;
        uint32_t culledByDistance = 0;
        bool renderingEnabled = true;
    };

    struct ImposterBakeResult
    {
        bool success = false;
        std::string outputPath;
        std::string errorMessage;
    };

    class IBillboardRenderProvider
    {
    public:
        virtual ~IBillboardRenderProvider() = default;

        virtual void setBillboardRenderingEnabled(bool enabled) = 0;
        virtual bool isBillboardRenderingEnabled() const = 0;

        virtual void setBillboardMaxDistance(float distance) = 0;

        virtual BillboardRenderStats getBillboardStats() const = 0;

        virtual ImposterBakeResult bakeImposter(const std::string& meshPath,
                                                  const std::string& outputPath,
                                                  const glm::vec3& meshCenter = glm::vec3(0.0f),
                                                  float meshScale = 1.0f) = 0;
    };
}
