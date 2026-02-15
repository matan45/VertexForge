#include "UIInteractionSystem.hpp"
#include "FramePreparationSystem.hpp"  // for FrameContext
#include "UICommon.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/ui/UIRenderTypes.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/UIEvents.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

using namespace controllers::offscreen::ui_common;

namespace controllers::offscreen
{
    void UIInteractionSystem::processButtonInteraction(const FrameContext& ctx)
    {
        if (!ctx.playModeActive)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto buttonView = registry.view<components::UIButtonComponent, components::UIRectComponent>();

        if (buttonView.size_hint() == 0)
            return;

        float vw = static_cast<float>(ctx.viewportWidth);
        float vh = static_cast<float>(ctx.viewportHeight);

        // Pre-compute scroll container info for scissor clipping
        auto scrollContainers = buildScrollContainerMap(registry, vw, vh);

        // PHASE 1: Hit test to find hovered button (smallest-area wins for z-order)
        entt::entity hoveredButton = entt::null;
        float smallestArea = std::numeric_limits<float>::max();

        for (auto buttonEntity : buttonView)
        {
            auto& buttonComp = registry.get<components::UIButtonComponent>(buttonEntity);

            if (!buttonComp.interactable)
                continue;

            if (registry.all_of<components::NameComponent>(buttonEntity))
                if (!registry.get<components::NameComponent>(buttonEntity).isActive)
                    continue;

            const auto* canvas = findCanvasForEntity(registry, buttonEntity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(buttonEntity))
                canvas = &registry.get<components::UICanvasComponent>(buttonEntity);
            if (!canvas)
                continue;

            float scale = computeCanvasScale(canvas, vw, vh);

            const auto& rectComp = registry.get<components::UIRectComponent>(buttonEntity);
            PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);

            // Find scroll ancestor and apply offset
            auto [scrollAncestor, scissor] = findScrollInfo(registry, buttonEntity, scrollContainers);
            applyScrollOffset(rect, scrollAncestor, scrollContainers);

            bool insideRect = hitTestRect(ctx.mousePosition, rect, scissor);

            if (insideRect)
            {
                float area = rect.w * rect.h;
                if (area < smallestArea)
                {
                    smallestArea = area;
                    hoveredButton = buttonEntity;
                }
            }
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // PHASE 2: State machine transitions
        for (auto buttonEntity : buttonView)
        {
            auto& comp = registry.get<components::UIButtonComponent>(buttonEntity);

            if (registry.all_of<components::NameComponent>(buttonEntity))
                if (!registry.get<components::NameComponent>(buttonEntity).isActive)
                    continue;

            auto previousState = comp.currentState;
            components::UIButtonState newState = components::UIButtonState::Normal;

            if (!comp.interactable)
            {
                newState = components::UIButtonState::Disabled;
            }
            else if (buttonEntity == hoveredButton)
            {
                if (ctx.leftMousePressed)
                {
                    newState = components::UIButtonState::Pressed;
                    auto [handle, name] = makeEntityPayload(registry, buttonEntity);
                    events::ui::UIButtonPressedNotification notif;
                    notif.entity = handle;
                    notif.entityName = std::move(name);
                    dispatcher.publish(notif);
                }
                else if (previousState == components::UIButtonState::Pressed && ctx.leftMouseDown)
                {
                    newState = components::UIButtonState::Pressed;
                }
                else if (previousState == components::UIButtonState::Pressed && ctx.leftMouseReleased)
                {
                    newState = components::UIButtonState::Normal;
                    auto [handle, name] = makeEntityPayload(registry, buttonEntity);

                    events::ui::UIButtonReleasedNotification relNotif;
                    relNotif.entity = handle;
                    relNotif.entityName = name;
                    dispatcher.publish(relNotif);

                    events::ui::UIButtonClickedNotification clickNotif;
                    clickNotif.entity = handle;
                    clickNotif.entityName = std::move(name);
                    dispatcher.publish(clickNotif);
                }
                else
                {
                    newState = components::UIButtonState::Hovered;
                }
            }
            else
            {
                if (previousState == components::UIButtonState::Pressed && ctx.leftMouseReleased)
                {
                    newState = components::UIButtonState::Normal;
                    auto [handle, name] = makeEntityPayload(registry, buttonEntity);
                    events::ui::UIButtonReleasedNotification relNotif;
                    relNotif.entity = handle;
                    relNotif.entityName = std::move(name);
                    dispatcher.publish(relNotif);
                }
                else
                {
                    newState = components::UIButtonState::Normal;
                }
            }

            // Publish HoverEnter/HoverExit transitions
            bool wasHovered = previousState == components::UIButtonState::Hovered
                || previousState == components::UIButtonState::Pressed;
            bool isHovered = newState == components::UIButtonState::Hovered
                || newState == components::UIButtonState::Pressed;

            if (!wasHovered && isHovered)
            {
                auto [handle, name] = makeEntityPayload(registry, buttonEntity);
                events::ui::UIButtonHoverEnterNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                dispatcher.publish(notif);
            }
            else if (wasHovered && !isHovered)
            {
                auto [handle, name] = makeEntityPayload(registry, buttonEntity);
                events::ui::UIButtonHoverExitNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                dispatcher.publish(notif);
            }

            comp.currentState = newState;
        }

        // PHASE 3: Color lerp + visual override
        for (auto buttonEntity : buttonView)
        {
            auto& comp = registry.get<components::UIButtonComponent>(buttonEntity);

            // Determine target color based on current state
            glm::vec4 targetColor;
            switch (comp.currentState)
            {
            case components::UIButtonState::Hovered:
                targetColor = comp.hoveredColor;
                break;
            case components::UIButtonState::Pressed:
                targetColor = comp.pressedColor;
                break;
            case components::UIButtonState::Disabled:
                targetColor = comp.disabledColor;
                break;
            default:
                targetColor = comp.normalColor;
                break;
            }

            // Lerp toward target color
            if (comp.colorTransitionDuration > 0.0f && ctx.deltaTime > 0.0f)
            {
                float t = std::min(1.0f, ctx.deltaTime / comp.colorTransitionDuration);
                comp.currentDisplayColor = glm::mix(comp.currentDisplayColor, targetColor, t);
            }
            else
            {
                comp.currentDisplayColor = targetColor;
            }

            // Override UIImageComponent color tint
            if (registry.all_of<components::UIImageComponent>(buttonEntity))
            {
                auto& imageComp = registry.get<components::UIImageComponent>(buttonEntity);
                imageComp.colorTint = comp.currentDisplayColor;

                // Texture swap: pick per-state texture if defined
                const std::string* stateTexture = nullptr;
                switch (comp.currentState)
                {
                case components::UIButtonState::Hovered:
                    if (!comp.hoverTexture.empty()) stateTexture = &comp.hoverTexture;
                    break;
                case components::UIButtonState::Pressed:
                    if (!comp.pressedTexture.empty()) stateTexture = &comp.pressedTexture;
                    break;
                case components::UIButtonState::Disabled:
                    if (!comp.disabledTexture.empty()) stateTexture = &comp.disabledTexture;
                    break;
                default:
                    if (!comp.normalTexture.empty()) stateTexture = &comp.normalTexture;
                    break;
                }

                if (stateTexture)
                    imageComp.texturePath = *stateTexture;
            }
        }
    }

    void UIInteractionSystem::processCheckboxInteraction(const FrameContext& ctx)
    {
        if (!ctx.playModeActive)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto checkboxView = registry.view<components::UICheckboxComponent, components::UIRectComponent>();

        if (checkboxView.size_hint() == 0)
            return;

        float vw = static_cast<float>(ctx.viewportWidth);
        float vh = static_cast<float>(ctx.viewportHeight);

        // Pre-compute scroll container info for scissor clipping
        auto scrollContainers = buildScrollContainerMap(registry, vw, vh);

        // PHASE 1: Hit test to find hovered checkbox (smallest-area wins for z-order)
        entt::entity hoveredCheckbox = entt::null;
        float smallestArea = std::numeric_limits<float>::max();

        for (auto checkboxEntity : checkboxView)
        {
            auto& checkboxComp = registry.get<components::UICheckboxComponent>(checkboxEntity);

            if (!checkboxComp.interactable)
                continue;

            if (registry.all_of<components::NameComponent>(checkboxEntity))
                if (!registry.get<components::NameComponent>(checkboxEntity).isActive)
                    continue;

            const auto* canvas = findCanvasForEntity(registry, checkboxEntity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(checkboxEntity))
                canvas = &registry.get<components::UICanvasComponent>(checkboxEntity);
            if (!canvas)
                continue;

            float scale = computeCanvasScale(canvas, vw, vh);

            const auto& rectComp = registry.get<components::UIRectComponent>(checkboxEntity);
            PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);

            auto [scrollAncestor, scissor] = findScrollInfo(registry, checkboxEntity, scrollContainers);
            applyScrollOffset(rect, scrollAncestor, scrollContainers);

            bool insideRect = hitTestRect(ctx.mousePosition, rect, scissor);

            // labelToggle: also test child UILabel rects
            if (!insideRect && checkboxComp.labelToggle
                && registry.all_of<components::ChildrenComponent>(checkboxEntity))
            {
                const auto& children = registry.get<components::ChildrenComponent>(checkboxEntity).children;
                for (auto child : children)
                {
                    if (!registry.valid(child))
                        continue;
                    if (!registry.all_of<components::UILabelComponent, components::UIRectComponent>(child))
                        continue;

                    const auto& childRect = registry.get<components::UIRectComponent>(child);
                    PixelRect childPixelRect = resolvePixelRect(childRect, vw, vh, scale);
                    applyScrollOffset(childPixelRect, scrollAncestor, scrollContainers);
                    if (hitTestRect(ctx.mousePosition, childPixelRect, scissor))
                    {
                        insideRect = true;
                        break;
                    }
                }
            }

            if (insideRect)
            {
                float area = rect.w * rect.h;
                if (area < smallestArea)
                {
                    smallestArea = area;
                    hoveredCheckbox = checkboxEntity;
                }
            }
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // PHASE 2: State machine transitions + toggle logic
        for (auto checkboxEntity : checkboxView)
        {
            auto& comp = registry.get<components::UICheckboxComponent>(checkboxEntity);

            if (registry.all_of<components::NameComponent>(checkboxEntity))
                if (!registry.get<components::NameComponent>(checkboxEntity).isActive)
                    continue;

            auto previousState = comp.currentState;
            components::UICheckboxState newState = components::UICheckboxState::Normal;

            if (!comp.interactable)
            {
                newState = components::UICheckboxState::Disabled;
            }
            else if (checkboxEntity == hoveredCheckbox)
            {
                newState = components::UICheckboxState::Hovered;

                // Toggle on click release
                if (ctx.leftMouseReleased)
                {
                    bool previousChecked = comp.isChecked;
                    bool toggled = false;

                    if (comp.groupName.empty())
                    {
                        // Independent checkbox: simply toggle
                        comp.isChecked = !comp.isChecked;
                        toggled = (comp.isChecked != previousChecked);
                    }
                    else
                    {
                        // Radio group logic
                        if (comp.isChecked && !comp.allowUncheck)
                        {
                            // Already checked, can't uncheck in strict radio mode
                        }
                        else if (!comp.isChecked)
                        {
                            // Check this one, uncheck all others in the same group
                            comp.isChecked = true;
                            toggled = true;

                            for (auto otherEntity : checkboxView)
                            {
                                if (otherEntity == checkboxEntity)
                                    continue;
                                auto& otherComp = registry.get<components::UICheckboxComponent>(otherEntity);
                                if (otherComp.groupName == comp.groupName && otherComp.isChecked)
                                {
                                    otherComp.isChecked = false;

                                    auto [handle, name] = makeEntityPayload(registry, otherEntity);
                                    events::ui::UICheckboxToggledNotification notif;
                                    notif.entity = handle;
                                    notif.entityName = std::move(name);
                                    notif.newCheckedState = false;
                                    notif.previousCheckedState = true;
                                    dispatcher.publish(notif);
                                }
                            }
                        }
                        else
                        {
                            // Radio group but allowUncheck is true: toggle off
                            comp.isChecked = false;
                            toggled = true;
                        }
                    }

                    if (toggled)
                    {
                        auto [handle, name] = makeEntityPayload(registry, checkboxEntity);
                        events::ui::UICheckboxToggledNotification notif;
                        notif.entity = handle;
                        notif.entityName = std::move(name);
                        notif.newCheckedState = comp.isChecked;
                        notif.previousCheckedState = previousChecked;
                        dispatcher.publish(notif);
                    }
                }
            }

            // Publish HoverEnter/HoverExit transitions
            bool wasHovered = previousState == components::UICheckboxState::Hovered;
            bool isHovered = newState == components::UICheckboxState::Hovered;

            if (!wasHovered && isHovered)
            {
                auto [handle, name] = makeEntityPayload(registry, checkboxEntity);
                events::ui::UICheckboxHoverEnterNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                dispatcher.publish(notif);
            }
            else if (wasHovered && !isHovered)
            {
                auto [handle, name] = makeEntityPayload(registry, checkboxEntity);
                events::ui::UICheckboxHoverExitNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                dispatcher.publish(notif);
            }

            comp.currentState = newState;
        }

        // PHASE 3: Color lerp + visual override
        for (auto checkboxEntity : checkboxView)
        {
            auto& comp = registry.get<components::UICheckboxComponent>(checkboxEntity);

            // Target color depends on state AND isChecked
            glm::vec4 targetColor;
            switch (comp.currentState)
            {
            case components::UICheckboxState::Hovered:
                targetColor = comp.hoveredColor;
                break;
            case components::UICheckboxState::Disabled:
                targetColor = comp.disabledColor;
                break;
            default: // Normal
                targetColor = comp.isChecked ? comp.checkedColor : comp.uncheckedColor;
                break;
            }

            // Lerp toward target color
            if (comp.colorTransitionDuration > 0.0f && ctx.deltaTime > 0.0f)
            {
                float t = std::min(1.0f, ctx.deltaTime / comp.colorTransitionDuration);
                comp.currentDisplayColor = glm::mix(comp.currentDisplayColor, targetColor, t);
            }
            else
            {
                comp.currentDisplayColor = targetColor;
            }

            // Override UIImageComponent color tint
            if (registry.all_of<components::UIImageComponent>(checkboxEntity))
            {
                auto& imageComp = registry.get<components::UIImageComponent>(checkboxEntity);
                imageComp.colorTint = comp.currentDisplayColor;

                // Texture swap: pick per-state texture if defined
                const std::string* stateTexture = nullptr;
                switch (comp.currentState)
                {
                case components::UICheckboxState::Hovered:
                    if (!comp.hoveredTexture.empty()) stateTexture = &comp.hoveredTexture;
                    break;
                case components::UICheckboxState::Disabled:
                    if (!comp.disabledTexture.empty()) stateTexture = &comp.disabledTexture;
                    break;
                default: // Normal - pick based on checked state
                    if (comp.isChecked)
                    {
                        if (!comp.checkedTexture.empty()) stateTexture = &comp.checkedTexture;
                    }
                    else
                    {
                        if (!comp.uncheckedTexture.empty()) stateTexture = &comp.uncheckedTexture;
                    }
                    break;
                }

                if (stateTexture)
                    imageComp.texturePath = *stateTexture;
            }
        }
    }
}
