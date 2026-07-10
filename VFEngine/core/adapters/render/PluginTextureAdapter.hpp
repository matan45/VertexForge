#pragma once
#include "../../services/providers/render/IPluginTextureProvider.hpp"

namespace controllers
{
    class OffScreen;
}

namespace core
{
    class PluginTextureAdapter : public services::IPluginTextureProvider
    {
    private:
        controllers::OffScreen* offScreen;

    public:
        explicit PluginTextureAdapter(controllers::OffScreen* offScreen);
        ~PluginTextureAdapter() override = default;

        plugin::PluginTextureHandle createTexture2D(uint32_t width, uint32_t height,
                                                    plugin::TextureFormat format) override;
        void updateTexture2D(plugin::PluginTextureHandle handle,
                             std::vector<std::byte>&& data) override;
        void destroyTexture2D(plugin::PluginTextureHandle handle) override;
        std::string registerUITexture(plugin::PluginTextureHandle handle) override;
        void unregisterUITexture(plugin::PluginTextureHandle handle) override;
        void bindWorldMask(plugin::PluginTextureHandle handle,
                           const glm::vec3& worldMin, const glm::vec3& worldMax,
                           const plugin::WorldMaskParams& params) override;
        void unbindWorldMask() override;
        void setWorldMaskParams(const plugin::WorldMaskParams& params) override;
        float sampleWorldMask(float worldX, float worldZ) const override;
    };
}
