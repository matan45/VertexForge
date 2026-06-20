#include "PostProcessEffectServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/render/PostProcessEffectEvents.hpp"
#include "../../providers/render/IPostProcessEffectProvider.hpp"

namespace services
{
    PostProcessEffectServiceImpl::PostProcessEffectServiceImpl(IPostProcessEffectProvider* provider)
        : provider(provider)
    {
    }

    PostProcessEffectServiceImpl::~PostProcessEffectServiceImpl() = default;

    void PostProcessEffectServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        registerEffectHandlers(dispatcher);
    }

    void PostProcessEffectServiceImpl::registerEffectHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::postprocessfx::RegisterPostProcessEffectCommand>(
            [this](const events::postprocessfx::RegisterPostProcessEffectCommand& cmd) -> plugin::PostProcessEffectHandle
            {
                return provider->registerEffect(cmd.desc);
            }
        );

        dispatcher.registerCommandHandler<events::postprocessfx::UpdatePostProcessEffectParamsCommand>(
            [this](const events::postprocessfx::UpdatePostProcessEffectParamsCommand& cmd)
            {
                provider->updateEffectParams(cmd.handle, std::move(cmd.params));
            }
        );

        dispatcher.registerCommandHandler<events::postprocessfx::SetPostProcessEffectEnabledCommand>(
            [this](const events::postprocessfx::SetPostProcessEffectEnabledCommand& cmd)
            {
                provider->setEffectEnabled(cmd.handle, cmd.enabled);
            }
        );

        dispatcher.registerCommandHandler<events::postprocessfx::UnregisterPostProcessEffectCommand>(
            [this](const events::postprocessfx::UnregisterPostProcessEffectCommand& cmd)
            {
                provider->unregisterEffect(cmd.handle);
            }
        );
    }
}
