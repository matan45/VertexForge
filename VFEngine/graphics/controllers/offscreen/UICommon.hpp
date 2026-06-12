#pragma once
#include "ui/UIRectMath.hpp"
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <algorithm>
#include <cmath>
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
    using utilities::ui::isInteractionAllowed;
    using utilities::ui::findOpenWindowAncestor;
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

    // Approximate text-mode tooltip bubble dimensions. Uses the same
    // avg-char-width estimate as the text-input caret (fontSize * 0.55) —
    // the actual glyph layout happens in the text pipeline; this only sizes
    // the background quad and the wrap rect.
    struct TooltipSizeInfo
    {
        glm::vec2 bgSize{0.0f, 0.0f};
        glm::vec2 contentSize{0.0f, 0.0f};
        glm::vec2 contentOffset{0.0f, 0.0f}; // content top-left relative to bg
    };

    inline TooltipSizeInfo estimateTooltipSize(const components::UITooltipComponent& tip, float scale)
    {
        TooltipSizeInfo info;
        float avgCharWidth = tip.fontSize * scale * 0.55f;
        float lineHeight = tip.fontSize * scale * 1.25f;
        float padL = tip.padding.x * scale;
        float padR = tip.padding.y * scale;
        float padT = tip.padding.z * scale;
        float padB = tip.padding.w * scale;

        float maxContentW = std::max(avgCharWidth, tip.maxWidth * scale - padL - padR);
        float textW = static_cast<float>(tip.text.size()) * avgCharWidth;
        float contentW = std::min(std::max(textW, avgCharWidth), maxContentW);
        int lineCount = std::max(1, static_cast<int>(std::ceil(textW / maxContentW)));

        info.contentSize = glm::vec2(contentW, static_cast<float>(lineCount) * lineHeight);
        info.contentOffset = glm::vec2(padL, padT);
        info.bgSize = info.contentSize + glm::vec2(padL + padR, padT + padB);
        return info;
    }
}
