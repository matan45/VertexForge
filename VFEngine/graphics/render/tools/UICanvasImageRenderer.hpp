#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <array>

namespace core
{
    class Device;
    class SwapChain;
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
    };

    struct UICanvasImagePushConstants
    {
        glm::mat4 viewProj;
        glm::mat4 modelMatrix;
        glm::vec4 colorTint;
    };

    class UICanvasImageRenderer
    {
    private:
        struct Vertex
        {
            glm::vec2 position;
            glm::vec2 texCoord;
        };

        inline static constexpr std::array<Vertex, 4> vertices = {{
            {{-0.5f, -0.5f}, {0.0f, 0.0f}},
            {{ 0.5f, -0.5f}, {1.0f, 0.0f}},
            {{ 0.5f,  0.5f}, {1.0f, 1.0f}},
            {{-0.5f,  0.5f}, {0.0f, 1.0f}},
        }};

        inline static constexpr std::array<uint32_t, 6> indices = {0, 1, 2, 2, 3, 0};

        core::Device& device;
        core::SwapChain& swapChain;

        std::shared_ptr<core::Shader> shader;

        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;

        vk::Buffer vertexBuffer;
        vk::DeviceMemory vertexBufferMemory;
        vk::Buffer indexBuffer;
        vk::DeviceMemory indexBufferMemory;

        struct TextureEntry
        {
            std::unique_ptr<core::Texture> texture;
            vk::DescriptorSet descriptorSet;
        };
        std::unordered_map<std::string, TextureEntry> textureCache;
        static constexpr uint32_t MAX_TEXTURES = 64;

        bool initialized = false;

    public:
        explicit UICanvasImageRenderer(core::Device& device, core::SwapChain& swapChain);
        ~UICanvasImageRenderer();

        void init(vk::RenderPass renderPass);
        void recreate(vk::RenderPass renderPass);
        void cleanUp();
        void cleanUpShader();

        void render(const vk::CommandBuffer& commandBuffer,
                    const std::vector<UICanvasImageRenderData>& imageDrawList,
                    const glm::mat4& editorView,
                    const glm::mat4& editorProjection) const;

        bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createPipeline(vk::RenderPass renderPass);
        void createBuffers();
        bool loadTexture(const std::string& texturePath);
        void updateDescriptorSet(vk::DescriptorSet dstSet, vk::ImageView imageView, vk::Sampler sampler);
    };
}
