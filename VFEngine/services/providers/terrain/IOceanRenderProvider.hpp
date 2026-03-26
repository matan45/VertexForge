#pragma once

#include <functional>
#include <glm/glm.hpp>
#include <cstdint>

namespace services
{
    struct OceanFFTConfigData;
    struct OceanVisualSettings;

    class IOceanRenderProvider
    {
    public:
        virtual ~IOceanRenderProvider() = default;

        virtual bool hasActiveOcean() const = 0;

        virtual OceanVisualSettings getOceanVisualSettings() const = 0;
        virtual float getBaseWaterHeight() const = 0;

        virtual bool isOceanFFTEnabled() const = 0;
        virtual OceanFFTConfigData getOceanFFTConfig() const = 0;
        virtual uint32_t getOceanFFTConfigVersion() const = 0;
        virtual float getOceanHeightAt(const glm::vec2& worldXZ) const = 0;
        virtual void setOceanHeightSampler(std::function<float(const glm::vec2&)> sampler) = 0;
        virtual float getPhysicsGravity() const = 0;
    };
}
