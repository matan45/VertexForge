#include "UIInteractionSystem.hpp"
#include "FramePreparationSystem.hpp"  // for FrameContext
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

namespace controllers::offscreen
{
    namespace
    {
        struct PixelRect
        {
            float x, y, w, h;
        };

        PixelRect resolvePixelRect(
            const components::UIRectComponent& rectComp,
            float parentW, float parentH, float scale)
        {
            float anchorLeftPx  = rectComp.anchorMin.x * parentW;
            float anchorRightPx = rectComp.anchorMax.x * parentW;
            float anchorTopPx   = (1.0f - rectComp.anchorMax.y) * parentH;
            float anchorBotPx   = (1.0f - rectComp.anchorMin.y) * parentH;

            float w = (anchorRightPx - anchorLeftPx) + rectComp.sizeDelta.x * scale;
            float h = (anchorBotPx - anchorTopPx) + rectComp.sizeDelta.y * scale;

            float cx = (anchorLeftPx + anchorRightPx) * 0.5f + rectComp.anchoredPosition.x * scale;
            float cy = (anchorTopPx + anchorBotPx) * 0.5f - rectComp.anchoredPosition.y * scale;

            float posX = cx - rectComp.pivot.x * w;
            float posY = cy - rectComp.pivot.y * h;

            return {posX, posY, w, h};
        }

        const components::UICanvasComponent* findCanvasForEntity(
            entt::registry& registry, entt::entity entity)
        {
            entt::entity current = entity;
            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parent = registry.get<components::ParentComponent>(current).parent;
                if (parent == entt::null || !registry.valid(parent))
                    break;
                if (registry.all_of<components::UICanvasComponent>(parent))
                    return &registry.get<components::UICanvasComponent>(parent);
                current = parent;
            }
            if (registry.all_of<components::UICanvasComponent>(entity))
                return &registry.get<components::UICanvasComponent>(entity);
            return nullptr;
        }
    } // anonymous namespace

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
        struct ScrollContainerInfo
        {
            glm::vec2 scrollOffset{0.0f, 0.0f};
            glm::vec4 scissorRect{0.0f, 0.0f, 0.0f, 0.0f};
        };
        std::unordered_map<uint32_t, ScrollContainerInfo> scrollContainers;
        {
            auto scrollView = registry.view<components::UIScrollComponent, components::UIRectComponent>();
            for (auto scrollEntity : scrollView)
            {
                const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                if (!scrollCanvas)
                    continue;

                float sc = 1.0f;
                if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

                const auto& scrollRect = registry.get<components::UIRectComponent>(scrollEntity);
                PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                const auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);

                float sx = std::max(0.0f, vpRect.x);
                float sy = std::max(0.0f, vpRect.y);
                float sw = std::max(0.0f, std::min(vpRect.x + vpRect.w, vw) - sx);
                float sh = std::max(0.0f, std::min(vpRect.y + vpRect.h, vh) - sy);

                ScrollContainerInfo info;
                info.scrollOffset = scrollComp.scrollOffset;
                info.scissorRect = glm::vec4(sx, sy, sw, sh);
                scrollContainers[static_cast<uint32_t>(scrollEntity)] = info;
            }
        }

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

            float scale = 1.0f;
            if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                scale = std::min(vw / canvas->referenceWidth, vh / canvas->referenceHeight);

            const auto& rectComp = registry.get<components::UIRectComponent>(buttonEntity);
            PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);

            // Find scroll ancestor and apply offset
            entt::entity scrollAncestor = entt::null;
            entt::entity current = buttonEntity;
            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parentEntity = registry.get<components::ParentComponent>(current).parent;
                if (parentEntity == entt::null || !registry.valid(parentEntity))
                    break;
                if (scrollAncestor == entt::null
                    && registry.all_of<components::UIScrollComponent>(parentEntity))
                    scrollAncestor = parentEntity;
                if (registry.all_of<components::UICanvasComponent>(parentEntity))
                    break;
                current = parentEntity;
            }

            glm::vec4 scissor{0.0f, 0.0f, 0.0f, 0.0f};
            if (scrollAncestor != entt::null)
            {
                auto it = scrollContainers.find(static_cast<uint32_t>(scrollAncestor));
                if (it != scrollContainers.end())
                {
                    rect.x -= it->second.scrollOffset.x;
                    rect.y -= it->second.scrollOffset.y;
                    scissor = it->second.scissorRect;
                }
            }

            // Check mouse inside button rect
            bool insideRect = ctx.mousePosition.x >= rect.x && ctx.mousePosition.x <= rect.x + rect.w
                && ctx.mousePosition.y >= rect.y && ctx.mousePosition.y <= rect.y + rect.h;

            // Check mouse inside scissor (if clipped by scroll)
            if (insideRect && scissor.z > 0.0f && scissor.w > 0.0f)
            {
                insideRect = ctx.mousePosition.x >= scissor.x
                    && ctx.mousePosition.x <= scissor.x + scissor.z
                    && ctx.mousePosition.y >= scissor.y
                    && ctx.mousePosition.y <= scissor.y + scissor.w;
            }

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

        // Helper to build notification payload
        auto makeEntityPayload = [&](entt::entity entity) -> std::pair<services::EntityHandle, std::string>
        {
            services::EntityHandle handle = services::internal::toHandle(entity);
            std::string name;
            if (registry.all_of<components::NameComponent>(entity))
                name = registry.get<components::NameComponent>(entity).name;
            return {handle, std::move(name)};
        };

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
                    auto [handle, name] = makeEntityPayload(buttonEntity);
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
                    auto [handle, name] = makeEntityPayload(buttonEntity);

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
                    auto [handle, name] = makeEntityPayload(buttonEntity);
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
                auto [handle, name] = makeEntityPayload(buttonEntity);
                events::ui::UIButtonHoverEnterNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                dispatcher.publish(notif);
            }
            else if (wasHovered && !isHovered)
            {
                auto [handle, name] = makeEntityPayload(buttonEntity);
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
        struct ScrollContainerInfo
        {
            glm::vec2 scrollOffset{0.0f, 0.0f};
            glm::vec4 scissorRect{0.0f, 0.0f, 0.0f, 0.0f};
        };
        std::unordered_map<uint32_t, ScrollContainerInfo> scrollContainers;
        {
            auto scrollView = registry.view<components::UIScrollComponent, components::UIRectComponent>();
            for (auto scrollEntity : scrollView)
            {
                const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                if (!scrollCanvas)
                    continue;

                float sc = 1.0f;
                if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

                const auto& scrollRect = registry.get<components::UIRectComponent>(scrollEntity);
                PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                const auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);

                float sx = std::max(0.0f, vpRect.x);
                float sy = std::max(0.0f, vpRect.y);
                float sw = std::max(0.0f, std::min(vpRect.x + vpRect.w, vw) - sx);
                float sh = std::max(0.0f, std::min(vpRect.y + vpRect.h, vh) - sy);

                ScrollContainerInfo info;
                info.scrollOffset = scrollComp.scrollOffset;
                info.scissorRect = glm::vec4(sx, sy, sw, sh);
                scrollContainers[static_cast<uint32_t>(scrollEntity)] = info;
            }
        }

        // Helper to find scroll ancestor and get scissor/offset
        auto findScrollInfo = [&](entt::entity entity) -> std::pair<entt::entity, glm::vec4>
        {
            entt::entity scrollAncestor = entt::null;
            entt::entity current = entity;
            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parentEntity = registry.get<components::ParentComponent>(current).parent;
                if (parentEntity == entt::null || !registry.valid(parentEntity))
                    break;
                if (scrollAncestor == entt::null
                    && registry.all_of<components::UIScrollComponent>(parentEntity))
                    scrollAncestor = parentEntity;
                if (registry.all_of<components::UICanvasComponent>(parentEntity))
                    break;
                current = parentEntity;
            }
            glm::vec4 scissor{0.0f, 0.0f, 0.0f, 0.0f};
            if (scrollAncestor != entt::null)
            {
                auto it = scrollContainers.find(static_cast<uint32_t>(scrollAncestor));
                if (it != scrollContainers.end())
                    scissor = it->second.scissorRect;
            }
            return {scrollAncestor, scissor};
        };

        // Helper to test mouse inside rect with scissor clipping
        auto hitTestRect = [&](PixelRect rect, glm::vec4 scissor) -> bool
        {
            bool inside = ctx.mousePosition.x >= rect.x && ctx.mousePosition.x <= rect.x + rect.w
                && ctx.mousePosition.y >= rect.y && ctx.mousePosition.y <= rect.y + rect.h;
            if (inside && scissor.z > 0.0f && scissor.w > 0.0f)
            {
                inside = ctx.mousePosition.x >= scissor.x
                    && ctx.mousePosition.x <= scissor.x + scissor.z
                    && ctx.mousePosition.y >= scissor.y
                    && ctx.mousePosition.y <= scissor.y + scissor.w;
            }
            return inside;
        };

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

            float scale = 1.0f;
            if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                scale = std::min(vw / canvas->referenceWidth, vh / canvas->referenceHeight);

            const auto& rectComp = registry.get<components::UIRectComponent>(checkboxEntity);
            PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);

            auto [scrollAncestor, scissor] = findScrollInfo(checkboxEntity);
            if (scrollAncestor != entt::null)
            {
                auto it = scrollContainers.find(static_cast<uint32_t>(scrollAncestor));
                if (it != scrollContainers.end())
                {
                    rect.x -= it->second.scrollOffset.x;
                    rect.y -= it->second.scrollOffset.y;
                }
            }

            bool insideRect = hitTestRect(rect, scissor);

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
                    if (scrollAncestor != entt::null)
                    {
                        auto it = scrollContainers.find(static_cast<uint32_t>(scrollAncestor));
                        if (it != scrollContainers.end())
                        {
                            childPixelRect.x -= it->second.scrollOffset.x;
                            childPixelRect.y -= it->second.scrollOffset.y;
                        }
                    }
                    if (hitTestRect(childPixelRect, scissor))
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

        // Helper to build notification payload
        auto makeEntityPayload = [&](entt::entity entity) -> std::pair<services::EntityHandle, std::string>
        {
            services::EntityHandle handle = services::internal::toHandle(entity);
            std::string name;
            if (registry.all_of<components::NameComponent>(entity))
                name = registry.get<components::NameComponent>(entity).name;
            return {handle, std::move(name)};
        };

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

                                    auto [handle, name] = makeEntityPayload(otherEntity);
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
                        auto [handle, name] = makeEntityPayload(checkboxEntity);
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
                auto [handle, name] = makeEntityPayload(checkboxEntity);
                events::ui::UICheckboxHoverEnterNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                dispatcher.publish(notif);
            }
            else if (wasHovered && !isHovered)
            {
                auto [handle, name] = makeEntityPayload(checkboxEntity);
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

    // GLFW key code constants (matching GLFW/glfw3.h)
    namespace keycode
    {
        constexpr int Backspace = 259;
        constexpr int Delete = 261;
        constexpr int Right = 262;
        constexpr int Left = 263;
        constexpr int Home = 268;
        constexpr int End = 269;
        constexpr int Enter = 257;
        constexpr int Escape = 256;
        constexpr int Tab = 258;
        constexpr int A = 65;
        constexpr int C = 67;
        constexpr int V = 86;
        constexpr int X = 88;
        constexpr int LeftControl = 341;
        constexpr int RightControl = 345;
        constexpr int LeftShift = 340;
        constexpr int RightShift = 344;
    }

    void UIInteractionSystem::processTextInputInteraction(const FrameContext& ctx)
    {
        if (!ctx.playModeActive)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto textInputView = registry.view<components::UITextInputComponent, components::UIRectComponent>();

        if (textInputView.size_hint() == 0)
        {
            focusedTextInput = entt::null;
            return;
        }

        float vw = static_cast<float>(ctx.viewportWidth);
        float vh = static_cast<float>(ctx.viewportHeight);

        // Pre-compute scroll container info for scissor clipping
        struct ScrollContainerInfo
        {
            glm::vec2 scrollOffset{0.0f, 0.0f};
            glm::vec4 scissorRect{0.0f, 0.0f, 0.0f, 0.0f};
        };
        std::unordered_map<uint32_t, ScrollContainerInfo> scrollContainers;
        {
            auto scrollView = registry.view<components::UIScrollComponent, components::UIRectComponent>();
            for (auto scrollEntity : scrollView)
            {
                const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                if (!scrollCanvas)
                    continue;

                float sc = 1.0f;
                if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

                const auto& scrollRect = registry.get<components::UIRectComponent>(scrollEntity);
                PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                const auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);

                float sx = std::max(0.0f, vpRect.x);
                float sy = std::max(0.0f, vpRect.y);
                float sw = std::max(0.0f, std::min(vpRect.x + vpRect.w, vw) - sx);
                float sh = std::max(0.0f, std::min(vpRect.y + vpRect.h, vh) - sy);

                ScrollContainerInfo info;
                info.scrollOffset = scrollComp.scrollOffset;
                info.scissorRect = glm::vec4(sx, sy, sw, sh);
                scrollContainers[static_cast<uint32_t>(scrollEntity)] = info;
            }
        }

        // Helper: check if Ctrl key is held
        bool ctrlDown = ctx.isKeyDown && (ctx.isKeyDown(keycode::LeftControl) || ctx.isKeyDown(keycode::RightControl));
        bool shiftDown = ctx.isKeyDown && (ctx.isKeyDown(keycode::LeftShift) || ctx.isKeyDown(keycode::RightShift));

        // PHASE 1: Hit test to find hovered text input (smallest-area wins)
        entt::entity hoveredTextInput = entt::null;
        float smallestArea = std::numeric_limits<float>::max();

        for (auto entity : textInputView)
        {
            auto& comp = registry.get<components::UITextInputComponent>(entity);

            if (!comp.interactable)
                continue;

            if (registry.all_of<components::NameComponent>(entity))
                if (!registry.get<components::NameComponent>(entity).isActive)
                    continue;

            const auto* canvas = findCanvasForEntity(registry, entity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
                canvas = &registry.get<components::UICanvasComponent>(entity);
            if (!canvas)
                continue;

            float scale = 1.0f;
            if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                scale = std::min(vw / canvas->referenceWidth, vh / canvas->referenceHeight);

            const auto& rectComp = registry.get<components::UIRectComponent>(entity);
            PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);

            // Find scroll ancestor
            entt::entity scrollAncestor = entt::null;
            entt::entity current = entity;
            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parentEntity = registry.get<components::ParentComponent>(current).parent;
                if (parentEntity == entt::null || !registry.valid(parentEntity))
                    break;
                if (scrollAncestor == entt::null
                    && registry.all_of<components::UIScrollComponent>(parentEntity))
                    scrollAncestor = parentEntity;
                if (registry.all_of<components::UICanvasComponent>(parentEntity))
                    break;
                current = parentEntity;
            }

            glm::vec4 scissor{0.0f, 0.0f, 0.0f, 0.0f};
            if (scrollAncestor != entt::null)
            {
                auto it = scrollContainers.find(static_cast<uint32_t>(scrollAncestor));
                if (it != scrollContainers.end())
                {
                    rect.x -= it->second.scrollOffset.x;
                    rect.y -= it->second.scrollOffset.y;
                    scissor = it->second.scissorRect;
                }
            }

            bool insideRect = ctx.mousePosition.x >= rect.x && ctx.mousePosition.x <= rect.x + rect.w
                && ctx.mousePosition.y >= rect.y && ctx.mousePosition.y <= rect.y + rect.h;

            if (insideRect && scissor.z > 0.0f && scissor.w > 0.0f)
            {
                insideRect = ctx.mousePosition.x >= scissor.x
                    && ctx.mousePosition.x <= scissor.x + scissor.z
                    && ctx.mousePosition.y >= scissor.y
                    && ctx.mousePosition.y <= scissor.y + scissor.w;
            }

            if (insideRect)
            {
                float area = rect.w * rect.h;
                if (area < smallestArea)
                {
                    smallestArea = area;
                    hoveredTextInput = entity;
                }
            }
        }

        auto& dispatcher = events::EventDispatcher::instance();

        auto makeEntityPayload = [&](entt::entity entity) -> std::pair<services::EntityHandle, std::string>
        {
            services::EntityHandle handle = services::internal::toHandle(entity);
            std::string name;
            if (registry.all_of<components::NameComponent>(entity))
                name = registry.get<components::NameComponent>(entity).name;
            return {handle, std::move(name)};
        };

        // Helper: check if entity has a valid selection range
        auto hasSelection = [](const components::UITextInputComponent& comp) -> bool
        {
            return comp.selectionStart >= 0 && comp.selectionEnd >= 0
                && comp.selectionStart != comp.selectionEnd;
        };

        // Helper: delete selected text, returns true if selection was deleted
        auto deleteSelection = [](components::UITextInputComponent& comp) -> bool
        {
            if (comp.selectionStart < 0 || comp.selectionEnd < 0
                || comp.selectionStart == comp.selectionEnd)
                return false;

            int selMin = std::min(comp.selectionStart, comp.selectionEnd);
            int selMax = std::max(comp.selectionStart, comp.selectionEnd);
            selMin = std::max(0, std::min(selMin, static_cast<int>(comp.text.size())));
            selMax = std::max(0, std::min(selMax, static_cast<int>(comp.text.size())));

            comp.text.erase(selMin, selMax - selMin);
            comp.cursorPosition = selMin;
            comp.selectionStart = -1;
            comp.selectionEnd = -1;
            return true;
        };

        // Helper: get selected text
        auto getSelectedText = [](const components::UITextInputComponent& comp) -> std::string
        {
            if (comp.selectionStart < 0 || comp.selectionEnd < 0
                || comp.selectionStart == comp.selectionEnd)
                return "";

            int selMin = std::min(comp.selectionStart, comp.selectionEnd);
            int selMax = std::max(comp.selectionStart, comp.selectionEnd);
            selMin = std::max(0, std::min(selMin, static_cast<int>(comp.text.size())));
            selMax = std::max(0, std::min(selMax, static_cast<int>(comp.text.size())));

            return comp.text.substr(selMin, selMax - selMin);
        };

        // Helper: find word boundary (for Ctrl+arrow / double-click)
        auto findWordBoundaryLeft = [](const std::string& text, int pos) -> int
        {
            if (pos <= 0) return 0;
            int p = pos - 1;
            // Skip non-alphanumeric
            while (p > 0 && !std::isalnum(static_cast<unsigned char>(text[p])))
                --p;
            // Skip alphanumeric
            while (p > 0 && std::isalnum(static_cast<unsigned char>(text[p - 1])))
                --p;
            return p;
        };

        auto findWordBoundaryRight = [](const std::string& text, int pos) -> int
        {
            int len = static_cast<int>(text.size());
            if (pos >= len) return len;
            int p = pos;
            // Skip alphanumeric
            while (p < len && std::isalnum(static_cast<unsigned char>(text[p])))
                ++p;
            // Skip non-alphanumeric
            while (p < len && !std::isalnum(static_cast<unsigned char>(text[p])))
                ++p;
            return p;
        };

        // PHASE 2: Focus management
        if (ctx.leftMousePressed)
        {
            if (hoveredTextInput != entt::null)
            {
                // Click on a text input -> focus it
                if (focusedTextInput != hoveredTextInput)
                {
                    // Unfocus previous
                    if (focusedTextInput != entt::null && registry.valid(focusedTextInput)
                        && registry.all_of<components::UITextInputComponent>(focusedTextInput))
                    {
                        auto& prevComp = registry.get<components::UITextInputComponent>(focusedTextInput);
                        prevComp.currentState = components::UITextInputState::Normal;
                        prevComp.selectionStart = -1;
                        prevComp.selectionEnd = -1;

                        auto [handle, name] = makeEntityPayload(focusedTextInput);
                        events::ui::UITextInputUnfocusedNotification notif;
                        notif.entity = handle;
                        notif.entityName = std::move(name);
                        dispatcher.publish(notif);
                    }

                    focusedTextInput = hoveredTextInput;
                    auto& comp = registry.get<components::UITextInputComponent>(focusedTextInput);
                    comp.currentState = components::UITextInputState::Focused;
                    comp.cursorPosition = static_cast<int>(comp.text.size());
                    comp.caretBlinkTimer = 0.0f;
                    comp.caretVisible = true;
                    comp.selectionStart = -1;
                    comp.selectionEnd = -1;

                    auto [handle, name] = makeEntityPayload(focusedTextInput);
                    events::ui::UITextInputFocusedNotification notif;
                    notif.entity = handle;
                    notif.entityName = std::move(name);
                    dispatcher.publish(notif);
                }
            }
            else
            {
                // Click outside -> unfocus
                if (focusedTextInput != entt::null && registry.valid(focusedTextInput)
                    && registry.all_of<components::UITextInputComponent>(focusedTextInput))
                {
                    auto& comp = registry.get<components::UITextInputComponent>(focusedTextInput);
                    comp.currentState = components::UITextInputState::Normal;
                    comp.selectionStart = -1;
                    comp.selectionEnd = -1;

                    auto [handle, name] = makeEntityPayload(focusedTextInput);
                    events::ui::UITextInputUnfocusedNotification notif;
                    notif.entity = handle;
                    notif.entityName = std::move(name);
                    dispatcher.publish(notif);
                }
                focusedTextInput = entt::null;
            }
        }

        // Double-click on focused text input -> select word
        if (ctx.leftMouseDoubleClick && focusedTextInput != entt::null
            && hoveredTextInput == focusedTextInput
            && registry.valid(focusedTextInput)
            && registry.all_of<components::UITextInputComponent>(focusedTextInput))
        {
            auto& comp = registry.get<components::UITextInputComponent>(focusedTextInput);
            if (!comp.text.empty())
            {
                int pos = comp.cursorPosition;
                comp.selectionStart = findWordBoundaryLeft(comp.text, pos);
                comp.selectionEnd = findWordBoundaryRight(comp.text, pos);
                comp.cursorPosition = comp.selectionEnd;
            }
        }

        // Escape -> unfocus
        if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Escape) && focusedTextInput != entt::null)
        {
            if (registry.valid(focusedTextInput)
                && registry.all_of<components::UITextInputComponent>(focusedTextInput))
            {
                auto& comp = registry.get<components::UITextInputComponent>(focusedTextInput);
                comp.currentState = components::UITextInputState::Normal;
                comp.selectionStart = -1;
                comp.selectionEnd = -1;

                auto [handle, name] = makeEntityPayload(focusedTextInput);
                events::ui::UITextInputUnfocusedNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                dispatcher.publish(notif);
            }
            focusedTextInput = entt::null;
        }

        // PHASE 3: Text editing (when focused)
        if (focusedTextInput != entt::null && registry.valid(focusedTextInput)
            && registry.all_of<components::UITextInputComponent>(focusedTextInput))
        {
            auto& comp = registry.get<components::UITextInputComponent>(focusedTextInput);
            bool textChanged = false;
            int textLen = static_cast<int>(comp.text.size());

            // Character input
            for (uint32_t codepoint : ctx.charInput)
            {
                // Skip control characters
                if (codepoint < 32 || codepoint == 127)
                    continue;

                // Delete selection first if any
                deleteSelection(comp);
                textLen = static_cast<int>(comp.text.size());

                // Check max length
                if (comp.maxLength > 0 && textLen >= comp.maxLength)
                    continue;

                // Insert character (ASCII only for simplicity — handles most use cases)
                if (codepoint < 128)
                {
                    comp.text.insert(comp.cursorPosition, 1, static_cast<char>(codepoint));
                }
                else
                {
                    // UTF-8 encode
                    std::string utf8;
                    if (codepoint < 0x80)
                    {
                        utf8 += static_cast<char>(codepoint);
                    }
                    else if (codepoint < 0x800)
                    {
                        utf8 += static_cast<char>(0xC0 | (codepoint >> 6));
                        utf8 += static_cast<char>(0x80 | (codepoint & 0x3F));
                    }
                    else if (codepoint < 0x10000)
                    {
                        utf8 += static_cast<char>(0xE0 | (codepoint >> 12));
                        utf8 += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                        utf8 += static_cast<char>(0x80 | (codepoint & 0x3F));
                    }
                    else
                    {
                        utf8 += static_cast<char>(0xF0 | (codepoint >> 18));
                        utf8 += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                        utf8 += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                        utf8 += static_cast<char>(0x80 | (codepoint & 0x3F));
                    }
                    comp.text.insert(comp.cursorPosition, utf8);
                }

                comp.cursorPosition++;
                textChanged = true;
                comp.caretBlinkTimer = 0.0f;
                comp.caretVisible = true;
            }
            textLen = static_cast<int>(comp.text.size());

            // Backspace
            if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Backspace))
            {
                if (hasSelection(comp))
                {
                    deleteSelection(comp);
                    textChanged = true;
                }
                else if (comp.cursorPosition > 0)
                {
                    if (ctrlDown)
                    {
                        int newPos = findWordBoundaryLeft(comp.text, comp.cursorPosition);
                        comp.text.erase(newPos, comp.cursorPosition - newPos);
                        comp.cursorPosition = newPos;
                    }
                    else
                    {
                        comp.text.erase(comp.cursorPosition - 1, 1);
                        comp.cursorPosition--;
                    }
                    textChanged = true;
                }
                comp.caretBlinkTimer = 0.0f;
                comp.caretVisible = true;
            }

            // Delete
            if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Delete))
            {
                textLen = static_cast<int>(comp.text.size());
                if (hasSelection(comp))
                {
                    deleteSelection(comp);
                    textChanged = true;
                }
                else if (comp.cursorPosition < textLen)
                {
                    if (ctrlDown)
                    {
                        int newPos = findWordBoundaryRight(comp.text, comp.cursorPosition);
                        comp.text.erase(comp.cursorPosition, newPos - comp.cursorPosition);
                    }
                    else
                    {
                        comp.text.erase(comp.cursorPosition, 1);
                    }
                    textChanged = true;
                }
                comp.caretBlinkTimer = 0.0f;
                comp.caretVisible = true;
            }

            // Left arrow
            if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Left))
            {
                textLen = static_cast<int>(comp.text.size());
                int prevPos = comp.cursorPosition;

                if (ctrlDown)
                    comp.cursorPosition = findWordBoundaryLeft(comp.text, comp.cursorPosition);
                else if (comp.cursorPosition > 0)
                    comp.cursorPosition--;

                if (shiftDown)
                {
                    if (comp.selectionStart < 0) comp.selectionStart = prevPos;
                    comp.selectionEnd = comp.cursorPosition;
                }
                else
                {
                    if (hasSelection(comp))
                        comp.cursorPosition = std::min(comp.selectionStart, comp.selectionEnd);
                    comp.selectionStart = -1;
                    comp.selectionEnd = -1;
                }
                comp.caretBlinkTimer = 0.0f;
                comp.caretVisible = true;
            }

            // Right arrow
            if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Right))
            {
                textLen = static_cast<int>(comp.text.size());
                int prevPos = comp.cursorPosition;

                if (ctrlDown)
                    comp.cursorPosition = findWordBoundaryRight(comp.text, comp.cursorPosition);
                else if (comp.cursorPosition < textLen)
                    comp.cursorPosition++;

                if (shiftDown)
                {
                    if (comp.selectionStart < 0) comp.selectionStart = prevPos;
                    comp.selectionEnd = comp.cursorPosition;
                }
                else
                {
                    if (hasSelection(comp))
                        comp.cursorPosition = std::max(comp.selectionStart, comp.selectionEnd);
                    comp.selectionStart = -1;
                    comp.selectionEnd = -1;
                }
                comp.caretBlinkTimer = 0.0f;
                comp.caretVisible = true;
            }

            // Home
            if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Home))
            {
                int prevPos = comp.cursorPosition;
                comp.cursorPosition = 0;

                if (shiftDown)
                {
                    if (comp.selectionStart < 0) comp.selectionStart = prevPos;
                    comp.selectionEnd = 0;
                }
                else
                {
                    comp.selectionStart = -1;
                    comp.selectionEnd = -1;
                }
                comp.caretBlinkTimer = 0.0f;
                comp.caretVisible = true;
            }

            // End
            if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::End))
            {
                textLen = static_cast<int>(comp.text.size());
                int prevPos = comp.cursorPosition;
                comp.cursorPosition = textLen;

                if (shiftDown)
                {
                    if (comp.selectionStart < 0) comp.selectionStart = prevPos;
                    comp.selectionEnd = textLen;
                }
                else
                {
                    comp.selectionStart = -1;
                    comp.selectionEnd = -1;
                }
                comp.caretBlinkTimer = 0.0f;
                comp.caretVisible = true;
            }

            // Ctrl+A -> select all
            if (ctx.isKeyPressed && ctrlDown && ctx.isKeyPressed(keycode::A))
            {
                textLen = static_cast<int>(comp.text.size());
                comp.selectionStart = 0;
                comp.selectionEnd = textLen;
                comp.cursorPosition = textLen;
            }

            // Ctrl+C -> copy
            if (ctx.isKeyPressed && ctrlDown && ctx.isKeyPressed(keycode::C))
            {
                if (hasSelection(comp) && ctx.setClipboardText)
                {
                    ctx.setClipboardText(getSelectedText(comp));
                }
            }

            // Ctrl+X -> cut
            if (ctx.isKeyPressed && ctrlDown && ctx.isKeyPressed(keycode::X))
            {
                if (hasSelection(comp) && ctx.setClipboardText)
                {
                    ctx.setClipboardText(getSelectedText(comp));
                    deleteSelection(comp);
                    textChanged = true;
                }
            }

            // Ctrl+V -> paste
            if (ctx.isKeyPressed && ctrlDown && ctx.isKeyPressed(keycode::V))
            {
                if (ctx.getClipboardText)
                {
                    std::string clipboard = ctx.getClipboardText();
                    if (!clipboard.empty())
                    {
                        deleteSelection(comp);
                        textLen = static_cast<int>(comp.text.size());

                        // Enforce max length
                        if (comp.maxLength > 0)
                        {
                            int remaining = comp.maxLength - textLen;
                            if (remaining <= 0)
                                clipboard.clear();
                            else if (static_cast<int>(clipboard.size()) > remaining)
                                clipboard = clipboard.substr(0, remaining);
                        }

                        if (!clipboard.empty())
                        {
                            comp.text.insert(comp.cursorPosition, clipboard);
                            comp.cursorPosition += static_cast<int>(clipboard.size());
                            textChanged = true;
                        }
                    }
                }
                comp.caretBlinkTimer = 0.0f;
                comp.caretVisible = true;
            }

            // Enter -> submit
            if (ctx.isKeyPressed && ctx.isKeyPressed(keycode::Enter))
            {
                auto [handle, name] = makeEntityPayload(focusedTextInput);
                events::ui::UITextInputSubmitNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                notif.text = comp.text;
                dispatcher.publish(notif);
            }

            // Publish text changed notification
            if (textChanged)
            {
                auto [handle, name] = makeEntityPayload(focusedTextInput);
                events::ui::UITextInputChangedNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                notif.text = comp.text;
                dispatcher.publish(notif);
            }

            // Clamp cursor position
            comp.cursorPosition = std::max(0, std::min(comp.cursorPosition, static_cast<int>(comp.text.size())));
        }

        // PHASE 4: State machine transitions + color lerp + visual override
        for (auto entity : textInputView)
        {
            auto& comp = registry.get<components::UITextInputComponent>(entity);

            if (registry.all_of<components::NameComponent>(entity))
                if (!registry.get<components::NameComponent>(entity).isActive)
                    continue;

            // State machine
            if (!comp.interactable)
            {
                comp.currentState = components::UITextInputState::Disabled;
            }
            else if (entity == focusedTextInput)
            {
                comp.currentState = components::UITextInputState::Focused;
            }
            else if (entity == hoveredTextInput)
            {
                comp.currentState = components::UITextInputState::Hovered;
            }
            else
            {
                comp.currentState = components::UITextInputState::Normal;
            }

            // Caret blink timer (only when focused)
            if (comp.currentState == components::UITextInputState::Focused && comp.caretBlinkRate > 0.0f)
            {
                comp.caretBlinkTimer += ctx.deltaTime;
                if (comp.caretBlinkTimer >= comp.caretBlinkRate)
                {
                    comp.caretBlinkTimer -= comp.caretBlinkRate;
                    comp.caretVisible = !comp.caretVisible;
                }
            }
            else
            {
                comp.caretVisible = false;
            }

            // Color lerp
            glm::vec4 targetColor;
            switch (comp.currentState)
            {
            case components::UITextInputState::Hovered:
                targetColor = comp.hoveredColor;
                break;
            case components::UITextInputState::Focused:
                targetColor = comp.focusedColor;
                break;
            case components::UITextInputState::Disabled:
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

            // Override UIImageComponent color tint
            if (registry.all_of<components::UIImageComponent>(entity))
            {
                auto& imageComp = registry.get<components::UIImageComponent>(entity);
                imageComp.colorTint = comp.currentDisplayColor;
            }
        }
    }

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
        struct ScrollContainerInfo
        {
            glm::vec2 scrollOffset{0.0f, 0.0f};
            glm::vec4 scissorRect{0.0f, 0.0f, 0.0f, 0.0f};
        };
        std::unordered_map<uint32_t, ScrollContainerInfo> scrollContainers;
        {
            auto scrollView = registry.view<components::UIScrollComponent, components::UIRectComponent>();
            for (auto scrollEntity : scrollView)
            {
                const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                if (!scrollCanvas)
                    continue;

                float sc = 1.0f;
                if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

                const auto& scrollRect = registry.get<components::UIRectComponent>(scrollEntity);
                PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                const auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);

                float sx = std::max(0.0f, vpRect.x);
                float sy = std::max(0.0f, vpRect.y);
                float sw = std::max(0.0f, std::min(vpRect.x + vpRect.w, vw) - sx);
                float sh = std::max(0.0f, std::min(vpRect.y + vpRect.h, vh) - sy);

                ScrollContainerInfo info;
                info.scrollOffset = scrollComp.scrollOffset;
                info.scissorRect = glm::vec4(sx, sy, sw, sh);
                scrollContainers[static_cast<uint32_t>(scrollEntity)] = info;
            }
        }

        auto findScrollInfo = [&](entt::entity entity) -> std::pair<entt::entity, glm::vec4>
        {
            entt::entity scrollAncestor = entt::null;
            entt::entity current = entity;
            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parentEntity = registry.get<components::ParentComponent>(current).parent;
                if (parentEntity == entt::null || !registry.valid(parentEntity))
                    break;
                if (scrollAncestor == entt::null
                    && registry.all_of<components::UIScrollComponent>(parentEntity))
                    scrollAncestor = parentEntity;
                if (registry.all_of<components::UICanvasComponent>(parentEntity))
                    break;
                current = parentEntity;
            }
            glm::vec4 scissor{0.0f, 0.0f, 0.0f, 0.0f};
            if (scrollAncestor != entt::null)
            {
                auto it = scrollContainers.find(static_cast<uint32_t>(scrollAncestor));
                if (it != scrollContainers.end())
                    scissor = it->second.scissorRect;
            }
            return {scrollAncestor, scissor};
        };

        auto hitTestRect = [&](PixelRect rect, glm::vec4 scissor) -> bool
        {
            bool inside = ctx.mousePosition.x >= rect.x && ctx.mousePosition.x <= rect.x + rect.w
                && ctx.mousePosition.y >= rect.y && ctx.mousePosition.y <= rect.y + rect.h;
            if (inside && scissor.z > 0.0f && scissor.w > 0.0f)
            {
                inside = ctx.mousePosition.x >= scissor.x
                    && ctx.mousePosition.x <= scissor.x + scissor.z
                    && ctx.mousePosition.y >= scissor.y
                    && ctx.mousePosition.y <= scissor.y + scissor.w;
            }
            return inside;
        };

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

            const auto* canvas = findCanvasForEntity(registry, dropdownEntity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(dropdownEntity))
                canvas = &registry.get<components::UICanvasComponent>(dropdownEntity);
            if (!canvas)
                continue;

            float scale = 1.0f;
            if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                scale = std::min(vw / canvas->referenceWidth, vh / canvas->referenceHeight);

            const auto& rectComp = registry.get<components::UIRectComponent>(dropdownEntity);
            PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);

            auto [scrollAncestor, scissor] = findScrollInfo(dropdownEntity);
            if (scrollAncestor != entt::null)
            {
                auto it = scrollContainers.find(static_cast<uint32_t>(scrollAncestor));
                if (it != scrollContainers.end())
                {
                    rect.x -= it->second.scrollOffset.x;
                    rect.y -= it->second.scrollOffset.y;
                }
            }

            // Hit test on header rect
            if (dropdownComp.interactable && hitTestRect(rect, scissor))
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

                if (hitTestRect(listRect, scissor))
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

        auto makeEntityPayload = [&](entt::entity entity) -> std::pair<services::EntityHandle, std::string>
        {
            services::EntityHandle handle = services::internal::toHandle(entity);
            std::string name;
            if (registry.all_of<components::NameComponent>(entity))
                name = registry.get<components::NameComponent>(entity).name;
            return {handle, std::move(name)};
        };

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

                    auto [handle, name] = makeEntityPayload(dropdownEntity);
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

                        auto [handle, name] = makeEntityPayload(dropdownEntity);

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

                        auto [handle, name] = makeEntityPayload(dropdownEntity);
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

                        auto [handle, name] = makeEntityPayload(dropdownEntity);
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

                        auto [otherHandle, otherName] = makeEntityPayload(
                            components::UIDropdownComponent::activeDropdownEntity);
                        events::ui::UIDropdownClosedNotification closeNotif;
                        closeNotif.entity = otherHandle;
                        closeNotif.entityName = std::move(otherName);
                        dispatcher.publish(closeNotif);
                    }

                    comp.isOpen = true;
                    comp.hoveredOptionIndex = -1;
                    components::UIDropdownComponent::activeDropdownEntity = dropdownEntity;
                    newState = components::UIDropdownState::Open;

                    auto [handle, name] = makeEntityPayload(dropdownEntity);
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
                    const auto* canvas = findCanvasForEntity(registry,
                        components::UIDropdownComponent::activeDropdownEntity);
                    float scale = 1.0f;
                    if (canvas && canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                        scale = std::min(vw / canvas->referenceWidth, vh / canvas->referenceHeight);
                    PixelRect headerRect = resolvePixelRect(rectComp, vw, vh, scale);
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
            const auto* canvas = findCanvasForEntity(registry, tabsEntity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(tabsEntity))
                canvas = &registry.get<components::UICanvasComponent>(tabsEntity);
            if (!canvas)
                continue;

            float scale = 1.0f;
            if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                scale = std::min(vw / canvas->referenceWidth, vh / canvas->referenceHeight);

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
                PixelRect rect = resolvePixelRect(btnRect, vw, vh, scale);

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
                    services::EntityHandle handle = services::internal::toHandle(tabsEntity);
                    std::string entityName;
                    if (registry.all_of<components::NameComponent>(tabsEntity))
                        entityName = registry.get<components::NameComponent>(tabsEntity).name;

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

        auto makeEntityPayload = [&](entt::entity entity) -> std::pair<services::EntityHandle, std::string>
        {
            services::EntityHandle handle = services::internal::toHandle(entity);
            std::string name;
            if (registry.all_of<components::NameComponent>(entity))
                name = registry.get<components::NameComponent>(entity).name;
            return {handle, std::move(name)};
        };

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
                const auto* canvas = findCanvasForEntity(registry, sliderEntity);
                if (!canvas) { comp.isDragging = false; break; }

                float scale = 1.0f;
                if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    scale = std::min(vw / canvas->referenceWidth, vh / canvas->referenceHeight);

                const auto& rectComp = registry.get<components::UIRectComponent>(sliderEntity);
                PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);

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
                    auto [handle, name] = makeEntityPayload(sliderEntity);
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

                auto [handle, name] = makeEntityPayload(sliderEntity);
                events::ui::UISliderDragEndNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                notif.finalValue = comp.value;
                dispatcher.publish(notif);
            }
            break; // only one slider drags at a time
        }

        // ============ PRE-COMPUTE SCROLL CONTAINERS ============
        struct ScrollContainerInfo
        {
            glm::vec2 scrollOffset{0.0f, 0.0f};
            glm::vec4 scissorRect{0.0f, 0.0f, 0.0f, 0.0f};
        };
        std::unordered_map<uint32_t, ScrollContainerInfo> scrollContainers;
        {
            auto scrollView = registry.view<components::UIScrollComponent, components::UIRectComponent>();
            for (auto scrollEntity : scrollView)
            {
                const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                if (!scrollCanvas)
                    continue;

                float sc = 1.0f;
                if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

                const auto& scrollRect = registry.get<components::UIRectComponent>(scrollEntity);
                PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                const auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);

                float sx = std::max(0.0f, vpRect.x);
                float sy = std::max(0.0f, vpRect.y);
                float sw = std::max(0.0f, std::min(vpRect.x + vpRect.w, vw) - sx);
                float sh = std::max(0.0f, std::min(vpRect.y + vpRect.h, vh) - sy);

                ScrollContainerInfo info;
                info.scrollOffset = scrollComp.scrollOffset;
                info.scissorRect = glm::vec4(sx, sy, sw, sh);
                scrollContainers[static_cast<uint32_t>(scrollEntity)] = info;
            }
        }

        auto findScrollInfo = [&](entt::entity entity) -> std::pair<entt::entity, glm::vec4>
        {
            entt::entity scrollAncestor = entt::null;
            entt::entity current = entity;
            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parentEntity = registry.get<components::ParentComponent>(current).parent;
                if (parentEntity == entt::null || !registry.valid(parentEntity))
                    break;
                if (scrollAncestor == entt::null
                    && registry.all_of<components::UIScrollComponent>(parentEntity))
                    scrollAncestor = parentEntity;
                if (registry.all_of<components::UICanvasComponent>(parentEntity))
                    break;
                current = parentEntity;
            }
            glm::vec4 scissor{0.0f, 0.0f, 0.0f, 0.0f};
            if (scrollAncestor != entt::null)
            {
                auto it = scrollContainers.find(static_cast<uint32_t>(scrollAncestor));
                if (it != scrollContainers.end())
                    scissor = it->second.scissorRect;
            }
            return {scrollAncestor, scissor};
        };

        auto hitTestRect = [&](PixelRect rect, glm::vec4 scissor) -> bool
        {
            bool inside = ctx.mousePosition.x >= rect.x && ctx.mousePosition.x <= rect.x + rect.w
                && ctx.mousePosition.y >= rect.y && ctx.mousePosition.y <= rect.y + rect.h;
            if (inside && scissor.z > 0.0f && scissor.w > 0.0f)
            {
                inside = ctx.mousePosition.x >= scissor.x
                    && ctx.mousePosition.x <= scissor.x + scissor.z
                    && ctx.mousePosition.y >= scissor.y
                    && ctx.mousePosition.y <= scissor.y + scissor.w;
            }
            return inside;
        };

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

            const auto* canvas = findCanvasForEntity(registry, sliderEntity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(sliderEntity))
                canvas = &registry.get<components::UICanvasComponent>(sliderEntity);
            if (!canvas)
                continue;

            float scale = 1.0f;
            if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                scale = std::min(vw / canvas->referenceWidth, vh / canvas->referenceHeight);

            const auto& rectComp = registry.get<components::UIRectComponent>(sliderEntity);
            PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);

            auto [scrollAncestor, scissor] = findScrollInfo(sliderEntity);
            if (scrollAncestor != entt::null)
            {
                auto it = scrollContainers.find(static_cast<uint32_t>(scrollAncestor));
                if (it != scrollContainers.end())
                {
                    rect.x -= it->second.scrollOffset.x;
                    rect.y -= it->second.scrollOffset.y;
                }
            }

            if (hitTestRect(rect, scissor))
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
                    const auto* canvas = findCanvasForEntity(registry, sliderEntity);
                    if (!canvas && registry.all_of<components::UICanvasComponent>(sliderEntity))
                        canvas = &registry.get<components::UICanvasComponent>(sliderEntity);

                    if (canvas)
                    {
                        float scale = 1.0f;
                        if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                            scale = std::min(vw / canvas->referenceWidth, vh / canvas->referenceHeight);

                        const auto& rectComp = registry.get<components::UIRectComponent>(sliderEntity);
                        PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);

                        auto [scrollAncestor, scissor] = findScrollInfo(sliderEntity);
                        if (scrollAncestor != entt::null)
                        {
                            auto it = scrollContainers.find(static_cast<uint32_t>(scrollAncestor));
                            if (it != scrollContainers.end())
                            {
                                rect.x -= it->second.scrollOffset.x;
                                rect.y -= it->second.scrollOffset.y;
                            }
                        }

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
                                auto [handle, name] = makeEntityPayload(sliderEntity);
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
                            auto [handle, name] = makeEntityPayload(sliderEntity);
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
                auto [handle, name] = makeEntityPayload(sliderEntity);
                events::ui::UISliderHoverEnterNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(name);
                dispatcher.publish(notif);
            }
            else if (wasHovered && !isHovered)
            {
                auto [handle, name] = makeEntityPayload(sliderEntity);
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
}
