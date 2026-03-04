#include "RenderHookServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/render/RenderHookEvents.hpp"
#include "../../providers/render/IRenderHookProvider.hpp"

namespace services
{
    RenderHookServiceImpl::RenderHookServiceImpl(IRenderHookProvider* provider)
        : provider(provider)
    {
    }

    RenderHookServiceImpl::~RenderHookServiceImpl() = default;

    void RenderHookServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        registerHookHandlers(dispatcher);
    }

    void RenderHookServiceImpl::registerHookHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::renderhook::RegisterRenderPassHookCommand>(
            [this](const events::renderhook::RegisterRenderPassHookCommand& cmd) -> plugin::RenderHookHandle
            {
                return provider->registerHook(cmd.hookPoint, std::move(cmd.callback));
            }
        );

        dispatcher.registerCommandHandler<events::renderhook::UnregisterRenderPassHookCommand>(
            [this](const events::renderhook::UnregisterRenderPassHookCommand& cmd)
            {
                provider->unregisterHook(cmd.handle);
            }
        );
    }
}
