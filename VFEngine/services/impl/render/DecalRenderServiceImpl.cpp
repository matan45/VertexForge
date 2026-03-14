#include "DecalRenderServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/render/DecalEvents.hpp"
#include "../../providers/render/IDecalRenderProvider.hpp"

namespace services
{
    DecalRenderServiceImpl::DecalRenderServiceImpl(IDecalRenderProvider* provider)
        : provider(provider)
    {
    }

    DecalRenderServiceImpl::~DecalRenderServiceImpl() = default;

    void DecalRenderServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        registerDecalHandlers(dispatcher);
    }

    void DecalRenderServiceImpl::registerDecalHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::render::SetDecalRenderingEnabledCommand>(
            [this](const events::render::SetDecalRenderingEnabledCommand& cmd)
            {
                provider->setDecalRenderingEnabled(cmd.enabled);
            }
        );

        dispatcher.registerQueryHandler<events::render::IsDecalRenderingEnabledQuery>(
            [this](const events::render::IsDecalRenderingEnabledQuery&) -> bool
            {
                return provider->isDecalRenderingEnabled();
            }
        );
    }
}
