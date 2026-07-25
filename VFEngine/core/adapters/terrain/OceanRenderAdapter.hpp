#pragma once

#include "../../services/providers/terrain/IOceanRenderProvider.hpp"
#include <functional>

namespace services
{
    class OceanService;
}

namespace core
{
    class OceanRenderAdapter : public services::IOceanRenderProvider
    {
    private:
        services::OceanService* oceanService = nullptr;
        std::function<float(const glm::vec2&)> oceanHeightSampler;

    public:
        OceanRenderAdapter() = default;
        ~OceanRenderAdapter() override = default;

        void setOceanService(services::OceanService* service);
        void setOceanHeightSampler(std::function<float(const glm::vec2&)> sampler) override;

        bool hasActiveOcean() const override;
        bool hasWaterToRender() const override;
        std::vector<water::WaterBodyDesc> getWaterBodies() const override;

        services::OceanVisualSettings getOceanVisualSettings() const override;
        float getBaseWaterHeight() const override;

        bool isOceanFFTEnabled() const override;
        services::OceanFFTConfigData getOceanFFTConfig() const override;
        uint32_t getOceanFFTConfigVersion() const override;
        float getOceanHeightAt(const glm::vec2& worldXZ) const override;
        float getPhysicsGravity() const override;

        bool isWorldModeActive() const override;
        const water::WaterTileGrid* getWaterTileGrid() const override;
        void processWaterTileStreaming() override;

        void updateShoreDepthField(const glm::vec2& cameraXZ) override;
        const water::ShoreDepthField* getShoreDepthField() const override;

        std::vector<water::WaterImpulse> drainWaterImpulses() override;
    };
}
