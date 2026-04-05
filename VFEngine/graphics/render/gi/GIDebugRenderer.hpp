#pragma once

#include "GITypes.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::gi
{
    class RadianceCascadeManager;

    class GIDebugRenderer
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Shader> probeDebugShader;

        vk::Pipeline probeDebugPipeline;
        vk::PipelineLayout probeDebugPipelineLayout;

        bool initialized = false;
        bool showProbes = false;
        bool showCascadeBounds = false;
        bool showProbeValidity = false;

    public:
        explicit GIDebugRenderer(core::Device& device);
        ~GIDebugRenderer();

        GIDebugRenderer(const GIDebugRenderer&) = delete;
        GIDebugRenderer& operator=(const GIDebugRenderer&) = delete;

        void init(vk::Format colorFormat, vk::Format depthFormat,
                  vk::DescriptorSetLayout probeDataLayout,
                  vk::DescriptorSetLayout cascadeInfoLayout);
        void cleanup();

        void render(vk::CommandBuffer cmd,
                    vk::DescriptorSet probeDataDescSet,
                    vk::DescriptorSet cascadeInfoDescSet,
                    const glm::mat4& viewProjection,
                    uint32_t totalProbes);

        void setShowProbes(bool show) { showProbes = show; }
        void setShowCascadeBounds(bool show) { showCascadeBounds = show; }
        void setShowProbeValidity(bool show) { showProbeValidity = show; }
        bool isShowProbes() const { return showProbes; }
        bool isShowCascadeBounds() const { return showCascadeBounds; }
        bool isShowProbeValidity() const { return showProbeValidity; }

        bool isInitialized() const { return initialized; }
        bool hasAnythingToRender() const { return showProbes || showCascadeBounds || showProbeValidity; }

    private:
        void loadShaders();
        void createPipelineLayout(vk::DescriptorSetLayout probeDataLayout,
                                  vk::DescriptorSetLayout cascadeInfoLayout);
        void createPipeline(vk::Format colorFormat, vk::Format depthFormat);
    };
}
