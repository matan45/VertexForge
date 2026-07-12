#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::selection
{
    // VK-1490 editor selection outline, pass 2 of 2: fullscreen composite that
    // dilates the visible-only selection mask into an ~2px #FFA100 silhouette
    // ring on the scene color target. Runs after post-processing (the outline
    // must not be tonemapped/blurred) and before the UI overlays. Frame-graph
    // managed: the caller declares mask read + scene-color write; this class
    // only records the draw.
    class SelectionOutlineComposite
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::shared_ptr<core::Shader> shader;
        vk::Pipeline pipeline;
        vk::PipelineLayout pipelineLayout;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // Last mask view written into the descriptor. The view only changes on a
        // resize (device idles there), so update-if-changed is safe.
        vk::ImageView boundMaskView;

        bool initialized = false;

    public:
        SelectionOutlineComposite(core::Device& device, core::SwapChain& swapChain);
        ~SelectionOutlineComposite();

        SelectionOutlineComposite(const SelectionOutlineComposite&) = delete;
        SelectionOutlineComposite& operator=(const SelectionOutlineComposite&) = delete;

        void init();
        void cleanup();
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // Rebinds the mask input when the view changed (mask recreate on resize).
        void setMaskInput(vk::ImageView maskView, vk::Sampler maskSampler);

        // Records the fullscreen outline draw onto the given scene color view.
        // Layout transitions are handled by the render graph.
        void executeGraphManaged(const vk::CommandBuffer& commandBuffer,
                                 vk::ImageView sceneColorView,
                                 vk::Extent2D extent,
                                 vk::Extent2D maskExtent);

    private:
        void createDescriptorResources();
        void loadShader();
        void createPipeline();
    };
}
