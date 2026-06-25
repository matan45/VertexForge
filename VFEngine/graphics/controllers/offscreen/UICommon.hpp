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

    // VK-1435 — scoped active check used only by the UI Layer Builder's offscreen preview.
    // Identical to scene::Entity::isEffectivelyActive EXCEPT the walk up the parent chain stops
    // at (and treats as active) `scopeRoot`: the builder sandbox canvas root is intentionally
    // inactive so the main UI passes skip it, but its descendants must still render in the
    // preview by their own (and intermediate parents') active flags. With scopeRoot == entt::null
    // this is byte-identical to the engine-wide effective-active test, so the runtime emit paths
    // that pass nothing keep their exact behavior.
    inline bool isEffectivelyActiveWithin(entt::registry& registry, entt::entity entity,
                                          entt::entity scopeRoot)
    {
        if (scopeRoot == entt::null)
            return scene::Entity::isEffectivelyActive(registry, entity);

        entt::entity current = entity;
        while (current != entt::null && registry.valid(current))
        {
            if (current == scopeRoot)
                return true; // reached the sandbox root — treat it as active, stop walking up
            if (registry.all_of<components::NameComponent>(current) &&
                !registry.get<components::NameComponent>(current).isActive)
                return false;
            if (registry.all_of<components::ParentComponent>(current))
                current = registry.get<components::ParentComponent>(current).parent;
            else
                break;
        }
        return true;
    }

    // VK-1435 — combined scoped gate for the UI Layer Builder's offscreen preview. The widget
    // emitters previously walked the parent chain twice on the scoped path: once via
    // isEffectivelyActiveWithin (active flags up to scopeRoot) and again via
    // findCanvasWithEntity (nearest canvas ancestor, compared to scopeRoot). This walks the chain
    // once and yields both, returning true only when the entity is effectively active within the
    // scope AND its owning canvas is scopeRoot. With scopeRoot == entt::null it is byte-identical
    // to isEffectivelyActiveWithin alone (the canvas check the emitters guard with
    // `scopeRoot != entt::null` was skipped there), so the runtime emit paths are unchanged.
    inline bool isEffectivelyActiveInScopedCanvas(entt::registry& registry, entt::entity entity,
                                                  entt::entity scopeRoot)
    {
        if (scopeRoot == entt::null)
            return scene::Entity::isEffectivelyActive(registry, entity);

        // Active decision (mirrors isEffectivelyActiveWithin) and nearest-canvas-ancestor lookup
        // (mirrors findCanvasWithEntity) computed in a single upward traversal.
        bool active = true;          // default-true, matching isEffectivelyActiveWithin
        bool activeDecided = false;
        entt::entity canvasEntity = entt::null;
        bool canvasDecided = false;

        entt::entity current = entity;
        while (current != entt::null && registry.valid(current))
        {
            // isEffectivelyActiveWithin per-node logic, evaluated on `current`.
            if (!activeDecided)
            {
                if (current == scopeRoot)
                {
                    active = true;       // reached the sandbox root — treat as active
                    activeDecided = true;
                }
                else if (registry.all_of<components::NameComponent>(current) &&
                         !registry.get<components::NameComponent>(current).isActive)
                {
                    active = false;
                    activeDecided = true;
                }
            }

            // findCanvasWithEntity inspects the *parent* for a canvas before descending; the
            // entity itself is only a canvas-match fallback once the chain is exhausted.
            entt::entity parent = entt::null;
            bool hasParent = registry.all_of<components::ParentComponent>(current);
            if (hasParent)
            {
                parent = registry.get<components::ParentComponent>(current).parent;
                if (parent == entt::null || !registry.valid(parent))
                    hasParent = false;
            }

            if (!canvasDecided && hasParent &&
                registry.all_of<components::UICanvasComponent>(parent))
            {
                canvasEntity = parent; // nearest canvas ancestor
                canvasDecided = true;
            }

            if (activeDecided && canvasDecided)
                break;

            if (hasParent)
                current = parent;
            else
                break;
        }

        // Self is the canvas only if no canvas ancestor was found (findCanvasWithEntity fallback).
        if (!canvasDecided &&
            registry.all_of<components::UICanvasComponent>(entity))
        {
            canvasEntity = entity;
        }

        return active && (canvasEntity == scopeRoot);
    }

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
        if (tip.text.size() > 1)
            textW += static_cast<float>(tip.text.size() - 1) * tip.letterSpacing * scale;
        float contentW = std::min(std::max(textW, avgCharWidth), maxContentW);
        int lineCount = std::max(1, static_cast<int>(std::ceil(textW / maxContentW)));

        info.contentSize = glm::vec2(contentW, static_cast<float>(lineCount) * lineHeight);
        info.contentOffset = glm::vec2(padL, padT);
        info.bgSize = info.contentSize + glm::vec2(padL + padR, padT + padB);
        return info;
    }
}
