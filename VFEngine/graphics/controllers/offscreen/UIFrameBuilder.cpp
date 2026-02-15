#include "UIFrameBuilder.hpp"
#include "UIInteractionSystem.hpp"
#include "FramePreparationSystem.hpp"  // for FrameContext
#include "../../render/RenderPassHandler.hpp"
#include "../../render/mesh/StaticMeshPipeline.hpp"
#include "../../render/mesh/MeshTypes.hpp"
#include "../../render/DebugRenderer.hpp"
#include "../../render/tools/UICanvasImageRenderer.hpp"
#include "../../render/ui/UIRenderTypes.hpp"
#include "../../render/ui/UITextRenderTypes.hpp"
#include "../../render/text/TextTypes.hpp"
#include "../../render/text/TextPipeline.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

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

    void UIFrameBuilder::prepareUIImages(const FrameContext& ctx, UIInteractionSystem& interactionSystem)
    {
        auto* renderHandler = ctx.renderHandler;

        if (ctx.playModeActive)
        {
            // Play mode: screen-space overlay via UIRenderPipeline
            renderHandler->setUICanvasImageDrawList({});
            prepareUIImagesScreenSpace(ctx, interactionSystem);
        }
        else
        {
            // Editor mode: world-space quads on canvas via UICanvasImageRenderer
            renderHandler->setUIImageDrawList({});
            prepareUIImagesWorldSpace(ctx);
        }

        prepareUILabels(ctx);
    }

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

        // --- Button interaction (state machine + visual override) ---
        interactionSystem.processButtonInteraction(ctx);

        // --- Checkbox interaction (toggle, radio groups, visual override) ---
        interactionSystem.processCheckboxInteraction(ctx);

        // --- Text input interaction (focus, editing, state machine) ---
        interactionSystem.processTextInputInteraction(ctx);

        // --- Dropdown interaction (open/close, option selection, state machine) ---
        interactionSystem.processDropdownInteraction(ctx);

        // --- Tabs interaction (tab switching logic only) ---
        interactionSystem.processTabsInteraction(ctx);

        // --- Slider interaction (drag, click-to-set, state machine) ---
        interactionSystem.processSliderInteraction(ctx);

        std::vector<render::ui::UIImageRenderData> drawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::UIImageComponent, components::UIRectComponent>();

        // --- Scrollbar drag interaction ---
        bool anyScrollDragging = false;
        {
            auto scrollDragView = registry.view<components::UIScrollComponent, components::UIRectComponent>();

            // Phase 1: Process active drags
            for (auto scrollEntity : scrollDragView)
            {
                auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);
                if (!scrollComp.isDragging)
                    continue;

                anyScrollDragging = true;

                if (ctx.leftMouseDown)
                {
                    const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                    if (!scrollCanvas) { scrollComp.isDragging = false; break; }

                    float vw = static_cast<float>(ctx.viewportWidth);
                    float vh = static_cast<float>(ctx.viewportHeight);
                    float sc = 1.0f;
                    if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                        sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

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
                break; // only one drag at a time
            }

            // Phase 2: Initiate new drag on mouse down over thumb
            if (!anyScrollDragging && ctx.leftMouseDown)
            {
                for (auto scrollEntity : scrollDragView)
                {
                    if (registry.all_of<components::NameComponent>(scrollEntity))
                        if (!registry.get<components::NameComponent>(scrollEntity).isActive)
                            continue;

                    const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                    if (!scrollCanvas) continue;

                    float vw = static_cast<float>(ctx.viewportWidth);
                    float vh = static_cast<float>(ctx.viewportHeight);
                    float sc = 1.0f;
                    if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                        sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

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
                            break;
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
                            break;
                        }
                    }
                }
            }
        }

        // --- Apply mouse wheel scroll input ---
        if (!anyScrollDragging && (ctx.scrollDelta.y != 0.0f || ctx.scrollDelta.x != 0.0f))
        {
            auto scrollInputView = registry.view<components::UIScrollComponent, components::UIRectComponent>();

            // Find the innermost scroll container under the mouse cursor
            entt::entity targetScroll = entt::null;
            float smallestArea = std::numeric_limits<float>::max();

            for (auto scrollEntity : scrollInputView)
            {
                if (registry.all_of<components::NameComponent>(scrollEntity))
                {
                    if (!registry.get<components::NameComponent>(scrollEntity).isActive)
                        continue;
                }

                const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                if (!scrollCanvas)
                    continue;

                float vw = static_cast<float>(ctx.viewportWidth);
                float vh = static_cast<float>(ctx.viewportHeight);
                float sc = 1.0f;
                if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

                const auto& scrollRect = registry.get<components::UIRectComponent>(scrollEntity);
                PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                // Hit test: is mouse inside this scroll container's viewport?
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
                    // Use horizontal scroll delta, or route vertical wheel to horizontal
                    // when this container only scrolls horizontally
                    float hDelta = ctx.scrollDelta.x;
                    if (!canScrollV && ctx.scrollDelta.y != 0.0f)
                        hDelta = ctx.scrollDelta.y;
                    scrollComp.scrollOffset.x -= hDelta * sensitivity;
                }
            }
        }

        // --- Layout group pass: auto-position children ---
        {
            auto layoutView = registry.view<components::UILayoutGroupComponent,
                                            components::ChildrenComponent,
                                            components::UIRectComponent>();
            for (auto layoutEntity : layoutView)
            {
                if (registry.all_of<components::NameComponent>(layoutEntity))
                    if (!registry.get<components::NameComponent>(layoutEntity).isActive)
                        continue;

                const auto* layoutCanvas = findCanvasForEntity(registry, layoutEntity);
                if (!layoutCanvas)
                    continue;

                float vw = static_cast<float>(ctx.viewportWidth);
                float vh = static_cast<float>(ctx.viewportHeight);
                float sc = 1.0f;
                if (layoutCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    sc = std::min(vw / layoutCanvas->referenceWidth, vh / layoutCanvas->referenceHeight);

                const auto& parentRect = registry.get<components::UIRectComponent>(layoutEntity);
                PixelRect pRect = resolvePixelRect(parentRect, vw, vh, sc);

                const auto& layoutComp = registry.get<components::UILayoutGroupComponent>(layoutEntity);
                const auto& children = registry.get<components::ChildrenComponent>(layoutEntity).children;

                bool isVertical = (layoutComp.direction == components::LayoutDirection::Vertical);
                bool isGrid = (layoutComp.direction == components::LayoutDirection::Grid);

                float cursorX = layoutComp.padding.x; // left
                float cursorY = layoutComp.padding.z;  // top
                int gridCol = 0;
                int cols = std::max(1, layoutComp.constraintCount);
                float rowHeight = 0.0f;

                // For non-grid: single cursor along main axis
                float cursor = isVertical ? layoutComp.padding.z : layoutComp.padding.x;

                for (auto child : children)
                {
                    if (!registry.valid(child) || !registry.all_of<components::UIRectComponent>(child))
                        continue;
                    if (registry.all_of<components::NameComponent>(child)
                        && !registry.get<components::NameComponent>(child).isActive)
                        continue;

                    auto& childRect = registry.get<components::UIRectComponent>(child);

                    // Compute child pixel size from its rect
                    float childW = (childRect.anchorMax.x - childRect.anchorMin.x) * vw + childRect.sizeDelta.x * sc;
                    float childH = ((1.0f - childRect.anchorMin.y) - (1.0f - childRect.anchorMax.y)) * vh + childRect.sizeDelta.y * sc;

                    // Compute target pixel position
                    float targetX, targetY;

                    if (isGrid)
                    {
                        float availW = pRect.w - layoutComp.padding.x - layoutComp.padding.y;
                        float cellW = (availW - layoutComp.spacing * (cols - 1)) / cols;

                        targetX = pRect.x + layoutComp.padding.x + gridCol * (cellW + layoutComp.spacing);
                        // Center child within cell
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
                        default: // Start
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
                        default: // Start
                            targetY = pRect.y + layoutComp.padding.z;
                            break;
                        }
                        cursor += childW + layoutComp.spacing;
                    }

                    // Back-calculate anchoredPosition from target pixel position
                    float anchorCenterX = (childRect.anchorMin.x + childRect.anchorMax.x) * 0.5f * vw;
                    float anchorCenterY = ((1.0f - childRect.anchorMax.y) + (1.0f - childRect.anchorMin.y)) * 0.5f * vh;

                    childRect.anchoredPosition.x = (targetX + childRect.pivot.x * childW - anchorCenterX) / sc;
                    childRect.anchoredPosition.y = -((targetY + childRect.pivot.y * childH) - anchorCenterY) / sc;
                }
            }
        }

        // --- Pre-compute scroll container info ---
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
                if (registry.all_of<components::NameComponent>(scrollEntity))
                {
                    if (!registry.get<components::NameComponent>(scrollEntity).isActive)
                        continue;
                }

                const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                if (!scrollCanvas)
                    continue;

                float vw = static_cast<float>(ctx.viewportWidth);
                float vh = static_cast<float>(ctx.viewportHeight);
                float sc = 1.0f;
                if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

                const auto& scrollRect = scrollView.get<components::UIRectComponent>(scrollEntity);
                PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                // Compute content bounding box from direct children
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
                            minX = relX;
                            minY = relY;
                            maxX = relX + cr.w;
                            maxY = relY + cr.h;
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

                auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);
                float maxScrollX = std::max(0.0f, contentW - vpRect.w);
                float maxScrollY = std::max(0.0f, contentH - vpRect.h);
                scrollComp.scrollOffset.x = scrollComp.horizontalScrollEnabled
                    ? glm::clamp(scrollComp.scrollOffset.x, 0.0f, maxScrollX) : 0.0f;
                scrollComp.scrollOffset.y = scrollComp.verticalScrollEnabled
                    ? glm::clamp(scrollComp.scrollOffset.y, 0.0f, maxScrollY) : 0.0f;

                scrollComp.contentSize = glm::vec2(contentW, contentH);
                scrollComp.viewportSize = glm::vec2(vpRect.w, vpRect.h);

                // Scissor rect clamped to screen bounds
                float sx = std::max(0.0f, vpRect.x);
                float sy = std::max(0.0f, vpRect.y);
                float sw = std::max(0.0f, std::min(vpRect.x + vpRect.w, vw) - sx);
                float sh = std::max(0.0f, std::min(vpRect.y + vpRect.h, vh) - sy);
                glm::vec4 scissor(sx, sy, sw, sh);
                scrollComp.computedScissorRect = scissor;

                scrollContainers[static_cast<uint32_t>(scrollEntity)] = {scrollComp.scrollOffset, scissor};
            }
        }

        // Helper lambda to emit one UIImage entity into drawList
        auto emitUIImage = [&](entt::entity entity, entt::entity scrollAncestor,
                               const components::UICanvasComponent* canvas)
        {
            const auto& imageComp = view.get<components::UIImageComponent>(entity);
            if (imageComp.texturePath.empty() && imageComp.colorTint.a < 0.01f)
                return;

            const auto& rectComp = view.get<components::UIRectComponent>(entity);

            float viewportW = static_cast<float>(ctx.viewportWidth);
            float viewportH = static_cast<float>(ctx.viewportHeight);
            float scale = 1.0f;
            if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                scale = std::min(viewportW / canvas->referenceWidth, viewportH / canvas->referenceHeight);

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
        };

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

            emitUIImage(entity, entt::null, canvas);
        }

        // Pass 2: All other UIImage entities (children of scroll containers, non-scroll elements)
        for (auto entity : view)
        {
            // Skip scroll containers (already emitted in pass 1)
            if (registry.all_of<components::UIScrollComponent>(entity))
                continue;

            if (registry.all_of<components::NameComponent>(entity))
                if (!registry.get<components::NameComponent>(entity).isActive)
                    continue;

            const auto& rectComp = view.get<components::UIRectComponent>(entity);

            // Walk parent hierarchy to find UICanvasComponent and nearest UIScrollComponent
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

            emitUIImage(entity, scrollAncestor, canvas);
        }

        // --- Generate slider fill + handle draw data (rendered on top of slider track) ---
        {
            const std::string whiteTex = "__white_1x1__";
            auto sliderDrawView = registry.view<components::UISliderComponent, components::UIRectComponent, components::UIImageComponent>();
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
                float scale = 1.0f;
                if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    scale = std::min(sliderVw / canvas->referenceWidth, sliderVh / canvas->referenceHeight);

                PixelRect sliderRect = resolvePixelRect(rectComp, sliderVw, sliderVh, scale);

                // Apply scroll offset if inside a scroll container
                entt::entity scrollAnc = entt::null;
                {
                    entt::entity cur = sliderEntity;
                    while (registry.all_of<components::ParentComponent>(cur))
                    {
                        entt::entity p = registry.get<components::ParentComponent>(cur).parent;
                        if (p == entt::null || !registry.valid(p)) break;
                        if (scrollAnc == entt::null && registry.all_of<components::UIScrollComponent>(p))
                            scrollAnc = p;
                        if (registry.all_of<components::UICanvasComponent>(p)) break;
                        cur = p;
                    }
                }
                glm::vec4 scissor{0.0f};
                if (scrollAnc != entt::null)
                {
                    auto scIt = scrollContainers.find(static_cast<uint32_t>(scrollAnc));
                    if (scIt != scrollContainers.end())
                    {
                        sliderRect.x -= scIt->second.scrollOffset.x;
                        sliderRect.y -= scIt->second.scrollOffset.y;
                        scissor = scIt->second.scissorRect;
                    }
                }

                float normalizedValue = (sliderComp.maxValue > sliderComp.minValue)
                    ? (sliderComp.value - sliderComp.minValue) / (sliderComp.maxValue - sliderComp.minValue)
                    : 0.0f;
                normalizedValue = std::max(0.0f, std::min(1.0f, normalizedValue));

                // Determine fill and handle texture
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
                    // Fill: left edge to value position
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

                    // Handle
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
                    // Fill: bottom edge up to value position (bottom = high y in screen coords)
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

                    // Handle
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

        // --- Generate progress bar track + fill draw data ---
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
                float scale = 1.0f;
                if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    scale = std::min(pbVw / canvas->referenceWidth, pbVh / canvas->referenceHeight);

                PixelRect pbRect = resolvePixelRect(rectComp, pbVw, pbVh, scale);

                // Apply scroll offset if inside a scroll container
                entt::entity scrollAnc = entt::null;
                {
                    entt::entity cur = pbEntity;
                    while (registry.all_of<components::ParentComponent>(cur))
                    {
                        entt::entity p = registry.get<components::ParentComponent>(cur).parent;
                        if (p == entt::null || !registry.valid(p)) break;
                        if (scrollAnc == entt::null && registry.all_of<components::UIScrollComponent>(p))
                            scrollAnc = p;
                        if (registry.all_of<components::UICanvasComponent>(p)) break;
                        cur = p;
                    }
                }
                glm::vec4 scissor{0.0f};
                if (scrollAnc != entt::null)
                {
                    auto scIt = scrollContainers.find(static_cast<uint32_t>(scrollAnc));
                    if (scIt != scrollContainers.end())
                    {
                        pbRect.x -= scIt->second.scrollOffset.x;
                        pbRect.y -= scIt->second.scrollOffset.y;
                        scissor = scIt->second.scissorRect;
                    }
                }

                // Smooth interpolation: lerp displayValue toward value
                if (pbComp.smoothInterpolation)
                {
                    float diff = pbComp.value - pbComp.displayValue;
                    if (std::abs(diff) > 0.0001f)
                    {
                        pbComp.displayValue += diff * std::min(1.0f, pbComp.interpolationSpeed * ctx.deltaTime);
                    }
                    else
                    {
                        pbComp.displayValue = pbComp.value;
                    }
                }
                else
                {
                    pbComp.displayValue = pbComp.value;
                }

                float normalizedValue = (pbComp.maxValue > pbComp.minValue)
                    ? (pbComp.displayValue - pbComp.minValue) / (pbComp.maxValue - pbComp.minValue)
                    : 0.0f;
                normalizedValue = std::max(0.0f, std::min(1.0f, normalizedValue));

                // Track background rect
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

        // --- Generate scrollbar draw data (rendered on top of content) ---
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
                {
                    if (!registry.get<components::NameComponent>(scrollEntity).isActive)
                        continue;
                }

                const auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);

                const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                if (!scrollCanvas)
                    continue;

                float vw = static_cast<float>(ctx.viewportWidth);
                float vh = static_cast<float>(ctx.viewportHeight);
                float sc = 1.0f;
                if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

                const auto& scrollRect = registry.get<components::UIRectComponent>(scrollEntity);
                PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                glm::vec4 scrollScissor = scrollComp.computedScissorRect;

                // --- Vertical scrollbar ---
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

                // --- Horizontal scrollbar ---
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

        // --- Text input caret and selection quads ---
        if (interactionSystem.getFocusedTextInput() != entt::null && registry.valid(interactionSystem.getFocusedTextInput())
            && registry.all_of<components::UITextInputComponent, components::UIRectComponent>(interactionSystem.getFocusedTextInput()))
        {
            const auto& tiComp = registry.get<components::UITextInputComponent>(interactionSystem.getFocusedTextInput());

            if (tiComp.currentState == components::UITextInputState::Focused && !tiComp.fontPath.empty())
            {
                const auto& rectComp = registry.get<components::UIRectComponent>(interactionSystem.getFocusedTextInput());

                const components::UICanvasComponent* canvas = findCanvasForEntity(registry, interactionSystem.getFocusedTextInput());
                if (!canvas && registry.all_of<components::UICanvasComponent>(interactionSystem.getFocusedTextInput()))
                    canvas = &registry.get<components::UICanvasComponent>(interactionSystem.getFocusedTextInput());

                if (canvas)
                {
                    float vw = static_cast<float>(ctx.viewportWidth);
                    float vh = static_cast<float>(ctx.viewportHeight);
                    float scale = 1.0f;
                    if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                        scale = std::min(vw / canvas->referenceWidth, vh / canvas->referenceHeight);

                    PixelRect tiRect = resolvePixelRect(rectComp, vw, vh, scale);

                    // Scroll ancestor offset
                    entt::entity scrollAncestor = entt::null;
                    entt::entity current = interactionSystem.getFocusedTextInput();
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
                        auto scrollView2 = registry.view<components::UIScrollComponent, components::UIRectComponent>();
                        if (registry.all_of<components::UIScrollComponent, components::UIRectComponent>(scrollAncestor))
                        {
                            const auto& scrollComp2 = registry.get<components::UIScrollComponent>(scrollAncestor);
                            tiRect.x -= scrollComp2.scrollOffset.x;
                            tiRect.y -= scrollComp2.scrollOffset.y;

                            const auto* scrollCanvas2 = findCanvasForEntity(registry, scrollAncestor);
                            if (scrollCanvas2)
                            {
                                float sc2 = 1.0f;
                                if (scrollCanvas2->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                                    sc2 = std::min(vw / scrollCanvas2->referenceWidth, vh / scrollCanvas2->referenceHeight);
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

                    // Compute caret X position using character width estimation
                    // (Precise font-based positioning would require font data access here;
                    //  the average-width approach is sufficient for most fonts)
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

                    // Apply scroll offset
                    float textStartX = tiRect.x + padding - tiComp.scrollOffsetX;

                    // Selection highlight (behind text)
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
            }
        }

        // --- Emit dropdown option list backgrounds (rendered on top of everything) ---
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
                float scale = 1.0f;
                if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    scale = std::min(viewW / canvas->referenceWidth, viewH / canvas->referenceHeight);

                const auto& rectComp = registry.get<components::UIRectComponent>(dropdownEntity);
                PixelRect headerRect = resolvePixelRect(rectComp, viewW, viewH, scale);

                int visibleCount = std::min(static_cast<int>(comp.options.size()),
                                            comp.maxVisibleItems);
                float itemHeight = headerRect.h;
                float listHeight = itemHeight * visibleCount;

                // Scissor rect for the option list area
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

                    // Skip items outside visible area
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

        renderHandler->setUIImageDrawList(std::move(drawList));
    }

    void UIFrameBuilder::prepareUIImagesWorldSpace(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        std::vector<render::mesh::UICanvasImageRenderData> drawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::UIImageComponent, components::UIRectComponent>();

        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                {
                    continue;
                }
            }

            const auto& imageComp = view.get<components::UIImageComponent>(entity);
            if (imageComp.texturePath.empty())
            {
                continue;
            }

            const auto& rectComp = view.get<components::UIRectComponent>(entity);

            // Walk parent hierarchy to find canvas entity with WorldTransformComponent
            const components::UICanvasComponent* canvas = nullptr;
            entt::entity canvasEntity = entt::null;
            entt::entity current = entity;

            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parentEntity = registry.get<components::ParentComponent>(current).parent;
                if (parentEntity == entt::null || !registry.valid(parentEntity))
                {
                    break;
                }

                if (registry.all_of<components::UICanvasComponent>(parentEntity))
                {
                    canvas = &registry.get<components::UICanvasComponent>(parentEntity);
                    canvasEntity = parentEntity;
                    break;
                }
                current = parentEntity;
            }

            if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
            {
                canvas = &registry.get<components::UICanvasComponent>(entity);
                canvasEntity = entity;
            }

            if (!canvas || canvasEntity == entt::null)
            {
                continue;
            }

            // Need canvas world transform for positioning in 3D space
            if (!registry.all_of<components::WorldTransformComponent>(canvasEntity))
            {
                continue;
            }

            const auto& canvasWorldTransform = registry.get<components::WorldTransformComponent>(canvasEntity);

            // Canvas world dimensions
            float canvasW = canvas->referenceWidth / canvas->pixelsPerUnit;
            float canvasH = canvas->referenceHeight / canvas->pixelsPerUnit;

            // Image rect in canvas pixels (referenceWidth x referenceHeight is the parent)
            float anchorLeft = rectComp.anchorMin.x * canvas->referenceWidth;
            float anchorRight = rectComp.anchorMax.x * canvas->referenceWidth;
            float anchorBottom = rectComp.anchorMin.y * canvas->referenceHeight;
            float anchorTop = rectComp.anchorMax.y * canvas->referenceHeight;

            float w = (anchorRight - anchorLeft) + rectComp.sizeDelta.x;
            float h = (anchorTop - anchorBottom) + rectComp.sizeDelta.y;
            float cx = (anchorLeft + anchorRight) * 0.5f + rectComp.anchoredPosition.x;
            float cy = (anchorBottom + anchorTop) * 0.5f + rectComp.anchoredPosition.y;

            // Normalize to canvas space (0..1), then shift to unit quad space (-0.5..0.5)
            float localCX = cx / canvas->referenceWidth - 0.5f;
            float localCY = cy / canvas->referenceHeight - 0.5f;
            float normW = w / canvas->referenceWidth;
            float normH = h / canvas->referenceHeight;

            // Model matrix: canvas world transform * canvas size * image offset * image size
            glm::mat4 canvasScaled = canvasWorldTransform.worldMatrix
                * glm::scale(glm::mat4(1.0f), glm::vec3(canvasW, canvasH, 1.0f));

            glm::mat4 imageModel = canvasScaled
                * glm::translate(glm::mat4(1.0f), glm::vec3(localCX, localCY, 0.001f))
                * glm::scale(glm::mat4(1.0f), glm::vec3(normW, normH, 1.0f));

            render::mesh::UICanvasImageRenderData renderData;
            renderData.modelMatrix = imageModel;
            renderData.texturePath = imageComp.texturePath;
            renderData.colorTint = imageComp.colorTint;

            drawList.push_back(std::move(renderData));
        }

        if (!drawList.empty())
        {
            if (!renderHandler->isMeshPipelineInitialized())
            {
                renderHandler->initMeshPipeline();
            }
            if (!renderHandler->isDebugRendererInitialized())
            {
                renderHandler->initDebugRenderer();
            }
        }

        renderHandler->setUICanvasImageDrawList(std::move(drawList));
    }

    void UIFrameBuilder::prepareUILabels(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;
        if (ctx.playModeActive)
        {
            prepareUILabelsScreenSpace(ctx);
        }
        else
        {
            // Editor mode: world-space text on canvas via TextPipeline
            renderHandler->setUITextDrawList({});
            prepareUILabelsWorldSpace(ctx);
        }
    }

    void UIFrameBuilder::prepareUILabelsWorldSpace(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        renderHandler->initTextPipeline();

        if (!renderHandler->isTextPipelineInitialized())
        {
            return;
        }

        std::vector<render::text::TextRenderData> drawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::UILabelComponent, components::UIRectComponent>();

        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                {
                    continue;
                }
            }

            const auto& labelComp = view.get<components::UILabelComponent>(entity);
            if (labelComp.text.empty() || labelComp.fontPath.empty())
            {
                continue;
            }

            const auto& rectComp = view.get<components::UIRectComponent>(entity);

            // Walk parent hierarchy to find canvas entity with WorldTransformComponent
            const components::UICanvasComponent* canvas = nullptr;
            entt::entity canvasEntity = entt::null;
            entt::entity current = entity;

            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parentEntity = registry.get<components::ParentComponent>(current).parent;
                if (parentEntity == entt::null || !registry.valid(parentEntity))
                {
                    break;
                }

                if (registry.all_of<components::UICanvasComponent>(parentEntity))
                {
                    canvas = &registry.get<components::UICanvasComponent>(parentEntity);
                    canvasEntity = parentEntity;
                    break;
                }
                current = parentEntity;
            }

            if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
            {
                canvas = &registry.get<components::UICanvasComponent>(entity);
                canvasEntity = entity;
            }

            if (!canvas || canvasEntity == entt::null)
            {
                continue;
            }

            if (!registry.all_of<components::WorldTransformComponent>(canvasEntity))
            {
                continue;
            }

            const auto& canvasWorldTransform = registry.get<components::WorldTransformComponent>(canvasEntity);

            // Canvas world dimensions
            float canvasW = canvas->referenceWidth / canvas->pixelsPerUnit;
            float canvasH = canvas->referenceHeight / canvas->pixelsPerUnit;

            // Label rect in canvas pixels
            float anchorLeft = rectComp.anchorMin.x * canvas->referenceWidth;
            float anchorRight = rectComp.anchorMax.x * canvas->referenceWidth;
            float anchorBottom = rectComp.anchorMin.y * canvas->referenceHeight;
            float anchorTop = rectComp.anchorMax.y * canvas->referenceHeight;

            float w = (anchorRight - anchorLeft) + rectComp.sizeDelta.x;
            float h = (anchorTop - anchorBottom) + rectComp.sizeDelta.y;
            float cx = (anchorLeft + anchorRight) * 0.5f + rectComp.anchoredPosition.x;
            float cy = (anchorBottom + anchorTop) * 0.5f + rectComp.anchoredPosition.y;

            // Top-left of rect in canvas pixels (Y-up: top = cy + h/2)
            float tlX = cx - w * 0.5f;
            float tlY = cy + h * 0.5f;

            // Normalize to unit quad space (-0.5..0.5)
            float localTLX = tlX / canvas->referenceWidth - 0.5f;
            float localTLY = tlY / canvas->referenceHeight - 0.5f;

            // Compute world position of label top-left corner
            // Text shader extends RIGHT (cameraRight) and DOWN (-cameraUp) from anchor
            glm::mat4 canvasScaled = canvasWorldTransform.worldMatrix
                * glm::scale(glm::mat4(1.0f), glm::vec3(canvasW, canvasH, 1.0f));

            glm::vec4 worldPos = canvasScaled * glm::vec4(localTLX, localTLY, 0.002f, 1.0f);

            // The world-space text shader applies quadratic scaling:
            //   worldScale = fontSize / 32, applied on top of layout scale (fontSize/32)
            //   effective size ∝ (fontSize/32)^2
            // To match screen-space proportions: worldFontSize = sqrt(32 * labelFontSize / ppu)
            float worldFontSize = std::sqrt(32.0f * labelComp.fontSize / canvas->pixelsPerUnit);

            // Convert rect width from canvas pixels to layout-pixel units
            // Layout uses scale = worldFontSize/32, shader converts by worldFontSize/32
            // Always pass rect width: used for word-wrapping AND horizontal alignment
            float worldMaxWidth = (w > 0.0f)
                ? (w / canvas->pixelsPerUnit) * 32.0f / worldFontSize
                : 0.0f;

            // Convert letterSpacing from screen-pixel scale to world layout scale
            float worldLetterSpacing = (labelComp.fontSize > 0.0f)
                ? labelComp.letterSpacing * worldFontSize / labelComp.fontSize
                : 0.0f;

            // Convert rect height to layout-pixel units (same conversion as maxWidth)
            float worldRectHeight = (h / canvas->pixelsPerUnit) * 32.0f / worldFontSize;

            render::text::TextRenderData renderData;
            renderData.fontPath = labelComp.fontPath;
            renderData.text = labelComp.text;
            renderData.worldPosition = glm::vec3(worldPos);
            renderData.fontSize = worldFontSize;
            renderData.color = labelComp.color;
            renderData.renderMode = 1; // WorldSpace
            renderData.entityId = static_cast<uint32_t>(entity);
            renderData.lineSpacing = labelComp.lineSpacing;
            renderData.letterSpacing = worldLetterSpacing;
            renderData.maxWidth = worldMaxWidth;
            renderData.horizontalAlignment = static_cast<uint8_t>(labelComp.horizontalAlignment);
            renderData.verticalAlignment = static_cast<uint8_t>(labelComp.verticalAlignment);
            renderData.rectHeight = worldRectHeight;

            drawList.push_back(std::move(renderData));
        }

        if (!drawList.empty())
        {
            renderHandler->appendTextDrawList(std::move(drawList));
        }
    }

    void UIFrameBuilder::prepareUILabelsScreenSpace(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        renderHandler->initUITextPipeline();

        if (!renderHandler->isUITextPipelineInitialized())
        {
            renderHandler->setUITextDrawList({});
            return;
        }

        if (ctx.viewportWidth == 0 || ctx.viewportHeight == 0)
        {
            renderHandler->setUITextDrawList({});
            return;
        }

        std::vector<render::ui::UITextRenderData> drawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::UILabelComponent, components::UIRectComponent>();

        float viewportW = static_cast<float>(ctx.viewportWidth);
        float viewportH = static_cast<float>(ctx.viewportHeight);

        // --- Pre-compute scroll container info (reads already-updated scrollOffset) ---
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
                if (registry.all_of<components::NameComponent>(scrollEntity))
                {
                    if (!registry.get<components::NameComponent>(scrollEntity).isActive)
                        continue;
                }

                const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                if (!scrollCanvas)
                    continue;

                float vw = viewportW;
                float vh = viewportH;
                float sc = 1.0f;
                if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

                const auto& scrollRect = scrollView.get<components::UIRectComponent>(scrollEntity);
                PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                const auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);

                float sx = std::max(0.0f, vpRect.x);
                float sy = std::max(0.0f, vpRect.y);
                float sw = std::max(0.0f, std::min(vpRect.x + vpRect.w, vw) - sx);
                float sh = std::max(0.0f, std::min(vpRect.y + vpRect.h, vh) - sy);
                glm::vec4 scissor(sx, sy, sw, sh);

                scrollContainers[static_cast<uint32_t>(scrollEntity)] = {scrollComp.scrollOffset, scissor};
            }
        }

        // --- Emit labels ---
        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
                if (!registry.get<components::NameComponent>(entity).isActive)
                    continue;

            const auto& labelComp = view.get<components::UILabelComponent>(entity);
            if (labelComp.text.empty() || labelComp.fontPath.empty())
                continue;

            const auto& rectComp = view.get<components::UIRectComponent>(entity);

            // Walk parent hierarchy for canvas + scroll ancestor
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

            // Resolve pixel rect
            float scale = 1.0f;
            if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                scale = std::min(viewportW / canvas->referenceWidth, viewportH / canvas->referenceHeight);

            PixelRect rect = resolvePixelRect(rectComp, viewportW, viewportH, scale);

            // Apply scroll offset + scissor
            glm::vec4 scissor{0.0f};
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

            // Build UITextRenderData
            render::ui::UITextRenderData renderData;
            renderData.fontPath = labelComp.fontPath;
            renderData.text = labelComp.text;
            renderData.fontSize = labelComp.fontSize * scale;
            renderData.color = labelComp.color;
            renderData.lineSpacing = labelComp.lineSpacing;
            renderData.letterSpacing = labelComp.letterSpacing;
            renderData.wordWrap = labelComp.wordWrap;
            renderData.horizontalAlignment = static_cast<uint8_t>(labelComp.horizontalAlignment);
            renderData.verticalAlignment = static_cast<uint8_t>(labelComp.verticalAlignment);
            renderData.overflow = static_cast<uint8_t>(labelComp.overflow);
            renderData.position = glm::vec2(rect.x, rect.y);
            renderData.size = glm::vec2(rect.w, rect.h);
            renderData.scissorRect = scissor;
            drawList.push_back(std::move(renderData));
        }

        // --- Emit text input text/placeholder ---
        {
            auto textInputView = registry.view<components::UITextInputComponent, components::UIRectComponent>();
            for (auto entity : textInputView)
            {
                if (registry.all_of<components::NameComponent>(entity))
                    if (!registry.get<components::NameComponent>(entity).isActive)
                        continue;

                const auto& tiComp = registry.get<components::UITextInputComponent>(entity);
                if (tiComp.fontPath.empty())
                    continue;

                // Determine display text and color
                bool showPlaceholder = tiComp.text.empty()
                    && tiComp.currentState != components::UITextInputState::Focused;
                const std::string& displayText = showPlaceholder ? tiComp.placeholderText : tiComp.text;
                const glm::vec4& textColor = showPlaceholder ? tiComp.placeholderColor : tiComp.textColor;

                if (displayText.empty())
                    continue;

                const auto& rectComp = registry.get<components::UIRectComponent>(entity);

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

                float scale = 1.0f;
                if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    scale = std::min(viewportW / canvas->referenceWidth, viewportH / canvas->referenceHeight);

                PixelRect rect = resolvePixelRect(rectComp, viewportW, viewportH, scale);

                glm::vec4 scissor{0.0f};
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

                // Use the text input rect itself as scissor to clip overflow
                if (scissor.z <= 0.0f || scissor.w <= 0.0f)
                {
                    scissor = glm::vec4(rect.x, rect.y, rect.w, rect.h);
                }
                else
                {
                    // Intersect with scroll scissor
                    float sx = std::max(scissor.x, rect.x);
                    float sy = std::max(scissor.y, rect.y);
                    float sw = std::min(scissor.x + scissor.z, rect.x + rect.w) - sx;
                    float sh = std::min(scissor.y + scissor.w, rect.y + rect.h) - sy;
                    scissor = glm::vec4(sx, sy, std::max(0.0f, sw), std::max(0.0f, sh));
                }

                // Small padding inside the rect
                float padding = 4.0f * scale;

                render::ui::UITextRenderData renderData;
                renderData.fontPath = tiComp.fontPath;
                renderData.text = displayText;
                renderData.fontSize = tiComp.fontSize * scale;
                renderData.color = textColor;
                renderData.lineSpacing = 1.0f;
                renderData.letterSpacing = 0.0f;
                renderData.wordWrap = false;
                renderData.horizontalAlignment = 0; // Left
                renderData.verticalAlignment = 1;   // Middle (vertically centered)
                renderData.overflow = 1;             // Clip
                renderData.position = glm::vec2(rect.x + padding - tiComp.scrollOffsetX, rect.y);
                renderData.size = glm::vec2(rect.w - padding * 2.0f + tiComp.scrollOffsetX, rect.h);
                renderData.scissorRect = scissor;
                drawList.push_back(std::move(renderData));
            }
        }

        // --- Emit dropdown option text labels ---
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

                float scale = 1.0f;
                if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    scale = std::min(viewportW / canvas->referenceWidth, viewportH / canvas->referenceHeight);

                const auto& rectComp = registry.get<components::UIRectComponent>(dropdownEntity);
                PixelRect headerRect = resolvePixelRect(rectComp, viewportW, viewportH, scale);

                int visibleCount = std::min(static_cast<int>(comp.options.size()),
                                            comp.maxVisibleItems);
                float itemHeight = headerRect.h;
                float listHeight = itemHeight * visibleCount;

                float listX = headerRect.x;
                float listY = headerRect.y + headerRect.h;
                glm::vec4 listScissor(listX, listY, headerRect.w, listHeight);

                // Determine font: use dropdown's font, or entity's own UILabel, or child label
                std::string fontPath = comp.fontPath;
                float fontSize = comp.fontSize;
                if (fontPath.empty())
                {
                    // Check if the dropdown entity itself has a UILabelComponent
                    if (registry.all_of<components::UILabelComponent>(dropdownEntity))
                    {
                        const auto& label = registry.get<components::UILabelComponent>(dropdownEntity);
                        fontPath = label.fontPath;
                        if (fontSize <= 0.0f)
                            fontSize = label.fontSize;
                    }
                    // Fall back to child label
                    if (fontPath.empty() && registry.all_of<components::ChildrenComponent>(dropdownEntity))
                    {
                        const auto& children = registry.get<components::ChildrenComponent>(dropdownEntity).children;
                        for (auto child : children)
                        {
                            if (registry.valid(child) && registry.all_of<components::UILabelComponent>(child))
                            {
                                const auto& label = registry.get<components::UILabelComponent>(child);
                                fontPath = label.fontPath;
                                if (fontSize <= 0.0f)
                                    fontSize = label.fontSize;
                                break;
                            }
                        }
                    }
                }
                if (fontPath.empty())
                    continue;

                float padding = 4.0f * scale;

                for (int i = 0; i < static_cast<int>(comp.options.size()); ++i)
                {
                    float optionY = listY + i * itemHeight - comp.listScrollOffset;

                    if (optionY + itemHeight < listY || optionY > listY + listHeight)
                        continue;

                    render::ui::UITextRenderData renderData;
                    renderData.fontPath = fontPath;
                    renderData.text = comp.options[i].text;
                    renderData.fontSize = fontSize * scale;
                    renderData.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    renderData.lineSpacing = 1.0f;
                    renderData.letterSpacing = 0.0f;
                    renderData.wordWrap = false;
                    renderData.horizontalAlignment = 0; // Left
                    renderData.verticalAlignment = 1;   // Middle
                    renderData.overflow = 1;             // Clip
                    renderData.position = glm::vec2(listX + padding, optionY);
                    renderData.size = glm::vec2(headerRect.w - padding * 2.0f, itemHeight);
                    renderData.scissorRect = listScissor;
                    drawList.push_back(std::move(renderData));
                }
            }
        }

        renderHandler->setUITextDrawList(std::move(drawList));
    }

} // namespace controllers::offscreen
