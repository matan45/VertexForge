#include "UIInteractionSystem.hpp"
#include "UICommon.hpp"
#include "FramePreparationSystem.hpp"  // for FrameContext
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
    using ui_common::ScrollContainerInfo;

    // ========== UI Dropdown Interaction ==========
    void UIInteractionSystem::processDropdownInteraction(const FrameContext& ctx)
    {
        if (!ctx.playModeActive)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto dropdownView = registry.view<components::UIDropdownComponent, components::UIRectComponent>();

        if (dropdownView.size_hint() == 0)
            return;

        float vw = static_cast<float>(ctx.viewportWidth);
        float vh = static_cast<float>(ctx.viewportHeight);

        // Pre-compute scroll container info for scissor clipping
        auto scrollContainers = ui_common::buildScrollContainerMap(registry, vw, vh);

        // PHASE 1: Hit test to find hovered dropdown header (smallest-area wins for z-order)
        entt::entity hoveredDropdown = entt::null;
        float smallestArea = std::numeric_limits<float>::max();

        // Also track if mouse is over any open dropdown's option list
        entt::entity hoveredOptionListOwner = entt::null;
        int hoveredOptionIdx = -1;

        for (auto dropdownEntity : dropdownView)
        {
            auto& dropdownComp = registry.get<components::UIDropdownComponent>(dropdownEntity);

            if (registry.all_of<components::NameComponent>(dropdownEntity))
                if (!registry.get<components::NameComponent>(dropdownEntity).isActive)
                    continue;

            const auto* canvas = ui_common::findCanvasForEntity(registry, dropdownEntity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(dropdownEntity))
                canvas = &registry.get<components::UICanvasComponent>(dropdownEntity);
            if (!canvas)
                continue;

            float scale = ui_common::computeCanvasScale(canvas, vw, vh);

            const auto& rectComp = registry.get<components::UIRectComponent>(dropdownEntity);
            PixelRect rect = ui_common::resolvePixelRect(rectComp, vw, vh, scale);

            auto [scrollAncestor, scissor] = ui_common::findScrollInfo(registry, dropdownEntity, scrollContainers);
            ui_common::applyScrollOffset(rect, scrollAncestor, scrollContainers);

            // Hit test on header rect
            if (dropdownComp.interactable && ui_common::hitTestRect(ctx.mousePosition, rect, scissor))
            {
                float area = rect.w * rect.h;
                if (area < smallestArea)
                {
                    smallestArea = area;
                    hoveredDropdown = dropdownEntity;
                }
            }

            // If this dropdown is open, hit test on the option list area below the header
            if (dropdownComp.isOpen && !dropdownComp.options.empty())
            {
                int visibleCount = std::min(static_cast<int>(dropdownComp.options.size()),
                                            dropdownComp.maxVisibleItems);
                float itemHeight = rect.h; // each option same height as header
                float listHeight = itemHeight * visibleCount;

                PixelRect listRect;
                listRect.x = rect.x;
                listRect.y = rect.y + rect.h; // below header
                listRect.w = rect.w;
                listRect.h = listHeight;

                if (ui_common::hitTestRect(ctx.mousePosition, listRect, scissor))
                {
                    hoveredOptionListOwner = dropdownEntity;
                    float relativeY = ctx.mousePosition.y - listRect.y + dropdownComp.listScrollOffset;
                    int idx = static_cast<int>(relativeY / itemHeight);
                    if (idx >= 0 && idx < static_cast<int>(dropdownComp.options.size()))
                        hoveredOptionIdx = idx;
                    else
                        hoveredOptionIdx = -1;
                }
            }
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // PHASE 2: Handle clicks and state transitions
        bool clickedOnDropdownUI = false; // track if click was consumed by any dropdown

        for (auto dropdownEntity : dropdownView)
        {
            auto& comp = registry.get<components::UIDropdownComponent>(dropdownEntity);

            if (registry.all_of<components::NameComponent>(dropdownEntity))
                if (!registry.get<components::NameComponent>(dropdownEntity).isActive)
                    continue;

            auto previousState = comp.currentState;
            components::UIDropdownState newState = components::UIDropdownState::Normal;

            if (!comp.interactable)
            {
                newState = components::UIDropdownState::Disabled;
                if (comp.isOpen)
                {
                    // Close disabled dropdown
                    comp.isOpen = false;
                    comp.hoveredOptionIndex = -1;
                    comp.listScrollOffset = 0.0f;
                    if (components::UIDropdownComponent::activeDropdownEntity == dropdownEntity)
                        components::UIDropdownComponent::activeDropdownEntity = entt::null;

                    auto [handle, name] = ui_common::makeEntityPayload(registry, dropdownEntity);
                    events::ui::UIDropdownClosedNotification notif;
                    notif.entity = handle;
                    notif.entityName = std::move(name);
                    dispatcher.publish(notif);
                }
            }
            else if (comp.isOpen)
            {
                newState = components::UIDropdownState::Open;

                // Update hovered option index
                if (hoveredOptionListOwner == dropdownEntity)
                    comp.hoveredOptionIndex = hoveredOptionIdx;
                else
                    comp.hoveredOptionIndex = -1;

                if (ctx.leftMouseReleased)
                {
                    if (hoveredOptionListOwner == dropdownEntity && hoveredOptionIdx >= 0)
                    {
                        // Option selected
                        clickedOnDropdownUI = true;
                        int previousIndex = comp.selectedIndex;
                        int newIndex = hoveredOptionIdx;

                        comp.selectedIndex = newIndex;

                        // Close the dropdown
                        comp.isOpen = false;
                        comp.hoveredOptionIndex = -1;
                        comp.listScrollOffset = 0.0f;
                        components::UIDropdownComponent::activeDropdownEntity = entt::null;
                        newState = components::UIDropdownState::Normal;

                        // Update UILabel to show selected text
                        bool labelUpdated = false;
                        // Check entity's own UILabel first
                        if (registry.all_of<components::UILabelComponent>(dropdownEntity))
                        {
                            auto& label = registry.get<components::UILabelComponent>(dropdownEntity);
                            label.text = comp.options[newIndex].text;
                            labelUpdated = true;
                        }
                        // Fall back to child UILabel
                        if (!labelUpdated && registry.all_of<components::ChildrenComponent>(dropdownEntity))
                        {
                            const auto& children = registry.get<components::ChildrenComponent>(dropdownEntity).children;
                            for (auto child : children)
                            {
                                if (registry.valid(child) && registry.all_of<components::UILabelComponent>(child))
                                {
                                    auto& label = registry.get<components::UILabelComponent>(child);
                                    label.text = comp.options[newIndex].text;
                                    break;
                                }
                            }
                        }

                        auto [handle, name] = ui_common::makeEntityPayload(registry, dropdownEntity);

                        // Publish selection changed if index actually changed
                        if (previousIndex != newIndex)
                        {
                            events::ui::UIDropdownSelectionChangedNotification selNotif;
                            selNotif.entity = handle;
                            selNotif.entityName = name;
                            selNotif.previousIndex = previousIndex;
                            selNotif.newIndex = newIndex;
                            selNotif.selectedValue = comp.options[newIndex].text;
                            dispatcher.publish(selNotif);
                        }

                        // Publish closed
                        events::ui::UIDropdownClosedNotification closeNotif;
                        closeNotif.entity = handle;
                        closeNotif.entityName = std::move(name);
                        dispatcher.publish(closeNotif);
                    }
                    else if (dropdownEntity == hoveredDropdown)
                    {
                        // Clicked on header while open -> close
                        clickedOnDropdownUI = true;
                        comp.isOpen = false;
                        comp.hoveredOptionIndex = -1;
                        comp.listScrollOffset = 0.0f;
                        components::UIDropdownComponent::activeDropdownEntity = entt::null;
                        newState = components::UIDropdownState::Hovered;

                        auto [handle, name] = ui_common::makeEntityPayload(registry, dropdownEntity);
                        events::ui::UIDropdownClosedNotification notif;
                        notif.entity = handle;
                        notif.entityName = std::move(name);
                        dispatcher.publish(notif);
                    }
                    else
                    {
                        // Clicked elsewhere -> close
                        comp.isOpen = false;
                        comp.hoveredOptionIndex = -1;
                        comp.listScrollOffset = 0.0f;
                        components::UIDropdownComponent::activeDropdownEntity = entt::null;
                        newState = components::UIDropdownState::Normal;

                        auto [handle, name] = ui_common::makeEntityPayload(registry, dropdownEntity);
                        events::ui::UIDropdownClosedNotification notif;
                        notif.entity = handle;
                        notif.entityName = std::move(name);
                        dispatcher.publish(notif);
                    }
                }
            }
            else if (dropdownEntity == hoveredDropdown)
            {
                newState = components::UIDropdownState::Hovered;

                // Click to open
                if (ctx.leftMouseReleased)
                {
                    clickedOnDropdownUI = true;

                    // Close any other open dropdown
                    if (components::UIDropdownComponent::activeDropdownEntity != entt::null
                        && components::UIDropdownComponent::activeDropdownEntity != dropdownEntity
                        && registry.valid(components::UIDropdownComponent::activeDropdownEntity))
                    {
                        auto& otherComp = registry.get<components::UIDropdownComponent>(
                            components::UIDropdownComponent::activeDropdownEntity);
                        otherComp.isOpen = false;
                        otherComp.hoveredOptionIndex = -1;
                        otherComp.listScrollOffset = 0.0f;

                        auto [otherHandle, otherName] = ui_common::makeEntityPayload(
                            registry, components::UIDropdownComponent::activeDropdownEntity);
                        events::ui::UIDropdownClosedNotification closeNotif;
                        closeNotif.entity = otherHandle;
                        closeNotif.entityName = std::move(otherName);
                        dispatcher.publish(closeNotif);
                    }

                    comp.isOpen = true;
                    comp.hoveredOptionIndex = -1;
                    components::UIDropdownComponent::activeDropdownEntity = dropdownEntity;
                    newState = components::UIDropdownState::Open;

                    auto [handle, name] = ui_common::makeEntityPayload(registry, dropdownEntity);
                    events::ui::UIDropdownOpenedNotification notif;
                    notif.entity = handle;
                    notif.entityName = std::move(name);
                    dispatcher.publish(notif);
                }
            }

            comp.currentState = newState;
        }

        // Handle scroll on open dropdown's option list
        if (components::UIDropdownComponent::activeDropdownEntity != entt::null
            && registry.valid(components::UIDropdownComponent::activeDropdownEntity)
            && ctx.scrollDelta.y != 0.0f
            && hoveredOptionListOwner != entt::null)
        {
            auto& comp = registry.get<components::UIDropdownComponent>(
                components::UIDropdownComponent::activeDropdownEntity);
            if (comp.isOpen)
            {
                int visibleCount = std::min(static_cast<int>(comp.options.size()),
                                            comp.maxVisibleItems);
                int totalOptions = static_cast<int>(comp.options.size());
                if (totalOptions > visibleCount)
                {
                    // Get header height for item height calculation
                    const auto& rectComp = registry.get<components::UIRectComponent>(
                        components::UIDropdownComponent::activeDropdownEntity);
                    const auto* canvas = ui_common::findCanvasForEntity(registry,
                        components::UIDropdownComponent::activeDropdownEntity);
                    float scale = ui_common::computeCanvasScale(canvas, vw, vh);
                    PixelRect headerRect = ui_common::resolvePixelRect(rectComp, vw, vh, scale);
                    float itemHeight = headerRect.h;

                    float maxScroll = (totalOptions - visibleCount) * itemHeight;
                    comp.listScrollOffset -= ctx.scrollDelta.y * itemHeight;
                    comp.listScrollOffset = std::max(0.0f, std::min(comp.listScrollOffset, maxScroll));
                }
            }
        }

        // PHASE 3: Color lerp + visual override
        for (auto dropdownEntity : dropdownView)
        {
            auto& comp = registry.get<components::UIDropdownComponent>(dropdownEntity);

            glm::vec4 targetColor;
            switch (comp.currentState)
            {
            case components::UIDropdownState::Hovered:
                targetColor = comp.hoveredColor;
                break;
            case components::UIDropdownState::Open:
                targetColor = comp.openColor;
                break;
            case components::UIDropdownState::Disabled:
                targetColor = comp.disabledColor;
                break;
            default:
                targetColor = comp.normalColor;
                break;
            }

            if (comp.colorTransitionDuration > 0.0f && ctx.deltaTime > 0.0f)
            {
                float t = std::min(1.0f, ctx.deltaTime / comp.colorTransitionDuration);
                comp.currentDisplayColor = glm::mix(comp.currentDisplayColor, targetColor, t);
            }
            else
            {
                comp.currentDisplayColor = targetColor;
            }

            // Override UIImageComponent color tint on header
            if (registry.all_of<components::UIImageComponent>(dropdownEntity))
            {
                auto& imageComp = registry.get<components::UIImageComponent>(dropdownEntity);
                imageComp.colorTint = comp.currentDisplayColor;
            }
        }
    }

    // ========== UI Tabs Interaction ==========
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
            if (registry.all_of<components::NameComponent>(tabsEntity))
                if (!registry.get<components::NameComponent>(tabsEntity).isActive)
                    continue;

            if (!registry.all_of<components::ChildrenComponent>(tabsEntity))
                continue;

            const auto& children = registry.get<components::ChildrenComponent>(tabsEntity).children;

            // Find tab bar: first child with UILayoutGroupComponent
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

            // Get tab bar children (tab buttons)
            if (!registry.all_of<components::ChildrenComponent>(tabBarEntity))
                continue;

            const auto& tabButtons = registry.get<components::ChildrenComponent>(tabBarEntity).children;

            // Find canvas for scale
            const auto* canvas = ui_common::findCanvasForEntity(registry, tabsEntity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(tabsEntity))
                canvas = &registry.get<components::UICanvasComponent>(tabsEntity);
            if (!canvas)
                continue;

            float scale = ui_common::computeCanvasScale(canvas, vw, vh);

            // Hit test each tab button
            for (int i = 0; i < static_cast<int>(tabButtons.size()); ++i)
            {
                auto btnEntity = tabButtons[i];
                if (!registry.valid(btnEntity)) continue;
                if (!registry.all_of<components::UIRectComponent>(btnEntity)) continue;

                if (registry.all_of<components::NameComponent>(btnEntity))
                    if (!registry.get<components::NameComponent>(btnEntity).isActive)
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

                    // Collect panels (non-tab-bar children)
                    std::vector<entt::entity> panels;
                    for (auto childEntity : children)
                    {
                        if (!registry.valid(childEntity)) continue;
                        if (childEntity == tabBarEntity) continue;
                        panels.push_back(childEntity);
                    }

                    // Toggle panel visibility
                    for (int p = 0; p < static_cast<int>(panels.size()); ++p)
                    {
                        if (registry.all_of<components::NameComponent>(panels[p]))
                        {
                            auto& nameComp = registry.get<components::NameComponent>(panels[p]);
                            nameComp.isActive = (p == i);
                        }
                    }

                    // Publish notifications
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

                    break; // Only one tab can be clicked per frame
                }
            }
        }
    }

    // ========== UI Slider Interaction ==========
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

        // Helper: snap value to step
        auto snapToStep = [](float value, float minVal, float maxVal, float stepSize) -> float
        {
            if (stepSize > 0.0f)
                value = std::round((value - minVal) / stepSize) * stepSize + minVal;
            return std::max(minVal, std::min(value, maxVal));
        };

        // ============ PHASE 1: PROCESS ACTIVE DRAGS ============
        for (auto sliderEntity : sliderView)
        {
            auto& comp = registry.get<components::UISliderComponent>(sliderEntity);
            if (!comp.isDragging)
                continue;

            if (ctx.leftMouseDown)
            {
                // Continuous drag: compute value from mouse delta
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
                // Mouse released: end drag
                comp.isDragging = false;
                comp.currentState = components::UISliderState::Normal;

                auto [handle, name] = ui_common::makeEntityPayload(registry, sliderEntity);
                events::ui::UISliderDragEndNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                notif.finalValue = comp.value;
                dispatcher.publish(notif);
            }
            break; // only one slider drags at a time
        }

        // ============ PRE-COMPUTE SCROLL CONTAINERS ============
        auto scrollContainers = ui_common::buildScrollContainerMap(registry, vw, vh);

        // ============ PHASE 2: HIT TEST + STATE MACHINE ============
        entt::entity hoveredSlider = entt::null;
        float smallestArea = std::numeric_limits<float>::max();

        for (auto sliderEntity : sliderView)
        {
            auto& comp = registry.get<components::UISliderComponent>(sliderEntity);

            if (!comp.interactable || comp.isDragging)
                continue;

            if (registry.all_of<components::NameComponent>(sliderEntity))
                if (!registry.get<components::NameComponent>(sliderEntity).isActive)
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

        // State machine transitions
        for (auto sliderEntity : sliderView)
        {
            auto& comp = registry.get<components::UISliderComponent>(sliderEntity);

            if (comp.isDragging)
                continue;

            if (registry.all_of<components::NameComponent>(sliderEntity))
                if (!registry.get<components::NameComponent>(sliderEntity).isActive)
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

                    // Find canvas and compute rect for value calculation
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

                        // Compute handle rect to determine if click was on handle or track
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
                            // Start drag from current value
                            comp.isDragging = true;
                            comp.dragStartMousePos = ctx.mousePosition;
                            comp.dragStartValue = comp.value;
                        }
                        else if (comp.clickTrackToSet)
                        {
                            // Jump value to click position, then start drag
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

            // HoverEnter/HoverExit
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

        // ============ PHASE 3: COLOR LERP ============
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

    // ========== UI Drag & Drop Interaction ==========

    bool UIInteractionSystem::tagMatches(const std::string& acceptTag, const std::string& dragTag)
    {
        if (acceptTag.empty())
            return true;

        size_t start = 0;
        while (start < acceptTag.size())
        {
            size_t end = acceptTag.find(',', start);
            if (end == std::string::npos)
                end = acceptTag.size();

            size_t tStart = start;
            size_t tEnd = end;
            while (tStart < tEnd && acceptTag[tStart] == ' ') tStart++;
            while (tEnd > tStart && acceptTag[tEnd - 1] == ' ') tEnd--;

            if (acceptTag.compare(tStart, tEnd - tStart, dragTag) == 0)
                return true;

            start = end + 1;
        }
        return false;
    }

    void UIInteractionSystem::processDragDropInteraction(const FrameContext& ctx)
    {
        if (!ctx.playModeActive)
        {
            components::UIDraggableComponent::activeDragEntity = entt::null;
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = events::EventDispatcher::instance();

        float vw = static_cast<float>(ctx.viewportWidth);
        float vh = static_cast<float>(ctx.viewportHeight);

        // ---- Phase 1: Update active drag ----
        if (components::UIDraggableComponent::activeDragEntity != entt::null)
        {
            entt::entity dragEntity = components::UIDraggableComponent::activeDragEntity;
            if (!registry.valid(dragEntity)
                || !registry.all_of<components::UIDraggableComponent>(dragEntity))
            {
                components::UIDraggableComponent::activeDragEntity = entt::null;
                return;
            }

            auto& dragComp = registry.get<components::UIDraggableComponent>(dragEntity);

            if (ctx.leftMouseDown)
            {
                // Update ghost position
                dragComp.currentGhostPos = ctx.mousePosition + dragComp.ghostOffset
                    - dragComp.ghostSize * 0.5f;

                // Constrain to parent canvas bounds
                if (dragComp.constrainToParent)
                {
                    auto canvasInfo = ui_common::findCanvasWithEntity(registry, dragEntity);
                    if (canvasInfo.canvas)
                    {
                        float scale = ui_common::computeCanvasScale(canvasInfo.canvas, vw, vh);
                        // Canvas fills viewport in screen-space mode
                        float minX = 0.0f, minY = 0.0f;
                        float maxX = vw - dragComp.ghostSize.x;
                        float maxY = vh - dragComp.ghostSize.y;

                        // If inside a scroll container, clamp to scroll bounds
                        auto [scrollAncestor, scissor] = ui_common::findScrollInfo(
                            registry, dragEntity,
                            ui_common::buildScrollContainerMap(registry, vw, vh));
                        if (scrollAncestor != entt::null && scissor.z > 0.0f && scissor.w > 0.0f)
                        {
                            minX = scissor.x;
                            minY = scissor.y;
                            maxX = scissor.x + scissor.z - dragComp.ghostSize.x;
                            maxY = scissor.y + scissor.w - dragComp.ghostSize.y;
                        }

                        dragComp.currentGhostPos.x = std::clamp(dragComp.currentGhostPos.x, minX, maxX);
                        dragComp.currentGhostPos.y = std::clamp(dragComp.currentGhostPos.y, minY, maxY);
                    }
                }

                // ---- Phase 2: Update drop target highlights ----
                auto dropView = registry.view<components::UIDropTargetComponent, components::UIRectComponent>();
                for (auto targetEntity : dropView)
                {
                    if (!ui_common::isEntityActive(registry, targetEntity))
                        continue;

                    auto& targetComp = registry.get<components::UIDropTargetComponent>(targetEntity);
                    targetComp.isHighlighted = false;
                    targetComp.isRejected = false;

                    if (!targetComp.interactable)
                        continue;

                    const auto* canvas = ui_common::findCanvasForEntity(registry, targetEntity);
                    if (!canvas) continue;

                    float scale = ui_common::computeCanvasScale(canvas, vw, vh);
                    const auto& rectComp = registry.get<components::UIRectComponent>(targetEntity);
                    PixelRect rect = ui_common::resolvePixelRect(rectComp, vw, vh, scale);

                    auto [scrollAnc, scissor] = ui_common::findScrollInfo(
                        registry, targetEntity,
                        ui_common::buildScrollContainerMap(registry, vw, vh));
                    ui_common::applyScrollOffset(rect, scrollAnc,
                        ui_common::buildScrollContainerMap(registry, vw, vh));

                    bool hovered = ui_common::hitTestRect(ctx.mousePosition, rect, scissor);
                    if (hovered)
                    {
                        if (tagMatches(targetComp.acceptTag, dragComp.dragTag))
                            targetComp.isHighlighted = true;
                        else
                            targetComp.isRejected = true;
                    }
                }
            }
            else
            {
                // Mouse released - end drag
                entt::entity dropTarget = entt::null;
                float smallestArea = std::numeric_limits<float>::max();

                auto dropView = registry.view<components::UIDropTargetComponent, components::UIRectComponent>();
                for (auto targetEntity : dropView)
                {
                    if (!ui_common::isEntityActive(registry, targetEntity))
                        continue;

                    auto& targetComp = registry.get<components::UIDropTargetComponent>(targetEntity);
                    if (!targetComp.interactable)
                        continue;

                    if (!tagMatches(targetComp.acceptTag, dragComp.dragTag))
                        continue;

                    const auto* canvas = ui_common::findCanvasForEntity(registry, targetEntity);
                    if (!canvas) continue;

                    float scale = ui_common::computeCanvasScale(canvas, vw, vh);
                    const auto& rectComp = registry.get<components::UIRectComponent>(targetEntity);
                    PixelRect rect = ui_common::resolvePixelRect(rectComp, vw, vh, scale);

                    auto scrollContainers = ui_common::buildScrollContainerMap(registry, vw, vh);
                    auto [scrollAnc, scissor] = ui_common::findScrollInfo(registry, targetEntity, scrollContainers);
                    ui_common::applyScrollOffset(rect, scrollAnc, scrollContainers);

                    if (ui_common::hitTestRect(ctx.mousePosition, rect, scissor))
                    {
                        float area = rect.w * rect.h;
                        if (area < smallestArea)
                        {
                            smallestArea = area;
                            dropTarget = targetEntity;
                        }
                    }
                }

                bool wasDropped = (dropTarget != entt::null);

                // Publish drop notification
                if (wasDropped)
                {
                    auto [srcHandle, srcName] = ui_common::makeEntityPayload(registry, dragEntity);
                    auto [tgtHandle, tgtName] = ui_common::makeEntityPayload(registry, dropTarget);
                    events::ui::UIDropNotification dropNotif;
                    dropNotif.sourceEntity = srcHandle;
                    dropNotif.sourceEntityName = std::move(srcName);
                    dropNotif.targetEntity = tgtHandle;
                    dropNotif.targetEntityName = std::move(tgtName);
                    dropNotif.dragTag = dragComp.dragTag;
                    dispatcher.publish(dropNotif);
                }

                // Publish drag end
                {
                    auto [handle, name] = ui_common::makeEntityPayload(registry, dragEntity);
                    events::ui::UIDragEndNotification endNotif;
                    endNotif.entity = handle;
                    endNotif.entityName = std::move(name);
                    endNotif.wasDropped = wasDropped;
                    dispatcher.publish(endNotif);
                }

                // Reset state
                dragComp.isDragging = false;
                components::UIDraggableComponent::activeDragEntity = entt::null;

                // Clear all highlights
                for (auto targetEntity : dropView)
                {
                    if (registry.all_of<components::UIDropTargetComponent>(targetEntity))
                    {
                        auto& tc = registry.get<components::UIDropTargetComponent>(targetEntity);
                        tc.isHighlighted = false;
                        tc.isRejected = false;
                    }
                }
            }
            return;
        }

        // ---- Phase 3: Initiate new drag ----
        if (!ctx.leftMousePressed)
            return;

        auto draggableView = registry.view<components::UIDraggableComponent, components::UIRectComponent>();
        if (draggableView.size_hint() == 0)
            return;

        entt::entity hoveredDraggable = entt::null;
        float smallestArea = std::numeric_limits<float>::max();

        for (auto entity : draggableView)
        {
            if (!ui_common::isEntityActive(registry, entity))
                continue;

            const auto* canvas = ui_common::findCanvasForEntity(registry, entity);
            if (!canvas) continue;

            float scale = ui_common::computeCanvasScale(canvas, vw, vh);
            const auto& rectComp = registry.get<components::UIRectComponent>(entity);
            PixelRect rect = ui_common::resolvePixelRect(rectComp, vw, vh, scale);

            auto scrollContainers = ui_common::buildScrollContainerMap(registry, vw, vh);
            auto [scrollAncestor, scissor] = ui_common::findScrollInfo(registry, entity, scrollContainers);
            ui_common::applyScrollOffset(rect, scrollAncestor, scrollContainers);

            if (ui_common::hitTestRect(ctx.mousePosition, rect, scissor))
            {
                float area = rect.w * rect.h;
                if (area < smallestArea)
                {
                    smallestArea = area;
                    hoveredDraggable = entity;
                }
            }
        }

        if (hoveredDraggable == entt::null)
            return;

        // Start drag
        auto& dragComp = registry.get<components::UIDraggableComponent>(hoveredDraggable);
        const auto* canvas = ui_common::findCanvasForEntity(registry, hoveredDraggable);
        if (!canvas) return;

        float scale = ui_common::computeCanvasScale(canvas, vw, vh);
        const auto& rectComp = registry.get<components::UIRectComponent>(hoveredDraggable);
        PixelRect rect = ui_common::resolvePixelRect(rectComp, vw, vh, scale);

        dragComp.isDragging = true;
        dragComp.dragStartMousePos = ctx.mousePosition;
        dragComp.dragStartEntityPos = glm::vec2(rect.x, rect.y);
        dragComp.ghostSize = glm::vec2(rect.w, rect.h);
        dragComp.currentGhostPos = ctx.mousePosition + dragComp.ghostOffset - dragComp.ghostSize * 0.5f;
        components::UIDraggableComponent::activeDragEntity = hoveredDraggable;

        auto [handle, name] = ui_common::makeEntityPayload(registry, hoveredDraggable);
        events::ui::UIDragStartNotification notif;
        notif.entity = handle;
        notif.entityName = std::move(name);
        notif.dragTag = dragComp.dragTag;
        dispatcher.publish(notif);
    }
}
