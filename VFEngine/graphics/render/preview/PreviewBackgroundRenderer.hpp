#pragma once

#include "../../core/OffScreen.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::preview
{
    struct GradientPushConstants
    {
        glm::vec4 topColor;
        glm::vec4 bottomColor;
    };

    class PreviewBackgroundRenderer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        std::shared_ptr<core::Shader> shader;
        vk::Pipeline pipeline;
        vk::PipelineLayout pipelineLayout;

        bool initialized = false;

    public:
        explicit PreviewBackgroundRenderer(core::Device& device, core::SwapChain& swapChain,
                                            core::OffscreenResources& offscreenResources);
        ~PreviewBackgroundRenderer();

        void init();
        void recreate();
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                    const glm::vec4& topColor, const glm::vec4& bottomColor) const;

        bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createPipeline();
    };
}
