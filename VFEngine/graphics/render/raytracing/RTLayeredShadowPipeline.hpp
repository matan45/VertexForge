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
    // One light scheduled for an RT shadow dispatch this frame (drives the per-pixel ray). Shared by
    // spot lights (VK-1175) and point lights (VK-1176): a point light is a spot light with the cone
    // disabled. Point callers pass cosOuterAngle = cosInnerAngle = -2.0 (a sentinel < -1 that the
    // shader reads as "no cone") and any unit direction (ignored). 'range' is the spot range or the
    // point radius — both cap the ray tMax.
    struct RTLayeredDispatchInfo
    {
        uint32_t slice;        // RT mask array layer to write
        glm::vec3 position;    // world position
        float range;           // ray tMax cap (spot range / point radius)
        glm::vec3 direction;   // spot axis (normalized); point lights pass {0,0,1} (ignored)
        float cosInnerAngle;   // smooth cone falloff start; ignored for point (sentinel)
        float cosOuterAngle;   // hard cone boundary; point lights pass -2.0 to disable the cone
    };

    struct RTLayeredShadowPushConstants
    {
        glm::vec4 lightPosition;    // xyz = world pos, w = range/radius
        glm::vec4 lightDirection;   // xyz = spot axis, w = cosOuterAngle (-2.0 = point, no cone)
        glm::vec4 biasParams;       // x = normal bias, y = t_min, z = cosInnerAngle, w = unused
    };
    static_assert(sizeof(RTLayeredShadowPushConstants) == 48);

    // Ray-traced shadow override for a budgeted set of spot or point lights. Writes into one slice of
    // an R8 sampler2DArray per light, so the fragment shader can pick a light's mask by its
    // rtMaskSlice. Reuses the shared TLAS. The renderer owns one instance per light type (spot/point),
    // each with its own mask array and per-instance ray bias settings. The cone test in the shader is
    // skipped for point lights via the -2.0 cosOuterAngle sentinel, so one pipeline + one shader serve
    // both — the only per-type difference is the push-constant the dispatch fills.
    class RTLayeredShadowPipeline
    {
    public:
        // Spot and point RT budgets share the same fixed slice count; one pipeline class serves both.
        static_assert(render::lighting::LightConstants::MAX_RT_SPOT_LIGHTS ==
                          render::lighting::LightConstants::MAX_RT_POINT_LIGHTS,
                      "RTLayeredShadowPipeline assumes spot and point RT budgets share the slice count");
        static constexpr uint32_t MAX_SLICES = render::lighting::LightConstants::MAX_RT_SPOT_LIGHTS;

        explicit RTLayeredShadowPipeline(core::Device& device);
        ~RTLayeredShadowPipeline();

        RTLayeredShadowPipeline(const RTLayeredShadowPipeline&) = delete;
        RTLayeredShadowPipeline& operator=(const RTLayeredShadowPipeline&) = delete;

        void init(uint32_t width, uint32_t height, vk::DescriptorSetLayout tlasLayout);
        void cleanup();
        bool resize(uint32_t width, uint32_t height);

        // Traces one shadow ray per pixel toward each scheduled light, writing its mask slice.
        // skipFinalTransitions leaves the mask array in eGeneral so the denoiser can read it.
        void dispatch(vk::CommandBuffer cmd,
                      vk::ImageView depthView,
                      vk::Image depthImage,
                      vk::ImageView normalView,
                      vk::Image normalImage,
                      vk::DescriptorSet tlasDescriptorSet,
                      const glm::mat4& invViewProjection,
                      const glm::vec3& cameraPos,
                      float farPlane,
                      uint32_t screenWidth, uint32_t screenHeight,
                      const std::vector<RTLayeredDispatchInfo>& lights,
                      bool skipFinalTransitions = false,
                      uint32_t frameIndex = 0);

        bool isInitialized() const { return initialized; }

        // For fragment shader consumption: raw (un-denoised) mask array.
        vk::DescriptorSetLayout getShadowMaskSamplerLayout() const { return shadowMaskSamplerLayout; }
        vk::DescriptorSet getShadowMaskSamplerDescriptorSet() const { return shadowMaskSamplerDescSet; }

        // For denoiser access to the raw mask array.
        vk::Image getShadowMaskImage() const { return shadowMaskImage; }
        vk::ImageView getShadowMaskLayerView(uint32_t slice) const { return shadowMaskLayerStorageViews[slice]; }

        void setMaxRayDistance(float d) { maxRayDistance = d; }
        void setNormalBias(float b) { normalBias = b; }
        void setRayTMin(float t) { rayTMin = t; }

    private:
        core::Device& device;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        std::unique_ptr<core::Shader> shader;

        vk::DescriptorSetLayout cachedTlasLayout;
        vk::DescriptorSetLayout inputLayout;    // Set 1
        vk::DescriptorSetLayout outputLayout;   // Set 2

        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet inputDescSet[core::MAX_FRAMES_IN_FLIGHT];
        vk::DescriptorSet outputDescSet[MAX_SLICES]; // one per layer, no rewrite between dispatches

        core::PerFrameBuffer paramsBuffer;

        vk::Sampler depthSampler;
        vk::Sampler normalSampler;

        // R8Unorm array, MAX_SLICES layers.
        vk::Image shadowMaskImage;
        core::VulkanAllocation shadowMaskAllocation;
        vk::ImageView shadowMaskLayerStorageViews[MAX_SLICES]; // e2D, baseArrayLayer = k (compute write)
        vk::ImageView shadowMaskArraySampledView;              // e2DArray (fragment read)

        vk::DescriptorSetLayout shadowMaskSamplerLayout;
        vk::DescriptorPool shadowMaskSamplerPool;
        vk::DescriptorSet shadowMaskSamplerDescSet;
        vk::Sampler shadowMaskSampler;

        uint32_t maskWidth = 0;
        uint32_t maskHeight = 0;
        bool initialized = false;
        bool firstFrame = true;
        bool shadowMaskInGeneral = false;

        float maxRayDistance = 500.0f;
        float normalBias = 0.05f;
        float rayTMin = 0.01f;

        void createShadowMaskImage(uint32_t w, uint32_t h);
        void destroyShadowMaskImage();
        void createSamplers();
        void createParamsBuffer();
        void createDescriptorLayouts(vk::DescriptorSetLayout tlasLayout);
        void createDescriptorPool();
        void allocateDescriptorSets();
        void createComputePipeline();
        void createShadowMaskSamplerDescriptor();
        void updateOutputDescriptors();
    };
}
