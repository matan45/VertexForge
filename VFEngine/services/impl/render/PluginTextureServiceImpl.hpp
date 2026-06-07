#pragma once
#include "../../interfaces/render/IPluginTextureService.hpp"

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class IPluginTextureProvider;

    class PluginTextureServiceImpl : public IPluginTextureService
    {
    private:
        IPluginTextureProvider* provider;

    public:
        explicit PluginTextureServiceImpl(IPluginTextureProvider* provider);
        ~PluginTextureServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void registerTextureHandlers(::events::EventDispatcher& dispatcher);
    };
}
