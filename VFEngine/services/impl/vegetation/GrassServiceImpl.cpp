#include "GrassServiceImpl.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/VegetationComponents.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vegetation/GrassEvents.hpp"

namespace services
{
    GrassServiceImpl::~GrassServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unregisterCommandHandler<events::vegetation::SetGrassConfigCommand>();
        dispatcher.unregisterQueryHandler<events::vegetation::GetGrassConfigQuery>();
    }

    void GrassServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::vegetation::SetGrassConfigCommand>(
            [this](const events::vegetation::SetGrassConfigCommand& cmd) {
                setGrassConfig(cmd.entityId, cmd.config);
            });

        dispatcher.registerQueryHandler<events::vegetation::GetGrassConfigQuery>(
            [this](const events::vegetation::GetGrassConfigQuery& query) {
                return getGrassConfig(query.entityId);
            });
    }

    void GrassServiceImpl::setGrassConfig(EntityHandle entityId, const vegetation::GrassRenderConfig& config)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entityId, registry)) {
            return;
        }

        auto entity = internal::fromHandle(entityId);
        if (!registry.all_of<components::GrassComponent>(entity)) {
            return;
        }

        auto& comp = registry.get<components::GrassComponent>(entity);
        comp.config = config;

        events::vegetation::GrassConfigChangedNotification notification;
        notification.entityId = entityId;
        events::EventDispatcher::instance().publish(notification);
    }

    vegetation::GrassRenderConfig GrassServiceImpl::getGrassConfig(EntityHandle entityId) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entityId, registry)) {
            return {};
        }

        auto entity = internal::fromHandle(entityId);
        if (!registry.all_of<components::GrassComponent>(entity)) {
            return {};
        }

        const auto& comp = registry.get<components::GrassComponent>(entity);
        return comp.config;
    }
}
