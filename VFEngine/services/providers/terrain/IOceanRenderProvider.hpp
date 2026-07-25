#pragma once

#include "../../../utilities/water/RippleSimMath.hpp"

#include <functional>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace water
{
    class WaterTileGrid;
    class ShoreDepthField;
}

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

        virtual bool isWorldModeActive() const = 0;
        virtual const water::WaterTileGrid* getWaterTileGrid() const = 0;
        virtual void processWaterTileStreaming() = 0;

        // VK-1605: advance the time-sliced shore-depth bake and hand the field to the renderer.
        // Driven from the render side (like processWaterTileStreaming) because that is where the
        // live view camera position is; OceanService has no camera of its own.
        virtual void updateShoreDepthField(const glm::vec2& cameraXZ) = 0;
        virtual const water::ShoreDepthField* getShoreDepthField() const = 0;

        // VK-1606: take the frame's water impulses (script calls, auto-wakes, wake emitters) and
        // clear the service-side queue. Called once per frame from the render side, which is the
        // only thread allowed to talk to the GPU ripple sim.
        virtual std::vector<water::WaterImpulse> drainWaterImpulses() = 0;
    };
}
