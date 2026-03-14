#include "UIScreenSpaceScroll.hpp"
#include "FramePreparationSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <algorithm>
#include <cmath>

using namespace controllers::offscreen::ui_common;

namespace controllers::offscreen::ui_screenspace
{
    void generateSliderDrawData(
        entt::registry& registry, const FrameContext& ctx,
        const ScrollContainerMap& scrollContainers,
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

    void generateProgressBarDrawData(
        entt::registry& registry, const FrameContext& ctx,
        const ScrollContainerMap& scrollContainers,
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

    void generateDragGhostDrawData(
        entt::registry& registry, const FrameContext& ctx,
        std::vector<render::ui::UIImageRenderData>& drawList)
    {
        if (components::UIDraggableComponent::activeDragEntity == entt::null)
            return;

        entt::entity dragEntity = components::UIDraggableComponent::activeDragEntity;
        if (!registry.valid(dragEntity)
            || !registry.all_of<components::UIDraggableComponent>(dragEntity))
            return;

        const auto& dragComp = registry.get<components::UIDraggableComponent>(dragEntity);
        if (!dragComp.isDragging)
            return;

        float vw = static_cast<float>(ctx.viewportWidth);
        float vh = static_cast<float>(ctx.viewportHeight);

        // Draw drop target highlights first (behind ghost)
        auto dropView = registry.view<components::UIDropTargetComponent, components::UIRectComponent>();
        for (auto targetEntity : dropView)
        {
            if (!isEntityActive(registry, targetEntity))
                continue;

            const auto& targetComp = registry.get<components::UIDropTargetComponent>(targetEntity);
            if (!targetComp.isHighlighted)
                continue;

            const auto* canvas = findCanvasForEntity(registry, targetEntity);
            if (!canvas) continue;

            float scale = computeCanvasScale(canvas, vw, vh);
            const auto& rectComp = registry.get<components::UIRectComponent>(targetEntity);
            PixelRect rect = resolvePixelRect(rectComp, vw, vh, scale);

            render::ui::UIImageRenderData highlight;
            highlight.texturePath = "__white_1x1__";
            highlight.position = glm::vec2(rect.x, rect.y);
            highlight.size = glm::vec2(rect.w, rect.h);
            highlight.colorTint = targetComp.highlightColor;
            drawList.push_back(std::move(highlight));
        }

        // Draw ghost image
        std::string ghostTexture = "__white_1x1__";
        glm::vec4 ghostTint{1.0f, 1.0f, 1.0f, dragComp.ghostOpacity};

        if (registry.all_of<components::UIImageComponent>(dragEntity))
        {
            const auto& imageComp = registry.get<components::UIImageComponent>(dragEntity);
            if (!imageComp.texturePath.empty())
                ghostTexture = imageComp.texturePath;
            ghostTint = imageComp.colorTint;
            ghostTint.a *= dragComp.ghostOpacity;
        }

        render::ui::UIImageRenderData ghost;
        ghost.texturePath = ghostTexture;
        ghost.position = dragComp.currentGhostPos;
        ghost.size = dragComp.ghostSize;
        ghost.colorTint = ghostTint;
        drawList.push_back(std::move(ghost));
    }

} // namespace controllers::offscreen::ui_screenspace
