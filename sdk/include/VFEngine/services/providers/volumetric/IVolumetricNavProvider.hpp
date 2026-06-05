#pragma once
#include "navigation/volumetric/VolumetricTypes.hpp"
#include <glm/glm.hpp>
#include <functional>
#include <cstdint>

namespace services
{
    class IVolumetricNavProvider
    {
    public:
        virtual ~IVolumetricNavProvider() = default;

        virtual bool bakeVolume(glm::vec3 boundsMin, glm::vec3 boundsMax, float voxelSize,
                                uint8_t connectivity, float agentClearance,
                                const std::function<bool(glm::vec3, float)>& isBlocked) = 0;
        virtual volumetric::VolumePath findPath3D(glm::vec3 start, glm::vec3 end) = 0;
        virtual bool isPointNavigable(glm::vec3 point) = 0;
        virtual void clear() = 0;
        virtual bool hasVolume() const = 0;
        virtual volumetric::VolumetricBakeProgress getBakeProgress() const = 0;
    };
}
