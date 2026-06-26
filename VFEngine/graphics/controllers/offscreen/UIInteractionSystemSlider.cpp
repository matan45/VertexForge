#include "UIInteractionSystem.hpp"
#include "UICommon.hpp"
#include "FramePreparationSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UIEvents.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace controllers::offscreen
{
    using ui_common::PixelRect;

    namespace
    {
        // VK-1442 — apply a tabs widget's pane visibility: the tab bar is the FIRST direct child
        // carrying a UILayoutGroup; the panes are the tabs entity's OTHER direct children, in order;
        // pane p is active iff p == activeTabIndex. Shared by processTabsInteraction's click handler
        // and applyTabsActivePaneScoped (edit preview). Writing NameComponent.isActive matches the
        // authored state the runtime sets, so persisting it is correct.
        void applyTabsPaneVisibility(entt::registry& registry, entt::entity tabsEntity, int activeTabIndex)
        {
            if (!registry.all_of<components::ChildrenComponent>(tabsEntity))
                return;

            const auto& children = registry.get<components::ChildrenComponent>(tabsEntity).children;

            entt::entity tabBarEntity = entt::null;
            for (auto childEntity : children)
            {
                if (!registry.valid(childEntity)) continue;
                if (registry.all_of<components::UILayoutGroupComponent>(childEntity))
                {
                    tabBarEntity = childEntity;
                    break;
                }
            }
            if (tabBarEntity == entt::null)
                return;

            int paneIndex = 0;
            for (auto childEntity : children)
            {
                if (!registry.valid(childEntity)) continue;
                if (childEntity == tabBarEntity) continue;
                if (registry.all_of<components::NameComponent>(childEntity))
                    registry.get<components::NameComponent>(childEntity).isActive = (paneIndex == activeTabIndex);
                ++paneIndex;
            }
        }
    }

    void UIInteractionSystem::processTabsInteraction(const FrameContext& ctx)
    {
        if (!ctx.playModeActive)
            return;

        if (!ctx.leftMouseReleased)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto tabsView = registry.view<components::UITabsComponent, components::UIRectComponent>();

        if (tabsView.size_hint() == 0)
            return;

        float vw = static_cast<float>(ctx.viewportWidth);
        float vh = static_cast<float>(ctx.viewportHeight);

        for (auto tabsEntity : tabsView)
        {
            if (!scene::Entity::isEffectivelyActive(registry, tabsEntity))
                continue;

            if (!ui_common::isInteractionAllowed(registry, tabsEntity))
                continue;

            if (!registry.all_of<components::ChildrenComponent>(tabsEntity))
                continue;

            const auto& children = registry.get<components::ChildrenComponent>(tabsEntity).children;

            entt::entity tabBarEntity = entt::null;
            for (auto childEntity : children)
            {
                if (!registry.valid(childEntity)) continue;
                if (registry.all_of<components::UILayoutGroupComponent>(childEntity))
                {
                    tabBarEntity = childEntity;
                    break;
                }
            }

            if (tabBarEntity == entt::null)
                continue;

            if (!registry.all_of<components::ChildrenComponent>(tabBarEntity))
                continue;

            const auto& tabButtons = registry.get<components::ChildrenComponent>(tabBarEntity).children;

            const auto* canvas = ui_common::findCanvasForEntity(registry, tabsEntity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(tabsEntity))
                canvas = &registry.get<components::UICanvasComponent>(tabsEntity);
            if (!canvas)
                continue;

            float scale = ui_common::computeCanvasScale(canvas, vw, vh);

            for (int i = 0; i < static_cast<int>(tabButtons.size()); ++i)
            {
                auto btnEntity = tabButtons[i];
                if (!registry.valid(btnEntity)) continue;
                if (!registry.all_of<components::UIRectComponent>(btnEntity)) continue;

                if (!scene::Entity::isEffectivelyActive(registry, btnEntity))
                    continue;

                const auto& btnRect = registry.get<components::UIRectComponent>(btnEntity);
                PixelRect rect = ui_common::resolvePixelRect(btnRect, vw, vh, scale);

                bool inside = ctx.mousePosition.x >= rect.x && ctx.mousePosition.x <= rect.x + rect.w
                    && ctx.mousePosition.y >= rect.y && ctx.mousePosition.y <= rect.y + rect.h;

                if (inside)
                {
                    auto& tabsComp = registry.get<components::UITabsComponent>(tabsEntity);

                    int previousTabIndex = tabsComp.activeTabIndex;
                    tabsComp.previousTabIndex = previousTabIndex;
                    tabsComp.activeTabIndex = i;

                    applyTabsPaneVisibility(registry, tabsEntity, i);

                    auto& dispatcher = events::EventDispatcher::instance();
                    auto [handle, entityName] = ui_common::makeEntityPayload(registry, tabsEntity);

                    events::ui::UITabSelectedNotification selectedNotif;
                    selectedNotif.entity = handle;
                    selectedNotif.entityName = entityName;
                    selectedNotif.tabIndex = i;
                    dispatcher.publish(selectedNotif);

                    if (previousTabIndex != i)
                    {
                        events::ui::UITabChangedNotification changedNotif;
                        changedNotif.entity = handle;
                        changedNotif.entityName = entityName;
                        changedNotif.newTabIndex = i;
                        changedNotif.previousTabIndex = previousTabIndex;
                        dispatcher.publish(changedNotif);
                    }

                    break;
                }
            }
        }
    }

    // VK-1442 — display-only tabs pass for the UI Layer Builder's scoped offscreen preview. For each
    // tabs widget active within `scopedCanvas`, it applies the active-pane visibility from the
    // authored activeTabIndex (NO hit-testing, no notifications). Runs BEFORE layout + image emit in
    // the scoped path so the active pane is laid out and rendered. Shares applyTabsPaneVisibility
    // with processTabsInteraction.
    void UIInteractionSystem::applyTabsActivePaneScoped(const FrameContext& ctx, entt::entity scopedCanvas)
    {
        (void)ctx;
        auto& registry = scene::EntityRegistry::getRegistry();
        auto tabsView = registry.view<components::UITabsComponent, components::UIRectComponent>();

        for (auto tabsEntity : tabsView)
        {
            if (!ui_common::isEffectivelyActiveInScopedCanvas(registry, tabsEntity, scopedCanvas))
                continue;
            const auto& tabsComp = registry.get<components::UITabsComponent>(tabsEntity);
            applyTabsPaneVisibility(registry, tabsEntity, tabsComp.activeTabIndex);
        }
    }

    void UIInteractionSystem::processSliderInteraction(const FrameContext& ctx)
    {
        if (!ctx.playModeActive)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto sliderView = registry.view<components::UISliderComponent, components::UIRectComponent>();

        if (sliderView.size_hint() == 0)
            return;

        float vw = static_cast<float>(ctx.viewportWidth);
        float vh = static_cast<float>(ctx.viewportHeight);

        auto& dispatcher = events::EventDispatcher::instance();

        auto snapToStep = [](float value, float minVal, float maxVal, float stepSize) -> float
        {
            if (stepSize > 0.0f)
                value = std::round((value - minVal) / stepSize) * stepSize + minVal;
            return std::max(minVal, std::min(value, maxVal));
        };

        // Phase 1: Process active drags
        for (auto sliderEntity : sliderView)
        {
            auto& comp = registry.get<components::UISliderComponent>(sliderEntity);
            if (!comp.isDragging)
                continue;

            if (ctx.leftMouseDown)
            {
                const auto* canvas = ui_common::findCanvasForEntity(registry, sliderEntity);
                if (!canvas) { comp.isDragging = false; break; }

                float scale = ui_common::computeCanvasScale(canvas, vw, vh);
                const auto& rectComp = registry.get<components::UIRectComponent>(sliderEntity);
                PixelRect rect = ui_common::resolvePixelRect(rectComp, vw, vh, scale);

                float previousValue = comp.value;
                float newValue;

                if (comp.orientation == components::UISliderOrientation::Horizontal)
                {
                    float trackWidth = rect.w;
                    if (trackWidth > 0.0f)
                    {
                        float deltaMouseX = ctx.mousePosition.x - comp.dragStartMousePos.x;
                        newValue = comp.dragStartValue + (deltaMouseX / trackWidth) * (comp.maxValue - comp.minValue);
                    }
                    else
                    {
                        newValue = comp.value;
                    }
                }
                else
                {
                    float trackHeight = rect.h;
                    if (trackHeight > 0.0f)
                    {
                        float deltaMouseY = ctx.mousePosition.y - comp.dragStartMousePos.y;
                        newValue = comp.dragStartValue - (deltaMouseY / trackHeight) * (comp.maxValue - comp.minValue);
                    }
                    else
                    {
                        newValue = comp.value;
                    }
                }

                newValue = snapToStep(newValue, comp.minValue, comp.maxValue, comp.stepSize);
                comp.value = newValue;

                if (newValue != previousValue)
                {
                    auto [handle, name] = ui_common::makeEntityPayload(registry, sliderEntity);
                    events::ui::UISliderValueChangedNotification notif;
                    notif.entity = handle;
                    notif.entityName = std::move(name);
                    notif.newValue = newValue;
                    notif.previousValue = previousValue;
                    dispatcher.publish(notif);
                }
            }
            else
            {
                comp.isDragging = false;
                comp.currentState = components::UISliderState::Normal;

                auto [handle, name] = ui_common::makeEntityPayload(registry, sliderEntity);
                events::ui::UISliderDragEndNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                notif.finalValue = comp.value;
                dispatcher.publish(notif);
            }
            break;
        }

        auto scrollContainers = ui_common::buildScrollContainerMap(registry, vw, vh);

        // Phase 2: Hit test and state machine
        entt::entity hoveredSlider = entt::null;
        float smallestArea = std::numeric_limits<float>::max();

        for (auto sliderEntity : sliderView)
        {
            auto& comp = registry.get<components::UISliderComponent>(sliderEntity);

            if (!comp.interactable || comp.isDragging)
                continue;

            if (!scene::Entity::isEffectivelyActive(registry, sliderEntity))
                continue;

            if (!ui_common::isInteractionAllowed(registry, sliderEntity))
                continue;

            const auto* canvas = ui_common::findCanvasForEntity(registry, sliderEntity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(sliderEntity))
                canvas = &registry.get<components::UICanvasComponent>(sliderEntity);
            if (!canvas)
                continue;

            float scale = ui_common::computeCanvasScale(canvas, vw, vh);
            const auto& rectComp = registry.get<components::UIRectComponent>(sliderEntity);
            PixelRect rect = ui_common::resolvePixelRect(rectComp, vw, vh, scale);

            auto [scrollAncestor, scissor] = ui_common::findScrollInfo(registry, sliderEntity, scrollContainers);
            ui_common::applyScrollOffset(rect, scrollAncestor, scrollContainers);

            if (ui_common::hitTestRect(ctx.mousePosition, rect, scissor))
            {
                float area = rect.w * rect.h;
                if (area < smallestArea)
                {
                    smallestArea = area;
                    hoveredSlider = sliderEntity;
                }
            }
        }

        for (auto sliderEntity : sliderView)
        {
            auto& comp = registry.get<components::UISliderComponent>(sliderEntity);

            if (comp.isDragging)
                continue;

            if (!scene::Entity::isEffectivelyActive(registry, sliderEntity))
                continue;

            auto previousState = comp.currentState;
            components::UISliderState newState = components::UISliderState::Normal;

            if (!comp.interactable)
            {
                newState = components::UISliderState::Disabled;
            }
            else if (sliderEntity == hoveredSlider)
            {
                if (ctx.leftMousePressed)
                {
                    newState = components::UISliderState::Pressed;

                    const auto* canvas = ui_common::findCanvasForEntity(registry, sliderEntity);
                    if (!canvas && registry.all_of<components::UICanvasComponent>(sliderEntity))
                        canvas = &registry.get<components::UICanvasComponent>(sliderEntity);

                    if (canvas)
                    {
                        float scale = ui_common::computeCanvasScale(canvas, vw, vh);
                        const auto& rectComp = registry.get<components::UIRectComponent>(sliderEntity);
                        PixelRect rect = ui_common::resolvePixelRect(rectComp, vw, vh, scale);

                        auto [scrollAncestor, scissor] = ui_common::findScrollInfo(registry, sliderEntity, scrollContainers);
                        ui_common::applyScrollOffset(rect, scrollAncestor, scrollContainers);

                        float normalizedValue = (comp.maxValue > comp.minValue)
                            ? (comp.value - comp.minValue) / (comp.maxValue - comp.minValue) : 0.0f;

                        bool clickedOnHandle = false;
                        if (comp.orientation == components::UISliderOrientation::Horizontal)
                        {
                            float handleW = rect.w * comp.handleSizeRatio;
                            float handleX = rect.x + normalizedValue * (rect.w - handleW);
                            clickedOnHandle = ctx.mousePosition.x >= handleX && ctx.mousePosition.x <= handleX + handleW
                                && ctx.mousePosition.y >= rect.y && ctx.mousePosition.y <= rect.y + rect.h;
                        }
                        else
                        {
                            float handleH = rect.h * comp.handleSizeRatio;
                            float handleY = rect.y + (1.0f - normalizedValue) * (rect.h - handleH);
                            clickedOnHandle = ctx.mousePosition.x >= rect.x && ctx.mousePosition.x <= rect.x + rect.w
                                && ctx.mousePosition.y >= handleY && ctx.mousePosition.y <= handleY + handleH;
                        }

                        if (clickedOnHandle)
                        {
                            comp.isDragging = true;
                            comp.dragStartMousePos = ctx.mousePosition;
                            comp.dragStartValue = comp.value;
                        }
                        else if (comp.clickTrackToSet)
                        {
                            float previousValue = comp.value;
                            float newValue;

                            if (comp.orientation == components::UISliderOrientation::Horizontal)
                            {
                                float handleW = rect.w * comp.handleSizeRatio;
                                float trackUsable = rect.w - handleW;
                                float clickPos = ctx.mousePosition.x - rect.x - handleW * 0.5f;
                                newValue = (trackUsable > 0.0f)
                                    ? comp.minValue + (clickPos / trackUsable) * (comp.maxValue - comp.minValue)
                                    : comp.minValue;
                            }
                            else
                            {
                                float handleH = rect.h * comp.handleSizeRatio;
                                float trackUsable = rect.h - handleH;
                                float clickPos = ctx.mousePosition.y - rect.y - handleH * 0.5f;
                                newValue = (trackUsable > 0.0f)
                                    ? comp.maxValue - (clickPos / trackUsable) * (comp.maxValue - comp.minValue)
                                    : comp.minValue;
                            }

                            newValue = snapToStep(newValue, comp.minValue, comp.maxValue, comp.stepSize);
                            comp.value = newValue;
                            comp.isDragging = true;
                            comp.dragStartMousePos = ctx.mousePosition;
                            comp.dragStartValue = newValue;

                            if (newValue != previousValue)
                            {
                                auto [handle, name] = ui_common::makeEntityPayload(registry, sliderEntity);
                                events::ui::UISliderValueChangedNotification valNotif;
                                valNotif.entity = handle;
                                valNotif.entityName = std::move(name);
                                valNotif.newValue = newValue;
                                valNotif.previousValue = previousValue;
                                dispatcher.publish(valNotif);
                            }
                        }

                        if (comp.isDragging)
                        {
                            auto [handle, name] = ui_common::makeEntityPayload(registry, sliderEntity);
                            events::ui::UISliderDragStartNotification dragNotif;
                            dragNotif.entity = handle;
                            dragNotif.entityName = std::move(name);
                            dispatcher.publish(dragNotif);
                        }
                    }
                }
                else
                {
                    newState = components::UISliderState::Hovered;
                }
            }

            bool wasHovered = previousState == components::UISliderState::Hovered
                || previousState == components::UISliderState::Pressed;
            bool isHovered = newState == components::UISliderState::Hovered
                || newState == components::UISliderState::Pressed;

            if (!wasHovered && isHovered)
            {
                auto [handle, name] = ui_common::makeEntityPayload(registry, sliderEntity);
                events::ui::UISliderHoverEnterNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                dispatcher.publish(notif);
            }
            else if (wasHovered && !isHovered)
            {
                auto [handle, name] = ui_common::makeEntityPayload(registry, sliderEntity);
                events::ui::UISliderHoverExitNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                dispatcher.publish(notif);
            }

            comp.currentState = newState;
        }

        // Phase 3: Color lerp
        for (auto sliderEntity : sliderView)
        {
            auto& comp = registry.get<components::UISliderComponent>(sliderEntity);

            glm::vec4 targetColor;
            switch (comp.currentState)
            {
            case components::UISliderState::Hovered:
                targetColor = comp.handleHoveredColor;
                break;
            case components::UISliderState::Pressed:
                targetColor = comp.handlePressedColor;
                break;
            case components::UISliderState::Disabled:
                targetColor = comp.handleDisabledColor;
                break;
            default:
                targetColor = comp.handleNormalColor;
                break;
            }

            if (comp.isDragging)
                targetColor = comp.handlePressedColor;

            if (comp.colorTransitionDuration > 0.0f && ctx.deltaTime > 0.0f)
            {
                float t = std::min(1.0f, ctx.deltaTime / comp.colorTransitionDuration);
                comp.currentHandleDisplayColor = glm::mix(comp.currentHandleDisplayColor, targetColor, t);
            }
            else
            {
                comp.currentHandleDisplayColor = targetColor;
            }
        }
    }
}
