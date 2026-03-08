#include "BillboardRenderServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/render/BillboardEvents.hpp"
#include "../../providers/render/IBillboardRenderProvider.hpp"

namespace services
{
    BillboardRenderServiceImpl::BillboardRenderServiceImpl(IBillboardRenderProvider* provider)
        : provider(provider)
    {
    }

    BillboardRenderServiceImpl::~BillboardRenderServiceImpl() = default;

    void BillboardRenderServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        registerBillboardHandlers(dispatcher);
    }

    void BillboardRenderServiceImpl::registerBillboardHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::render::SetBillboardRenderingEnabledCommand>(
            [this](const events::render::SetBillboardRenderingEnabledCommand& cmd)
            {
                provider->setBillboardRenderingEnabled(cmd.enabled);
            }
        );

        dispatcher.registerCommandHandler<events::render::SetBillboardMaxDistanceCommand>(
            [this](const events::render::SetBillboardMaxDistanceCommand& cmd)
            {
                provider->setBillboardMaxDistance(cmd.distance);
            }
        );

        dispatcher.registerQueryHandler<events::render::GetBillboardStatsQuery>(
            [this](const events::render::GetBillboardStatsQuery&) -> BillboardRenderStats
            {
                return provider->getBillboardStats();
            }
        );

        dispatcher.registerCommandHandler<events::render::BakeImposterCommand>(
            [this](const events::render::BakeImposterCommand& cmd) -> ImposterBakeResult
            {
                return provider->bakeImposter(cmd.meshPath, cmd.outputPath);
            }
        );
    }
}
