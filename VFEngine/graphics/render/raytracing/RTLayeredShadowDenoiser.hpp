#pragma once

#include "../../core/VulkanMemoryManager.hpp"
#include "../../core/PerFrameBuffer.hpp"
#include "../lighting/GPULightTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class Shader;
}

namespace render::raytracing
{
    // One mask slice scheduled for denoising this frame.
    struct RTLayeredDenoiseInfo
    {
        uint32_t slice;                 // RT mask array layer (matches the pipeline's layer)
        vk::ImageView rawLayerView;     // pipeline's raw R8 layer view for this slice (eGeneral)
        bool resetHistory;              // true when this slice was just (re)assigned to a new light
    };

    // Per-slice temporal + spatial à-trous denoiser for the per-light RT shadow masks shared by
    // both spot lights (VK-1175) and point lights (VK-1176). Every internal buffer is array-layered:
    // each mask slice carries its own temporal history layer so reprojection is correct per light.
    // Reuses the directional temporal/spatial compute shaders unchanged via per-layer (e2D) views.
    // Spatial ping-pong buffers are shared 2D scratch (each slice fully completes before the next).
    // The renderer owns one instance per light type, each with its own independent slice set 0..N-1.
    class RTLayeredShadowDenoiser
    {
    public:
        // Spot and point RT budgets share the same fixed slice count; one denoiser class serves both.
        static_assert(render::lighting::LightConstants::MAX_RT_SPOT_LIGHTS ==
                          render::lighting::LightConstants::MAX_RT_POINT_LIGHTS,
                      "RTLayeredShadowDenoiser assumes spot and point RT budgets share the slice count");
        static constexpr uint32_t MAX_SLICES = render::lighting::LightConstants::MAX_RT_SPOT_LIGHTS;
        static constexpr int MAX_SPATIAL_PASSES = 5;

        explicit RTLayeredShadowDenoiser(core::Device& device);
        ~RTLayeredShadowDenoiser();

        RTLayeredShadowDenoiser(const RTLayeredShadowDenoiser&) = delete;
        RTLayeredShadowDenoiser& operator=(const RTLayeredShadowDenoiser&) = delete;

        void init(uint32_t width, uint32_t height);
        void cleanup();
        void resize(uint32_t width, uint32_t height);

        // Denoises each scheduled slice into the denoised output array. Contract mirrors the
        // directional denoiser: depth/normal arrive in eShaderReadOnlyOptimal and are transitioned
        // back to attachment at the end; the raw mask array stays in eGeneral (the pipeline owns it).
        void dispatch(vk::CommandBuffer cmd,
                      const std::vector<RTLayeredDenoiseInfo>& slices,
                      vk::ImageView depthView,
                      vk::Image depthImage,
                      vk::ImageView normalView,
                      vk::Image normalImage,
                      const glm::mat4& invViewProjection,
                      const glm::mat4& viewProjection,
                      uint32_t screenWidth,
                      uint32_t screenHeight,
                      uint32_t frameIndex,
                      uint32_t resourceFrameIndex);

        bool isInitialized() const { return initialized; }

        // Fragment shader consumption: denoised mask array.
        vk::DescriptorSetLayout getDenoisedMaskSamplerLayout() const { return denoisedMaskSamplerLayout; }
        vk::DescriptorSet getDenoisedMaskSamplerDescriptorSet() const { return denoisedMaskSamplerDescSet; }
        // VK-1430: per-slice denoised layer view (e2D), read by the half-res layered upsample as input.
        // The output image carries eSampled usage, so this storage-style layer view is samplable too.
        vk::ImageView getDenoisedMaskLayerView(uint32_t slice) const { return denoisedOutputLayerView[slice]; }

        void setTemporalBlend(float v) { temporalBlend = v; }
        void setDepthThreshold(float v) { depthThreshold = v; }
        void setNormalThreshold(float v) { normalThreshold = v; }
        void setSpatialPhiDepth(float v) { spatialPhiDepth = v; }
        void setSpatialPhiNormal(float v) { spatialPhiNormal = v; }
        void setSpatialPasses(int v) { spatialPassCount = v; }

    private:
        core::Device& device;
        bool initialized = false;
        uint32_t maskWidth = 0;
        uint32_t maskHeight = 0;
        bool historyImagesInGeneral = false;
        bool spatialImagesReady = false;
        bool sliceHistoryValid[MAX_SLICES]{};
        glm::mat4 prevViewProjection{1.0f};

        float temporalBlend = 0.9f;
        float depthThreshold = 0.01f;
        float normalThreshold = 0.9f;
        float spatialPhiDepth = 0.005f;
        float spatialPhiNormal = 32.0f;
        int spatialPassCount = 3;

        // Temporal
        vk::Pipeline temporalPipeline;
        vk::PipelineLayout temporalPipelineLayout;
        std::unique_ptr<core::Shader> temporalShader;
        vk::DescriptorSetLayout temporalDSLayout;
        vk::DescriptorPool temporalDSPool;
        vk::DescriptorSet temporalDescSets[MAX_SLICES][2]; // [slice][ping-pong]

        struct ArrayBuffer
        {
            vk::Image image;
            core::VulkanAllocation allocation;
            vk::ImageView layerView[MAX_SLICES]; // e2D, baseArrayLayer = k
        };
        ArrayBuffer history[2]{};
        uint32_t currentHistoryIndex = 0;

        // Spatial (shared 2D scratch, reused per slice)
        vk::Pipeline spatialPipeline;
        vk::PipelineLayout spatialPipelineLayout;
        std::unique_ptr<core::Shader> spatialShader;
        vk::DescriptorSetLayout spatialDSLayout;
        vk::DescriptorPool spatialDSPool;
        vk::DescriptorSet spatialDescSets[core::MAX_FRAMES_IN_FLIGHT][MAX_SLICES][MAX_SPATIAL_PASSES];

        struct SpatialBuffer
        {
            vk::Image image;
            core::VulkanAllocation allocation;
            vk::ImageView storageView;
        };
        SpatialBuffer spatialBuf[2]{};

        // Denoised output array (R16Sfloat, MAX_SLICES layers)
        vk::Image denoisedOutputImage;
        core::VulkanAllocation denoisedOutputAllocation;
        vk::ImageView denoisedOutputLayerView[MAX_SLICES]; // e2D storage, baseArrayLayer = k
        vk::ImageView denoisedOutputArraySampledView;      // e2DArray (fragment read)

        vk::DescriptorSetLayout denoisedMaskSamplerLayout;
        vk::DescriptorPool denoisedMaskSamplerPool;
        vk::DescriptorSet denoisedMaskSamplerDescSet;
        vk::Sampler denoisedMaskSampler;

        core::PerFrameBuffer paramsBuffer;
        vk::Sampler nearestSampler;

        void createImages(uint32_t w, uint32_t h);
        void destroyImages();
        void createSamplers();
        void createParamsBuffer();
        void createDescriptorLayouts();
        void createDescriptorPools();
        void allocateDescriptorSets();
        void createTemporalPipeline();
        void createSpatialPipeline();
        void createDenoisedMaskSamplerDescriptor();
        vk::ImageView makeLayerView(vk::Image image, vk::Format format, uint32_t layer);
    };
}
