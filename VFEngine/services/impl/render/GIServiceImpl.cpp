#include "GIServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/render/GIEvents.hpp"
#include "../../providers/render/IGIProvider.hpp"

namespace services
{
    GIServiceImpl::GIServiceImpl(IGIProvider* provider)
        : provider(provider)
    {
    }

    GIServiceImpl::~GIServiceImpl() = default;

    void GIServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::render::gi::ApplyGISettingsCommand>(
            [this](const events::render::gi::ApplyGISettingsCommand& cmd)
            {
                provider->applyGISettings(cmd.settings);
            }
        );

        dispatcher.registerCommandHandler<events::render::gi::SetGIQualityCommand>(
            [this](const events::render::gi::SetGIQualityCommand& cmd)
            {
                auto settings = render::gi::GISettings::fromQuality(cmd.quality);
                provider->applyGISettings(settings);
            }
        );

        dispatcher.registerCommandHandler<events::render::gi::SetGIEnabledCommand>(
            [this](const events::render::gi::SetGIEnabledCommand& cmd)
            {
                provider->setGIEnabled(cmd.enabled);
            }
        );

        dispatcher.registerCommandHandler<events::render::gi::SetGIDebugProbesCommand>(
            [this](const events::render::gi::SetGIDebugProbesCommand& cmd)
            {
                provider->setShowProbes(cmd.show);
            }
        );

        dispatcher.registerCommandHandler<events::render::gi::SetGIDebugCascadeBoundsCommand>(
            [this](const events::render::gi::SetGIDebugCascadeBoundsCommand& cmd)
            {
                provider->setShowCascadeBounds(cmd.show);
            }
        );

        dispatcher.registerCommandHandler<events::render::gi::SetGIDebugProbeValidityCommand>(
            [this](const events::render::gi::SetGIDebugProbeValidityCommand& cmd)
            {
                provider->setShowProbeValidity(cmd.show);
            }
        );

        dispatcher.registerQueryHandler<events::render::gi::GetGISettingsQuery>(
            [this](const events::render::gi::GetGISettingsQuery&) -> render::gi::GISettings
            {
                return provider->getGISettings();
            }
        );

        dispatcher.registerQueryHandler<events::render::gi::GetGIDebugStatsQuery>(
            [this](const events::render::gi::GetGIDebugStatsQuery&) -> render::gi::GIDebugStats
            {
                return provider->getGIDebugStats();
            }
        );

        dispatcher.registerQueryHandler<events::render::gi::IsGIEnabledQuery>(
            [this](const events::render::gi::IsGIEnabledQuery&) -> bool
            {
                return provider->isGIEnabled();
            }
        );
    }
}
