#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <entt/entt.hpp>
#include <vector>
#include "../../render/ui/UIRenderTypes.hpp"
#include "../../render/ui/UITextRenderTypes.hpp"

namespace controllers::offscreen
{
    struct FrameContext;
    class UIInteractionSystem;
    class UIAnimationSystem;

    // VK-1435 — scoped screen-space draw lists for ONE UICanvasComponent subtree, emitted into a
    // caller-owned sink (never into RenderPassHandler). Consumed by UILayerPreviewController's own
    // UIRenderPipeline + UITextPipeline so the builder preview never touches the main UI draw lists.
    struct UICanvasDrawLists
    {
        std::vector<render::ui::UIImageRenderData> images;
        std::vector<render::ui::UITextRenderData> labels;
    };

    class UIFrameBuilder
    {
    public:
        UIFrameBuilder() = default;

        void prepareUIImages(const FrameContext& ctx, UIInteractionSystem& interactionSystem,
                             UIAnimationSystem& animationSystem);
        void prepareUILabels(const FrameContext& ctx);

        // VK-1435 — builds the screen-space image + label draw lists for the subtree rooted at
        // canvasRoot, resolved against targetExtent (so a ScaleWithScreenSize canvas has scale==1
        // at its reference resolution), and writes them into `out`. Reuses the runtime emit logic;
        // the canvas ROOT's active-gate is bypassed (the sandbox root is intentionally inactive so
        // the main passes skip it) while every descendant keeps its own active check. Pure data
        // emit — touches no shared RenderPassHandler state.
        void prepareUICanvasScoped(entt::entity canvasRoot, vk::Extent2D targetExtent,
                                   UICanvasDrawLists& out);

    private:
        void prepareUIImagesScreenSpace(const FrameContext& ctx, UIInteractionSystem& interactionSystem,
                                         UIAnimationSystem& animationSystem);
        void prepareUIImagesWorldSpace(const FrameContext& ctx);
        void prepareUILabelsScreenSpace(const FrameContext& ctx);
        void prepareUILabelsWorldSpace(const FrameContext& ctx);
    };
}
