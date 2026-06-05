#pragma once
#include "ui/UIRectMath.hpp"
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <string>
#include <utility>

namespace services
{
    struct EntityHandle;
}

// Pure UI layout/hit-test math lives in utilities (ui/UIRectMath.hpp) so the
// services layer can reuse it (editor viewport UI picking). Re-exported here
// so existing graphics code keeps using controllers::offscreen::ui_common.
namespace controllers::offscreen::ui_common
{
    using utilities::ui::PixelRect;
    using utilities::ui::ScrollContainerInfo;
    using utilities::ui::CanvasEntityInfo;
    using utilities::ui::resolvePixelRect;
    using utilities::ui::findCanvasForEntity;
    using utilities::ui::findCanvasWithEntity;
    using utilities::ui::computeCanvasScale;
    using utilities::ui::isEntityActive;
    using utilities::ui::buildScrollContainerMap;
    using utilities::ui::findScrollInfo;
    using utilities::ui::applyScrollOffset;
    using utilities::ui::hitTestRect;
    using utilities::ui::computeCanvasImageModelMatrix;
    using utilities::ui::computeCanvasSubRectModelMatrix;

    inline std::pair<services::EntityHandle, std::string> makeEntityPayload(
        entt::registry& registry, entt::entity entity)
    {
        services::EntityHandle handle = services::internal::toHandle(entity);
        std::string name;
        if (registry.all_of<components::NameComponent>(entity))
            name = registry.get<components::NameComponent>(entity).name;
        return {handle, std::move(name)};
    }
}
