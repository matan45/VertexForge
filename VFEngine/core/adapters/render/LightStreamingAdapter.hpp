#pragma once
#include "../../../services/providers/render/ILightStreamingProvider.hpp"

namespace controllers
{
    class OffScreen;
}

namespace core::adapters
{
    class LightStreamingAdapter : public services::ILightStreamingProvider
    {
    private:
        controllers::OffScreen* offScreen = nullptr;

    public:
        LightStreamingAdapter() = default;
        ~LightStreamingAdapter() override = default;

        void setOffScreenController(controllers::OffScreen* controller) { offScreen = controller; }

        void setLightStreamingConfig(const render::lighting::LightStreamingConfig& config) override;
        render::lighting::LightStreamingConfig getLightStreamingConfig() const override;
        render::lighting::LightStreamingStats getLightStreamingStats() const override;
        void registerSectorLights(uint32_t sectorId, const std::vector<uint32_t>& lightEntityIds) override;
        void unregisterSectorLights(uint32_t sectorId) override;
    };
}
