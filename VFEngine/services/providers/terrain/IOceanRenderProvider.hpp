#pragma once

#include "../../../utilities/water/RippleSimMath.hpp"
#include "../../../utilities/water/WaterBodyMath.hpp"

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

        // VK-1607: is there ANY water this frame - the ocean, or at least one active water body.
        // Every render-side gate that used to ask hasActiveOcean() asks this instead, so a scene
        // whose only water is a lake still gets its water pass, its refraction copy and its
        // underwater post-process. hasActiveOcean() survives for the things that genuinely need an
        // OceanComponent (the FFT config, the ripple sim's parameters).
        virtual bool hasWaterToRender() const = 0;

        // Every active water body, resolved to plain rectangles. Returned by value once per frame,
        // like drainWaterImpulses - the renderer must not hold a pointer into the registry.
        virtual std::vector<water::WaterBodyDesc> getWaterBodies() const = 0;

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
