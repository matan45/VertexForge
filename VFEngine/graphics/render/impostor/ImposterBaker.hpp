#pragma once

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

    struct BakeAtlasConfig
    {
        uint32_t horizontalAngles = 8;
        uint32_t verticalAngles = 3;
        uint32_t viewResolution = 256;
        float meshScale = 1.0f;
        glm::vec3 meshCenter{0.0f};
        bool generateNormalMap = true;
    };

    struct ImposterBakeRequest
    {
        std::string meshPath;
        std::string outputPath;          // .vfImposter output
        BakeAtlasConfig config;
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

        void setRenderPassHandler(render::RenderPassHandler* handler) { renderPassHandler = handler; }

        ImposterBakeResult bake(const ImposterBakeRequest& request);

    private:
        core::Device* devicePtr = nullptr;
        core::SwapChain* swapChainPtr = nullptr;
        render::RenderPassHandler* renderPassHandler = nullptr;

        // Internal view info for atlas layout
        struct ViewInfo
        {
            float horizontalAngle;
            float verticalAngle;
            glm::vec4 uvRect; // x,y = offset, z,w = size in atlas UV space
        };

        struct AtlasLayout
        {
            uint32_t atlasWidth = 0;
            uint32_t atlasHeight = 0;
            std::vector<ViewInfo> views;
        };

        static AtlasLayout generateAtlasLayout(const BakeAtlasConfig& config);
        bool saveAtlas(const std::string& filePath, const AtlasLayout& layout,
                       const BakeAtlasConfig& config,
                       const std::vector<uint8_t>& colorData,
                       const std::vector<uint8_t>& normalData);

        bool readbackImage(void* srcImage, uint32_t width, uint32_t height,
                           std::vector<uint8_t>& outPixels);

        static glm::mat4 computeOrbitalView(float horizontalAngle, float verticalAngle,
                                              float distance, const glm::vec3& target);
    };
}
