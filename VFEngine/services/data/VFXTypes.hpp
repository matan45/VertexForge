#pragma once

#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace services
{
    using VFXInstanceId = uint32_t;

    enum class VFXEmitterPriority : uint8_t
    {
        Critical = 0,
        High = 1,
        Normal = 2,
        Low = 3
    };

    struct VFXRuntimeParams
    {
        std::string vfxAssetPath;
        glm::mat4 worldTransform{1.0f};
        bool loop = true;
        uint32_t entityId = 0;
        VFXEmitterPriority priority = VFXEmitterPriority::Normal;
        bool cameraRelative = false;
    };

    struct VFXCameraParams
    {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::vec3 cameraPos{0.0f};
        float time = 0.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
    };
}
