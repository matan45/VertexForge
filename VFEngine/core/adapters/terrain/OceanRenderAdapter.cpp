#include "OceanRenderAdapter.hpp"
#include "../../services/impl/scene/OceanService.hpp"
#include "../../services/data/OceanData.hpp"
#include "../../services/providers/physics/IPhysicsProvider.hpp"

namespace core
{
    void OceanRenderAdapter::setOceanService(services::OceanService* service)
    {
        oceanService = service;
        if (oceanService && oceanHeightSampler)
            oceanService->setOceanHeightSampler(oceanHeightSampler);
    }

    void OceanRenderAdapter::setOceanHeightSampler(std::function<float(const glm::vec2&)> sampler)
    {
        oceanHeightSampler = std::move(sampler);
        if (oceanService && oceanHeightSampler)
            oceanService->setOceanHeightSampler(oceanHeightSampler);
    }

    bool OceanRenderAdapter::hasActiveOcean() const
    {
        return oceanService && oceanService->hasActiveOcean();
    }

    services::OceanVisualSettings OceanRenderAdapter::getOceanVisualSettings() const
    {
        if (!oceanService) return {};
        return oceanService->getOceanVisualSettings();
    }

    float OceanRenderAdapter::getBaseWaterHeight() const
    {
        if (!oceanService) return 0.0f;
        return oceanService->getBaseWaterHeight();
    }

    bool OceanRenderAdapter::isOceanFFTEnabled() const
    {
        return oceanService && oceanService->isOceanFFTEnabled();
    }

    services::OceanFFTConfigData OceanRenderAdapter::getOceanFFTConfig() const
    {
        if (!oceanService) return {};
        return oceanService->getOceanFFTConfig();
    }

    uint32_t OceanRenderAdapter::getOceanFFTConfigVersion() const
    {
        if (!oceanService) return 0;
        return oceanService->getOceanFFTConfigVersion();
    }

    float OceanRenderAdapter::getOceanHeightAt(const glm::vec2& worldXZ) const
    {
        if (oceanHeightSampler)
            return oceanHeightSampler(worldXZ);
        return 0.0f;
    }

    float OceanRenderAdapter::getPhysicsGravity() const
    {
        if (oceanService && oceanService->getPhysicsProvider())
        {
            glm::vec3 g = oceanService->getPhysicsProvider()->getGravity();
            return glm::length(g);
        }
        return 9.81f;
    }
}
