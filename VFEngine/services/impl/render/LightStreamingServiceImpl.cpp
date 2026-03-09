#include "LightStreamingServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/render/LightStreamingEvents.hpp"
#include "../../providers/render/ILightStreamingProvider.hpp"

namespace services
{
    LightStreamingServiceImpl::LightStreamingServiceImpl(ILightStreamingProvider* provider)
        : provider(provider)
    {
    }

    LightStreamingServiceImpl::~LightStreamingServiceImpl() = default;

    void LightStreamingServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::render::lightstreaming::SetLightStreamingConfigCommand>(
            [this](const events::render::lightstreaming::SetLightStreamingConfigCommand& cmd)
            {
                provider->setLightStreamingConfig(cmd.config);
            }
        );

        dispatcher.registerCommandHandler<events::render::lightstreaming::RegisterSectorLightsCommand>(
            [this](const events::render::lightstreaming::RegisterSectorLightsCommand& cmd)
            {
                provider->registerSectorLights(cmd.sectorId);
            }
        );

        dispatcher.registerCommandHandler<events::render::lightstreaming::UnregisterSectorLightsCommand>(
            [this](const events::render::lightstreaming::UnregisterSectorLightsCommand& cmd)
            {
                provider->unregisterSectorLights(cmd.sectorId);
            }
        );

        dispatcher.registerQueryHandler<events::render::lightstreaming::GetLightStreamingStatsQuery>(
            [this](const events::render::lightstreaming::GetLightStreamingStatsQuery&)
                -> render::lighting::LightStreamingStats
            {
                return provider->getLightStreamingStats();
            }
        );

        dispatcher.registerQueryHandler<events::render::lightstreaming::GetLightStreamingConfigQuery>(
            [this](const events::render::lightstreaming::GetLightStreamingConfigQuery&)
                -> render::lighting::LightStreamingConfig
            {
                return provider->getLightStreamingConfig();
            }
        );
    }
}
