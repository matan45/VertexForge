#pragma once

#include "../tools/GridRenderer.hpp"
#include "../../core/OffScreen.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
}

namespace render::preview
{
    class PreviewGridRenderer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        std::unique_ptr<mesh::GridRenderer> gridRenderer;

        bool initialized = false;

    public:
        explicit PreviewGridRenderer(core::Device& device, core::SwapChain& swapChain,
                                      core::OffscreenResources& offscreenResources);
        ~PreviewGridRenderer();

        void init();
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                    const glm::mat4& view, const glm::mat4& projection,
                    bool visible) const;

        bool isInitialized() const { return initialized; }

    private:
    };
}
