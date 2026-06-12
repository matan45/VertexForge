#pragma once
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <entt/entt.hpp>
#include <unordered_map>
#include <algorithm>
#include <optional>
#include <utility>

// Pure UI layout math shared between the graphics UI systems (rendering,
// runtime interaction) and the services layer (editor viewport picking).
// Depends only on utilities (components, scene) + glm + entt.
namespace utilities::ui
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

    // --- World-space UI (edit-mode rendering): model matrix mapping the unit
    // quad [-0.5, 0.5]^2 to the element's world-space rect on its canvas ---
    inline glm::mat4 computeCanvasImageModelMatrix(
        const components::UICanvasComponent& canvas,
        const glm::mat4& worldMatrix,
        const components::UIRectComponent& rectComp)
    {
        float canvasW = canvas.referenceWidth / canvas.pixelsPerUnit;
        float canvasH = canvas.referenceHeight / canvas.pixelsPerUnit;

        float anchorLeft = rectComp.anchorMin.x * canvas.referenceWidth;
        float anchorRight = rectComp.anchorMax.x * canvas.referenceWidth;
        float anchorBottom = rectComp.anchorMin.y * canvas.referenceHeight;
        float anchorTop = rectComp.anchorMax.y * canvas.referenceHeight;

        float w = (anchorRight - anchorLeft) + rectComp.sizeDelta.x;
        float h = (anchorTop - anchorBottom) + rectComp.sizeDelta.y;
        float cx = (anchorLeft + anchorRight) * 0.5f + rectComp.anchoredPosition.x;
        float cy = (anchorBottom + anchorTop) * 0.5f + rectComp.anchoredPosition.y;

        float localCX = cx / canvas.referenceWidth - 0.5f;
        float localCY = cy / canvas.referenceHeight - 0.5f;
        float normW = w / canvas.referenceWidth;
        float normH = h / canvas.referenceHeight;

        glm::mat4 canvasScaled = worldMatrix
            * glm::scale(glm::mat4(1.0f), glm::vec3(canvasW, canvasH, 1.0f));

        return canvasScaled
            * glm::translate(glm::mat4(1.0f), glm::vec3(localCX, localCY, 0.001f))
            * glm::scale(glm::mat4(1.0f), glm::vec3(normW, normH, 1.0f));
    }

    // Model matrix for an arbitrary sub-rect within the canvas (canvas pixel coords)
    inline glm::mat4 computeCanvasSubRectModelMatrix(
        const components::UICanvasComponent& canvas,
        const glm::mat4& worldMatrix,
        float patchCX, float patchCY, float patchW, float patchH)
    {
        float canvasW = canvas.referenceWidth / canvas.pixelsPerUnit;
        float canvasH = canvas.referenceHeight / canvas.pixelsPerUnit;
        float localCX = patchCX / canvas.referenceWidth - 0.5f;
        float localCY = patchCY / canvas.referenceHeight - 0.5f;
        float normW = patchW / canvas.referenceWidth;
        float normH = patchH / canvas.referenceHeight;

        glm::mat4 canvasScaled = worldMatrix
            * glm::scale(glm::mat4(1.0f), glm::vec3(canvasW, canvasH, 1.0f));

        return canvasScaled
            * glm::translate(glm::mat4(1.0f), glm::vec3(localCX, localCY, 0.001f))
            * glm::scale(glm::mat4(1.0f), glm::vec3(normW, normH, 1.0f));
    }

    // World-space corners (TL, TR, BR, BL) of the unit quad [-0.5, 0.5]^2
    // mapped by a canvas element model matrix.
    inline void computeWorldQuadCorners(const glm::mat4& model, glm::vec3 outCorners[4])
    {
        outCorners[0] = glm::vec3(model * glm::vec4(-0.5f,  0.5f, 0.0f, 1.0f)); // TL
        outCorners[1] = glm::vec3(model * glm::vec4( 0.5f,  0.5f, 0.0f, 1.0f)); // TR
        outCorners[2] = glm::vec3(model * glm::vec4( 0.5f, -0.5f, 0.0f, 1.0f)); // BR
        outCorners[3] = glm::vec3(model * glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f)); // BL
    }

    // Ray vs. quad (TL, TR, BR, BL corners): plane intersection + point-in-quad
    // via the edge axes. Returns hit distance t along the ray, or nullopt on miss.
    inline std::optional<float> intersectRayQuad(
        const glm::vec3& origin, const glm::vec3& direction, const glm::vec3 corners[4])
    {
        glm::vec3 u = corners[1] - corners[0]; // TL -> TR
        glm::vec3 v = corners[3] - corners[0]; // TL -> BL
        glm::vec3 normal = glm::cross(u, v);
        if (glm::dot(normal, normal) < 1e-12f)
            return std::nullopt; // degenerate quad

        float denom = glm::dot(normal, direction);
        if (std::abs(denom) < 1e-8f)
            return std::nullopt; // parallel to the quad plane

        float t = glm::dot(normal, corners[0] - origin) / denom;
        if (t < 0.0f)
            return std::nullopt; // behind the ray origin

        glm::vec3 local = origin + t * direction - corners[0];
        float s = glm::dot(local, u) / glm::dot(u, u);
        float q = glm::dot(local, v) / glm::dot(v, v);
        if (s < 0.0f || s > 1.0f || q < 0.0f || q > 1.0f)
            return std::nullopt;
        return t;
    }

    // Pick tie-break: nearest hit wins; smallest quad area breaks coplanar ties
    // (mirrors the runtime smallest-area widget hit-test). hasBest must be false
    // until the first hit is accepted — bestT/bestArea are unset before that.
    inline bool isBetterQuadHit(bool hasBest, float bestT, float bestArea, float t, float area)
    {
        if (!hasBest)
            return true;
        float epsilon = 1e-4f * glm::max(1.0f, t);
        bool closer = t < bestT - epsilon;
        bool coplanarSmaller = std::abs(t - bestT) <= epsilon && area < bestArea;
        return closer || coplanarSmaller;
    }

    // Nearest ancestor (or self) carrying a UIWindowComponent, entt::null if
    // none. Window subtrees render on the UI overlay layer so they sit above
    // the modal backdrop and all main-pass UI.
    inline entt::entity findOpenWindowAncestor(entt::registry& registry, entt::entity entity)
    {
        entt::entity current = entity;
        while (current != entt::null && registry.valid(current))
        {
            if (registry.all_of<components::UIWindowComponent>(current))
                return current;
            auto* parent = registry.try_get<components::ParentComponent>(current);
            current = parent ? parent->parent : entt::null;
        }
        return entt::null;
    }

    // Modal gating: true when no modal window is active, or `entity` is the
    // active (top-of-stack) modal or one of its descendants. Every pointer
    // interaction loop checks this so an open modal blocks the UI beneath it.
    inline bool isInteractionAllowed(entt::registry& registry, entt::entity entity)
    {
        const auto* state = registry.ctx().find<components::UIModalState>();
        if (!state || state->modalStack.empty())
            return true;
        entt::entity modal = state->modalStack.back();
        if (modal == entt::null || !registry.valid(modal))
            return true;

        entt::entity current = entity;
        while (current != entt::null && registry.valid(current))
        {
            if (current == modal)
                return true;
            auto* parent = registry.try_get<components::ParentComponent>(current);
            current = parent ? parent->parent : entt::null;
        }
        return false;
    }

    // Places a tooltip of the given pixel size near anchorPos (cursor or element
    // corner): below-right by default, flipped above the anchor when it would
    // cross the bottom edge, then clamped into the viewport.
    inline glm::vec2 computeTooltipPlacement(
        const glm::vec2& anchorPos, const glm::vec2& offset,
        const glm::vec2& tooltipSize, float viewportW, float viewportH)
    {
        glm::vec2 pos = anchorPos + offset;
        if (pos.y + tooltipSize.y > viewportH)
        {
            pos.y = anchorPos.y - offset.y - tooltipSize.y;
        }
        if (pos.x + tooltipSize.x > viewportW)
        {
            pos.x = viewportW - tooltipSize.x;
        }
        pos.x = std::max(0.0f, pos.x);
        pos.y = std::max(0.0f, pos.y);
        return pos;
    }
}
