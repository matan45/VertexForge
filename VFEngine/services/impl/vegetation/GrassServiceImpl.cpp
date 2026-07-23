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
        dispatcher.unregisterCommandHandler<events::vegetation::SetGlobalGrassConfigCommand>();
        dispatcher.unregisterQueryHandler<events::vegetation::GetGlobalGrassConfigQuery>();
        dispatcher.unregisterCommandHandler<events::vegetation::SetGlobalScatterProfileCommand>();
        dispatcher.unregisterQueryHandler<events::vegetation::GetGlobalScatterProfileQuery>();
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

        dispatcher.registerCommandHandler<events::vegetation::SetGlobalGrassConfigCommand>(
            [this](const events::vegetation::SetGlobalGrassConfigCommand& cmd) {
                setGlobalGrassConfig(cmd.config);
            });

        dispatcher.registerQueryHandler<events::vegetation::GetGlobalGrassConfigQuery>(
            [this](const events::vegetation::GetGlobalGrassConfigQuery&) {
                return getGlobalGrassConfig();
            });

        dispatcher.registerCommandHandler<events::vegetation::SetGlobalScatterProfileCommand>(
            [this](const events::vegetation::SetGlobalScatterProfileCommand& cmd) {
                setGlobalScatterProfile(cmd.profile);
            });

        dispatcher.registerQueryHandler<events::vegetation::GetGlobalScatterProfileQuery>(
            [this](const events::vegetation::GetGlobalScatterProfileQuery&) {
                return getGlobalScatterProfile();
            });

        // Billboard palette handlers moved to VegetationBrushServiceImpl
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

    void GrassServiceImpl::setGlobalGrassConfig(const vegetation::GrassRenderConfig& config)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::GrassComponent>();

        entt::entity target = entt::null;
        for (auto entity : view)
        {
            target = entity;
            break;
        }

        if (target == entt::null)
        {
            // Create a dedicated grass config entity
            target = registry.create();
            registry.emplace<components::GrassComponent>(target);
        }

        auto& comp = registry.get<components::GrassComponent>(target);
        comp.config = config;

        events::vegetation::GrassConfigChangedNotification notification;
        notification.entityId = internal::toHandle(target);
        events::EventDispatcher::instance().publish(notification);
    }

    vegetation::GrassRenderConfig GrassServiceImpl::getGlobalGrassConfig() const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::GrassComponent>();
        for (auto entity : view)
        {
            const auto& comp = view.get<components::GrassComponent>(entity);
            return comp.config;
        }
        return {};
    }

    void GrassServiceImpl::setGlobalScatterProfile(const vegetation::ScatterProfile& profile)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::GrassComponent>();

        entt::entity target = entt::null;
        for (auto entity : view)
        {
            target = entity;
            break;
        }

        if (target == entt::null)
        {
            target = registry.create();
            registry.emplace<components::GrassComponent>(target);
        }

        registry.get<components::GrassComponent>(target).scatterProfile = profile;
    }

    vegetation::ScatterProfile GrassServiceImpl::getGlobalScatterProfile() const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::GrassComponent>();
        for (auto entity : view)
        {
            return view.get<components::GrassComponent>(entity).scatterProfile;
        }
        return {};
    }
}
