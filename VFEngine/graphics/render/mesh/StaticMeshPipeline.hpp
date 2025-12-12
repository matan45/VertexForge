#pragma once

#include "MeshTypes.hpp"
#include "../ibl/IBLTypes.hpp"
#include <memory>
#include <vector>
#include <string_view>
#include <unordered_map>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
    struct OffscreenResources;
}

namespace resource
{
    struct MeshesData;
}

namespace render::mesh
{
    class StaticMeshPipeline
    {
    public:
        StaticMeshPipeline(core::Device& device, core::SwapChain& swapChain,
                          core::OffscreenResources& offscreenResources);
        ~StaticMeshPipeline() = default;

        // Initialize pipeline with IBL resources
        // irradianceMap: binding 1 - diffuse IBL
        // prefilterMap: binding 2 - specular IBL
        // brdfLUT: binding 3 - BRDF lookup texture
        void init(const ibl::ImageData& irradianceMap,
                  const ibl::ImageData& prefilterMap,
                  const ibl::ImageData& brdfLUT);

        // Initialize pipeline with default placeholder IBL textures
        // Use this when no IBL environment is set in the scene
        void initWithDefaults();

        // Recreate pipeline (e.g., on window resize)
        void recreate();

        // Cleanup resources
        void cleanUp();
        void cleanUpShader();

        // Cleanup only descriptor/pipeline resources (preserves loaded meshes)
        // Used when switching between IBL and default textures
        void cleanUpForReinit();

        // Accessors for external use
        vk::Pipeline getGraphicsPipeline() const { return graphicsPipeline; }
        vk::PipelineLayout getPipelineLayout() const { return pipelineLayout; }
        vk::RenderPass getRenderPass() const { return renderPass; }
        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }

        // Update camera UBO (call once per frame before rendering)
        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                            const glm::vec3& cameraPos) const;

        // Get framebuffer for a given image index
        vk::Framebuffer getFramebuffer(uint32_t imageIndex) const { return framebuffers[imageIndex]; }

        // Mesh loading and management
        // Load a mesh from .vfmesh file and create GPU buffers
        // Returns mesh ID for later reference, or empty string on failure
        std::string loadMesh(std::string_view meshPath);

        // Unload a mesh and free GPU resources
        void unloadMesh(const std::string& meshId);

        // Unload all meshes
        void unloadAllMeshes();

        // Get mesh GPU data by ID (returns nullptr if not found)
        const MeshGPUData* getMesh(const std::string& meshId) const;

        // Check if mesh is loaded
        bool isMeshLoaded(const std::string& meshId) const;

        // Get all loaded mesh IDs
        std::vector<std::string> getLoadedMeshIds() const;

        // Record rendering commands for all meshes in the draw list
        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                 uint32_t imageIndex,
                                 const std::vector<MeshRenderData>& meshDrawList) const;

    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        // Shader
        std::shared_ptr<core::Shader> meshShader;

        // Vulkan resources
        vk::RenderPass renderPass;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;
        std::vector<vk::Framebuffer> framebuffers;

        // Command pool for buffer upload operations
        vk::UniqueCommandPool commandPool;

        // Camera uniform buffer
        vk::Buffer cameraUBO;
        vk::DeviceMemory cameraUBOMemory;

        // Loaded meshes (key = mesh path)
        std::unordered_map<std::string, MeshGPUData> loadedMeshes;

        // Default IBL textures (used when no IBL is set)
        bool usingDefaultTextures = false;
        ibl::ImageData defaultIrradiance{};
        ibl::ImageData defaultPrefilter{};
        ibl::ImageData defaultBrdfLUT{};

        // Private initialization methods
        void createDefaultIBLTextures();
        void createRenderPass();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createDescriptorSet(const ibl::ImageData& irradianceMap,
                                 const ibl::ImageData& prefilterMap,
                                 const ibl::ImageData& brdfLUT);
        void createCameraUBO();
        void createPipelineLayout();
        void createGraphicsPipeline();
        void createFramebuffers();
    };
}
