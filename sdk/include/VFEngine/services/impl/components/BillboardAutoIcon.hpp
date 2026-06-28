#pragma once

#include "../../data/EntityConversion.hpp"
#include "../../data/EntityHandle.hpp"
#include "components/Components.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"

#include <functional>

namespace services::components_helpers
{
    using IconStillNeeded = std::function<bool(const scene::Entity&)>;

    inline void autoAttachBillboard(EntityHandle entity,
                                    components::BillboardIconType iconType,
                                    bool editorOnly = true,
                                    bool selectable = true)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::BillboardComponent>())
        {
            auto& billboard = sceneEntity.addComponent<components::BillboardComponent>();
            billboard.iconType = iconType;
            billboard.editorOnly = editorOnly;
            billboard.selectable = selectable;
        }
    }

    inline void autoDetachBillboard(EntityHandle entity,
                                    components::BillboardIconType iconType,
                                    IconStillNeeded iconStillNeeded = {})
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return;

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (iconStillNeeded && iconStillNeeded(sceneEntity))
            return;

        if (sceneEntity.hasComponent<components::BillboardComponent>())
        {
            auto& billboard = sceneEntity.getComponent<components::BillboardComponent>();
            if (billboard.iconType == iconType)
                sceneEntity.removeComponent<components::BillboardComponent>();
        }
    }
}
