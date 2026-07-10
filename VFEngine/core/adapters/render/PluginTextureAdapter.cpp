#include "PluginTextureAdapter.hpp"
#include "../../controllers/OffScreen.hpp"
#include "../../../graphics/render/RenderPassHandler.hpp"

namespace core
{
    PluginTextureAdapter::PluginTextureAdapter(controllers::OffScreen* offScreen)
        : offScreen(offScreen)
    {
    }

    plugin::PluginTextureHandle PluginTextureAdapter::createTexture2D(uint32_t width, uint32_t height,
                                                                      plugin::TextureFormat format)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return {};
        return handler->createPluginTexture2D(width, height, format);
    }

    void PluginTextureAdapter::updateTexture2D(plugin::PluginTextureHandle handle,
                                               std::vector<std::byte>&& data)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return;
        handler->updatePluginTexture2D(handle, std::move(data));
    }

    void PluginTextureAdapter::destroyTexture2D(plugin::PluginTextureHandle handle)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return;
        handler->destroyPluginTexture2D(handle);
    }

    std::string PluginTextureAdapter::registerUITexture(plugin::PluginTextureHandle handle)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return {};
        return handler->registerPluginUITexture(handle);
    }

    void PluginTextureAdapter::unregisterUITexture(plugin::PluginTextureHandle handle)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return;
        handler->unregisterPluginUITexture(handle);
    }

    void PluginTextureAdapter::bindWorldMask(plugin::PluginTextureHandle handle,
                                             const glm::vec3& worldMin, const glm::vec3& worldMax,
                                             const plugin::WorldMaskParams& params)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return;
        handler->bindWorldMask(handle, worldMin, worldMax, params);
    }

    void PluginTextureAdapter::unbindWorldMask()
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return;
        handler->unbindWorldMask();
    }

    void PluginTextureAdapter::setWorldMaskParams(const plugin::WorldMaskParams& params)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return;
        handler->setWorldMaskParams(params);
    }

    float PluginTextureAdapter::sampleWorldMask(float worldX, float worldZ) const
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return 1.0f;  // no renderer -> mask has no effect
        return handler->sampleWorldMask(worldX, worldZ);
    }
}
