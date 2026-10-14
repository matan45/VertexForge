#include "GIServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/render/GIEvents.hpp"
#include "../../events/scene/ComponentPhysicsLightEvents.hpp"
#include "../../events/scene/SceneSettingsEvents.hpp"
#include "../../providers/render/IGIProvider.hpp"
#include "print/Log.hpp"
#include <exception>

namespace services
{
    GIServiceImpl::GIServiceImpl(IGIProvider* provider)
        : provider(provider)
    {
    }

    GIServiceImpl::~GIServiceImpl()
    {
        if (renderSettingsChangedToken.isValid())
        {
            ::events::EventDispatcher::instance().unsubscribe(renderSettingsChangedToken);
        }
    }

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

        // VK-1654: ScenePersistenceService re-applies post-process, atmosphere and clouds on a scene
        // load / new scene / Play->Stop restore, but not GI (this service is registered by the Editor
        // only). Apply the scene's GI here. The debug views are session state, not scene data, so
        // the live flags are kept.
        renderSettingsChangedToken = dispatcher.subscribe<events::scene::RenderSettingsChangedNotification>(
            [this](const events::scene::RenderSettingsChangedNotification& n)
            {
                if (!n.sceneLoad || !provider)
                    return;

                // EventDispatcher::publish swallows subscriber exceptions without a log; report a
                // failed apply here so a scene that loads without its GI is diagnosable.
                try
                {
                    auto gi = ::events::EventDispatcher::instance().query(events::scene::GetRenderSettingsQuery{}).gi;
                    const auto live = provider->getGISettings();
                    gi.showProbes = live.showProbes;
                    gi.showCascadeBounds = live.showCascadeBounds;
                    gi.showProbeValidity = live.showProbeValidity;
                    provider->applyGISettings(gi);
                }
                catch (const std::exception& e)
                {
                    vfLogWarning("[GI] Applying the scene's GI settings failed: {}", e.what());
                }
            }
        );
    }
}
