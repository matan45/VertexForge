#include "UIScreenSpaceScroll.hpp"
#include "FramePreparationSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

using namespace controllers::offscreen::ui_common;

namespace controllers::offscreen::ui_screenspace
{
    bool processActiveScrollDrags(entt::registry& registry, const FrameContext& ctx)
    {
        auto scrollDragView = registry.view<components::UIScrollComponent, components::UIRectComponent>();

        for (auto scrollEntity : scrollDragView)
        {
            auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);
            if (!scrollComp.isDragging)
                continue;

            if (ctx.leftMouseDown)
            {
                const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                if (!scrollCanvas) { scrollComp.isDragging = false; return true; }

                float vw = static_cast<float>(ctx.viewportWidth);
                float vh = static_cast<float>(ctx.viewportHeight);
                float sc = computeCanvasScale(scrollCanvas, vw, vh);

                const auto& scrollRect = registry.get<components::UIRectComponent>(scrollEntity);
                PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                if (scrollComp.dragAxis == 1) // vertical
                {
                    float maxScrollY = std::max(0.0f, scrollComp.contentSize.y - scrollComp.viewportSize.y);
                    float ratio = scrollComp.viewportSize.y / scrollComp.contentSize.y;
                    float thumbH = std::max(20.0f, vpRect.h * ratio);
                    float scrollRange = vpRect.h - thumbH;
                    if (scrollRange > 0.0f)
                    {
                        float deltaMouseY = ctx.mousePosition.y - scrollComp.dragStartMousePos.y;
                        scrollComp.scrollOffset.y = glm::clamp(
                            scrollComp.dragStartScrollOffset.y + (deltaMouseY / scrollRange) * maxScrollY,
                            0.0f, maxScrollY);
                    }
                }
                else // horizontal (dragAxis == 0)
                {
                    float maxScrollX = std::max(0.0f, scrollComp.contentSize.x - scrollComp.viewportSize.x);
                    float ratio = scrollComp.viewportSize.x / scrollComp.contentSize.x;
                    float thumbW = std::max(20.0f, vpRect.w * ratio);
                    float scrollRange = vpRect.w - thumbW;
                    if (scrollRange > 0.0f)
                    {
                        float deltaMouseX = ctx.mousePosition.x - scrollComp.dragStartMousePos.x;
                        scrollComp.scrollOffset.x = glm::clamp(
                            scrollComp.dragStartScrollOffset.x + (deltaMouseX / scrollRange) * maxScrollX,
                            0.0f, maxScrollX);
                    }
                }
            }
            else
            {
                scrollComp.isDragging = false;
            }
            return true; // only one drag at a time
        }

        return false;
    }

    void initiateScrollThumbDrag(entt::registry& registry, const FrameContext& ctx)
    {
        if (!ctx.leftMouseDown)
            return;

        auto scrollDragView = registry.view<components::UIScrollComponent, components::UIRectComponent>();

        for (auto scrollEntity : scrollDragView)
        {
            if (registry.all_of<components::NameComponent>(scrollEntity))
                if (!registry.get<components::NameComponent>(scrollEntity).isActive)
                    continue;

            const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
            if (!scrollCanvas) continue;

            float vw = static_cast<float>(ctx.viewportWidth);
            float vh = static_cast<float>(ctx.viewportHeight);
            float sc = computeCanvasScale(scrollCanvas, vw, vh);

            auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);
            const auto& scrollRect = registry.get<components::UIRectComponent>(scrollEntity);
            PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

            // Check vertical thumb hit
            if (scrollComp.verticalScrollEnabled && scrollComp.contentSize.y > scrollComp.viewportSize.y
                && scrollComp.verticalScrollbarVisibility != components::ScrollbarVisibility::Hidden)
            {
                float maxScrollY = std::max(0.0f, scrollComp.contentSize.y - scrollComp.viewportSize.y);
                float ratio = scrollComp.viewportSize.y / scrollComp.contentSize.y;
                float thumbH = std::max(20.0f, vpRect.h * ratio);
                float scrollRange = vpRect.h - thumbH;
                float thumbY = (maxScrollY > 0.0f)
                    ? vpRect.y + scrollRange * (scrollComp.scrollOffset.y / maxScrollY) : vpRect.y;
                float thumbX = vpRect.x + vpRect.w - 8.0f;

                if (ctx.mousePosition.x >= thumbX && ctx.mousePosition.x <= thumbX + 8.0f
                    && ctx.mousePosition.y >= thumbY && ctx.mousePosition.y <= thumbY + thumbH)
                {
                    scrollComp.isDragging = true;
                    scrollComp.dragAxis = 1;
                    scrollComp.dragStartScrollOffset = scrollComp.scrollOffset;
                    scrollComp.dragStartMousePos = ctx.mousePosition;
                    return;
                }
            }

            // Check horizontal thumb hit
            if (scrollComp.horizontalScrollEnabled && scrollComp.contentSize.x > scrollComp.viewportSize.x
                && scrollComp.horizontalScrollbarVisibility != components::ScrollbarVisibility::Hidden)
            {
                float maxScrollX = std::max(0.0f, scrollComp.contentSize.x - scrollComp.viewportSize.x);
                float ratio = scrollComp.viewportSize.x / scrollComp.contentSize.x;
                float thumbW = std::max(20.0f, vpRect.w * ratio);
                float scrollRange = vpRect.w - thumbW;
                float thumbX = (maxScrollX > 0.0f)
                    ? vpRect.x + scrollRange * (scrollComp.scrollOffset.x / maxScrollX) : vpRect.x;
                float thumbY = vpRect.y + vpRect.h - 8.0f;

                if (ctx.mousePosition.x >= thumbX && ctx.mousePosition.x <= thumbX + thumbW
                    && ctx.mousePosition.y >= thumbY && ctx.mousePosition.y <= thumbY + 8.0f)
                {
                    scrollComp.isDragging = true;
                    scrollComp.dragAxis = 0;
                    scrollComp.dragStartScrollOffset = scrollComp.scrollOffset;
                    scrollComp.dragStartMousePos = ctx.mousePosition;
                    return;
                }
            }
        }
    }

    void processScrollWheelInput(entt::registry& registry, const FrameContext& ctx, bool anyScrollDragging)
    {
        if (anyScrollDragging)
            return;
        if (ctx.scrollDelta.y == 0.0f && ctx.scrollDelta.x == 0.0f)
            return;

        auto scrollInputView = registry.view<components::UIScrollComponent, components::UIRectComponent>();

        // Find the innermost scroll container under the mouse cursor
        entt::entity targetScroll = entt::null;
        float smallestArea = std::numeric_limits<float>::max();

        for (auto scrollEntity : scrollInputView)
        {
            if (registry.all_of<components::NameComponent>(scrollEntity))
                if (!registry.get<components::NameComponent>(scrollEntity).isActive)
                    continue;

            const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
            if (!scrollCanvas)
                continue;

            float vw = static_cast<float>(ctx.viewportWidth);
            float vh = static_cast<float>(ctx.viewportHeight);
            float sc = computeCanvasScale(scrollCanvas, vw, vh);

            const auto& scrollRect = registry.get<components::UIRectComponent>(scrollEntity);
            PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

            if (ctx.mousePosition.x >= vpRect.x && ctx.mousePosition.x <= vpRect.x + vpRect.w
                && ctx.mousePosition.y >= vpRect.y && ctx.mousePosition.y <= vpRect.y + vpRect.h)
            {
                float area = vpRect.w * vpRect.h;
                if (area < smallestArea)
                {
                    smallestArea = area;
                    targetScroll = scrollEntity;
                }
            }
        }

        if (targetScroll != entt::null)
        {
            auto& scrollComp = registry.get<components::UIScrollComponent>(targetScroll);
            float sensitivity = scrollComp.scrollSensitivity * 20.0f;

            bool canScrollV = scrollComp.verticalScrollEnabled
                && scrollComp.contentSize.y > scrollComp.viewportSize.y;
            bool canScrollH = scrollComp.horizontalScrollEnabled
                && scrollComp.contentSize.x > scrollComp.viewportSize.x;

            if (canScrollV)
                scrollComp.scrollOffset.y -= ctx.scrollDelta.y * sensitivity;

            if (canScrollH)
            {
                float hDelta = ctx.scrollDelta.x;
                if (!canScrollV && ctx.scrollDelta.y != 0.0f)
                    hDelta = ctx.scrollDelta.y;
                scrollComp.scrollOffset.x -= hDelta * sensitivity;
            }
        }
    }

    namespace
    {
        std::pair<float, float> computeContentBounds(
            entt::registry& registry, entt::entity scrollEntity,
            const PixelRect& vpRect, float vw, float vh, float sc)
        {
            float minX = 0.0f, minY = 0.0f, maxX = vpRect.w, maxY = vpRect.h;

            if (registry.all_of<components::ChildrenComponent>(scrollEntity))
            {
                bool first = true;
                for (auto child : registry.get<components::ChildrenComponent>(scrollEntity).children)
                {
                    if (!registry.valid(child) || !registry.all_of<components::UIRectComponent>(child))
                        continue;
                    if (registry.all_of<components::NameComponent>(child)
                        && !registry.get<components::NameComponent>(child).isActive)
                        continue;

                    PixelRect cr = resolvePixelRect(
                        registry.get<components::UIRectComponent>(child), vw, vh, sc);
                    float relX = cr.x - vpRect.x;
                    float relY = cr.y - vpRect.y;

                    if (first)
                    {
                        minX = relX; minY = relY;
                        maxX = relX + cr.w; maxY = relY + cr.h;
                        first = false;
                    }
                    else
                    {
                        minX = std::min(minX, relX);
                        minY = std::min(minY, relY);
                        maxX = std::max(maxX, relX + cr.w);
                        maxY = std::max(maxY, relY + cr.h);
                    }
                }
            }

            float contentW = maxX - std::min(minX, 0.0f);
            float contentH = maxY - std::min(minY, 0.0f);
            return {contentW, contentH};
        }
    } // anonymous namespace

    ScrollContainerMap buildScrollContainerData(entt::registry& registry, const FrameContext& ctx)
    {
        ScrollContainerMap scrollContainers;
        float vw = static_cast<float>(ctx.viewportWidth);
        float vh = static_cast<float>(ctx.viewportHeight);

        auto scrollView = registry.view<components::UIScrollComponent, components::UIRectComponent>();
        for (auto scrollEntity : scrollView)
        {
            if (registry.all_of<components::NameComponent>(scrollEntity))
                if (!registry.get<components::NameComponent>(scrollEntity).isActive)
                    continue;

            const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
            if (!scrollCanvas)
                continue;

            float sc = computeCanvasScale(scrollCanvas, vw, vh);

            const auto& scrollRect = scrollView.get<components::UIRectComponent>(scrollEntity);
            PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

            auto [contentW, contentH] = computeContentBounds(registry, scrollEntity, vpRect, vw, vh, sc);

            auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);
            float maxScrollX = std::max(0.0f, contentW - vpRect.w);
            float maxScrollY = std::max(0.0f, contentH - vpRect.h);
            scrollComp.scrollOffset.x = scrollComp.horizontalScrollEnabled
                ? glm::clamp(scrollComp.scrollOffset.x, 0.0f, maxScrollX) : 0.0f;
            scrollComp.scrollOffset.y = scrollComp.verticalScrollEnabled
                ? glm::clamp(scrollComp.scrollOffset.y, 0.0f, maxScrollY) : 0.0f;

            scrollComp.contentSize = glm::vec2(contentW, contentH);
            scrollComp.viewportSize = glm::vec2(vpRect.w, vpRect.h);

            float sx = std::max(0.0f, vpRect.x);
            float sy = std::max(0.0f, vpRect.y);
            float sw = std::max(0.0f, std::min(vpRect.x + vpRect.w, vw) - sx);
            float sh = std::max(0.0f, std::min(vpRect.y + vpRect.h, vh) - sy);
            glm::vec4 scissor(sx, sy, sw, sh);
            scrollComp.computedScissorRect = scissor;

            scrollContainers[static_cast<uint32_t>(scrollEntity)] = {scrollComp.scrollOffset, scissor};
        }

        return scrollContainers;
    }

} // namespace controllers::offscreen::ui_screenspace
