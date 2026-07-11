#pragma once
#include "../../core/VulkanMemoryManager.hpp"
#include "../../core/GraphicsConstants.hpp" // core::MAX_FRAMES_IN_FLIGHT
#include <vulkan/vulkan.hpp>
#include <array>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::gpudriven
{
    struct SelectionMaskPushConstants
    {
        uint32_t baseDrawIndex;
        uint32_t viewMode;
        float screenWidth;
        float screenHeight;
    };

    struct SelectionMaskInitInfo
    {
        vk::DescriptorSetLayout cameraLayout;
        vk::DescriptorSetLayout perDrawLayout;
        vk::DescriptorSetLayout bindlessTextureLayout;
        vk::DescriptorSetLayout meshletDataLayout;
        vk::DescriptorSetLayout vertexDataLayout;
        vk::DescriptorSetLayout boneMatrixLayout;
        uint32_t maxObjectCount = 0; // sizes the selection bitmask SSBO
    };

    // VK-1490 editor selection outline, pass 1 of 2: renders the SELECTED
    // objects — through the same post-cull combined indirect stream the scene
    // pass consumes (same LODs, same visibility) — into an R8 mask. Visibility
    // is tested in the FRAGMENT shader against the SAMPLED resolved scene depth
    // with a 3x3 neighborhood tolerance (a fixed-function depth test is not
    // usable here: re-rasterized depth is not invariant with the scene pass and
    // MSAA-resolved depth is sample 0, which speckles on subpixel geometry).
    // A per-object selection bitmask SSBO (set 6 binding 0, host-visible ring)
    // drives the task-shader early-out; set 6 binding 1 samples the scene
    // depth; sets 0-5 mirror the depth prepass layout so the live scene
    // descriptor sets bind unchanged. Editor-only: never records in play mode
    // because the selection list is forced empty there.
    class SelectionMaskPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<core::Shader> shader;
        vk::Pipeline pipeline;
        vk::PipelineLayout pipelineLayout;

        // Selection bitmask ring (one bit per GPU object slot)
        struct BitsFrame
        {
            vk::Buffer buffer;
            core::VulkanAllocation allocation;
            void* mapped = nullptr;
            vk::DescriptorSet descriptorSet;
        };
        std::array<BitsFrame, core::MAX_FRAMES_IN_FLIGHT> bitsFrames{};
        uint32_t currentBitsFrame = 0;
        uint32_t bitsWordCount = 0;
        bool lastBitsEmpty = true;
        vk::DescriptorSetLayout selectionBitsLayout;
        vk::DescriptorPool descriptorPool;

        // R8 mask render target (swapchain extent; recreated on resize)
        vk::Image maskImage;
        core::VulkanAllocation maskAllocation;
        vk::ImageView maskImageView;
        vk::Sampler maskSampler;
        vk::Extent2D maskExtent{};

        // Sampled scene-depth input. The depth-only view is engine-owned and
        // non-owning here; descriptors refresh when it changes on recreation.
        vk::ImageView sceneDepthView;
        vk::Sampler depthSampler;

        bool initialized = false;

    public:
        explicit SelectionMaskPipeline(core::Device& device, core::SwapChain& swapChain);
        ~SelectionMaskPipeline();

        SelectionMaskPipeline(const SelectionMaskPipeline&) = delete;
        SelectionMaskPipeline& operator=(const SelectionMaskPipeline&) = delete;

        void init(const SelectionMaskInitInfo& info);
        void cleanup();
        bool isInitialized() const { return initialized; }

        static vk::Format getMaskFormat() { return vk::Format::eR8Unorm; }

        // (Re)creates the mask target at the given extent when needed. Callers
        // invoke this at frame-graph build time so the image exists for import.
        void ensureMaskTarget(vk::Extent2D extent);

        // Advances the host-visible ring, clears it and sets one bit per slot.
        // MUST be called from updateScene right after the object-slot rebuild —
        // the slots reshuffle every frame with the camera-culled draw list, so
        // the bits have to snapshot the same rebuild the GPU consumes this
        // frame. An empty-after-empty write is skipped.
        void writeSelectionBits(const std::vector<uint32_t>& selectedSlots);

        // Set-6 descriptor of the last written ring entry (valid after init;
        // zero-filled until the first write, which emits nothing).
        vk::DescriptorSet getCurrentBitsDescriptorSet() const
        {
            return bitsFrames[currentBitsFrame].descriptorSet;
        }

        // Points set-6 binding 1 at the engine-owned depth-only scene view.
        // Recreation must quiesce GPU work before replacing the view and
        // rewriting the ring descriptor sets.
        void updateSceneDepthInput(vk::ImageView depthImageView);

        // Dynamic rendering scope: clears the mask (color only; the visibility
        // test happens in the fragment shader against the sampled scene depth).
        void beginMaskPass(vk::CommandBuffer cmd) const;
        void endMaskPass(vk::CommandBuffer cmd) const;

        void bindPipeline(vk::CommandBuffer cmd) const;
        void pushConstants(vk::CommandBuffer cmd, const SelectionMaskPushConstants& pc) const;
        vk::PipelineLayout getPipelineLayout() const { return pipelineLayout; }

        vk::Image getMaskImage() const { return maskImage; }
        vk::ImageView getMaskImageView() const { return maskImageView; }
        vk::Sampler getMaskSampler() const { return maskSampler; }
        vk::Extent2D getMaskExtent() const { return maskExtent; }

    private:
        void createSelectionBitsResources(uint32_t maxObjectCount);
        void createPipeline(const SelectionMaskInitInfo& info);
        void destroyMaskTarget();
    };
}
