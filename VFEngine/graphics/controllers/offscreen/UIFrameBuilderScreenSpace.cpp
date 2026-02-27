#include "UIFrameBuilder.hpp"
#include "UICommon.hpp"
#include "UIInteractionSystem.hpp"
#include "UIScreenSpaceScroll.hpp"
#include "FramePreparationSystem.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/ui/UIRenderTypes.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>

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

            std::string effectiveTexturePath = imageComp.texturePath;
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

            render::ui::UIImageRenderData renderData;
            renderData.texturePath = effectiveTexturePath.empty() ? "__white_1x1__" : effectiveTexturePath;
            renderData.position = glm::vec2(rect.x, rect.y);
            renderData.size = glm::vec2(rect.w, rect.h);
            renderData.colorTint = imageComp.colorTint;
            renderData.scissorRect = scissor;
            drawList.push_back(std::move(renderData));
        }

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

    } // anonymous namespace

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

        interactionSystem.processButtonInteraction(ctx);
        interactionSystem.processCheckboxInteraction(ctx);
        interactionSystem.processTextInputInteraction(ctx);
        interactionSystem.processDropdownInteraction(ctx);
        interactionSystem.processTabsInteraction(ctx);
        interactionSystem.processSliderInteraction(ctx);

        std::vector<render::ui::UIImageRenderData> drawList;
        auto& registry = scene::EntityRegistry::getRegistry();

        bool anyScrollDragging = ui_screenspace::processActiveScrollDrags(registry, ctx);
        if (!anyScrollDragging)
            ui_screenspace::initiateScrollThumbDrag(registry, ctx);

        ui_screenspace::processScrollWheelInput(registry, ctx, anyScrollDragging);

        processLayoutGroups(registry, ctx);

        auto scrollContainers = ui_screenspace::buildScrollContainerData(registry, ctx);

        emitUIImagePasses(registry, ctx, scrollContainers, drawList);

        ui_screenspace::generateSliderDrawData(registry, ctx, scrollContainers, drawList);
        ui_screenspace::generateProgressBarDrawData(registry, ctx, scrollContainers, drawList);
        ui_screenspace::generateScrollbarDrawData(registry, ctx, drawList);
        ui_screenspace::generateTextInputCaretDrawData(registry, ctx, interactionSystem.getFocusedTextInput(), drawList);
        ui_screenspace::generateDropdownDrawData(registry, ctx, drawList);

        renderHandler->setUIImageDrawList(std::move(drawList));
    }

} // namespace controllers::offscreen
