#include "UIInteractionSystem.hpp"
#include "FramePreparationSystem.hpp"  // for FrameContext
#include "UICommon.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UIWindowEvents.hpp"
#include <algorithm>

using namespace controllers::offscreen::ui_common;

namespace controllers::offscreen
{
    void UIInteractionSystem::processWindowInteraction(const FrameContext& ctx)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& modalState = registry.ctx().emplace<components::UIModalState>();

        // Prune the modal stack: entries must still be valid, modal, and open
        std::erase_if(modalState.modalStack, [&registry](entt::entity e) {
            if (!registry.valid(e)) return true;
            auto* window = registry.try_get<components::UIWindowComponent>(e);
            if (!window || !window->modal) return true;
            return !scene::Entity::isEffectivelyActive(registry, e);
        });

        auto view = registry.view<components::UIWindowComponent, components::UIRectComponent>();

        // Append active modal windows the stack doesn't know about
        // (scene load, Entity::setActive from scripts).
        for (auto entity : view)
        {
            const auto& window = view.get<components::UIWindowComponent>(entity);
            if (!window.modal) continue;
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;
            if (std::find(modalState.modalStack.begin(), modalState.modalStack.end(), entity)
                == modalState.modalStack.end())
            {
                modalState.modalStack.push_back(entity);
            }
        }

        if (!ctx.playModeActive)
        {
            for (auto entity : view)
            {
                auto& window = view.get<components::UIWindowComponent>(entity);
                window.isDraggingWindow = false;
                window.closeHovered = false;
            }
            return;
        }

        float vw = static_cast<float>(ctx.viewportWidth);
        float vh = static_cast<float>(ctx.viewportHeight);
        const glm::vec4 noScissor{0.0f, 0.0f, 0.0f, 0.0f};
        auto& dispatcher = events::EventDispatcher::instance();

        for (auto entity : view)
        {
            auto& window = view.get<components::UIWindowComponent>(entity);

            if (!scene::Entity::isEffectivelyActive(registry, entity))
            {
                window.isDraggingWindow = false;
                window.closeHovered = false;
                continue;
            }

            const auto* canvas = findCanvasForEntity(registry, entity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
                canvas = &registry.get<components::UICanvasComponent>(entity);
            if (!canvas)
                continue;

            float scale = computeCanvasScale(canvas, vw, vh);
            auto& rectComp = registry.get<components::UIRectComponent>(entity);

            // Continue an active title-bar drag regardless of gating (the
            // window being dragged is by definition the interactable one).
            if (window.isDraggingWindow)
            {
                if (!ctx.leftMouseDown)
                {
                    window.isDraggingWindow = false;
                }
                else
                {
                    glm::vec2 delta = ctx.mousePosition - window.dragStartMousePos;
                    // anchoredPosition.y is up-positive; screen y is down-positive
                    rectComp.anchoredPosition = window.dragStartAnchoredPos
                        + glm::vec2(delta.x, -delta.y) / scale;
                }
                continue;
            }

            if (!isInteractionAllowed(registry, entity))
            {
                window.closeHovered = false;
                continue;
            }

            if (!window.showTitleBar)
            {
                window.closeHovered = false;
                continue;
            }

            PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);
            float titleH = window.titleBarHeight * scale;
            PixelRect titleRect{rect.x, rect.y, rect.w, titleH};
            PixelRect closeRect{rect.x + rect.w - titleH, rect.y, titleH, titleH};

            window.closeHovered = window.closable
                && hitTestRect(ctx.mousePosition, closeRect, noScissor);

            if (!ctx.leftMousePressed)
                continue;

            if (window.closeHovered)
            {
                // Close: deactivate, pop from the modal stack, notify scripts
                if (auto* name = registry.try_get<components::NameComponent>(entity))
                    name->isActive = false;
                window.closeHovered = false;
                std::erase(modalState.modalStack, entity);

                auto [handle, entityName] = makeEntityPayload(registry, entity);
                events::ui::UIWindowClosedNotification notif;
                notif.entity = handle;
                notif.entityName = std::move(entityName);
                dispatcher.publish(notif);
                continue;
            }

            if (window.draggable && hitTestRect(ctx.mousePosition, titleRect, noScissor))
            {
                window.isDraggingWindow = true;
                window.dragStartMousePos = ctx.mousePosition;
                window.dragStartAnchoredPos = rectComp.anchoredPosition;
            }
        }
    }
}
