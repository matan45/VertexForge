#include "UIInteractionSystem.hpp"
#include "UICommon.hpp"
#include "FramePreparationSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UIEvents.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include <algorithm>
#include <limits>

namespace controllers::offscreen
{
    using ui_common::PixelRect;

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

        auto scrollContainers = ui_common::buildScrollContainerMap(registry, vw, vh);

        entt::entity hoveredDropdown = entt::null;
        float smallestArea = std::numeric_limits<float>::max();

        entt::entity hoveredOptionListOwner = entt::null;
        int hoveredOptionIdx = -1;

        for (auto dropdownEntity : dropdownView)
        {
            auto& dropdownComp = registry.get<components::UIDropdownComponent>(dropdownEntity);

            if (!scene::Entity::isEffectivelyActive(registry, dropdownEntity))
                continue;

            if (!ui_common::isInteractionAllowed(registry, dropdownEntity))
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

            if (dropdownComp.interactable && ui_common::hitTestRect(ctx.mousePosition, rect, scissor))
            {
                float area = rect.w * rect.h;
                if (area < smallestArea)
                {
                    smallestArea = area;
                    hoveredDropdown = dropdownEntity;
                }
            }

            if (dropdownComp.isOpen && !dropdownComp.options.empty())
            {
                int visibleCount = std::min(static_cast<int>(dropdownComp.options.size()),
                                            dropdownComp.maxVisibleItems);
                float itemHeight = rect.h;
                float listHeight = itemHeight * visibleCount;

                PixelRect listRect;
                listRect.x = rect.x;
                listRect.y = rect.y + rect.h;
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

        for (auto dropdownEntity : dropdownView)
        {
            auto& comp = registry.get<components::UIDropdownComponent>(dropdownEntity);

            if (!scene::Entity::isEffectivelyActive(registry, dropdownEntity))
                continue;

            auto previousState = comp.currentState;
            components::UIDropdownState newState = components::UIDropdownState::Normal;

            if (!comp.interactable)
            {
                newState = components::UIDropdownState::Disabled;
                if (comp.isOpen)
                {
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

                if (hoveredOptionListOwner == dropdownEntity)
                    comp.hoveredOptionIndex = hoveredOptionIdx;
                else
                    comp.hoveredOptionIndex = -1;

                if (ctx.leftMouseReleased)
                {
                    if (hoveredOptionListOwner == dropdownEntity && hoveredOptionIdx >= 0)
                    {
                        int previousIndex = comp.selectedIndex;
                        int newIndex = hoveredOptionIdx;

                        comp.selectedIndex = newIndex;
                        comp.isOpen = false;
                        comp.hoveredOptionIndex = -1;
                        comp.listScrollOffset = 0.0f;
                        components::UIDropdownComponent::activeDropdownEntity = entt::null;
                        newState = components::UIDropdownState::Normal;

                        bool labelUpdated = false;
                        if (registry.all_of<components::UILabelComponent>(dropdownEntity))
                        {
                            auto& label = registry.get<components::UILabelComponent>(dropdownEntity);
                            label.text = comp.options[newIndex].text;
                            labelUpdated = true;
                        }
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

                        events::ui::UIDropdownClosedNotification closeNotif;
                        closeNotif.entity = handle;
                        closeNotif.entityName = std::move(name);
                        dispatcher.publish(closeNotif);
                    }
                    else if (dropdownEntity == hoveredDropdown)
                    {
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

                if (ctx.leftMouseReleased)
                {
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

        // Color lerp and visual override
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

            if (registry.all_of<components::UIImageComponent>(dropdownEntity))
            {
                auto& imageComp = registry.get<components::UIImageComponent>(dropdownEntity);
                imageComp.colorTint = comp.currentDisplayColor;
            }
        }
    }
}
