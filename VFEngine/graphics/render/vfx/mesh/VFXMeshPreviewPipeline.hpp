#pragma once

#include "../billboard/VFXBillboardTypes.hpp"
#include "vfx/VFXBlendMode.hpp"
#include "../../../core/VulkanMemoryManager.hpp"
#include <memory>
#include <vector>
#include <string>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
    class Texture;
    struct OffscreenResources;
}

namespace render::mesh
{
    class MeshGPUCache;
}

namespace render::vfx
{
    struct VFXMeshPreviewPushConstants
    {
        float alphaClipThreshold = 0.1f;
        uint32_t blendMode = 0;
        float glowColorR = 1.0f;
        float glowColorG = 1.0f;
        float glowColorB = 1.0f;
        float emissiveIntensity = 1.0f;
        float uvScrollSpeedU = 0.0f;
        float uvScrollSpeedV = 0.0f;
        // VK-1476: mesh orientation. 4-byte scalars only (no vec3) to keep the
        // push-constant layout std430-simple and identical across vertex+fragment.
        uint32_t meshOrientationMode = 0; // vfx::VFXOrientationMode (0 = VelocityForward)
        float orientAxisX = 0.0f;
        float orientAxisY = 1.0f;
        float orientAxisZ = 0.0f;
        float meshOrientationSpinRate = 1.0f;
    };

    class VFXMeshPreviewPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;
        render::mesh::MeshGPUCache& meshCache;

        bool initialized = false;

        std::shared_ptr<core::Shader> meshShader;

        vk::Pipeline graphicsPipeline;
        vk::Pipeline multiplyPipeline; // VK-1472: Multiply blend variant (shares pipelineLayout)
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        vk::Buffer instanceBuffer;
        core::VulkanAllocation instanceBufferAllocation;
        void* instanceBufferMapped = nullptr;
        vk::Buffer cameraUBO;
        core::VulkanAllocation cameraUBOAllocation;
        void* cameraUBOMapped = nullptr;

        uint32_t maxInstances = 1024;
        uint32_t currentInstanceCount = 0;

        std::string currentMeshPath;
        std::string currentMeshId;
        vk::Buffer meshVertexBuffer;
        vk::Buffer meshIndexBuffer;
        uint32_t meshIndexCount = 0;

        vk::Image defaultTextureImage;
        core::VulkanAllocation defaultTextureAllocation;
        vk::ImageView defaultTextureImageView;
        vk::Sampler textureSampler;

        std::unique_ptr<core::Texture> customTexture;
        std::string currentTexturePath;

        VFXMeshPreviewPushConstants pushConstants;

    public:
        explicit VFXMeshPreviewPipeline(core::Device& device, core::SwapChain& swapChain,
                                         core::OffscreenResources& offscreenResources,
                                         render::mesh::MeshGPUCache& meshCache);
        ~VFXMeshPreviewPipeline();

        void init();
        void recreate();
        void cleanUp();

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time) const;

        void setParticleInstances(const std::vector<VFXInstanceData>& instances);

        void setTexture(const std::string& texturePath);
        void setMesh(const std::string& meshPath);
        void setRenderingConfig(float alphaClipThreshold, ::vfx::VFXBlendMode blendMode,
                                const glm::vec3& glowColor = glm::vec3(1.0f),
                                float emissiveIntensity = 1.0f,
                                float uvScrollSpeedU = 0.0f, float uvScrollSpeedV = 0.0f);

        // VK-1476: mesh orientation mode + params (kept separate from setRenderingConfig
        // so ribbon/billboard callers are untouched). `mode` = vfx::VFXOrientationMode.
        void setOrientationConfig(uint32_t mode, const glm::vec3& axis, float spinRate);

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        // Bind+draw only (no render-pass begin/end) for VK-1451 composited preview.
        void recordDraws(const vk::CommandBuffer& commandBuffer) const;

        bool isInitialized() const { return initialized; }
        bool hasMesh() const { return meshIndexCount > 0; }

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createDescriptorSet();
        void createPipeline();
        void createBuffers();
        void createDefaultTexture();
        void createSampler();

        void updateDescriptorSet();
    };
}
