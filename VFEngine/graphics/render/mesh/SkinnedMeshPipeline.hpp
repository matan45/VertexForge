#pragma once

#include "MeshTypes.hpp"
#include "SkinnedMeshTypes.hpp"
#include "../ibl/IBLTypes.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <memory>
#include <vector>
#include <string>

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

namespace render::ibl
{
    class DefaultIBLTextureFactory;
}

namespace render::mesh
{
    struct SkinnedMeshGPUData
    {
        std::string meshPath;
        MeshGPUData meshData;
        bool hasSkinning = false;
        resource::SkeletonInfo skeleton;
    };

    class SkinnedMeshPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        std::shared_ptr<core::Shader> skinnedMeshShader;

        vk::RenderPass renderPass;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;

        vk::DescriptorSetLayout cameraIBLDescriptorSetLayout;
        vk::DescriptorPool cameraIBLDescriptorPool;
        vk::DescriptorSet cameraIBLDescriptorSet;

        vk::DescriptorSetLayout textureDescriptorSetLayout;
        vk::DescriptorPool textureDescriptorPool;
        vk::DescriptorSet textureDescriptorSet;

        vk::DescriptorSetLayout boneDescriptorSetLayout;
        vk::DescriptorPool boneDescriptorPool;
        vk::DescriptorSet boneDescriptorSet;

        std::vector<vk::Framebuffer> framebuffers;

        vk::Buffer cameraUBO;
        core::VulkanAllocation cameraUBOAllocation;
        bool externalCameraBuffer = false;

        vk::Buffer boneSSBO;
        core::VulkanAllocation boneSSBOAllocation;
        void* boneSSBOMapped = nullptr;

        std::unique_ptr<SkinnedMeshGPUData> loadedMesh;

        bool usingDefaultTextures = false;
        std::unique_ptr<ibl::DefaultIBLTextureFactory> defaultIBLFactory;


    public:
        explicit SkinnedMeshPipeline(core::Device& device, core::SwapChain& swapChain,
                                     core::OffscreenResources& offscreenResources);
        ~SkinnedMeshPipeline();

        void init();
        void cleanUp();

        bool loadMeshFromFile(const std::string& meshPath);
        void unloadMesh();

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time = 0.0f) const;

        void setExternalCameraBuffer(vk::Buffer buffer)
        {
            cameraUBO = buffer;
            externalCameraBuffer = true;
        }

        void updateBoneMatrices(const std::vector<glm::mat4>& boneMatrices);

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                 uint32_t imageIndex,
                                 const SkinnedMeshRenderData& renderData) const;

    private:
        void loadShaders();
        void createRenderPass();
        void createDescriptorSetLayouts();
        void createDescriptorPools();
        void createDescriptorSets();
        void createCameraUBO();
        void createBoneSSBO();
        void createPipelineLayout();
        void createGraphicsPipeline();
        void createFramebuffers();

        void createMeshGPUBuffers(const resource::MeshesData& meshData);
        void destroyMeshGPUBuffers();
    };
}
