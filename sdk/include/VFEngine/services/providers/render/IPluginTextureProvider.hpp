#pragma once
#include <cstddef>
#include <vector>
#include "../../data/PluginTextureTypes.hpp"

namespace services {

    class IPluginTextureProvider {
    public:
        virtual ~IPluginTextureProvider() = default;

        virtual plugin::PluginTextureHandle createTexture2D(uint32_t width, uint32_t height,
                                                            plugin::TextureFormat format) = 0;

        virtual void updateTexture2D(plugin::PluginTextureHandle handle,
                                     std::vector<std::byte>&& data) = 0;

        virtual void destroyTexture2D(plugin::PluginTextureHandle handle) = 0;

        virtual void bindWorldMask(plugin::PluginTextureHandle handle,
                                   const glm::vec3& worldMin, const glm::vec3& worldMax,
                                   const plugin::WorldMaskParams& params) = 0;

        virtual void unbindWorldMask() = 0;

        virtual void setWorldMaskParams(const plugin::WorldMaskParams& params) = 0;

        // CPU sample of the bound world mask's red channel at a world (x,z) position.
        // Returns 1.0 when no mask is bound / disabled / out of bounds (shader parity).
        virtual float sampleWorldMask(float worldX, float worldZ) const = 0;
    };

}
