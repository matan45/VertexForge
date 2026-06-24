#pragma once

// VK-1435 Layer B — UI Layer Builder offscreen WYSIWYG preview controller.
//
// Renders ONE UICanvasComponent-rooted subtree (the builder's isolated sandbox, tagged with
// UIPreviewTagComponent) to a single ImGui image at a chosen reference resolution, reusing
// the runtime screen-space UI layout + draw path so the preview matches play mode exactly.
//
// Unlike PrefabRigPreviewController (entt-free), the authored UI entities live in the one
// singleton registry: this controller is handed a live canvas-root entt::entity + a
// reference extent and reads the subtree directly each frame. It owns its OWN OffscreenResources
// (color + its own uiStencilImage, sized to the reference resolution), its OWN UIRenderPipeline
// + UITextPipeline (so it never clobbers the main RenderPassHandler draw lists), and a private
// command pool + per-image fences — recording on the editor thread exactly like the other
// preview controllers. The main UI render path stays untouched.
//
// The Editor never includes this controller; Layer C reaches it through the
// IUILayerPreviewProvider / adapter / service boundary (only EntityHandle + glm cross).

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <memory>
#include <optional>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
    class CommandPool;
    struct OffscreenResources;
}

namespace render
{
    class ClearColor;
}

namespace render::text
{
    class TextFontCache;
}

namespace render::ui
{
    class UIRenderPipeline;
    class UITextPipeline;
}

namespace controllers::offscreen
{
    class UIFrameBuilder;
}

namespace controllers
{
    class UILayerPreviewController
    {
    public:
        explicit UILayerPreviewController();
        ~UILayerPreviewController();

        UILayerPreviewController(const UILayerPreviewController&) = delete;
        UILayerPreviewController& operator=(const UILayerPreviewController&) = delete;
        UILayerPreviewController(UILayerPreviewController&&) = delete;
        UILayerPreviewController& operator=(UILayerPreviewController&&) = delete;

        void init();
        void cleanUp();

        // Binds the live canvas-root entity and (re)creates the offscreen target at the
        // reference resolution. Re-callable. Returns false (and logs) if canvasRoot is not a
        // valid entity carrying UICanvasComponent.
        bool buildFromCanvas(entt::entity canvasRoot, uint32_t refWidth, uint32_t refHeight);

        bool isBuilt() const { return built; }
        entt::entity canvasRoot() const { return rootEntity; }

        // Change the WYSIWYG reference resolution; recreates the offscreen color/stencil at
        // the new extent (waits idle, rebuilds descriptor sets).
        void setReferenceResolution(uint32_t refWidth, uint32_t refHeight);

        // Builds the scoped screen-space draw lists for the bound subtree at the reference
        // extent, records the UI image + text pipelines into the offscreen color image, and
        // returns the ImGui descriptor set for ImGui::Image() (or nullptr if not built).
        void* render();

        // Hit-test: top-most element of the bound subtree whose resolved rect contains refPx
        // (reference-pixel space, top-left origin, y down). entt::null on a miss.
        entt::entity pickElementAt(glm::vec2 refPx) const;

        // Resolved rect {x, y, w, h} of an element in the reference-pixel space the handles
        // draw in (same layout math the render path uses). nullopt if not resolvable.
        std::optional<glm::vec4> resolvedRect(entt::entity entity) const;

    private:
        void createOffscreenResources();
        void cleanupOffscreenResources();
        void createSampler();
        void updateDescriptorSet(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;

        core::Device& device;
        core::SwapChain& swapChain;
        std::unique_ptr<core::CommandPool> commandPool;

        vk::Sampler sampler;
        std::unique_ptr<core::OffscreenResources> offscreenResources;
        std::vector<vk::Fence> inFlightFences;

        std::unique_ptr<render::ClearColor> clearColor;
        std::unique_ptr<render::ui::UIRenderPipeline> uiPipeline;
        std::unique_ptr<render::ui::UITextPipeline> textPipeline;
        std::unique_ptr<controllers::offscreen::UIFrameBuilder> frameBuilder;

        // Font cache backing the owned UITextPipeline. The engine has no shared/global font-cache
        // accessor reachable from a preview controller (the only instances are private members of
        // the main/preview TextPipelines), so this single-instance editor-thread-only controller
        // owns its own TextFontCache — fully self-contained (own atlases, default texture, async
        // loads); see UILayerPreviewController.cpp init(). `fontCache` aliases it (kept non-owning
        // per the contract so the UITextPipeline holds a reference, not ownership).
        std::unique_ptr<render::text::TextFontCache> ownedFontCache;
        render::text::TextFontCache* fontCache = nullptr;

        entt::entity rootEntity{entt::null};
        vk::Extent2D refExtent{1920, 1080};

        bool initialized = false;
        bool built = false;
    };
}
