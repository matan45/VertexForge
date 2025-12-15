#pragma once

#include "MeshTypes.hpp"
#include "../ibl/IBLTypes.hpp"
#include "material/MaterialTypes.hpp"
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
    class MaterialShaderCache;
    class MeshGPUCache;
    class MaterialTextureCache;
}

namespace render::mesh
{
    class StaticMeshPipeline
    {
    public:
        explicit StaticMeshPipeline(core::Device& device, core::SwapChain& swapChain,
                           core::OffscreenResources& offscreenResources);
        ~StaticMeshPipeline();
        
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

        // Clear cached material to force reload (called when materials are saved)
        void invalidateMaterialCache(const std::string& materialPath = "");

        // Inject a material into the cache (for preview with unsaved changes)
        void injectMaterialForPreview(const std::string& materialPath,
                                      std::shared_ptr<material::MaterialData> materialData);

        // Get last shader compilation error (for UI display)
        std::string getLastShaderCompilationError() const;

        // Texture descriptor set (set 1)
        vk::DescriptorSetLayout getTextureDescriptorSetLayout() const { return textureDescriptorSetLayout; }
        vk::DescriptorSet getTextureDescriptorSet() const { return textureDescriptorSet; }
        bool hasTextureDescriptors() const { return textureDescriptorsInitialized; }

        // Update texture descriptors for material preview (8 texture slots)
        void updateTextureDescriptors(
            const std::array<vk::ImageView, 8>& imageViews,
            const std::array<vk::Sampler, 8>& samplers);

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time = 0.0f) const;

        
        vk::Framebuffer getFramebuffer(uint32_t imageIndex) const { return framebuffers[imageIndex]; }
        
        std::string loadMesh(std::string_view meshPath);

        // Upload procedural mesh data directly (bypasses file loading)
        std::string uploadMesh(const std::string& meshId, const resource::MeshesData& meshData);

        void unloadMesh(const std::string& meshId);
        
        void unloadAllMeshes();
        
        const MeshGPUData* getMesh(const std::string& meshId) const;

        bool isMeshLoaded(const std::string& meshId) const;

        // Get bounding box of a loaded mesh (for frustum culling)
        const math::AABB* getMeshBoundingBox(const std::string& meshId) const;
        
        std::vector<std::string> getLoadedMeshIds() const;
        
        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                 uint32_t imageIndex,
                                 const std::vector<MeshRenderData>& meshDrawList,
                                 const math::Frustum* frustum) const;

    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        // Shaders
        std::shared_ptr<core::Shader> meshShader;
        std::shared_ptr<core::Shader> wireframeShader;

        // Vulkan resources
        vk::RenderPass renderPass;
        vk::Pipeline graphicsPipeline;           // Opaque pipeline
        vk::Pipeline translucentPipeline;        // Translucent pipeline (alpha blending)
        vk::Pipeline maskedPipeline;             // Masked pipeline (alpha testing)
        vk::PipelineLayout pipelineLayout;       // Shared layout for all pipelines

        // Wireframe pipeline for AABB debug rendering
        vk::Pipeline wireframePipeline;
        vk::PipelineLayout wireframePipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // Texture descriptor set (set 1) for material textures
        vk::DescriptorSetLayout textureDescriptorSetLayout;
        vk::DescriptorPool textureDescriptorPool;
        vk::DescriptorSet textureDescriptorSet;
        bool textureDescriptorsInitialized = false;

        std::vector<vk::Framebuffer> framebuffers;
        
        vk::UniqueCommandPool commandPool;

        // Mesh GPU cache for loading/unloading mesh buffers
        std::unique_ptr<MeshGPUCache> meshCache;

        // Material texture cache for loading/managing material textures
        std::unique_ptr<MaterialTextureCache> textureCache;

        // Material shader cache for per-material compiled shaders and pipelines
        std::unique_ptr<MaterialShaderCache> materialShaderCache;

        vk::Buffer cameraUBO;
        vk::DeviceMemory cameraUBOMemory;

        // AABB wireframe vertex/index buffers (unit cube, transformed via push constants)
        vk::Buffer aabbVertexBuffer;
        vk::DeviceMemory aabbVertexBufferMemory;
        vk::Buffer aabbIndexBuffer;
        vk::DeviceMemory aabbIndexBufferMemory;

        // Current camera matrices for AABB rendering and translucent sorting
        mutable glm::mat4 currentView{1.0f};
        mutable glm::mat4 currentProjection{1.0f};
        mutable glm::vec3 currentCameraPos{0.0f};
        mutable float currentTime{0.0f};

        // Cache for loaded materials to prevent reloading every frame
        mutable std::unordered_map<std::string, std::shared_ptr<material::MaterialData>> materialCache;

        // Flag to indicate cache should be invalidated (set by external notification)
        mutable bool materialCacheInvalidated = false;

        // Prepare textures for frame rendering
        void prepareTexturesForFrame(const std::vector<MeshRenderData>& meshDrawList) const;

        // Default IBL textures (used when no IBL is set)
        bool usingDefaultTextures = false;
        ibl::ImageData defaultIrradiance{};
        ibl::ImageData defaultPrefilter{};
        ibl::ImageData defaultBrdfLUT{};
        
        void loadShaders();
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
        void createWireframePipeline();
        void createAABBBuffers();

        // Texture descriptor set methods (set 1)
        void createTextureDescriptorSetLayout();
        void createTextureDescriptorPool();
        void initializeDefaultTextureDescriptors();
    };
}
