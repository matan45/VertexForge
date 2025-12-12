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
        
        void init(const ibl::ImageData& irradianceMap,
                  const ibl::ImageData& prefilterMap,
                  const ibl::ImageData& brdfLUT);
        
        void initWithDefaults();
        
        void recreate();
        
        void cleanUp();
        void cleanUpShader();
        
        void cleanUpForReinit();
        
        vk::Pipeline getGraphicsPipeline() const { return graphicsPipeline; }
        vk::PipelineLayout getPipelineLayout() const { return pipelineLayout; }
        vk::RenderPass getRenderPass() const { return renderPass; }
        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }

        // Update camera UBO (call once per frame before rendering)
        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos) const;

        
        vk::Framebuffer getFramebuffer(uint32_t imageIndex) const { return framebuffers[imageIndex]; }
        
        std::string loadMesh(std::string_view meshPath);
        
        void unloadMesh(const std::string& meshId);
        
        void unloadAllMeshes();
        
        const MeshGPUData* getMesh(const std::string& meshId) const;
        
        bool isMeshLoaded(const std::string& meshId) const;
        
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
        
        vk::UniqueCommandPool commandPool;
        
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
