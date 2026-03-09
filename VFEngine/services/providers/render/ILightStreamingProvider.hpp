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
        virtual void registerSectorLights(uint32_t sectorId, const std::vector<uint32_t>& lightEntityIds) = 0;
        virtual void unregisterSectorLights(uint32_t sectorId) = 0;
    };
}
