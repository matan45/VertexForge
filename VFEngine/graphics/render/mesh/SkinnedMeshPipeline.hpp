#pragma once

#include "MeshTypes.hpp"
#include "SkinnedMeshTypes.hpp"
#include "../ibl/IBLTypes.hpp"
#include <memory>
#include <vector>
#include <string_view>
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
    // GPU resources for skinned mesh rendering
    struct SkinnedMeshGPUData
    {
        std::string meshPath;
        MeshGPUData meshData; // Reuse static mesh GPU data
        bool hasSkinning = false;
        resource::SkeletonInfo skeleton; // Mesh skeleton info for bone name verification
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

        // Descriptor set layout for camera + IBL (set 0)
        vk::DescriptorSetLayout cameraIBLDescriptorSetLayout;
        vk::DescriptorPool cameraIBLDescriptorPool;
        vk::DescriptorSet cameraIBLDescriptorSet;

        // Descriptor set layout for textures (set 1)
        vk::DescriptorSetLayout textureDescriptorSetLayout;
        vk::DescriptorPool textureDescriptorPool;
        vk::DescriptorSet textureDescriptorSet;

        // Descriptor set layout for bone matrices SSBO (set 2)
        vk::DescriptorSetLayout boneDescriptorSetLayout;
        vk::DescriptorPool boneDescriptorPool;
        vk::DescriptorSet boneDescriptorSet;

        std::vector<vk::Framebuffer> framebuffers;

        vk::Buffer cameraUBO;
        vk::DeviceMemory cameraUBOMemory;

        vk::Buffer boneSSBO;
        vk::DeviceMemory boneSSBOMemory;
        void* boneSSBOMapped = nullptr;

        std::unique_ptr<SkinnedMeshGPUData> loadedMesh;

        bool usingDefaultTextures = false;
        std::unique_ptr<ibl::DefaultIBLTextureFactory> defaultIBLFactory;

        mutable glm::mat4 currentView{1.0f};
        mutable glm::mat4 currentProjection{1.0f};
        mutable glm::vec3 currentCameraPos{0.0f};

    public:
        explicit SkinnedMeshPipeline(core::Device& device, core::SwapChain& swapChain,
                                     core::OffscreenResources& offscreenResources);
        ~SkinnedMeshPipeline();

        void init();
        void cleanUp();

        bool loadMeshFromAnimation(const resource::AnimationData& animData);
        void unloadMesh();
        const SkinnedMeshGPUData* getLoadedMesh() const { return loadedMesh.get(); }

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time = 0.0f) const;

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
