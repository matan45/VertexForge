#include "UIFrameBuilder.hpp"
#include "UICommon.hpp"
#include "FramePreparationSystem.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/mesh/MeshTypes.hpp"
#include "../../render/tools/UICanvasImageRenderer.hpp"
#include "../../render/ui/UIRenderTypes.hpp"
#include "../../render/ui/UISliceHelper.hpp"
#include "../../render/ui/UITextRenderTypes.hpp"
#include "../../render/text/TextTypes.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "text/RichTextParser.hpp"
#include <algorithm>
#include <cmath>

using namespace controllers::offscreen::ui_common;

namespace controllers::offscreen
{
    namespace
    {
        // World-space model matrices (computeCanvasImageModelMatrix /
        // computeCanvasSubRectModelMatrix) live in utilities ui/UIRectMath.hpp,
        // re-exported via UICommon.hpp — shared with editor viewport UI picking.

        // --- World-space label: compute world position + font parameters ---
        struct WorldLabelParams
        {
            glm::vec3 worldPosition;
            float worldFontSize;
            float worldMaxWidth;
            float worldLetterSpacing;
            float worldRectHeight;
        };

        WorldLabelParams computeWorldLabelParams(
            const components::UICanvasComponent& canvas,
            const glm::mat4& worldMatrix,
            const components::UIRectComponent& rectComp,
            const components::UILabelComponent& labelComp)
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

            float tlX = cx - w * 0.5f;
            float tlY = cy + h * 0.5f;
            float localTLX = tlX / canvas.referenceWidth - 0.5f;
            float localTLY = tlY / canvas.referenceHeight - 0.5f;

            glm::mat4 canvasScaled = worldMatrix
                * glm::scale(glm::mat4(1.0f), glm::vec3(canvasW, canvasH, 1.0f));
            glm::vec4 worldPos = canvasScaled * glm::vec4(localTLX, localTLY, 0.002f, 1.0f);

            float worldFontSize = std::sqrt(32.0f * labelComp.fontSize / canvas.pixelsPerUnit);
            float worldMaxWidth = (w > 0.0f)
                ? (w / canvas.pixelsPerUnit) * 32.0f / worldFontSize : 0.0f;
            float worldLetterSpacing = (labelComp.fontSize > 0.0f)
                ? labelComp.letterSpacing * worldFontSize / labelComp.fontSize : 0.0f;
            float worldRectHeight = (h / canvas.pixelsPerUnit) * 32.0f / worldFontSize;

            return {glm::vec3(worldPos), worldFontSize, worldMaxWidth, worldLetterSpacing, worldRectHeight};
        }

        // --- Screen-space labels: emit UILabel entities ---
        // Count mask depth for a label entity by walking up parent chain
        uint8_t computeStencilDepthForEntity(entt::registry& registry, entt::entity entity)
        {
            uint8_t depth = 0;
            entt::entity current = entity;
            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parent = registry.get<components::ParentComponent>(current).parent;
                if (parent == entt::null || !registry.valid(parent))
                    break;
                if (registry.all_of<components::UIMaskComponent>(parent))
                    depth++;
                current = parent;
            }
            return depth;
        }

        void emitLabelEntities(
            entt::registry& registry,
            std::vector<render::ui::UITextRenderData>& drawList,
            float viewportW, float viewportH,
            const std::unordered_map<uint32_t, ScrollContainerInfo>& scrollContainers)
        {
            auto view = registry.view<components::UILabelComponent, components::UIRectComponent>();

            for (auto entity : view)
            {
                if (!isEntityActive(registry, entity))
                    continue;

                const auto& labelComp = view.get<components::UILabelComponent>(entity);
                if (labelComp.text.empty() || !labelComp.fontRef.isValid())
                    continue;

                const auto* canvas = findCanvasForEntity(registry, entity);
                if (!canvas) continue;

                float scale = computeCanvasScale(canvas, viewportW, viewportH);
                const auto& rectComp = view.get<components::UIRectComponent>(entity);
                PixelRect rect = resolvePixelRect(rectComp, viewportW, viewportH, scale);

                auto [scrollAncestor, scissor] = findScrollInfo(registry, entity, scrollContainers);

                // If the label opts into Clip overflow, intersect the label's own rect
                // into the scissor. Done in pre-scroll-offset space — both `scissor` and
                // `rect` are in viewport coords at this point.
                if (labelComp.overflow == components::TextOverflow::Clip)
                {
                    if (scissor.z <= 0.0f || scissor.w <= 0.0f)
                    {
                        scissor = glm::vec4(rect.x, rect.y, rect.w, rect.h);
                    }
                    else
                    {
                        float sx = std::max(scissor.x, static_cast<float>(rect.x));
                        float sy = std::max(scissor.y, static_cast<float>(rect.y));
                        float sw = std::min(scissor.x + scissor.z,
                                             static_cast<float>(rect.x + rect.w)) - sx;
                        float sh = std::min(scissor.y + scissor.w,
                                             static_cast<float>(rect.y + rect.h)) - sy;
                        scissor = glm::vec4(sx, sy, std::max(0.0f, sw), std::max(0.0f, sh));
                    }
                }

                applyScrollOffset(rect, scrollAncestor, scrollContainers);

                // Determine stencil depth for this label
                uint8_t stencilDepth = computeStencilDepthForEntity(registry, entity);

                render::ui::UITextRenderData renderData;
                renderData.fontPath = labelComp.fontRef.resolve();
                renderData.text = labelComp.text;
                renderData.fontSize = labelComp.fontSize * scale;
                renderData.color = labelComp.color;
                renderData.fontStyle = labelComp.fontStyle;
                renderData.lineSpacing = labelComp.lineSpacing;
                renderData.letterSpacing = labelComp.letterSpacing;
                renderData.wordWrap = labelComp.wordWrap;
                renderData.richText = labelComp.richText;
                renderData.horizontalAlignment = static_cast<uint8_t>(labelComp.horizontalAlignment);
                renderData.verticalAlignment = static_cast<uint8_t>(labelComp.verticalAlignment);
                renderData.overflow = labelComp.overflow;
                renderData.position = glm::vec2(rect.x, rect.y);
                renderData.size = glm::vec2(rect.w, rect.h);
                renderData.scissorRect = scissor;
                renderData.stencilOp = (stencilDepth > 0)
                    ? render::ui::UIStencilOp::Test : render::ui::UIStencilOp::None;
                renderData.stencilRef = stencilDepth;
                // Labels inside windows join the overlay layer with the window
                renderData.overlay = findOpenWindowAncestor(registry, entity) != entt::null;
                drawList.push_back(std::move(renderData));
            }
        }

        // --- Screen-space labels: emit text input text/placeholder ---
        void emitTextInputLabels(
            entt::registry& registry,
            std::vector<render::ui::UITextRenderData>& drawList,
            float viewportW, float viewportH,
            const std::unordered_map<uint32_t, ScrollContainerInfo>& scrollContainers)
        {
            auto textInputView = registry.view<components::UITextInputComponent, components::UIRectComponent>();
            for (auto entity : textInputView)
            {
                if (!isEntityActive(registry, entity))
                    continue;

                const auto& tiComp = registry.get<components::UITextInputComponent>(entity);
                if (!tiComp.fontRef.isValid())
                    continue;

                bool showPlaceholder = tiComp.text.empty()
                    && tiComp.currentState != components::UITextInputState::Focused;
                const std::string& displayText = showPlaceholder ? tiComp.placeholderText : tiComp.text;
                const glm::vec4& textColor = showPlaceholder ? tiComp.placeholderColor : tiComp.textColor;
                if (displayText.empty())
                    continue;

                const auto* canvas = findCanvasForEntity(registry, entity);
                if (!canvas) continue;

                float scale = computeCanvasScale(canvas, viewportW, viewportH);
                const auto& rectComp = registry.get<components::UIRectComponent>(entity);
                PixelRect rect = resolvePixelRect(rectComp, viewportW, viewportH, scale);

                auto [scrollAncestor, scissor] = findScrollInfo(registry, entity, scrollContainers);
                applyScrollOffset(rect, scrollAncestor, scrollContainers);

                // Use text input rect as scissor, intersecting with scroll scissor if present
                if (scissor.z <= 0.0f || scissor.w <= 0.0f)
                {
                    scissor = glm::vec4(rect.x, rect.y, rect.w, rect.h);
                }
                else
                {
                    float sx = std::max(scissor.x, rect.x);
                    float sy = std::max(scissor.y, rect.y);
                    float sw = std::min(scissor.x + scissor.z, rect.x + rect.w) - sx;
                    float sh = std::min(scissor.y + scissor.w, rect.y + rect.h) - sy;
                    scissor = glm::vec4(sx, sy, std::max(0.0f, sw), std::max(0.0f, sh));
                }

                float padding = 4.0f * scale;
                render::ui::UITextRenderData renderData;
                renderData.fontPath = tiComp.fontRef.resolve();
                renderData.text = displayText;
                renderData.fontSize = tiComp.fontSize * scale;
                renderData.color = textColor;
                renderData.lineSpacing = 1.0f;
                renderData.letterSpacing = 0.0f;
                renderData.wordWrap = false;
                renderData.horizontalAlignment = 0; // Left
                renderData.verticalAlignment = 1;   // Middle
                renderData.overflow = components::TextOverflow::Clip;
                renderData.position = glm::vec2(rect.x + padding - tiComp.scrollOffsetX, rect.y);
                renderData.size = glm::vec2(rect.w - padding * 2.0f + tiComp.scrollOffsetX, rect.h);
                renderData.scissorRect = scissor;
                renderData.overlay = findOpenWindowAncestor(registry, entity) != entt::null;
                drawList.push_back(std::move(renderData));
            }
        }

        // --- Resolve dropdown font from component, entity label, or child label ---
        std::string findDropdownFont(entt::registry& registry, entt::entity dropdownEntity,
            const components::UIDropdownComponent& comp, float& fontSize)
        {
            std::string fontPath = comp.fontRef.resolve();
            fontSize = comp.fontSize;
            if (!fontPath.empty())
                return fontPath;

            if (registry.all_of<components::UILabelComponent>(dropdownEntity))
            {
                const auto& label = registry.get<components::UILabelComponent>(dropdownEntity);
                fontPath = label.fontRef.resolve();
                if (fontSize <= 0.0f) fontSize = label.fontSize;
            }
            if (fontPath.empty() && registry.all_of<components::ChildrenComponent>(dropdownEntity))
            {
                for (auto child : registry.get<components::ChildrenComponent>(dropdownEntity).children)
                {
                    if (registry.valid(child) && registry.all_of<components::UILabelComponent>(child))
                    {
                        const auto& label = registry.get<components::UILabelComponent>(child);
                        fontPath = label.fontRef.resolve();
                        if (fontSize <= 0.0f) fontSize = label.fontSize;
                        break;
                    }
                }
            }
            return fontPath;
        }

        // --- Screen-space labels: emit dropdown option text labels ---
        void emitDropdownOptionLabels(
            entt::registry& registry,
            std::vector<render::ui::UITextRenderData>& drawList,
            float viewportW, float viewportH)
        {
            auto dropdownView = registry.view<components::UIDropdownComponent, components::UIRectComponent>();
            for (auto dropdownEntity : dropdownView)
            {
                auto& comp = registry.get<components::UIDropdownComponent>(dropdownEntity);
                if (!comp.isOpen || comp.options.empty())
                    continue;
                if (!isEntityActive(registry, dropdownEntity))
                    continue;

                const auto* canvas = findCanvasForEntity(registry, dropdownEntity);
                if (!canvas) continue;

                float scale = computeCanvasScale(canvas, viewportW, viewportH);
                const auto& rectComp = registry.get<components::UIRectComponent>(dropdownEntity);
                PixelRect headerRect = resolvePixelRect(rectComp, viewportW, viewportH, scale);

                float fontSize = 0.0f;
                std::string fontPath = findDropdownFont(registry, dropdownEntity, comp, fontSize);
                if (fontPath.empty()) continue;

                int visibleCount = std::min(static_cast<int>(comp.options.size()), comp.maxVisibleItems);
                float itemHeight = headerRect.h;
                float listHeight = itemHeight * visibleCount;
                float listX = headerRect.x;
                float listY = headerRect.y + headerRect.h;
                glm::vec4 listScissor(listX, listY, headerRect.w, listHeight);
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
                    renderData.overflow = components::TextOverflow::Clip;
                    renderData.position = glm::vec2(listX + padding, optionY);
                    renderData.size = glm::vec2(headerRect.w - padding * 2.0f, itemHeight);
                    renderData.scissorRect = listScissor;
                    renderData.overlay = findOpenWindowAncestor(registry, dropdownEntity) != entt::null;
                    drawList.push_back(std::move(renderData));
                }
            }
        }

        // --- Screen-space labels: window title bar text + close glyph ---
        void emitWindowTitleLabels(
            entt::registry& registry,
            std::vector<render::ui::UITextRenderData>& drawList,
            float viewportW, float viewportH)
        {
            auto view = registry.view<components::UIWindowComponent, components::UIRectComponent>();
            for (auto entity : view)
            {
                if (!isEntityActive(registry, entity))
                    continue;

                const auto& window = view.get<components::UIWindowComponent>(entity);
                if (!window.showTitleBar || !window.fontRef.isValid())
                    continue;

                const auto* canvas = findCanvasForEntity(registry, entity);
                if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
                    canvas = &registry.get<components::UICanvasComponent>(entity);
                if (!canvas) continue;

                float scale = computeCanvasScale(canvas, viewportW, viewportH);
                const auto& rectComp = view.get<components::UIRectComponent>(entity);
                PixelRect rect = resolvePixelRect(rectComp, viewportW, viewportH, scale);
                float titleH = window.titleBarHeight * scale;
                float padding = 8.0f * scale;

                if (!window.title.empty())
                {
                    render::ui::UITextRenderData title;
                    title.fontPath = window.fontRef.resolve();
                    title.text = window.title;
                    title.fontSize = window.titleFontSize * scale;
                    title.color = window.titleTextColor;
                    title.wordWrap = false;
                    title.horizontalAlignment = 0; // Left
                    title.verticalAlignment = 1;   // Middle
                    title.overflow = components::TextOverflow::Clip;
                    title.position = glm::vec2(rect.x + padding, rect.y);
                    title.size = glm::vec2(rect.w - titleH - padding * 2.0f, titleH);
                    title.overlay = true;
                    drawList.push_back(std::move(title));
                }

                if (window.closable)
                {
                    render::ui::UITextRenderData closeGlyph;
                    closeGlyph.fontPath = window.fontRef.resolve();
                    closeGlyph.text = "x";
                    closeGlyph.fontSize = window.titleFontSize * scale;
                    closeGlyph.color = window.titleTextColor;
                    closeGlyph.wordWrap = false;
                    closeGlyph.horizontalAlignment = 1; // Center
                    closeGlyph.verticalAlignment = 1;   // Middle
                    closeGlyph.overflow = components::TextOverflow::Overflow;
                    closeGlyph.position = glm::vec2(rect.x + rect.w - titleH, rect.y);
                    closeGlyph.size = glm::vec2(titleH, titleH);
                    closeGlyph.overlay = true;
                    drawList.push_back(std::move(closeGlyph));
                }
            }
        }

    } // anonymous namespace

    // =================================================================
    // Public dispatchers
    // =================================================================

    void UIFrameBuilder::prepareUIImages(const FrameContext& ctx, UIInteractionSystem& interactionSystem,
                                          UIAnimationSystem& animationSystem)
    {
        auto* renderHandler = ctx.renderHandler;

        if (ctx.playModeActive)
        {
            renderHandler->setUICanvasImageDrawList({});
            prepareUIImagesScreenSpace(ctx, interactionSystem, animationSystem);
        }
        else
        {
            renderHandler->setUIImageDrawList({});
            prepareUIImagesWorldSpace(ctx);
        }

        prepareUILabels(ctx);
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
            renderHandler->setUITextDrawList({});
            prepareUILabelsWorldSpace(ctx);
        }
    }

    // =================================================================
    // World-space rendering
    // =================================================================

    void UIFrameBuilder::prepareUIImagesWorldSpace(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;
        std::vector<render::mesh::UICanvasImageRenderData> drawList;
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::UIImageComponent, components::UIRectComponent>();

        for (auto entity : view)
        {
            if (!isEntityActive(registry, entity))
                continue;

            const auto& imageComp = view.get<components::UIImageComponent>(entity);
            if (!imageComp.textureRef.isValid())
                continue;

            auto canvasInfo = findCanvasWithEntity(registry, entity);
            if (!canvasInfo.canvas || canvasInfo.canvasEntity == entt::null)
                continue;
            if (!registry.all_of<components::WorldTransformComponent>(canvasInfo.canvasEntity))
                continue;

            const auto& worldTransform = registry.get<components::WorldTransformComponent>(canvasInfo.canvasEntity);
            const auto& rectComp = view.get<components::UIRectComponent>(entity);

            bool useSlice = imageComp.imageType != components::UIImageType::Simple
                && imageComp.sourceWidth > 0 && imageComp.sourceHeight > 0
                && (imageComp.border.x > 0.0f || imageComp.border.y > 0.0f
                    || imageComp.border.z > 0.0f || imageComp.border.w > 0.0f);

            if (!useSlice)
            {
                render::mesh::UICanvasImageRenderData renderData;
                renderData.modelMatrix = computeCanvasImageModelMatrix(
                    *canvasInfo.canvas, worldTransform.worldMatrix, rectComp);
                renderData.texturePath = imageComp.textureRef.resolve();
                renderData.colorTint = imageComp.colorTint;
                drawList.push_back(std::move(renderData));
            }
            else
            {
                // Compute element pixel rect in canvas space
                const auto& canvas = *canvasInfo.canvas;
                float anchorLeft = rectComp.anchorMin.x * canvas.referenceWidth;
                float anchorRight = rectComp.anchorMax.x * canvas.referenceWidth;
                float anchorBottom = rectComp.anchorMin.y * canvas.referenceHeight;
                float anchorTop = rectComp.anchorMax.y * canvas.referenceHeight;
                float w = (anchorRight - anchorLeft) + rectComp.sizeDelta.x;
                float h = (anchorTop - anchorBottom) + rectComp.sizeDelta.y;
                float cx = (anchorLeft + anchorRight) * 0.5f + rectComp.anchoredPosition.x;
                float cy = (anchorBottom + anchorTop) * 0.5f + rectComp.anchoredPosition.y;
                float elemLeft = cx - w * 0.5f;
                float elemTop = cy - h * 0.5f;

                // Generate screen-space slice data, then convert to world-space model matrices
                std::vector<render::ui::UIImageRenderData> sliceData;
                render::ui::generateSlicedInstances(
                    glm::vec2(elemLeft, elemTop), glm::vec2(w, h),
                    imageComp.border,
                    imageComp.sourceWidth, imageComp.sourceHeight,
                    imageComp.colorTint, glm::vec4(0.0f),
                    imageComp.textureRef.resolve(), imageComp.imageType,
                    sliceData);

                for (const auto& slice : sliceData)
                {
                    float patchCX = slice.position.x + slice.size.x * 0.5f;
                    float patchCY = slice.position.y + slice.size.y * 0.5f;

                    render::mesh::UICanvasImageRenderData renderData;
                    renderData.modelMatrix = computeCanvasSubRectModelMatrix(
                        canvas, worldTransform.worldMatrix,
                        patchCX, patchCY, slice.size.x, slice.size.y);
                    renderData.texturePath = imageComp.textureRef.resolve();
                    renderData.colorTint = imageComp.colorTint;
                    renderData.uvRect = slice.uvRect;
                    drawList.push_back(std::move(renderData));
                }
            }
        }

        if (!drawList.empty())
        {
            if (!renderHandler->isMeshPipelineInitialized())
                renderHandler->initMeshPipeline();
            if (!renderHandler->isDebugRendererInitialized())
                renderHandler->initDebugRenderer();
        }
        renderHandler->setUICanvasImageDrawList(std::move(drawList));
    }

    void UIFrameBuilder::prepareUILabelsWorldSpace(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;
        renderHandler->initTextPipeline();
        if (!renderHandler->isTextPipelineInitialized())
            return;

        std::vector<render::text::TextRenderData> drawList;
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::UILabelComponent, components::UIRectComponent>();

        for (auto entity : view)
        {
            if (!isEntityActive(registry, entity))
                continue;

            const auto& labelComp = view.get<components::UILabelComponent>(entity);
            if (labelComp.text.empty() || !labelComp.fontRef.isValid())
                continue;

            auto canvasInfo = findCanvasWithEntity(registry, entity);
            if (!canvasInfo.canvas || canvasInfo.canvasEntity == entt::null)
                continue;
            if (!registry.all_of<components::WorldTransformComponent>(canvasInfo.canvasEntity))
                continue;

            const auto& worldTransform = registry.get<components::WorldTransformComponent>(canvasInfo.canvasEntity);
            const auto& rectComp = view.get<components::UIRectComponent>(entity);

            auto params = computeWorldLabelParams(
                *canvasInfo.canvas, worldTransform.worldMatrix, rectComp, labelComp);

            render::text::TextRenderData renderData;
            renderData.fontPath = labelComp.fontRef.resolve();
            // World-space (edit-mode) labels render through the 3D text
            // pipeline which has no per-char styles — strip markup so tags
            // don't show literally; styled spans are screen-space only.
            renderData.text = labelComp.richText
                ? ::text::parseRichText(labelComp.text).strippedText
                : labelComp.text;
            renderData.worldPosition = params.worldPosition;
            renderData.fontSize = params.worldFontSize;
            renderData.color = labelComp.color;
            renderData.renderMode = 1; // WorldSpace
            renderData.entityId = static_cast<uint32_t>(entity);
            renderData.lineSpacing = labelComp.lineSpacing;
            renderData.letterSpacing = params.worldLetterSpacing;
            renderData.maxWidth = params.worldMaxWidth;
            renderData.horizontalAlignment = static_cast<uint8_t>(labelComp.horizontalAlignment);
            renderData.verticalAlignment = static_cast<uint8_t>(labelComp.verticalAlignment);
            renderData.rectHeight = params.worldRectHeight;
            renderData.fontStyle = labelComp.fontStyle;
            drawList.push_back(std::move(renderData));
        }

        if (!drawList.empty())
            renderHandler->appendTextDrawList(std::move(drawList));
    }

    // =================================================================
    // Screen-space label rendering
    // =================================================================

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

        auto& registry = scene::EntityRegistry::getRegistry();
        float viewportW = static_cast<float>(ctx.viewportWidth);
        float viewportH = static_cast<float>(ctx.viewportHeight);
        auto scrollContainers = buildScrollContainerMap(registry, viewportW, viewportH);

        std::vector<render::ui::UITextRenderData> drawList;
        emitLabelEntities(registry, drawList, viewportW, viewportH, scrollContainers);
        emitTextInputLabels(registry, drawList, viewportW, viewportH, scrollContainers);
        emitDropdownOptionLabels(registry, drawList, viewportW, viewportH);
        emitWindowTitleLabels(registry, drawList, viewportW, viewportH);

        // Text-mode tooltip content — overlay layer, drawn over the bubble
        // background the image pass emitted this frame.
        {
            auto& tooltipState = registry.ctx().emplace<components::UITooltipState>();
            if (tooltipState.visible && tooltipState.hoveredEntity != entt::null &&
                registry.valid(tooltipState.hoveredEntity))
            {
                const auto* tip = registry.try_get<components::UITooltipComponent>(tooltipState.hoveredEntity);
                if (tip && tip->mode == components::UITooltipMode::Text &&
                    !tip->text.empty() && tip->fontRef.isValid())
                {
                    render::ui::UITextRenderData tipText;
                    tipText.fontPath = tip->fontRef.resolve();
                    tipText.text = tip->text;
                    tipText.fontSize = tip->fontSize * tooltipState.canvasScale;
                    tipText.letterSpacing = tip->letterSpacing * tooltipState.canvasScale;
                    tipText.color = tip->textColor;
                    tipText.position = tooltipState.displayPos + tooltipState.contentOffset;
                    tipText.size = tooltipState.contentSize;
                    tipText.wordWrap = true;
                    tipText.overflow = components::TextOverflow::Overflow;
                    tipText.overlay = true;
                    drawList.push_back(std::move(tipText));
                }
            }
        }

        renderHandler->setUITextDrawList(std::move(drawList));
    }

} // namespace controllers::offscreen
