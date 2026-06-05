#include "UIInteractionSystem.hpp"
#include "FramePreparationSystem.hpp"  // for FrameContext
#include "UICommon.hpp"
#include "asset/AssetRef.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/ui/UIRenderTypes.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UIEvents.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

using namespace controllers::offscreen::ui_common;

namespace controllers::offscreen
{
    namespace
    {
        // Shared hit test (PHASE 1): returns the smallest-area interactable +
        // active widget in `view` under the cursor (entt::null if none).
        // `extraHit(entity, scale, scrollAncestor, scissor)` adds widget-specific
        // hit area (e.g. a checkbox's toggle label); pass a lambda returning
        // false when there is none.
        template <typename ComponentT, typename View, typename ExtraHit>
        entt::entity findHoveredWidget(entt::registry& registry, View view,
                                       const FrameContext& ctx, float vw, float vh,
                                       const std::unordered_map<uint32_t, ScrollContainerInfo>& scrollContainers,
                                       ExtraHit extraHit)
        {
            entt::entity hovered = entt::null;
            float smallestArea = std::numeric_limits<float>::max();

            for (auto entity : view)
            {
                auto& comp = registry.get<ComponentT>(entity);
                if (!comp.interactable)
                    continue;
                if (!scene::Entity::isEffectivelyActive(registry, entity))
                    continue;

                const auto* canvas = findCanvasForEntity(registry, entity);
                if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
                    canvas = &registry.get<components::UICanvasComponent>(entity);
                if (!canvas)
                    continue;

                float scale = computeCanvasScale(canvas, vw, vh);
                const auto& rectComp = registry.get<components::UIRectComponent>(entity);
                PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);

                auto [scrollAncestor, scissor] = findScrollInfo(registry, entity, scrollContainers);
                applyScrollOffset(rect, scrollAncestor, scrollContainers);

                bool insideRect = hitTestRect(ctx.mousePosition, rect, scissor);
                if (!insideRect)
                    insideRect = extraHit(entity, scale, scrollAncestor, scissor);

                if (insideRect)
                {
                    float area = rect.w * rect.h;
                    if (area < smallestArea)
                    {
                        smallestArea = area;
                        hovered = entity;
                    }
                }
            }
            return hovered;
        }

        // Shared visual override (PHASE 3): lerp the widget's display color toward
        // `targetColor` (per its colorTransitionDuration) and push it — plus the
        // per-state texture, if any — onto the widget's UIImage. Runs in edit and
        // play mode so a widget's resting skin previews in the editor.
        template <typename ComponentT>
        void applyWidgetVisual(entt::registry& registry, entt::entity entity, ComponentT& comp,
                               const glm::vec4& targetColor, const asset::AssetRef* stateTexture,
                               float deltaTime)
        {
            if (comp.colorTransitionDuration > 0.0f && deltaTime > 0.0f)
            {
                float t = std::min(1.0f, deltaTime / comp.colorTransitionDuration);
                comp.currentDisplayColor = glm::mix(comp.currentDisplayColor, targetColor, t);
            }
            else
            {
                comp.currentDisplayColor = targetColor;
            }

            if (registry.all_of<components::UIImageComponent>(entity))
            {
                auto& imageComp = registry.get<components::UIImageComponent>(entity);
                imageComp.colorTint = comp.currentDisplayColor;
                if (stateTexture)
                    imageComp.textureRef = *stateTexture;
            }
        }
    }

    void UIInteractionSystem::processButtonInteraction(const FrameContext& ctx)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto buttonView = registry.view<components::UIButtonComponent, components::UIRectComponent>();

        if (buttonView.size_hint() == 0)
            return;

        // Hit-testing, state transitions and click/hover notifications only run
        // in play mode. In edit mode each button rests in Normal (or Disabled),
        // so the PHASE 3 visual pass below still previews the resting skin/color
        // in the editor instead of leaving the button image untextured.
        if (ctx.playModeActive)
        {
            float vw = static_cast<float>(ctx.viewportWidth);
            float vh = static_cast<float>(ctx.viewportHeight);

            // Pre-compute scroll container info for scissor clipping
            auto scrollContainers = buildScrollContainerMap(registry, vw, vh);

            // PHASE 1: hovered button (smallest-area wins for z-order)
            entt::entity hoveredButton = findHoveredWidget<components::UIButtonComponent>(
                registry, buttonView, ctx, vw, vh, scrollContainers,
                [](entt::entity, float, entt::entity, const glm::vec4&) { return false; });

            auto& dispatcher = events::EventDispatcher::instance();

            // PHASE 2: State machine transitions
            for (auto buttonEntity : buttonView)
            {
                auto& comp = registry.get<components::UIButtonComponent>(buttonEntity);

                if (!scene::Entity::isEffectivelyActive(registry, buttonEntity))
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
        }
        else
        {
            // Edit mode: rest each button in its non-interactive visual state so
            // PHASE 3 applies the matching skin/color for the editor preview.
            for (auto buttonEntity : buttonView)
            {
                auto& comp = registry.get<components::UIButtonComponent>(buttonEntity);
                comp.currentState = comp.interactable
                                        ? components::UIButtonState::Normal
                                        : components::UIButtonState::Disabled;
            }
        }

        // PHASE 3: Color lerp + visual override (runs in edit AND play mode so a
        // button's current-state texture/color is applied to its UIImage).
        for (auto buttonEntity : buttonView)
        {
            auto& comp = registry.get<components::UIButtonComponent>(buttonEntity);

            // Pick the target color + per-state texture for the current state.
            glm::vec4 targetColor;
            const asset::AssetRef* stateTexture = nullptr;
            switch (comp.currentState)
            {
            case components::UIButtonState::Hovered:
                targetColor = comp.hoveredColor;
                if (comp.hoverTextureRef.isValid()) stateTexture = &comp.hoverTextureRef;
                break;
            case components::UIButtonState::Pressed:
                targetColor = comp.pressedColor;
                if (comp.pressedTextureRef.isValid()) stateTexture = &comp.pressedTextureRef;
                break;
            case components::UIButtonState::Disabled:
                targetColor = comp.disabledColor;
                if (comp.disabledTextureRef.isValid()) stateTexture = &comp.disabledTextureRef;
                break;
            default:
                targetColor = comp.normalColor;
                if (comp.normalTextureRef.isValid()) stateTexture = &comp.normalTextureRef;
                break;
            }

            applyWidgetVisual(registry, buttonEntity, comp, targetColor, stateTexture, ctx.deltaTime);
        }
    }

    void UIInteractionSystem::computePointerOverUI(const FrameContext& ctx)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& pointerState = registry.ctx().emplace<components::UIPointerState>();

        // Only meaningful in play mode — editor viewport picking is unaffected.
        if (!ctx.playModeActive)
        {
            pointerState.overUI = false;
            return;
        }

        float vw = static_cast<float>(ctx.viewportWidth);
        float vh = static_cast<float>(ctx.viewportHeight);

        auto scrollContainers = buildScrollContainerMap(registry, vw, vh);

        bool found = false;
        auto rectView = registry.view<components::UIRectComponent>();
        for (auto entity : rectView)
        {
            // Bare layout rects don't block — only elements with visible content.
            if (!registry.any_of<components::UIImageComponent, components::UILabelComponent,
                                 components::UIButtonComponent, components::UICheckboxComponent,
                                 components::UITextInputComponent, components::UIDropdownComponent,
                                 components::UISliderComponent, components::UIProgressBarComponent>(entity))
                continue;
            if (!scene::Entity::isEffectivelyActive(registry, entity))
                continue;

            const auto* canvas = findCanvasForEntity(registry, entity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
                canvas = &registry.get<components::UICanvasComponent>(entity);
            if (!canvas)
                continue;

            const auto& rectComp = rectView.get<components::UIRectComponent>(entity);
            if (!rectComp.blocksRaycast)
                continue;

            float scale = computeCanvasScale(canvas, vw, vh);
            PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);

            auto [scrollAncestor, scissor] = findScrollInfo(registry, entity, scrollContainers);
            applyScrollOffset(rect, scrollAncestor, scrollContainers);

            if (hitTestRect(ctx.mousePosition, rect, scissor))
            {
                found = true;
                break;
            }
        }

        pointerState.overUI = found;
    }

    void UIInteractionSystem::processCheckboxInteraction(const FrameContext& ctx)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto checkboxView = registry.view<components::UICheckboxComponent, components::UIRectComponent>();

        if (checkboxView.size_hint() == 0)
            return;

        // Hit-testing, state transitions and toggle only run in play mode. In
        // edit mode each checkbox rests in Normal (or Disabled) so the PHASE 3
        // visual pass below still previews the resting checked/unchecked
        // skin/color in the editor instead of leaving the image untextured.
        if (ctx.playModeActive)
        {
        float vw = static_cast<float>(ctx.viewportWidth);
        float vh = static_cast<float>(ctx.viewportHeight);

        // Pre-compute scroll container info for scissor clipping
        auto scrollContainers = buildScrollContainerMap(registry, vw, vh);

        // PHASE 1: hovered checkbox (smallest-area wins for z-order). labelToggle
        // checkboxes also accept hits on their child UILabel rects.
        entt::entity hoveredCheckbox = findHoveredWidget<components::UICheckboxComponent>(
            registry, checkboxView, ctx, vw, vh, scrollContainers,
            [&](entt::entity entity, float scale, entt::entity scrollAncestor,
                const glm::vec4& scissor) -> bool
            {
                auto& checkboxComp = registry.get<components::UICheckboxComponent>(entity);
                if (!checkboxComp.labelToggle
                    || !registry.all_of<components::ChildrenComponent>(entity))
                    return false;

                const auto& children = registry.get<components::ChildrenComponent>(entity).children;
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
                        return true;
                }
                return false;
            });

        auto& dispatcher = events::EventDispatcher::instance();

        // PHASE 2: State machine transitions + toggle logic
        for (auto checkboxEntity : checkboxView)
        {
            auto& comp = registry.get<components::UICheckboxComponent>(checkboxEntity);

            if (!scene::Entity::isEffectivelyActive(registry, checkboxEntity))
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
        }
        else
        {
            // Edit mode: rest each checkbox in its non-interactive state so
            // PHASE 3 applies the matching skin/color for the editor preview.
            for (auto checkboxEntity : checkboxView)
            {
                auto& comp = registry.get<components::UICheckboxComponent>(checkboxEntity);
                comp.currentState = comp.interactable
                    ? components::UICheckboxState::Normal
                    : components::UICheckboxState::Disabled;
            }
        }

        // PHASE 3: Color lerp + visual override (runs in edit AND play mode so a
        // checkbox's current-state texture/color is applied to its UIImage).
        for (auto checkboxEntity : checkboxView)
        {
            auto& comp = registry.get<components::UICheckboxComponent>(checkboxEntity);

            // Pick the target color + per-state texture. Normal depends on the
            // checked state (checked vs unchecked skin/color).
            glm::vec4 targetColor;
            const asset::AssetRef* stateTexture = nullptr;
            switch (comp.currentState)
            {
            case components::UICheckboxState::Hovered:
                targetColor = comp.hoveredColor;
                if (comp.hoveredTextureRef.isValid()) stateTexture = &comp.hoveredTextureRef;
                break;
            case components::UICheckboxState::Disabled:
                targetColor = comp.disabledColor;
                if (comp.disabledTextureRef.isValid()) stateTexture = &comp.disabledTextureRef;
                break;
            default: // Normal
                if (comp.isChecked)
                {
                    targetColor = comp.checkedColor;
                    if (comp.checkedTextureRef.isValid()) stateTexture = &comp.checkedTextureRef;
                }
                else
                {
                    targetColor = comp.uncheckedColor;
                    if (comp.uncheckedTextureRef.isValid()) stateTexture = &comp.uncheckedTextureRef;
                }
                break;
            }

            applyWidgetVisual(registry, checkboxEntity, comp, targetColor, stateTexture, ctx.deltaTime);
        }
    }
}
