#pragma once
#include "../../services/providers/render/ILightStreamingProvider.hpp"

namespace render::gpudriven
{
    class GPUDrivenRenderer;
}

namespace core
{
    class LightStreamingAdapter : public services::ILightStreamingProvider
    {
    private:
        render::gpudriven::GPUDrivenRenderer* renderer = nullptr;

    public:
        LightStreamingAdapter() = default;
        ~LightStreamingAdapter() override = default;

        void setRenderer(render::gpudriven::GPUDrivenRenderer* r) { renderer = r; }

        void setLightStreamingConfig(const render::lighting::LightStreamingConfig& config) override
        {
            if (renderer && renderer->getLightStreamManager())
            {
                renderer->getLightStreamManager()->setConfig(config);
            }
        }

        render::lighting::LightStreamingConfig getLightStreamingConfig() const override
        {
            if (renderer && renderer->getLightStreamManager())
            {
                return renderer->getLightStreamManager()->getConfig();
            }
            return {};
        }

        render::lighting::LightStreamingStats getLightStreamingStats() const override
        {
            if (renderer && renderer->getLightStreamManager())
            {
                return renderer->getLightStreamManager()->getStats();
            }
            return {};
        }

        void registerSectorLights(uint32_t sectorId) override
        {
            if (renderer && renderer->getLightStreamManager())
            {
                // Sector entity IDs would be resolved by the WorldSectorService
                // For now, pass empty list - lights will be registered individually
                renderer->getLightStreamManager()->registerSectorLights(sectorId, {});
            }
        }

        void unregisterSectorLights(uint32_t sectorId) override
        {
            if (renderer && renderer->getLightStreamManager())
            {
                renderer->getLightStreamManager()->unregisterSectorLights(sectorId);
            }
        }
    };
}
