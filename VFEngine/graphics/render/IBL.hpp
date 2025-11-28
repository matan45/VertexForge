#pragma once
#include <vector>
#include "../core/OffScreen.hpp"
#include "../core/Shader.hpp"
#include "../core/Texture.hpp"
#include "components/Components.hpp"

namespace core
{
    class Device;
    class SwapChain;
}

namespace render
{
    // Cube vertices for CUBEMAP GENERATION (rendering to faces from outside/origin)
    inline static const std::vector<glm::vec3> cubeVertices = {
        {-1.0f, 1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},
        {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},

        {-1.0f, -1.0f, 1.0f}, {-1.0f, -1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
        {-1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, 1.0f}, {-1.0f, -1.0f, 1.0f},

        {1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},

        {-1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}, {1.0f, -1.0f, 1.0f}, {-1.0f, -1.0f, 1.0f},

        {-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, -1.0f},

        {-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, -1.0f},
        {1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, 1.0f}
    };

    // Skybox cube vertices - wound for viewing from INSIDE the cube
    inline static const std::vector<glm::vec3> skyboxVertices = {
        // Front face (z = -1)
        {-1.0f,  1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f},
        {-1.0f,  1.0f, -1.0f}, {1.0f,  1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},

        // Back face (z = +1)
        {-1.0f, -1.0f,  1.0f}, {1.0f, -1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f},
        {-1.0f,  1.0f,  1.0f}, {1.0f, -1.0f,  1.0f}, {1.0f,  1.0f,  1.0f},

        // Left face (x = -1)
        {-1.0f, -1.0f,  1.0f}, {-1.0f,  1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f},
        {-1.0f, -1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f, -1.0f},

        // Right face (x = +1)
        {1.0f, -1.0f, -1.0f}, {1.0f,  1.0f, -1.0f}, {1.0f, -1.0f,  1.0f},
        {1.0f,  1.0f, -1.0f}, {1.0f,  1.0f,  1.0f}, {1.0f, -1.0f,  1.0f},

        // Top face (y = +1)
        {-1.0f,  1.0f, -1.0f}, {1.0f,  1.0f,  1.0f}, {1.0f,  1.0f, -1.0f},
        {-1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f,  1.0f}, {1.0f,  1.0f,  1.0f},

        // Bottom face (y = -1)
        {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f,  1.0f},
        {1.0f, -1.0f, -1.0f}, {1.0f, -1.0f,  1.0f}, {-1.0f, -1.0f,  1.0f}
    };

    struct QuadVertex
    {
        glm::vec3 position;
        glm::vec2 texture;
    };

    inline static const std::vector<QuadVertex> quad = {
        {{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}, // Vertex 0
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}}, // Vertex 1
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f}}, // Vertex 2

        {{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}, // Vertex 0 (repeated)
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f}}, // Vertex 2 (repeated)
        {{1.0f, 1.0f, 0.0f}, {1.0f, 0.0f}} // Vertex 3
    };

    struct UniformBufferObject
    {
        alignas(16) glm::mat4 projection;
        alignas(16) glm::mat4 view;
    };


    struct ImageData
    {
        vk::Image image;
        vk::DeviceMemory imageMemory;
        vk::ImageView imageView;
        vk::Sampler sampler;
    };

    struct CameraViewMatrix
    {
        // Vulkan cubemap face order: +X, -X, +Y, -Y, +Z, -Z
        // Standard OpenGL-style capture views - coordinate conversion happens in shaders
        inline static const std::array<glm::mat4, 6> captureViews = {
            glm::lookAt(glm::vec3(0, 0, 0), glm::vec3( 1, 0, 0), glm::vec3(0, -1, 0)),  // +X
            glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0)),  // -X
            glm::lookAt(glm::vec3(0, 0, 0), glm::vec3( 0, 1, 0), glm::vec3(0,  0, 1)),  // +Y
            glm::lookAt(glm::vec3(0, 0, 0), glm::vec3( 0,-1, 0), glm::vec3(0,  0,-1)),  // -Y
            glm::lookAt(glm::vec3(0, 0, 0), glm::vec3( 0, 0, 1), glm::vec3(0, -1, 0)),  // +Z
            glm::lookAt(glm::vec3(0, 0, 0), glm::vec3( 0, 0,-1), glm::vec3(0, -1, 0))   // -Z
        };
        inline static const glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    };

    struct OffScreenHelper
    {
		vk::Image image;
		vk::ImageView view;
		vk::DeviceMemory memory;
		vk::Framebuffer framebuffer;
    };

	struct Skybox
	{
		std::shared_ptr<core::Shader> skyboxShader;
		vk::RenderPass renderPass;
		vk::Pipeline graphicsPipeline;
		std::vector<vk::Framebuffer> framebuffers;
		vk::Buffer vertexBuffer;
        vk::DeviceMemory vertexBufferMemory;

		vk::Buffer uniformBuffer;
		vk::DeviceMemory uniformBufferMemory;

		vk::PipelineLayout pipelineLayout;

        vk::DescriptorSetLayout descriptorSetLayout;
		vk::DescriptorSet descriptorSet;
        vk::DescriptorPool descriptorPool;
	};


    class IBL
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;
        vk::UniqueCommandPool commandPool;
        std::shared_ptr<core::Texture> hdrTexture;
        components::CameraComponent* camera = nullptr;
        bool isDisplay = false;

        static constexpr uint32_t CUBE_MAP_SIZE = 512;
        
        ImageData imageIrradianceCube;
        std::shared_ptr<core::Shader> shaderIrradianceCube;

        ImageData brdfLUTImage;
        std::shared_ptr<core::Shader> brdfLUTShader;

        ImageData prefilterImage;
        std::shared_ptr<core::Shader> prefilterShader;

        Skybox skybox;

    public:
        explicit IBL(core::Device& device, core::SwapChain& swapChain,
                     core::OffscreenResources& offscreenResources);
        ~IBL() = default;

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        void init(std::string_view path);
        void recreate();
        void remove();
        void cleanUp();

        void setCamera(components::CameraComponent* _camera) {
            camera = _camera;
            isDisplay = (camera != nullptr);
        }

        const ImageData& getBrdfLUTImage()const {
            return brdfLUTImage;
        }

		const ImageData& getPrefilterImage()const {
			return prefilterImage;
		}

    private:
        void generateIrradianceCube();
        void generateBRDFLUT();
        void generatePrefilteredCube();
        void drawCube();
        void updateUniformBuffer(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix,
                                 const vk::DeviceMemory& uniformBufferMemory) const;

        void cleanUpImage(const ImageData& image) const;
       
    };
}
