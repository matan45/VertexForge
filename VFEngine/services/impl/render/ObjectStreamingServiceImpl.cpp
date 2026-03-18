#include "ObjectStreamingServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/render/ObjectStreamingEvents.hpp"
#include "../../providers/render/IObjectStreamingProvider.hpp"

namespace services
{
    ObjectStreamingServiceImpl::ObjectStreamingServiceImpl(IObjectStreamingProvider* provider)
        : provider(provider)
    {
    }

    ObjectStreamingServiceImpl::~ObjectStreamingServiceImpl() = default;

    void ObjectStreamingServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::render::objectstreaming::SetObjectStreamingEnabledCommand>(
            [this](const events::render::objectstreaming::SetObjectStreamingEnabledCommand& cmd)
            {
                provider->setObjectStreamingEnabled(cmd.enabled);
            }
        );

        dispatcher.registerCommandHandler<events::render::objectstreaming::SetObjectStreamingConfigCommand>(
            [this](const events::render::objectstreaming::SetObjectStreamingConfigCommand& cmd)
            {
                provider->setObjectStreamingConfig(cmd.config);
            }
        );

        dispatcher.registerCommandHandler<events::render::objectstreaming::RegisterSectorObjectsCommand>(
            [this](const events::render::objectstreaming::RegisterSectorObjectsCommand& cmd)
            {
                provider->registerSectorObjects(cmd.sectorId, cmd.entities);
            }
        );

        dispatcher.registerCommandHandler<events::render::objectstreaming::UnregisterSectorObjectsCommand>(
            [this](const events::render::objectstreaming::UnregisterSectorObjectsCommand& cmd)
            {
                provider->unregisterSectorObjects(cmd.sectorId);
            }
        );

        dispatcher.registerQueryHandler<events::render::objectstreaming::GetObjectStreamingStatsQuery>(
            [this](const events::render::objectstreaming::GetObjectStreamingStatsQuery&)
                -> render::gpudriven::ObjectStreamingStats
            {
                return provider->getObjectStreamingStats();
            }
        );

        dispatcher.registerQueryHandler<events::render::objectstreaming::GetObjectStreamingConfigQuery>(
            [this](const events::render::objectstreaming::GetObjectStreamingConfigQuery&)
                -> render::gpudriven::ObjectStreamConfig
            {
                return provider->getObjectStreamingConfig();
            }
        );
    }
}
