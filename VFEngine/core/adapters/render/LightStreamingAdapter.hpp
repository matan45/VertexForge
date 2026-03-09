#pragma once
#include "../../../services/providers/render/ILightStreamingProvider.hpp"

namespace render::lighting
{
    class LightStreamManager;
}

namespace core::adapters
{
    class LightStreamingAdapter : public services::ILightStreamingProvider
    {
    private:
        render::lighting::LightStreamManager* streamManager = nullptr;

    public:
        LightStreamingAdapter() = default;
        ~LightStreamingAdapter() override = default;

        void setStreamManager(render::lighting::LightStreamManager* mgr) { streamManager = mgr; }

        void setLightStreamingConfig(const render::lighting::LightStreamingConfig& config) override;
        render::lighting::LightStreamingConfig getLightStreamingConfig() const override;
        render::lighting::LightStreamingStats getLightStreamingStats() const override;
        void registerSectorLights(uint32_t sectorId) override;
        void unregisterSectorLights(uint32_t sectorId) override;
    };
}
