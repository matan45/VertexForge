#pragma once
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <unordered_map>
#include <algorithm>
#include <string>
#include <utility>

namespace services
{
    struct EntityHandle;
}

namespace controllers::offscreen::ui_common
{
    struct PixelRect
    {
        float x, y, w, h;
    };

    struct ScrollContainerInfo
    {
        glm::vec2 scrollOffset{0.0f, 0.0f};
        glm::vec4 scissorRect{0.0f, 0.0f, 0.0f, 0.0f};
    };

    inline PixelRect resolvePixelRect(
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

    inline const components::UICanvasComponent* findCanvasForEntity(
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

    inline float computeCanvasScale(
        const components::UICanvasComponent* canvas,
        float vw, float vh)
    {
        float scale = 1.0f;
        if (canvas && canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
            scale = std::min(vw / canvas->referenceWidth, vh / canvas->referenceHeight);
        return scale;
    }

    struct CanvasEntityInfo
    {
        const components::UICanvasComponent* canvas = nullptr;
        entt::entity canvasEntity = entt::null;
    };

    inline CanvasEntityInfo findCanvasWithEntity(
        entt::registry& registry, entt::entity entity)
    {
        CanvasEntityInfo result;
        entt::entity current = entity;
        while (registry.all_of<components::ParentComponent>(current))
        {
            entt::entity parent = registry.get<components::ParentComponent>(current).parent;
            if (parent == entt::null || !registry.valid(parent))
                break;
            if (registry.all_of<components::UICanvasComponent>(parent))
            {
                result.canvas = &registry.get<components::UICanvasComponent>(parent);
                result.canvasEntity = parent;
                return result;
            }
            current = parent;
        }
        if (registry.all_of<components::UICanvasComponent>(entity))
        {
            result.canvas = &registry.get<components::UICanvasComponent>(entity);
            result.canvasEntity = entity;
        }
        return result;
    }

    inline bool isEntityActive(entt::registry& registry, entt::entity entity)
    {
        return scene::Entity::isEffectivelyActive(registry, entity);
    }

    inline std::unordered_map<uint32_t, ScrollContainerInfo> buildScrollContainerMap(
        entt::registry& registry, float vw, float vh)
    {
        std::unordered_map<uint32_t, ScrollContainerInfo> scrollContainers;
        auto scrollView = registry.view<components::UIScrollComponent, components::UIRectComponent>();
        for (auto scrollEntity : scrollView)
        {
            if (!isEntityActive(registry, scrollEntity))
                continue;

            const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
            if (!scrollCanvas)
                continue;

            float sc = computeCanvasScale(scrollCanvas, vw, vh);

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
        return scrollContainers;
    }

    inline std::pair<entt::entity, glm::vec4> findScrollInfo(
        entt::registry& registry, entt::entity entity,
        const std::unordered_map<uint32_t, ScrollContainerInfo>& scrollContainers)
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
    }

    inline void applyScrollOffset(
        PixelRect& rect, entt::entity scrollAncestor,
        const std::unordered_map<uint32_t, ScrollContainerInfo>& scrollContainers)
    {
        if (scrollAncestor != entt::null)
        {
            auto it = scrollContainers.find(static_cast<uint32_t>(scrollAncestor));
            if (it != scrollContainers.end())
            {
                rect.x -= it->second.scrollOffset.x;
                rect.y -= it->second.scrollOffset.y;
            }
        }
    }

    inline bool hitTestRect(
        const glm::vec2& mousePosition,
        const PixelRect& rect, const glm::vec4& scissor)
    {
        bool inside = mousePosition.x >= rect.x && mousePosition.x <= rect.x + rect.w
            && mousePosition.y >= rect.y && mousePosition.y <= rect.y + rect.h;
        if (inside && scissor.z > 0.0f && scissor.w > 0.0f)
        {
            inside = mousePosition.x >= scissor.x
                && mousePosition.x <= scissor.x + scissor.z
                && mousePosition.y >= scissor.y
                && mousePosition.y <= scissor.y + scissor.w;
        }
        return inside;
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
}
