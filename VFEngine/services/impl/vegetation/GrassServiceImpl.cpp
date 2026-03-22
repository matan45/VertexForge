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

        dispatcher.registerCommandHandler<events::vegetation::SetBillboardPaletteCommand>(
            [this](const events::vegetation::SetBillboardPaletteCommand& cmd) {
                // Save to ECS component
                auto& registry = scene::EntityRegistry::getRegistry();
                auto view = registry.view<components::GrassComponent>();
                for (auto entity : view)
                {
                    registry.get<components::GrassComponent>(entity).billboardPalette = cmd.entries;
                    break;
                }
                if (billboardPaletteCb) billboardPaletteCb(cmd.entries, cmd.activeEntry);
            });

        dispatcher.registerQueryHandler<events::vegetation::GetBillboardPaletteQuery>(
            [](const events::vegetation::GetBillboardPaletteQuery&) -> std::vector<vegetation::BillboardPaletteEntry> {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto view = registry.view<components::GrassComponent>();
                for (auto entity : view)
                {
                    return view.get<components::GrassComponent>(entity).billboardPalette;
                }
                return {};
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
}
