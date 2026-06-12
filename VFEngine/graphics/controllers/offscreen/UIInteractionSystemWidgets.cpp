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

        // Update active drag
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
                dragComp.currentGhostPos = ctx.mousePosition + dragComp.ghostOffset
                    - dragComp.ghostSize * 0.5f;

                if (dragComp.constrainToParent)
                {
                    auto canvasInfo = ui_common::findCanvasWithEntity(registry, dragEntity);
                    if (canvasInfo.canvas)
                    {
                        float minX = 0.0f, minY = 0.0f;
                        float maxX = vw - dragComp.ghostSize.x;
                        float maxY = vh - dragComp.ghostSize.y;

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

                    auto scrollContainers = ui_common::buildScrollContainerMap(registry, vw, vh);
                    auto [scrollAnc, scissor] = ui_common::findScrollInfo(registry, targetEntity, scrollContainers);
                    ui_common::applyScrollOffset(rect, scrollAnc, scrollContainers);

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

                {
                    auto [handle, name] = ui_common::makeEntityPayload(registry, dragEntity);
                    events::ui::UIDragEndNotification endNotif;
                    endNotif.entity = handle;
                    endNotif.entityName = std::move(name);
                    endNotif.wasDropped = wasDropped;
                    dispatcher.publish(endNotif);
                }

                dragComp.isDragging = false;
                components::UIDraggableComponent::activeDragEntity = entt::null;

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

        // Initiate new drag
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

            if (!ui_common::isInteractionAllowed(registry, entity))
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
