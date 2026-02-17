#pragma once

#include "VolumetricTypes.hpp"
#include "VolumetricGridManager.hpp"
#include "VolumetricLightInjection.hpp"
#include "VolumetricTemporalFilter.hpp"
#include "VolumetricRayMarch.hpp"
#include "postprocess/PostProcessTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace core
{
    class Device;
}

namespace render::volumetric
{
    class VolumetricPipeline
    {
    private:
        core::Device& device;

        std::unique_ptr<VolumetricGridManager> gridManager;
        std::unique_ptr<VolumetricLightInjection> lightInjection;
        std::unique_ptr<VolumetricTemporalFilter> temporalFilter;
        std::unique_ptr<VolumetricRayMarch> rayMarch;

        glm::mat4 prevViewProjection{1.0f};
        uint32_t frameIndex = 0;
        bool enabled = false;
        bool initialized = false;

    public:
        explicit VolumetricPipeline(core::Device& device);
        ~VolumetricPipeline();

        VolumetricPipeline(const VolumetricPipeline&) = delete;
        VolumetricPipeline& operator=(const VolumetricPipeline&) = delete;

        void init(VolumetricQuality quality,
                  vk::DescriptorSetLayout clusterGridLayout,
                  vk::DescriptorSetLayout lightBufferLayout,
                  vk::DescriptorSetLayout lightCullingLayout,
                  vk::DescriptorSetLayout shadowDataLayout,
                  vk::DescriptorSetLayout shadowTextureLayout);
        void cleanup();
        void recreate(VolumetricQuality quality,
                      vk::DescriptorSetLayout clusterGridLayout,
                      vk::DescriptorSetLayout lightBufferLayout,
                      vk::DescriptorSetLayout lightCullingLayout,
                      vk::DescriptorSetLayout shadowDataLayout,
                      vk::DescriptorSetLayout shadowTextureLayout);

        void update(const glm::mat4& viewProj, const glm::mat4& invViewProj,
                    const glm::vec3& cameraPos, float nearPlane, float farPlane,
                    const ::postprocess::VolumetricFogSettings& settings);

        void dispatch(vk::CommandBuffer cmd,
                      vk::DescriptorSet clusterGridDescSet,
                      vk::DescriptorSet lightBufferDescSet,
                      vk::DescriptorSet lightCullingDescSet,
                      vk::DescriptorSet shadowDataDescSet,
                      vk::DescriptorSet shadowTextureDescSet);

        void setEnabled(bool value) { enabled = value; }
        [[nodiscard]] bool isEnabled() const { return enabled && initialized; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

        [[nodiscard]] vk::ImageView getIntegratedVolumeImageView() const;
        [[nodiscard]] vk::Image getIntegratedVolumeImage() const;
        [[nodiscard]] vk::Sampler getVolumeSampler() const;
        [[nodiscard]] const VolumetricGridDimensions& getDimensions() const;
    };
}
