#include "PluginTextureServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/render/PluginTextureEvents.hpp"
#include "../../providers/render/IPluginTextureProvider.hpp"

namespace services
{
    PluginTextureServiceImpl::PluginTextureServiceImpl(IPluginTextureProvider* provider)
        : provider(provider)
    {
    }

    PluginTextureServiceImpl::~PluginTextureServiceImpl() = default;

    void PluginTextureServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        registerTextureHandlers(dispatcher);
    }

    void PluginTextureServiceImpl::registerTextureHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::plugintexture::CreateTexture2DCommand>(
            [this](const events::plugintexture::CreateTexture2DCommand& cmd) -> plugin::PluginTextureHandle
            {
                return provider->createTexture2D(cmd.width, cmd.height, cmd.format);
            }
        );

        dispatcher.registerCommandHandler<events::plugintexture::UpdateTexture2DCommand>(
            [this](const events::plugintexture::UpdateTexture2DCommand& cmd)
            {
                provider->updateTexture2D(cmd.handle, std::move(cmd.data));
            }
        );

        dispatcher.registerCommandHandler<events::plugintexture::DestroyTexture2DCommand>(
            [this](const events::plugintexture::DestroyTexture2DCommand& cmd)
            {
                provider->destroyTexture2D(cmd.handle);
            }
        );

        dispatcher.registerCommandHandler<events::plugintexture::BindWorldMaskCommand>(
            [this](const events::plugintexture::BindWorldMaskCommand& cmd)
            {
                provider->bindWorldMask(cmd.handle, cmd.worldMin, cmd.worldMax, cmd.params);
            }
        );

        dispatcher.registerCommandHandler<events::plugintexture::UnbindWorldMaskCommand>(
            [this](const events::plugintexture::UnbindWorldMaskCommand&)
            {
                provider->unbindWorldMask();
            }
        );

        dispatcher.registerCommandHandler<events::plugintexture::SetWorldMaskParamsCommand>(
            [this](const events::plugintexture::SetWorldMaskParamsCommand& cmd)
            {
                provider->setWorldMaskParams(cmd.params);
            }
        );
    }
}
