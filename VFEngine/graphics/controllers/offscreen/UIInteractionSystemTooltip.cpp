#include "UIInteractionSystem.hpp"
#include "FramePreparationSystem.hpp"  // for FrameContext
#include "UICommon.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include <limits>

using namespace controllers::offscreen::ui_common;

namespace controllers::offscreen
{
    namespace
    {
        entt::entity findTooltipPanelChild(entt::registry& registry, entt::entity owner,
                                           const std::string& panelChildName)
        {
            auto* children = registry.try_get<components::ChildrenComponent>(owner);
            if (!children) return entt::null;

            for (entt::entity child : children->children)
            {
                if (!registry.valid(child)) continue;
                if (!registry.all_of<components::UIRectComponent>(child)) continue;

                if (!panelChildName.empty())
                {
                    auto* name = registry.try_get<components::NameComponent>(child);
                    if (name && name->name == panelChildName)
                        return child;
                }
                else
                {
                    // First inactive child panel by convention
                    auto* name = registry.try_get<components::NameComponent>(child);
                    if (name && !name->isActive)
                        return child;
                }
            }
            return entt::null;
        }

        void hideShownPanel(entt::registry& registry, components::UITooltipState& state)
        {
            if (state.shownPanelChild != entt::null && registry.valid(state.shownPanelChild))
            {
                if (auto* name = registry.try_get<components::NameComponent>(state.shownPanelChild))
                    name->isActive = false;
            }
            state.shownPanelChild = entt::null;
        }
    }

    void UIInteractionSystem::processTooltipInteraction(const FrameContext& ctx)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& state = registry.ctx().emplace<components::UITooltipState>();

        if (!ctx.playModeActive)
        {
            hideShownPanel(registry, state);
            state.hoveredEntity = entt::null;
            state.hoverTime = 0.0f;
            state.visible = false;
            return;
        }

        float vw = static_cast<float>(ctx.viewportWidth);
        float vh = static_cast<float>(ctx.viewportHeight);
        auto scrollContainers = buildScrollContainerMap(registry, vw, vh);

        // Smallest-area enabled tooltip owner under the cursor
        entt::entity hovered = entt::null;
        float smallestArea = std::numeric_limits<float>::max();
        float hoveredScale = 1.0f;
        PixelRect hoveredRect{0.0f, 0.0f, 0.0f, 0.0f};

        auto view = registry.view<components::UITooltipComponent, components::UIRectComponent>();
        for (auto entity : view)
        {
            const auto& tip = view.get<components::UITooltipComponent>(entity);
            if (!tip.enabled)
                continue;
            if (!scene::Entity::isEffectivelyActive(registry, entity))
                continue;

            const auto* canvas = findCanvasForEntity(registry, entity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
                canvas = &registry.get<components::UICanvasComponent>(entity);
            if (!canvas)
                continue;

            float scale = computeCanvasScale(canvas, vw, vh);
            const auto& rectComp = view.get<components::UIRectComponent>(entity);
            PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);

            auto [scrollAncestor, scissor] = findScrollInfo(registry, entity, scrollContainers);
            applyScrollOffset(rect, scrollAncestor, scrollContainers);

            if (hitTestRect(ctx.mousePosition, rect, scissor))
            {
                float area = rect.w * rect.h;
                if (area < smallestArea)
                {
                    smallestArea = area;
                    hovered = entity;
                    hoveredScale = scale;
                    hoveredRect = rect;
                }
            }
        }

        // Hover target changed: reset the delay timer and hide
        if (hovered != state.hoveredEntity)
        {
            hideShownPanel(registry, state);
            state.hoveredEntity = hovered;
            state.hoverTime = 0.0f;
            state.visible = false;
        }

        if (hovered == entt::null)
            return;

        state.hoverTime += ctx.deltaTime;
        const auto& tip = registry.get<components::UITooltipComponent>(hovered);

        bool shouldShow = state.hoverTime >= tip.showDelay;
        if (!shouldShow)
            return;

        if (tip.mode == components::UITooltipMode::ChildPanel)
        {
            if (state.shownPanelChild == entt::null)
            {
                entt::entity panel = findTooltipPanelChild(registry, hovered, tip.panelChildName);
                if (panel != entt::null)
                {
                    if (auto* name = registry.try_get<components::NameComponent>(panel))
                        name->isActive = true;
                    state.shownPanelChild = panel;
                }
            }
            state.visible = state.shownPanelChild != entt::null;
            return;
        }

        // Text mode: place the synthetic bubble (frame builders emit it on the
        // UI overlay layer). followCursor anchors at the mouse; otherwise the
        // element's bottom-left corner.
        auto sizeInfo = estimateTooltipSize(tip, hoveredScale);
        glm::vec2 anchor = tip.followCursor
            ? ctx.mousePosition
            : glm::vec2(hoveredRect.x, hoveredRect.y + hoveredRect.h);
        glm::vec2 offset = tip.offset * hoveredScale;

        state.displayPos = utilities::ui::computeTooltipPlacement(
            anchor, offset, sizeInfo.bgSize, vw, vh);
        state.bgSize = sizeInfo.bgSize;
        state.contentOffset = sizeInfo.contentOffset;
        state.contentSize = sizeInfo.contentSize;
        state.canvasScale = hoveredScale;
        state.visible = true;
    }
}
