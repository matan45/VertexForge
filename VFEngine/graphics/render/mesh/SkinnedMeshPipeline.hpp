#pragma once

#include "MeshTypes.hpp"
#include "SkinnedMeshTypes.hpp"
#include "../ibl/IBLTypes.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <array>
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
    class MaterialTextureCache;
    struct ExtractedPBRValues;
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

        vk::Pipeline graphicsPipeline;
        // VK-1433 Phase 3: PolygonMode::eLine variant, sharing layout + shader with graphicsPipeline.
        // Built lazily on the first wireframe draw (mutable so the const recordCommandBuffer can
        // populate it); stays null until then so non-wireframe callers pay nothing.
        mutable vk::Pipeline wireframePipeline;
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


        glm::vec4 clearColorValue{0.06f, 0.06f, 0.06f, 1.0f};

        vk::Buffer cameraUBO;
        core::VulkanAllocation cameraUBOAllocation;
        bool externalCameraBuffer = false;

        vk::Buffer boneSSBO;
        core::VulkanAllocation boneSSBOAllocation;
        void* boneSSBOMapped = nullptr;

        std::unique_ptr<SkinnedMeshGPUData> loadedMesh;

        bool usingDefaultTextures = false;
        std::unique_ptr<ibl::DefaultIBLTextureFactory> defaultIBLFactory;

        // VK-1433: optional real-material texture binding for the prefab rig preview.
        // Created lazily on the first loadMaterial() call. When present, its loaded textures
        // are written into textureDescriptorSet (set 1) and materialTextureIndices is packed
        // so the shader samples them; otherwise the set stays bound to the default BRDF-LUT
        // and the indices stay all-NONE (original scalar-only behavior).
        std::unique_ptr<MaterialTextureCache> materialTextureCache;
        std::array<uint32_t, 4> materialTextureIndices{
            0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        bool materialTexturesActive = false;


    public:
        explicit SkinnedMeshPipeline(core::Device& device, core::SwapChain& swapChain,
                                     core::OffscreenResources& offscreenResources);
        ~SkinnedMeshPipeline();

        void init();
        void cleanUp();

        bool loadMeshFromFile(const std::string& meshPath);
        void unloadMesh();

        // VK-1433: bind a material's real PBR textures into set 1 so the skinned shader
        // samples them (albedo/normal/ORM-or-MRA/emission), lit by the default IBL. Empty
        // texture slots fall back to the matching scalar PBR value. Idempotent: re-call to
        // switch materials. Default callers never invoke this, so their output is unchanged.
        void loadMaterial(const ExtractedPBRValues& pbr);
        // Clears any bound material textures and returns the set to its default state.
        void clearMaterial();
        // Packed per-slot texture indices for the currently bound material (all-NONE if
        // none). The owning controller copies this into SkinnedMeshRenderData before render.
        const std::array<uint32_t, 4>& getMaterialTextureIndices() const { return materialTextureIndices; }

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time = 0.0f) const;

        void setExternalCameraBuffer(vk::Buffer buffer)
        {
            cameraUBO = buffer;
            externalCameraBuffer = true;
        }

        void updateBoneMatrices(const std::vector<glm::mat4>& boneMatrices);

        void setClearColor(const glm::vec4& color) { clearColorValue = color; }

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                 uint32_t imageIndex,
                                 const SkinnedMeshRenderData& renderData,
                                 bool clearAttachments = true) const;

    private:
        void loadShaders();
        void createDescriptorSetLayouts();
        void createDescriptorPools();
        void createDescriptorSets();
        void createCameraUBO();
        void createBoneSSBO();
        void createPipelineLayout();
        void createGraphicsPipeline();
        // Builds a graphics pipeline identical to graphicsPipeline except for polygonMode (used for
        // the fill pipeline at init and the lazily-built wireframe variant). Shares layout + shader.
        vk::Pipeline createPipelineVariant(vk::PolygonMode polygonMode) const;
        // Ensures wireframePipeline exists (builds it on first call). Const + mutable so it can be
        // invoked from the const recordCommandBuffer path.
        void ensureWireframePipeline() const;

        void createMeshGPUBuffers(const resource::MeshesData& meshData);
        void destroyMeshGPUBuffers();
    };
}
