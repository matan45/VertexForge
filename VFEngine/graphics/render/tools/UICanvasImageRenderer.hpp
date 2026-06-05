#pragma once

#include "DebugRendererBase.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <array>

namespace core
{
    class Shader;
    class Texture;
}

namespace render::mesh
{
    struct UICanvasImageRenderData
    {
        glm::mat4 modelMatrix;
        std::string texturePath;
        glm::vec4 colorTint{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 uvRect{0.0f, 0.0f, 1.0f, 1.0f};
    };

    struct UICanvasImagePushConstants
    {
        glm::mat4 viewProj;
        glm::mat4 modelMatrix;
        glm::vec4 colorTint;
        glm::vec4 uvRect;
    };

    class UICanvasImageRenderer : public DebugRendererBase
    {
    private:
        struct Vertex
        {
            glm::vec2 position;
            glm::vec2 texCoord;
        };

        // V flipped vs the play-mode quad (UIRenderTypes.hpp): the editor camera's
        // Y-flipped projection inverts the quad's screen Y, so compensate in the UVs.
        // NOTE: this bakes the editor convention into the vertex data — this renderer
        // is edit-mode only; driving it with a non-Y-flipped (play-mode) projection
        // would render textures vertically flipped.
        inline static constexpr std::array<Vertex, 4> vertices = {{
            {{-0.5f, -0.5f}, {0.0f, 1.0f}},
            {{ 0.5f, -0.5f}, {1.0f, 1.0f}},
            {{ 0.5f,  0.5f}, {1.0f, 0.0f}},
            {{-0.5f,  0.5f}, {0.0f, 0.0f}},
        }};

        inline static constexpr std::array<uint32_t, 6> indices = {0, 1, 2, 2, 3, 0};

        std::shared_ptr<core::Shader> shader;

        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;

        vk::Buffer vertexBuffer;
        core::VulkanAllocation vertexBufferAllocation;
        vk::Buffer indexBuffer;
        core::VulkanAllocation indexBufferAllocation;

        struct TextureEntry
        {
            std::unique_ptr<core::Texture> texture;
            vk::DescriptorSet descriptorSet;
        };
        std::unordered_map<std::string, TextureEntry> textureCache;
        static constexpr uint32_t MAX_TEXTURES = 64;

    public:
        explicit UICanvasImageRenderer(core::Device& device, core::SwapChain& swapChain);
        ~UICanvasImageRenderer();

        void init(vk::Format colorFormat, vk::Format depthFormat);
        void recreate(vk::Format colorFormat, vk::Format depthFormat);
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer,
                    const std::vector<UICanvasImageRenderData>& imageDrawList,
                    const glm::mat4& editorView,
                    const glm::mat4& editorProjection) const;

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createPipeline(vk::Format colorFormat, vk::Format depthFormat);
        void createBuffers();
        bool loadTexture(const std::string& texturePath);
        void updateDescriptorSet(vk::DescriptorSet dstSet, vk::ImageView imageView, vk::Sampler sampler);
    };
}
