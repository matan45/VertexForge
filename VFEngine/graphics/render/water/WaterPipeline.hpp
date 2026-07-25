#pragma once

#include "WaterGPUTypes.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::water
{
    class WaterMeshBuffer;
    class OceanFFT;

    struct WaterPipelineLayoutConfig
    {
        vk::DescriptorSetLayout iblLayout;
        vk::DescriptorSetLayout lightDataLayout;
        vk::DescriptorSetLayout clusterGridLayout;
        vk::DescriptorSetLayout cullingOutputLayout;
        vk::DescriptorSetLayout shadowDataLayout;
        vk::DescriptorSetLayout shadowTextureLayout;
        vk::DescriptorSetLayout oceanTextureLayout; // Optional: from OceanFFT
        vk::DescriptorSetLayout refractionLayout;  // Optional: from WaterRefractionResources
        // Dynamic rendering formats (Vulkan 1.3)
        std::vector<vk::Format> colorAttachmentFormats;
        vk::Format depthAttachmentFormat = vk::Format::eUndefined;
    };

    struct WaterRenderDescriptors
    {
        vk::DescriptorSet iblDescSet;
        vk::DescriptorSet lightDataDescSet;
        vk::DescriptorSet clusterGridDescSet;
        vk::DescriptorSet cullingOutputDescSet;
        vk::DescriptorSet shadowDataDescSet;
        vk::DescriptorSet shadowTextureDescSet;
        vk::DescriptorSet oceanTextureDescSet; // Optional: from OceanFFT
        vk::DescriptorSet refractionDescSet;  // Optional: from WaterRefractionResources
    };

    class WaterPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::shared_ptr<core::Shader> waterShader;

        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;

        vk::DescriptorSetLayout waterTileLayout;
        vk::DescriptorPool waterTilePool;
        vk::DescriptorSet waterTileDescriptorSet;

        vk::DescriptorSetLayout dudvTextureLayout;
        vk::DescriptorPool dudvTexturePool;
        vk::DescriptorSet dudvTextureDescriptorSet;

        vk::Image dudvImage;
        core::VulkanAllocation dudvImageAllocation;
        vk::ImageView dudvImageView;
        vk::Sampler dudvSampler;

        vk::DescriptorSetLayout cachedIBLLayout;
        vk::DescriptorSetLayout cachedLightDataLayout;
        vk::DescriptorSetLayout cachedClusterGridLayout;
        vk::DescriptorSetLayout cachedCullingOutputLayout;
        vk::DescriptorSetLayout cachedShadowDataLayout;
        vk::DescriptorSetLayout cachedShadowTextureLayout;

        // Refraction support
        vk::DescriptorSetLayout refractionLayout;         // Currently active layout (dummy or external)
        vk::DescriptorSetLayout refractionDummyLayout;
        vk::DescriptorPool refractionDummyPool;
        vk::DescriptorSet refractionDummyDescSet;
        // VK-1604: set 9 binding 2 needs a valid buffer even on the dummy path.
        vk::Buffer refractionDummyParamsBuffer;
        core::VulkanAllocation refractionDummyParamsAllocation;
        // VK-1605: the dummy set owns its own 1x1 image now. It used to borrow the ocean dummy's,
        // which only exists when no ocean layout was supplied — so the dummy could not be built
        // unconditionally, and on the normal path it was never built at all.
        vk::Image refractionDummyImage;
        core::VulkanAllocation refractionDummyImageAllocation;
        vk::ImageView refractionDummyView;
        vk::Sampler refractionDummySampler;

        // Ocean FFT texture support
        vk::DescriptorSetLayout oceanTextureLayout;       // Currently active layout (dummy or external)
        vk::DescriptorSetLayout oceanDummyLayout;         // Our owned dummy layout
        vk::DescriptorPool oceanDummyPool;
        vk::DescriptorSet oceanDummyDescSet;
        vk::Image oceanDummyImage;
        core::VulkanAllocation oceanDummyAllocation;
        vk::ImageView oceanDummyView;
        vk::Sampler oceanDummySampler;
        bool initialized = false;
        bool wireframeMode = false;
        uint32_t lastDescriptorTileCount = 0;

    public:
        explicit WaterPipeline(core::Device& device, core::SwapChain& swapChain);
        ~WaterPipeline();

        void setWireframeMode(bool enabled) { wireframeMode = enabled; }

        void init(const WaterPipelineLayoutConfig& config);
        void recreate(const WaterPipelineLayoutConfig& config);
        void cleanup();

        void updateDescriptors(vk::Buffer tileSSBO, uint32_t tileCount);

        // VK-1605: point the DUMMY set 9's binding 3 at the real shore-depth texture. RTT and
        // reflection-probe views bind the dummy set (they must not sample the main view's scene
        // colour/depth), but shoaling and the breaking deformer displace VERTICES — if a probe did
        // not shoal, it would reflect a water surface that does not exist. The shore field is
        // view-independent, so it is safe (and required) to hand it to the dummy path too.
        void updateDummyShoreDepth(vk::ImageView shoreDepthView, vk::Sampler shoreDepthSampler);

        // VK-1606: same deal for binding 4, the ripple patch. Ripples displace vertices too, and the
        // patch is a single camera-following window shared by every view, so a probe must see it.
        void updateDummyRipple(vk::ImageView rippleView, vk::Sampler rippleSampler);

        // VK-1607: refresh the DUMMY set 9's binding 2 (WaterExtendedParams) with this frame's real
        // values. It used to be written exactly once at init with the struct defaults - flags = 0 -
        // so every view that binds the dummy set (RTT / reflection probes, and the ocean-disabled
        // path) silently ran with hex tiling, shoaling, shore waves, ripples AND the water-body clip
        // all off, whatever WATER_VIEW_FLAGS_RTT said it was allowed to keep. The caller is expected
        // to hand over a copy whose flags are already masked to what a dummy-set view may use.
        void updateDummyParams(const WaterExtendedParams& params);

        void render(vk::CommandBuffer cmd, const WaterRenderDescriptors& descriptors,
                    WaterMeshBuffer& meshBuffer, const WaterPushConstants& pushConstants);

        // Per-LOD rendering: lodTileCounts[i] = number of tiles at LOD i, tiles sorted by LOD in SSBO
        void renderMultiLOD(vk::CommandBuffer cmd, const WaterRenderDescriptors& descriptors,
                            WaterMeshBuffer& meshBuffer, const WaterPushConstants& pushConstants,
                            const uint32_t lodTileCounts[WATER_LOD_COUNT]);

        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createWaterTileDescriptor();
        void createDuDvTexture();
        void createDuDvDescriptor();
        void createOceanDummyTexture();
        void createRefractionDummy();
        void updateDummyBinding(uint32_t binding, vk::ImageView view, vk::Sampler sampler);
        void createGraphicsPipeline(const WaterPipelineLayoutConfig& config);
    };
}
