#include "UIFrameBuilder.hpp"
#include "UICommon.hpp"
#include "UIInteractionSystem.hpp"
#include "FramePreparationSystem.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/ui/UIRenderTypes.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

using namespace controllers::offscreen::ui_common;

namespace controllers::offscreen
{
    namespace
    {
        // ---------------------------------------------------------------
        // Scrollbar drag interaction — Phase 1: process active drags
        // Returns true if any scroll container is currently being dragged.
        // ---------------------------------------------------------------
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

        // ---------------------------------------------------------------
        // Scrollbar drag interaction — Phase 2: initiate new thumb drag
        // ---------------------------------------------------------------
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

        // ---------------------------------------------------------------
        // Apply mouse wheel scroll input to the innermost scroll container
        // under the cursor.
        // ---------------------------------------------------------------
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

            // Apply scroll delta to the target container
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

        // ---------------------------------------------------------------
        // Position a single child within a layout group.
        // Updates targetX/targetY output, and mutates grid/cursor state.
        // ---------------------------------------------------------------
        void positionLayoutChild(
            const components::UILayoutGroupComponent& layoutComp,
            const PixelRect& pRect, bool isGrid, bool isVertical,
            float childW, float childH, float vw, float vh,
            int cols, int& gridCol, float& cursorY, float& rowHeight, float& cursor,
            float& targetX, float& targetY)
        {
            if (isGrid)
            {
                float availW = pRect.w - layoutComp.padding.x - layoutComp.padding.y;
                float cellW = (availW - layoutComp.spacing * (cols - 1)) / cols;

                targetX = pRect.x + layoutComp.padding.x + gridCol * (cellW + layoutComp.spacing);
                targetX += (cellW - childW) * 0.5f;
                targetY = pRect.y + cursorY;

                rowHeight = std::max(rowHeight, childH);
                gridCol++;
                if (gridCol >= cols)
                {
                    gridCol = 0;
                    cursorY += rowHeight + layoutComp.spacing;
                    rowHeight = 0.0f;
                }
            }
            else if (isVertical)
            {
                targetY = pRect.y + cursor;
                float availW = pRect.w - layoutComp.padding.x - layoutComp.padding.y;
                switch (layoutComp.childAlignment)
                {
                case components::ChildAlignment::Center:
                    targetX = pRect.x + layoutComp.padding.x + (availW - childW) * 0.5f;
                    break;
                case components::ChildAlignment::End:
                    targetX = pRect.x + layoutComp.padding.x + availW - childW;
                    break;
                default:
                    targetX = pRect.x + layoutComp.padding.x;
                    break;
                }
                cursor += childH + layoutComp.spacing;
            }
            else // Horizontal
            {
                targetX = pRect.x + cursor;
                float availH = pRect.h - layoutComp.padding.z - layoutComp.padding.w;
                switch (layoutComp.childAlignment)
                {
                case components::ChildAlignment::Center:
                    targetY = pRect.y + layoutComp.padding.z + (availH - childH) * 0.5f;
                    break;
                case components::ChildAlignment::End:
                    targetY = pRect.y + layoutComp.padding.z + availH - childH;
                    break;
                default:
                    targetY = pRect.y + layoutComp.padding.z;
                    break;
                }
                cursor += childW + layoutComp.spacing;
            }
        }

        // ---------------------------------------------------------------
        // Layout group pass: auto-position children of all layout groups.
        // ---------------------------------------------------------------
        void processLayoutGroups(entt::registry& registry, const FrameContext& ctx)
        {
            auto layoutView = registry.view<components::UILayoutGroupComponent,
                                            components::ChildrenComponent,
                                            components::UIRectComponent>();

            float vw = static_cast<float>(ctx.viewportWidth);
            float vh = static_cast<float>(ctx.viewportHeight);

            for (auto layoutEntity : layoutView)
            {
                if (registry.all_of<components::NameComponent>(layoutEntity))
                    if (!registry.get<components::NameComponent>(layoutEntity).isActive)
                        continue;

                const auto* layoutCanvas = findCanvasForEntity(registry, layoutEntity);
                if (!layoutCanvas)
                    continue;

                float sc = computeCanvasScale(layoutCanvas, vw, vh);

                const auto& parentRect = registry.get<components::UIRectComponent>(layoutEntity);
                PixelRect pRect = resolvePixelRect(parentRect, vw, vh, sc);

                const auto& layoutComp = registry.get<components::UILayoutGroupComponent>(layoutEntity);
                const auto& children = registry.get<components::ChildrenComponent>(layoutEntity).children;

                bool isVertical = (layoutComp.direction == components::LayoutDirection::Vertical);
                bool isGrid = (layoutComp.direction == components::LayoutDirection::Grid);

                float cursorY = layoutComp.padding.z;
                int gridCol = 0;
                int cols = std::max(1, layoutComp.constraintCount);
                float rowHeight = 0.0f;
                float cursor = isVertical ? layoutComp.padding.z : layoutComp.padding.x;

                for (auto child : children)
                {
                    if (!registry.valid(child) || !registry.all_of<components::UIRectComponent>(child))
                        continue;
                    if (registry.all_of<components::NameComponent>(child)
                        && !registry.get<components::NameComponent>(child).isActive)
                        continue;

                    auto& childRect = registry.get<components::UIRectComponent>(child);

                    float childW = (childRect.anchorMax.x - childRect.anchorMin.x) * vw + childRect.sizeDelta.x * sc;
                    float childH = ((1.0f - childRect.anchorMin.y) - (1.0f - childRect.anchorMax.y)) * vh + childRect.sizeDelta.y * sc;

                    float targetX = 0.0f, targetY = 0.0f;
                    positionLayoutChild(layoutComp, pRect, isGrid, isVertical,
                        childW, childH, vw, vh, cols, gridCol, cursorY, rowHeight, cursor,
                        targetX, targetY);

                    float anchorCenterX = (childRect.anchorMin.x + childRect.anchorMax.x) * 0.5f * vw;
                    float anchorCenterY = ((1.0f - childRect.anchorMax.y) + (1.0f - childRect.anchorMin.y)) * 0.5f * vh;

                    childRect.anchoredPosition.x = (targetX + childRect.pivot.x * childW - anchorCenterX) / sc;
                    childRect.anchoredPosition.y = -((targetY + childRect.pivot.y * childH) - anchorCenterY) / sc;
                }
            }
        }

        // ---------------------------------------------------------------
        // Compute content bounding box from direct children of a scroll entity.
        // Returns {contentW, contentH}.
        // ---------------------------------------------------------------
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

        // ---------------------------------------------------------------
        // Build scroll container data: computes content bounds, clamps
        // scroll offsets, and writes back contentSize/viewportSize/
        // computedScissorRect to the scroll components.
        // ---------------------------------------------------------------
        std::unordered_map<uint32_t, ScrollContainerInfo> buildScrollContainerData(
            entt::registry& registry, const FrameContext& ctx)
        {
            std::unordered_map<uint32_t, ScrollContainerInfo> scrollContainers;
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

        // ---------------------------------------------------------------
        // Emit one UIImage entity into the draw list.
        // ---------------------------------------------------------------
        void emitUIImageEntity(
            entt::registry& registry, entt::entity entity,
            entt::entity scrollAncestor,
            const components::UICanvasComponent* canvas,
            const FrameContext& ctx,
            const std::unordered_map<uint32_t, ScrollContainerInfo>& scrollContainers,
            std::vector<render::ui::UIImageRenderData>& drawList)
        {
            const auto& imageComp = registry.get<components::UIImageComponent>(entity);
            if (imageComp.texturePath.empty() && imageComp.colorTint.a < 0.01f)
                return;

            const auto& rectComp = registry.get<components::UIRectComponent>(entity);

            float viewportW = static_cast<float>(ctx.viewportWidth);
            float viewportH = static_cast<float>(ctx.viewportHeight);
            float scale = computeCanvasScale(canvas, viewportW, viewportH);

            PixelRect rect = resolvePixelRect(rectComp, viewportW, viewportH, scale);

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
            else if (registry.all_of<components::UIScrollComponent>(entity))
            {
                auto it = scrollContainers.find(static_cast<uint32_t>(entity));
                if (it != scrollContainers.end())
                    scissor = it->second.scissorRect;
            }

            render::ui::UIImageRenderData renderData;
            renderData.texturePath = imageComp.texturePath.empty() ? "__white_1x1__" : imageComp.texturePath;
            renderData.position = glm::vec2(rect.x, rect.y);
            renderData.size = glm::vec2(rect.w, rect.h);
            renderData.colorTint = imageComp.colorTint;
            renderData.scissorRect = scissor;
            drawList.push_back(std::move(renderData));
        }

        // ---------------------------------------------------------------
        // Two-pass UIImage rendering:
        //   Pass 1: scroll container backgrounds
        //   Pass 2: all other UIImage entities
        // ---------------------------------------------------------------
        void emitUIImagePasses(
            entt::registry& registry, const FrameContext& ctx,
            const std::unordered_map<uint32_t, ScrollContainerInfo>& scrollContainers,
            std::vector<render::ui::UIImageRenderData>& drawList)
        {
            auto view = registry.view<components::UIImageComponent, components::UIRectComponent>();

            // Pass 1: Scroll container backgrounds (must render before their children)
            for (auto entity : view)
            {
                if (!registry.all_of<components::UIScrollComponent>(entity))
                    continue;

                if (registry.all_of<components::NameComponent>(entity))
                    if (!registry.get<components::NameComponent>(entity).isActive)
                        continue;

                const auto* canvas = findCanvasForEntity(registry, entity);
                if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
                    canvas = &registry.get<components::UICanvasComponent>(entity);
                if (!canvas) continue;

                emitUIImageEntity(registry, entity, entt::null, canvas, ctx, scrollContainers, drawList);
            }

            // Pass 2: All other UIImage entities
            for (auto entity : view)
            {
                if (registry.all_of<components::UIScrollComponent>(entity))
                    continue;

                if (registry.all_of<components::NameComponent>(entity))
                    if (!registry.get<components::NameComponent>(entity).isActive)
                        continue;

                const components::UICanvasComponent* canvas = nullptr;
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
                    {
                        canvas = &registry.get<components::UICanvasComponent>(parentEntity);
                        break;
                    }
                    current = parentEntity;
                }

                if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
                    canvas = &registry.get<components::UICanvasComponent>(entity);

                if (!canvas) continue;

                emitUIImageEntity(registry, entity, scrollAncestor, canvas, ctx, scrollContainers, drawList);
            }
        }

        // ---------------------------------------------------------------
        // Generate slider fill + handle draw data.
        // ---------------------------------------------------------------
        void generateSliderDrawData(
            entt::registry& registry, const FrameContext& ctx,
            const std::unordered_map<uint32_t, ScrollContainerInfo>& scrollContainers,
            std::vector<render::ui::UIImageRenderData>& drawList)
        {
            const std::string whiteTex = "__white_1x1__";
            auto sliderDrawView = registry.view<components::UISliderComponent,
                                                components::UIRectComponent,
                                                components::UIImageComponent>();

            for (auto sliderEntity : sliderDrawView)
            {
                if (registry.all_of<components::NameComponent>(sliderEntity))
                    if (!registry.get<components::NameComponent>(sliderEntity).isActive)
                        continue;

                const auto& sliderComp = registry.get<components::UISliderComponent>(sliderEntity);
                const auto& rectComp = registry.get<components::UIRectComponent>(sliderEntity);

                const auto* canvas = findCanvasForEntity(registry, sliderEntity);
                if (!canvas && registry.all_of<components::UICanvasComponent>(sliderEntity))
                    canvas = &registry.get<components::UICanvasComponent>(sliderEntity);
                if (!canvas) continue;

                float sliderVw = static_cast<float>(ctx.viewportWidth);
                float sliderVh = static_cast<float>(ctx.viewportHeight);
                float scale = computeCanvasScale(canvas, sliderVw, sliderVh);

                PixelRect sliderRect = resolvePixelRect(rectComp, sliderVw, sliderVh, scale);

                auto [scrollAnc, scissor] = findScrollInfo(registry, sliderEntity, scrollContainers);
                applyScrollOffset(sliderRect, scrollAnc, scrollContainers);

                float normalizedValue = (sliderComp.maxValue > sliderComp.minValue)
                    ? (sliderComp.value - sliderComp.minValue) / (sliderComp.maxValue - sliderComp.minValue)
                    : 0.0f;
                normalizedValue = std::max(0.0f, std::min(1.0f, normalizedValue));

                std::string fillTex = sliderComp.fillTexture.empty() ? whiteTex : sliderComp.fillTexture;
                std::string handleTex = whiteTex;
                switch (sliderComp.currentState)
                {
                case components::UISliderState::Hovered:
                    handleTex = sliderComp.handleHoveredTexture.empty() ? whiteTex : sliderComp.handleHoveredTexture;
                    break;
                case components::UISliderState::Pressed:
                    handleTex = sliderComp.handlePressedTexture.empty() ? whiteTex : sliderComp.handlePressedTexture;
                    break;
                case components::UISliderState::Disabled:
                    handleTex = sliderComp.handleDisabledTexture.empty() ? whiteTex : sliderComp.handleDisabledTexture;
                    break;
                default:
                    handleTex = sliderComp.handleNormalTexture.empty() ? whiteTex : sliderComp.handleNormalTexture;
                    break;
                }

                if (sliderComp.orientation == components::UISliderOrientation::Horizontal)
                {
                    float fillW = sliderRect.w * normalizedValue;
                    if (fillW > 0.0f)
                    {
                        render::ui::UIImageRenderData fill;
                        fill.texturePath = fillTex;
                        fill.position = glm::vec2(sliderRect.x, sliderRect.y);
                        fill.size = glm::vec2(fillW, sliderRect.h);
                        fill.colorTint = sliderComp.fillColor;
                        fill.scissorRect = scissor;
                        drawList.push_back(std::move(fill));
                    }

                    float handleW = sliderRect.w * sliderComp.handleSizeRatio;
                    float handleX = sliderRect.x + normalizedValue * (sliderRect.w - handleW);
                    render::ui::UIImageRenderData handle;
                    handle.texturePath = handleTex;
                    handle.position = glm::vec2(handleX, sliderRect.y);
                    handle.size = glm::vec2(handleW, sliderRect.h);
                    handle.colorTint = sliderComp.currentHandleDisplayColor;
                    handle.scissorRect = scissor;
                    drawList.push_back(std::move(handle));
                }
                else // Vertical
                {
                    float fillH = sliderRect.h * normalizedValue;
                    if (fillH > 0.0f)
                    {
                        render::ui::UIImageRenderData fill;
                        fill.texturePath = fillTex;
                        fill.position = glm::vec2(sliderRect.x, sliderRect.y + sliderRect.h - fillH);
                        fill.size = glm::vec2(sliderRect.w, fillH);
                        fill.colorTint = sliderComp.fillColor;
                        fill.scissorRect = scissor;
                        drawList.push_back(std::move(fill));
                    }

                    float handleH = sliderRect.h * sliderComp.handleSizeRatio;
                    float handleY = sliderRect.y + (1.0f - normalizedValue) * (sliderRect.h - handleH);
                    render::ui::UIImageRenderData handle;
                    handle.texturePath = handleTex;
                    handle.position = glm::vec2(sliderRect.x, handleY);
                    handle.size = glm::vec2(sliderRect.w, handleH);
                    handle.colorTint = sliderComp.currentHandleDisplayColor;
                    handle.scissorRect = scissor;
                    drawList.push_back(std::move(handle));
                }
            }
        }

        // ---------------------------------------------------------------
        // Generate progress bar track + fill draw data.
        // ---------------------------------------------------------------
        void generateProgressBarDrawData(
            entt::registry& registry, const FrameContext& ctx,
            const std::unordered_map<uint32_t, ScrollContainerInfo>& scrollContainers,
            std::vector<render::ui::UIImageRenderData>& drawList)
        {
            const std::string whiteTex = "__white_1x1__";
            auto progressBarView = registry.view<components::UIProgressBarComponent, components::UIRectComponent>();

            for (auto pbEntity : progressBarView)
            {
                if (registry.all_of<components::NameComponent>(pbEntity))
                    if (!registry.get<components::NameComponent>(pbEntity).isActive)
                        continue;

                auto& pbComp = registry.get<components::UIProgressBarComponent>(pbEntity);
                const auto& rectComp = registry.get<components::UIRectComponent>(pbEntity);

                const auto* canvas = findCanvasForEntity(registry, pbEntity);
                if (!canvas && registry.all_of<components::UICanvasComponent>(pbEntity))
                    canvas = &registry.get<components::UICanvasComponent>(pbEntity);
                if (!canvas) continue;

                float pbVw = static_cast<float>(ctx.viewportWidth);
                float pbVh = static_cast<float>(ctx.viewportHeight);
                float scale = computeCanvasScale(canvas, pbVw, pbVh);

                PixelRect pbRect = resolvePixelRect(rectComp, pbVw, pbVh, scale);

                auto [scrollAnc, scissor] = findScrollInfo(registry, pbEntity, scrollContainers);
                applyScrollOffset(pbRect, scrollAnc, scrollContainers);

                // Smooth interpolation
                if (pbComp.smoothInterpolation)
                {
                    float diff = pbComp.value - pbComp.displayValue;
                    if (std::abs(diff) > 0.0001f)
                        pbComp.displayValue += diff * std::min(1.0f, pbComp.interpolationSpeed * ctx.deltaTime);
                    else
                        pbComp.displayValue = pbComp.value;
                }
                else
                {
                    pbComp.displayValue = pbComp.value;
                }

                float normalizedValue = (pbComp.maxValue > pbComp.minValue)
                    ? (pbComp.displayValue - pbComp.minValue) / (pbComp.maxValue - pbComp.minValue)
                    : 0.0f;
                normalizedValue = std::max(0.0f, std::min(1.0f, normalizedValue));

                // Track background
                std::string trackTex = pbComp.trackTexture.empty() ? whiteTex : pbComp.trackTexture;
                {
                    render::ui::UIImageRenderData track;
                    track.texturePath = trackTex;
                    track.position = glm::vec2(pbRect.x, pbRect.y);
                    track.size = glm::vec2(pbRect.w, pbRect.h);
                    track.colorTint = pbComp.trackColor;
                    track.scissorRect = scissor;
                    drawList.push_back(std::move(track));
                }

                // Fill rect
                std::string fillTex = pbComp.fillTexture.empty() ? whiteTex : pbComp.fillTexture;
                if (normalizedValue > 0.0f)
                {
                    if (pbComp.orientation == components::UISliderOrientation::Horizontal)
                    {
                        float fillW = pbRect.w * normalizedValue;
                        float fillX = pbComp.invertDirection
                            ? (pbRect.x + pbRect.w - fillW)
                            : pbRect.x;

                        render::ui::UIImageRenderData fill;
                        fill.texturePath = fillTex;
                        fill.position = glm::vec2(fillX, pbRect.y);
                        fill.size = glm::vec2(fillW, pbRect.h);
                        fill.colorTint = pbComp.fillColor;
                        fill.scissorRect = scissor;
                        drawList.push_back(std::move(fill));
                    }
                    else // Vertical
                    {
                        float fillH = pbRect.h * normalizedValue;
                        float fillY = pbComp.invertDirection
                            ? pbRect.y
                            : (pbRect.y + pbRect.h - fillH);

                        render::ui::UIImageRenderData fill;
                        fill.texturePath = fillTex;
                        fill.position = glm::vec2(pbRect.x, fillY);
                        fill.size = glm::vec2(pbRect.w, fillH);
                        fill.colorTint = pbComp.fillColor;
                        fill.scissorRect = scissor;
                        drawList.push_back(std::move(fill));
                    }
                }
            }
        }

        // ---------------------------------------------------------------
        // Generate scrollbar tracks + thumbs draw data.
        // ---------------------------------------------------------------
        void generateScrollbarDrawData(
            entt::registry& registry, const FrameContext& ctx,
            std::vector<render::ui::UIImageRenderData>& drawList)
        {
            constexpr float SCROLLBAR_WIDTH = 8.0f;
            constexpr float SCROLLBAR_MIN_THUMB = 20.0f;
            const glm::vec4 TRACK_COLOR{0.2f, 0.2f, 0.2f, 0.3f};
            const glm::vec4 THUMB_COLOR{0.6f, 0.6f, 0.6f, 0.6f};
            const std::string whiteTex = "__white_1x1__";

            auto scrollBarView = registry.view<components::UIScrollComponent, components::UIRectComponent>();
            for (auto scrollEntity : scrollBarView)
            {
                if (registry.all_of<components::NameComponent>(scrollEntity))
                    if (!registry.get<components::NameComponent>(scrollEntity).isActive)
                        continue;

                const auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);

                const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                if (!scrollCanvas)
                    continue;

                float vw = static_cast<float>(ctx.viewportWidth);
                float vh = static_cast<float>(ctx.viewportHeight);
                float sc = computeCanvasScale(scrollCanvas, vw, vh);

                const auto& scrollRect = registry.get<components::UIRectComponent>(scrollEntity);
                PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                glm::vec4 scrollScissor = scrollComp.computedScissorRect;

                // Vertical scrollbar
                bool showVertical = false;
                if (scrollComp.verticalScrollEnabled && scrollComp.contentSize.y > scrollComp.viewportSize.y)
                {
                    if (scrollComp.verticalScrollbarVisibility != components::ScrollbarVisibility::Hidden)
                        showVertical = true;
                }
                else if (scrollComp.verticalScrollbarVisibility == components::ScrollbarVisibility::AlwaysVisible)
                {
                    showVertical = true;
                }

                if (showVertical)
                {
                    render::ui::UIImageRenderData track;
                    track.texturePath = whiteTex;
                    track.position = glm::vec2(vpRect.x + vpRect.w - SCROLLBAR_WIDTH, vpRect.y);
                    track.size = glm::vec2(SCROLLBAR_WIDTH, vpRect.h);
                    track.colorTint = TRACK_COLOR;
                    track.scissorRect = scrollScissor;
                    drawList.push_back(std::move(track));

                    float maxScrollY = std::max(0.0f, scrollComp.contentSize.y - scrollComp.viewportSize.y);
                    float ratio = scrollComp.viewportSize.y / scrollComp.contentSize.y;
                    float thumbH = std::max(SCROLLBAR_MIN_THUMB, vpRect.h * ratio);
                    float scrollRange = vpRect.h - thumbH;
                    float thumbY = (maxScrollY > 0.0f)
                        ? vpRect.y + scrollRange * (scrollComp.scrollOffset.y / maxScrollY)
                        : vpRect.y;

                    render::ui::UIImageRenderData thumb;
                    thumb.texturePath = whiteTex;
                    thumb.position = glm::vec2(vpRect.x + vpRect.w - SCROLLBAR_WIDTH, thumbY);
                    thumb.size = glm::vec2(SCROLLBAR_WIDTH, thumbH);
                    thumb.colorTint = THUMB_COLOR;
                    thumb.scissorRect = scrollScissor;
                    drawList.push_back(std::move(thumb));
                }

                // Horizontal scrollbar
                bool showHorizontal = false;
                if (scrollComp.horizontalScrollEnabled && scrollComp.contentSize.x > scrollComp.viewportSize.x)
                {
                    if (scrollComp.horizontalScrollbarVisibility != components::ScrollbarVisibility::Hidden)
                        showHorizontal = true;
                }
                else if (scrollComp.horizontalScrollbarVisibility == components::ScrollbarVisibility::AlwaysVisible)
                {
                    showHorizontal = true;
                }

                if (showHorizontal)
                {
                    render::ui::UIImageRenderData track;
                    track.texturePath = whiteTex;
                    track.position = glm::vec2(vpRect.x, vpRect.y + vpRect.h - SCROLLBAR_WIDTH);
                    track.size = glm::vec2(vpRect.w, SCROLLBAR_WIDTH);
                    track.colorTint = TRACK_COLOR;
                    track.scissorRect = scrollScissor;
                    drawList.push_back(std::move(track));

                    float maxScrollX = std::max(0.0f, scrollComp.contentSize.x - scrollComp.viewportSize.x);
                    float ratio = scrollComp.viewportSize.x / scrollComp.contentSize.x;
                    float thumbW = std::max(SCROLLBAR_MIN_THUMB, vpRect.w * ratio);
                    float scrollRange = vpRect.w - thumbW;
                    float thumbX = (maxScrollX > 0.0f)
                        ? vpRect.x + scrollRange * (scrollComp.scrollOffset.x / maxScrollX)
                        : vpRect.x;

                    render::ui::UIImageRenderData thumb;
                    thumb.texturePath = whiteTex;
                    thumb.position = glm::vec2(thumbX, vpRect.y + vpRect.h - SCROLLBAR_WIDTH);
                    thumb.size = glm::vec2(thumbW, SCROLLBAR_WIDTH);
                    thumb.colorTint = THUMB_COLOR;
                    thumb.scissorRect = scrollScissor;
                    drawList.push_back(std::move(thumb));
                }
            }
        }

        // ---------------------------------------------------------------
        // Generate text input caret and selection highlight quads.
        // ---------------------------------------------------------------
        void generateTextInputCaretDrawData(
            entt::registry& registry, const FrameContext& ctx,
            entt::entity focusedEntity,
            std::vector<render::ui::UIImageRenderData>& drawList)
        {
            if (focusedEntity == entt::null || !registry.valid(focusedEntity))
                return;
            if (!registry.all_of<components::UITextInputComponent, components::UIRectComponent>(focusedEntity))
                return;

            const auto& tiComp = registry.get<components::UITextInputComponent>(focusedEntity);
            if (tiComp.currentState != components::UITextInputState::Focused || tiComp.fontPath.empty())
                return;

            const auto& rectComp = registry.get<components::UIRectComponent>(focusedEntity);

            const components::UICanvasComponent* canvas = findCanvasForEntity(registry, focusedEntity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(focusedEntity))
                canvas = &registry.get<components::UICanvasComponent>(focusedEntity);
            if (!canvas)
                return;

            float vw = static_cast<float>(ctx.viewportWidth);
            float vh = static_cast<float>(ctx.viewportHeight);
            float scale = computeCanvasScale(canvas, vw, vh);

            PixelRect tiRect = resolvePixelRect(rectComp, vw, vh, scale);

            // Find scroll ancestor
            entt::entity scrollAncestor = entt::null;
            entt::entity current = focusedEntity;
            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parentEntity = registry.get<components::ParentComponent>(current).parent;
                if (parentEntity == entt::null || !registry.valid(parentEntity)) break;
                if (scrollAncestor == entt::null && registry.all_of<components::UIScrollComponent>(parentEntity))
                    scrollAncestor = parentEntity;
                if (registry.all_of<components::UICanvasComponent>(parentEntity)) break;
                current = parentEntity;
            }

            glm::vec4 scissor = glm::vec4(tiRect.x, tiRect.y, tiRect.w, tiRect.h);
            if (scrollAncestor != entt::null)
            {
                if (registry.all_of<components::UIScrollComponent, components::UIRectComponent>(scrollAncestor))
                {
                    const auto& scrollComp2 = registry.get<components::UIScrollComponent>(scrollAncestor);
                    tiRect.x -= scrollComp2.scrollOffset.x;
                    tiRect.y -= scrollComp2.scrollOffset.y;

                    const auto* scrollCanvas2 = findCanvasForEntity(registry, scrollAncestor);
                    if (scrollCanvas2)
                    {
                        float sc2 = computeCanvasScale(scrollCanvas2, vw, vh);
                        const auto& scrollRect2 = registry.get<components::UIRectComponent>(scrollAncestor);
                        PixelRect sRect = resolvePixelRect(scrollRect2, vw, vh, sc2);

                        float sx = std::max(sRect.x, tiRect.x);
                        float sy = std::max(sRect.y, tiRect.y);
                        float sw = std::min(sRect.x + sRect.w, tiRect.x + tiRect.w) - sx;
                        float sh = std::min(sRect.y + sRect.h, tiRect.y + tiRect.h) - sy;
                        scissor = glm::vec4(sx, sy, std::max(0.0f, sw), std::max(0.0f, sh));
                    }
                }
            }

            float padding = 4.0f * scale;
            float scaledFontSize = tiComp.fontSize * scale;

            float avgCharWidth = scaledFontSize * 0.55f;
            float caretX = static_cast<float>(tiComp.cursorPosition) * avgCharWidth;
            float selStartX = 0.0f;
            float selEndX = 0.0f;
            bool hasSelectionRange = tiComp.selectionStart >= 0 && tiComp.selectionEnd >= 0
                && tiComp.selectionStart != tiComp.selectionEnd;

            if (hasSelectionRange)
            {
                int selMin = std::min(tiComp.selectionStart, tiComp.selectionEnd);
                int selMax = std::max(tiComp.selectionStart, tiComp.selectionEnd);
                selStartX = static_cast<float>(selMin) * avgCharWidth;
                selEndX = static_cast<float>(selMax) * avgCharWidth;
            }

            float textStartX = tiRect.x + padding - tiComp.scrollOffsetX;

            // Selection highlight
            if (hasSelectionRange)
            {
                render::ui::UIImageRenderData selection;
                selection.texturePath = "__white_1x1__";
                selection.position = glm::vec2(textStartX + selStartX, tiRect.y + 2.0f * scale);
                selection.size = glm::vec2(selEndX - selStartX, tiRect.h - 4.0f * scale);
                selection.colorTint = tiComp.selectionColor;
                selection.scissorRect = scissor;
                drawList.push_back(std::move(selection));
            }

            // Caret (blinking)
            if (tiComp.caretVisible)
            {
                render::ui::UIImageRenderData caret;
                caret.texturePath = "__white_1x1__";
                caret.position = glm::vec2(textStartX + caretX, tiRect.y + 2.0f * scale);
                caret.size = glm::vec2(tiComp.caretWidth * scale, tiRect.h - 4.0f * scale);
                caret.colorTint = tiComp.caretColor;
                caret.scissorRect = scissor;
                drawList.push_back(std::move(caret));
            }
        }

        // ---------------------------------------------------------------
        // Generate dropdown option list background quads.
        // ---------------------------------------------------------------
        void generateDropdownDrawData(
            entt::registry& registry, const FrameContext& ctx,
            std::vector<render::ui::UIImageRenderData>& drawList)
        {
            auto dropdownView = registry.view<components::UIDropdownComponent, components::UIRectComponent>();
            for (auto dropdownEntity : dropdownView)
            {
                auto& comp = registry.get<components::UIDropdownComponent>(dropdownEntity);
                if (!comp.isOpen || comp.options.empty())
                    continue;

                if (registry.all_of<components::NameComponent>(dropdownEntity))
                    if (!registry.get<components::NameComponent>(dropdownEntity).isActive)
                        continue;

                const auto* canvas = findCanvasForEntity(registry, dropdownEntity);
                if (!canvas && registry.all_of<components::UICanvasComponent>(dropdownEntity))
                    canvas = &registry.get<components::UICanvasComponent>(dropdownEntity);
                if (!canvas) continue;

                float viewW = static_cast<float>(ctx.viewportWidth);
                float viewH = static_cast<float>(ctx.viewportHeight);
                float scale = computeCanvasScale(canvas, viewW, viewH);

                const auto& rectComp = registry.get<components::UIRectComponent>(dropdownEntity);
                PixelRect headerRect = resolvePixelRect(rectComp, viewW, viewH, scale);

                int visibleCount = std::min(static_cast<int>(comp.options.size()),
                                            comp.maxVisibleItems);
                float itemHeight = headerRect.h;
                float listHeight = itemHeight * visibleCount;

                float listX = headerRect.x;
                float listY = headerRect.y + headerRect.h;
                glm::vec4 listScissor(listX, listY, headerRect.w, listHeight);

                // List background
                render::ui::UIImageRenderData listBg;
                listBg.texturePath = "__white_1x1__";
                listBg.position = glm::vec2(listX, listY);
                listBg.size = glm::vec2(headerRect.w, listHeight);
                listBg.colorTint = comp.listBackgroundColor;
                listBg.scissorRect = glm::vec4(0.0f);
                drawList.push_back(std::move(listBg));

                // Individual option items
                for (int i = 0; i < static_cast<int>(comp.options.size()); ++i)
                {
                    float optionY = listY + i * itemHeight - comp.listScrollOffset;

                    if (optionY + itemHeight < listY || optionY > listY + listHeight)
                        continue;

                    glm::vec4 itemColor = (i == comp.hoveredOptionIndex)
                        ? comp.itemHoveredColor
                        : (i == comp.selectedIndex ? glm::vec4(comp.itemHoveredColor.r * 0.7f,
                                                               comp.itemHoveredColor.g * 0.7f,
                                                               comp.itemHoveredColor.b * 0.7f,
                                                               comp.itemHoveredColor.a * 0.5f)
                                                   : comp.itemNormalColor);

                    render::ui::UIImageRenderData itemBg;
                    itemBg.texturePath = "__white_1x1__";
                    itemBg.position = glm::vec2(listX, optionY);
                    itemBg.size = glm::vec2(headerRect.w, itemHeight);
                    itemBg.colorTint = itemColor;
                    itemBg.scissorRect = listScissor;
                    drawList.push_back(std::move(itemBg));
                }
            }
        }

    } // anonymous namespace

    // ===================================================================
    // Orchestrator: UIFrameBuilder::prepareUIImagesScreenSpace
    // ===================================================================
    void UIFrameBuilder::prepareUIImagesScreenSpace(const FrameContext& ctx, UIInteractionSystem& interactionSystem)
    {
        auto* renderHandler = ctx.renderHandler;

        renderHandler->initUIRenderPipeline();

        if (!renderHandler->isUIRenderPipelineInitialized())
        {
            renderHandler->setUIImageDrawList({});
            return;
        }

        if (ctx.viewportWidth == 0 || ctx.viewportHeight == 0)
        {
            renderHandler->setUIImageDrawList({});
            return;
        }

        // Process all widget interactions
        interactionSystem.processButtonInteraction(ctx);
        interactionSystem.processCheckboxInteraction(ctx);
        interactionSystem.processTextInputInteraction(ctx);
        interactionSystem.processDropdownInteraction(ctx);
        interactionSystem.processTabsInteraction(ctx);
        interactionSystem.processSliderInteraction(ctx);

        std::vector<render::ui::UIImageRenderData> drawList;
        auto& registry = scene::EntityRegistry::getRegistry();

        // Scrollbar drag interaction
        bool anyScrollDragging = processActiveScrollDrags(registry, ctx);
        if (!anyScrollDragging)
            initiateScrollThumbDrag(registry, ctx);

        // Mouse wheel scroll
        processScrollWheelInput(registry, ctx, anyScrollDragging);

        // Layout groups
        processLayoutGroups(registry, ctx);

        // Build scroll container data (writes back to components)
        auto scrollContainers = buildScrollContainerData(registry, ctx);

        // Two-pass UIImage rendering
        emitUIImagePasses(registry, ctx, scrollContainers, drawList);

        // Widget overlays
        generateSliderDrawData(registry, ctx, scrollContainers, drawList);
        generateProgressBarDrawData(registry, ctx, scrollContainers, drawList);
        generateScrollbarDrawData(registry, ctx, drawList);
        generateTextInputCaretDrawData(registry, ctx, interactionSystem.getFocusedTextInput(), drawList);
        generateDropdownDrawData(registry, ctx, drawList);

        renderHandler->setUIImageDrawList(std::move(drawList));
    }

} // namespace controllers::offscreen
