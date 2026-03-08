#pragma once

#include "../../../import/types/ImposterAtlasGenerator.hpp"
#include "../../../import/types/ImposterSerializer.hpp"
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <glm/glm.hpp>

namespace core
{
    class Device;
    class SwapChain;
}

namespace render
{
    class RenderPassHandler;
    class RenderTextureViewPort;
}

namespace render::impostor
{
    using BakeProgressCallback = std::function<void(float progress)>;

    struct ImposterBakeRequest
    {
        std::string meshPath;
        std::string outputPath;          // .vfImposter output
        importTypes::ImposterAtlasConfig config;
        BakeProgressCallback progressCallback;
    };

    struct ImposterBakeResult
    {
        bool success = false;
        std::string outputPath;
        std::string errorMessage;
        uint32_t atlasWidth = 0;
        uint32_t atlasHeight = 0;
        uint32_t viewCount = 0;
    };

    class ImposterBaker
    {
    public:
        ImposterBaker() = default;
        ~ImposterBaker() = default;

        ImposterBaker(const ImposterBaker&) = delete;
        ImposterBaker& operator=(const ImposterBaker&) = delete;

        void init(core::Device& device, core::SwapChain& swapChain);

        // Set the render pass handler for GPU rendering (must be set before bake())
        void setRenderPassHandler(render::RenderPassHandler* handler) { renderPassHandler = handler; }

        ImposterBakeResult bake(const ImposterBakeRequest& request);

    private:
        core::Device* devicePtr = nullptr;
        core::SwapChain* swapChainPtr = nullptr;
        render::RenderPassHandler* renderPassHandler = nullptr;

        // Read back pixels from a rendered color image into CPU buffer (srcImage is VkImage cast to void*)
        bool readbackImage(void* srcImage, uint32_t width, uint32_t height,
                           std::vector<uint8_t>& outPixels);

        // Compute view matrix for a given orbital camera angle
        static glm::mat4 computeOrbitalView(float horizontalAngle, float verticalAngle,
                                              float distance, const glm::vec3& target);
    };
}
