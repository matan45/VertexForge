#pragma once
#include "../../../graphics/render/lighting/LightStreamManager.hpp"

namespace services
{
    class ILightStreamingProvider
    {
    public:
        virtual ~ILightStreamingProvider() = default;

        virtual void setLightStreamingConfig(const render::lighting::LightStreamingConfig& config) = 0;
        virtual render::lighting::LightStreamingConfig getLightStreamingConfig() const = 0;
        virtual render::lighting::LightStreamingStats getLightStreamingStats() const = 0;
        // VK-1599: `sectorId` is world::sectorRegistrationId(gridIndex, coord) - 64 bits, because a
        // world may run several streaming grids and the packed coord alone already fills 32.
        virtual void registerSectorLights(uint64_t sectorId, const std::vector<uint32_t>& lightEntityIds) = 0;
        virtual void unregisterSectorLights(uint64_t sectorId) = 0;
    };
}
