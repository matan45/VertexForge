#include "UIFrameBuilder.hpp"
#include "UICommon.hpp"
#include "UIInteractionSystem.hpp"
#include "UIAnimationSystem.hpp"
#include "UIScreenSpaceScroll.hpp"
#include "FramePreparationSystem.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/ui/UIRenderTypes.hpp"
#include "../../render/ui/UISliceHelper.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

using namespace controllers::offscreen::ui_common;

namespace controllers::offscreen
{
    namespace
    {
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

        void processLayoutGroups(entt::registry& registry, const FrameContext& ctx)
        {
            auto layoutView = registry.view<components::UILayoutGroupComponent,
                                            components::ChildrenComponent,
                                            components::UIRectComponent>();

            float vw = static_cast<float>(ctx.viewportWidth);
            float vh = static_cast<float>(ctx.viewportHeight);

            for (auto layoutEntity : layoutView)
            {
                if (!scene::Entity::isEffectivelyActive(registry, layoutEntity))
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
                    if (!scene::Entity::isEffectivelyActive(registry, child))
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

        void emitUIImageEntity(
            entt::registry& registry, entt::entity entity,
            entt::entity scrollAncestor,
            const components::UICanvasComponent* canvas,
            const FrameContext& ctx,
            const std::unordered_map<uint32_t, ScrollContainerInfo>& scrollContainers,
            std::vector<render::ui::UIImageRenderData>& drawList,
            render::ui::UIStencilOp stencilOp = render::ui::UIStencilOp::None,
            uint8_t stencilRef = 0,
            bool discardColor = false,
            float alphaThreshold = 0.0f)
        {
            const auto& imageComp = registry.get<components::UIImageComponent>(entity);
            if (!imageComp.textureRef.isValid() && imageComp.colorTint.a < 0.01f)
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

            std::string effectiveTexturePath = imageComp.textureRef.resolve();
            if (imageComp.renderTextureSource != entt::null
                && registry.valid(imageComp.renderTextureSource)
                && registry.all_of<components::RenderTextureComponent>(imageComp.renderTextureSource))
            {
                const auto& rtt = registry.get<components::RenderTextureComponent>(imageComp.renderTextureSource);
                if (rtt.textureId != rendertexture::INVALID_RENDER_TEXTURE_ID)
                {
                    effectiveTexturePath = "__rtt_" + std::to_string(rtt.textureId) + "__";
                }
            }

            std::string resolvedPath = effectiveTexturePath.empty() ? "__white_1x1__" : effectiveTexturePath;

            if (imageComp.imageType != components::UIImageType::Simple
                && imageComp.sourceWidth > 0 && imageComp.sourceHeight > 0
                && (imageComp.border.x > 0.0f || imageComp.border.y > 0.0f
                    || imageComp.border.z > 0.0f || imageComp.border.w > 0.0f))
            {
                size_t preSliceCount = drawList.size();
                render::ui::generateSlicedInstances(
                    glm::vec2(rect.x, rect.y), glm::vec2(rect.w, rect.h),
                    imageComp.border,
                    imageComp.sourceWidth, imageComp.sourceHeight,
                    imageComp.colorTint, scissor,
                    resolvedPath, imageComp.imageType,
                    drawList);
                // Apply stencil to sliced instances
                if (stencilOp != render::ui::UIStencilOp::None)
                {
                    for (size_t i = preSliceCount; i < drawList.size(); i++)
                    {
                        drawList[i].stencilOp = stencilOp;
                        drawList[i].stencilRef = stencilRef;
                    }
                }
            }
            else
            {
                render::ui::UIImageRenderData renderData;
                renderData.texturePath = resolvedPath;
                renderData.position = glm::vec2(rect.x, rect.y);
                renderData.size = glm::vec2(rect.w, rect.h);
                renderData.colorTint = imageComp.colorTint;
                renderData.scissorRect = scissor;
                renderData.stencilOp = stencilOp;
                renderData.stencilRef = stencilRef;
                renderData.discardColor = discardColor;
                renderData.alphaThreshold = alphaThreshold;
                drawList.push_back(std::move(renderData));
            }
        }

        // Emit a full-rect quad for mask shape (used for Rectangle mask mode or restore)
        void emitMaskRect(
            entt::registry& registry, entt::entity entity,
            entt::entity scrollAncestor,
            const components::UICanvasComponent* canvas,
            const FrameContext& ctx,
            const std::unordered_map<uint32_t, ScrollContainerInfo>& scrollContainers,
            std::vector<render::ui::UIImageRenderData>& drawList,
            render::ui::UIStencilOp stencilOp, uint8_t stencilRef,
            bool discardColor, float alphaThreshold,
            const std::string& texturePath)
        {
            const auto& rectComp = registry.get<components::UIRectComponent>(entity);

            float viewportW = static_cast<float>(ctx.viewportWidth);
            float viewportH = static_cast<float>(ctx.viewportHeight);
            float scale = computeCanvasScale(canvas, viewportW, viewportH);

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

            render::ui::UIImageRenderData renderData;
            renderData.texturePath = texturePath;
            renderData.position = glm::vec2(rect.x, rect.y);
            renderData.size = glm::vec2(rect.w, rect.h);
            renderData.colorTint = glm::vec4(1.0f);
            renderData.scissorRect = scissor;
            renderData.stencilOp = stencilOp;
            renderData.stencilRef = stencilRef;
            renderData.discardColor = discardColor;
            renderData.alphaThreshold = alphaThreshold;
            drawList.push_back(std::move(renderData));
        }

        // Recursive depth-first traversal for stencil mask support
        void traverseEntity(
            entt::registry& registry, entt::entity entity,
            entt::entity scrollAncestor,
            const components::UICanvasComponent* canvas,
            const FrameContext& ctx,
            const std::unordered_map<uint32_t, ScrollContainerInfo>& scrollContainers,
            std::vector<render::ui::UIImageRenderData>& drawList,
            uint8_t stencilDepth)
        {
            if (!registry.valid(entity))
                return;

            if (!scene::Entity::isEffectivelyActive(registry, entity))
                return;

            // Window subtrees render on the overlay layer (above the modal
            // backdrop). Everything this subtree emits gets marked below.
            bool isWindowRoot = registry.all_of<components::UIWindowComponent>(entity);
            size_t windowStartIdx = drawList.size();

            // Track scroll ancestor
            entt::entity effectiveScrollAncestor = scrollAncestor;
            if (effectiveScrollAncestor == entt::null
                && registry.all_of<components::UIScrollComponent>(entity))
                effectiveScrollAncestor = entity;

            bool hasMask = registry.all_of<components::UIMaskComponent, components::UIRectComponent>(entity);

            if (hasMask)
            {
                const auto& maskComp = registry.get<components::UIMaskComponent>(entity);
                uint8_t newRef = stencilDepth + 1;

                // Emit mask shape → StencilOp::Write
                bool showGraphic = maskComp.showMaskGraphic;
                std::string maskTex = !maskComp.maskTextureRef.isValid()
                    ? "__white_1x1__" : maskComp.maskTextureRef.resolve();
                float threshold = !maskComp.maskTextureRef.isValid()
                    ? 0.0f : maskComp.alphaThreshold;

                emitMaskRect(registry, entity, effectiveScrollAncestor, canvas, ctx,
                    scrollContainers, drawList,
                    render::ui::UIStencilOp::Write, newRef,
                    !showGraphic, threshold, maskTex);

                // Also emit the image if entity has one (when mask is visible)
                if (showGraphic && registry.all_of<components::UIImageComponent>(entity))
                {
                    // The mask write already rendered it if showGraphic is true
                    // (discardColor = false above)
                }

                // Recurse into children at increased stencil depth
                if (registry.all_of<components::ChildrenComponent>(entity))
                {
                    for (auto child : registry.get<components::ChildrenComponent>(entity).children)
                    {
                        traverseEntity(registry, child, effectiveScrollAncestor, canvas, ctx,
                            scrollContainers, drawList, newRef);
                    }
                }

                // Emit restore quad → StencilOp::Restore
                emitMaskRect(registry, entity, effectiveScrollAncestor, canvas, ctx,
                    scrollContainers, drawList,
                    render::ui::UIStencilOp::Restore, newRef,
                    true, 0.0f, "__white_1x1__");
            }
            else
            {
                // Regular entity (no mask)
                if (registry.all_of<components::UIImageComponent, components::UIRectComponent>(entity))
                {
                    render::ui::UIStencilOp op = (stencilDepth > 0)
                        ? render::ui::UIStencilOp::Test : render::ui::UIStencilOp::None;
                    emitUIImageEntity(registry, entity, effectiveScrollAncestor, canvas, ctx,
                        scrollContainers, drawList, op, stencilDepth);
                }

                // Recurse into children
                if (registry.all_of<components::ChildrenComponent>(entity))
                {
                    for (auto child : registry.get<components::ChildrenComponent>(entity).children)
                    {
                        traverseEntity(registry, child, effectiveScrollAncestor, canvas, ctx,
                            scrollContainers, drawList, stencilDepth);
                    }
                }
            }

            if (isWindowRoot)
            {
                for (size_t i = windowStartIdx; i < drawList.size(); ++i)
                {
                    drawList[i].overlay = true;
                }
            }
        }

        // Dedupes orphan warnings across frames. Best-effort: not cleared on scene
        // reload, so if a scene change recycles an entity id into a new orphan we
        // may miss it. Acceptable since this is an authoring-mistake hint, not load-bearing.
        std::unordered_set<uint32_t> g_orphanWarnedIds;

        void warnOrphanUIEntities(entt::registry& registry)
        {
            auto view = registry.view<components::UIImageComponent, components::UIRectComponent>();
            for (auto entity : view)
            {
                if (registry.all_of<components::UICanvasComponent>(entity))
                    continue;
                if (findCanvasForEntity(registry, entity) != nullptr)
                    continue;

                uint32_t id = static_cast<uint32_t>(entity);
                if (g_orphanWarnedIds.insert(id).second)
                {
                    vfLogWarning("UI entity {} has no UICanvasComponent ancestor - will not render", id);
                }
            }
        }

        void emitUIImagePasses(
            entt::registry& registry, const FrameContext& ctx,
            const std::unordered_map<uint32_t, ScrollContainerInfo>& scrollContainers,
            std::vector<render::ui::UIImageRenderData>& drawList)
        {
            // Collect active canvases, then stable-sort by UICanvasComponent.sortOrder
            // so author-controllable z is honored (ascending: lower = behind, higher = on top).
            // Stable sort preserves registry order for ties.
            auto canvasView = registry.view<components::UICanvasComponent>();
            std::vector<entt::entity> sortedCanvases;
            sortedCanvases.reserve(canvasView.size());
            for (auto e : canvasView)
            {
                if (isEntityActive(registry, e))
                    sortedCanvases.push_back(e);
            }
            std::stable_sort(sortedCanvases.begin(), sortedCanvases.end(),
                [&registry](entt::entity a, entt::entity b) {
                    return registry.get<components::UICanvasComponent>(a).sortOrder
                         < registry.get<components::UICanvasComponent>(b).sortOrder;
                });

            for (auto canvasEntity : sortedCanvases)
            {
                const auto* canvas = &registry.get<components::UICanvasComponent>(canvasEntity);
                traverseEntity(registry, canvasEntity, entt::null, canvas, ctx,
                    scrollContainers, drawList, 0);
            }

            warnOrphanUIEntities(registry);
        }

        // Window chrome: modal backdrop, window background, title bar and the
        // close button. Emitted BEFORE the canvas traversal so window children
        // (also overlay-tagged) draw over the chrome; the backdrop leads the
        // overlay layer so it dims everything beneath.
        void emitWindowChrome(
            entt::registry& registry, const FrameContext& ctx,
            std::vector<render::ui::UIImageRenderData>& drawList)
        {
            float vw = static_cast<float>(ctx.viewportWidth);
            float vh = static_cast<float>(ctx.viewportHeight);

            auto& modalState = registry.ctx().emplace<components::UIModalState>();
            entt::entity modal = modalState.activeModal();
            if (modal != entt::null && registry.valid(modal) &&
                registry.all_of<components::UIWindowComponent>(modal) &&
                scene::Entity::isEffectivelyActive(registry, modal))
            {
                const auto& window = registry.get<components::UIWindowComponent>(modal);
                render::ui::UIImageRenderData backdrop;
                backdrop.texturePath = "__white_1x1__";
                backdrop.position = glm::vec2(0.0f, 0.0f);
                backdrop.size = glm::vec2(vw, vh);
                backdrop.colorTint = window.backdropColor;
                backdrop.overlay = true;
                drawList.push_back(std::move(backdrop));
            }

            auto view = registry.view<components::UIWindowComponent, components::UIRectComponent>();
            for (auto entity : view)
            {
                if (!scene::Entity::isEffectivelyActive(registry, entity))
                    continue;

                const auto& window = view.get<components::UIWindowComponent>(entity);

                const auto* canvas = findCanvasForEntity(registry, entity);
                if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
                    canvas = &registry.get<components::UICanvasComponent>(entity);
                if (!canvas)
                    continue;

                float scale = computeCanvasScale(canvas, vw, vh);
                const auto& rectComp = view.get<components::UIRectComponent>(entity);
                PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);

                if (window.backgroundColor.a > 0.01f)
                {
                    render::ui::UIImageRenderData bg;
                    bg.texturePath = "__white_1x1__";
                    bg.position = glm::vec2(rect.x, rect.y);
                    bg.size = glm::vec2(rect.w, rect.h);
                    bg.colorTint = window.backgroundColor;
                    bg.overlay = true;
                    drawList.push_back(std::move(bg));
                }

                if (window.showTitleBar)
                {
                    float titleH = window.titleBarHeight * scale;

                    render::ui::UIImageRenderData titleBar;
                    titleBar.texturePath = "__white_1x1__";
                    titleBar.position = glm::vec2(rect.x, rect.y);
                    titleBar.size = glm::vec2(rect.w, titleH);
                    titleBar.colorTint = window.titleBarColor;
                    titleBar.overlay = true;
                    drawList.push_back(std::move(titleBar));

                    if (window.closable)
                    {
                        render::ui::UIImageRenderData closeBtn;
                        closeBtn.texturePath = "__white_1x1__";
                        closeBtn.position = glm::vec2(rect.x + rect.w - titleH, rect.y);
                        closeBtn.size = glm::vec2(titleH, titleH);
                        closeBtn.colorTint = window.closeHovered
                            ? glm::vec4(0.8f, 0.2f, 0.2f, 1.0f)
                            : glm::vec4(window.titleBarColor.r * 1.3f,
                                        window.titleBarColor.g * 1.1f,
                                        window.titleBarColor.b * 1.1f,
                                        window.titleBarColor.a);
                        closeBtn.overlay = true;
                        drawList.push_back(std::move(closeBtn));
                    }
                }
            }
        }

    } // anonymous namespace

    void UIFrameBuilder::prepareUIImagesScreenSpace(const FrameContext& ctx, UIInteractionSystem& interactionSystem,
                                                      UIAnimationSystem& animationSystem)
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

        animationSystem.processAnimations(ctx);
        // Windows run first so this frame's modal stack gates the other widgets
        interactionSystem.processWindowInteraction(ctx);
        interactionSystem.processButtonInteraction(ctx);
        interactionSystem.processCheckboxInteraction(ctx);
        interactionSystem.processTextInputInteraction(ctx);
        interactionSystem.processDropdownInteraction(ctx);
        interactionSystem.processTabsInteraction(ctx);
        interactionSystem.processSliderInteraction(ctx);
        interactionSystem.processDragDropInteraction(ctx);
        interactionSystem.processListViewInteraction(ctx);
        interactionSystem.processTooltipInteraction(ctx);
        interactionSystem.computePointerOverUI(ctx);

        std::vector<render::ui::UIImageRenderData> drawList;
        auto& registry = scene::EntityRegistry::getRegistry();

        bool anyScrollDragging = ui_screenspace::processActiveScrollDrags(registry, ctx);
        if (!anyScrollDragging)
            ui_screenspace::initiateScrollThumbDrag(registry, ctx);

        ui_screenspace::processScrollWheelInput(registry, ctx, anyScrollDragging);

        processLayoutGroups(registry, ctx);

        auto scrollContainers = ui_screenspace::buildScrollContainerData(registry, ctx);

        emitWindowChrome(registry, ctx, drawList);
        emitUIImagePasses(registry, ctx, scrollContainers, drawList);

        ui_screenspace::generateSliderDrawData(registry, ctx, scrollContainers, drawList);
        ui_screenspace::generateProgressBarDrawData(registry, ctx, scrollContainers, drawList);
        ui_screenspace::generateScrollbarDrawData(registry, ctx, drawList);
        ui_screenspace::generateTextInputCaretDrawData(registry, ctx, interactionSystem.getFocusedTextInput(), drawList);
        ui_screenspace::generateDropdownDrawData(registry, ctx, drawList);
        ui_screenspace::generateListSelectionDrawData(registry, ctx, scrollContainers, drawList);
        ui_screenspace::generateDragGhostDrawData(registry, ctx, drawList);

        // Text-mode tooltip bubble background — overlay layer (records after
        // all main UI images AND text, so it covers underlying labels too).
        {
            auto& tooltipState = registry.ctx().emplace<components::UITooltipState>();
            if (tooltipState.visible && tooltipState.hoveredEntity != entt::null &&
                registry.valid(tooltipState.hoveredEntity))
            {
                const auto* tip = registry.try_get<components::UITooltipComponent>(tooltipState.hoveredEntity);
                if (tip && tip->mode == components::UITooltipMode::Text && !tip->text.empty())
                {
                    render::ui::UIImageRenderData bg;
                    bg.texturePath = "__white_1x1__";
                    bg.position = tooltipState.displayPos;
                    bg.size = tooltipState.bgSize;
                    bg.colorTint = tip->backgroundColor;
                    bg.overlay = true;
                    drawList.push_back(std::move(bg));
                }
            }
        }

        renderHandler->setUIImageDrawList(std::move(drawList));
    }

} // namespace controllers::offscreen
