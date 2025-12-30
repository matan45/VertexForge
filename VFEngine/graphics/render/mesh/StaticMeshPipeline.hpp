#pragma once

#include "MeshTypes.hpp"
#include "../ibl/IBLTypes.hpp"
#include "material/MaterialTypes.hpp"
#include <array>
#include <memory>
#include <mutex>
#include <shared_mutex>
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

namespace render
{
    class DebugRenderer;
}

namespace render::ibl
{
    class DefaultIBLTextureFactory;
}

namespace render::mesh
{
    class MaterialShaderCache;
    class MeshGPUCache;
    class MaterialTextureCache;
    class MaterialCacheManager;
}

namespace render::mesh
{
    class StaticMeshPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        // Shaders
        std::shared_ptr<core::Shader> meshShader;
        
        vk::RenderPass renderPass;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // Texture descriptor set (set 1) for material textures
        vk::DescriptorSetLayout textureDescriptorSetLayout;
        vk::DescriptorPool textureDescriptorPool;
        vk::DescriptorSet textureDescriptorSet;
        bool textureDescriptorsInitialized = false;

        std::vector<vk::Framebuffer> framebuffers;

        std::unique_ptr<MeshGPUCache> meshCache;

        std::unique_ptr<MaterialTextureCache> textureCache;

        std::unique_ptr<MaterialShaderCache> materialShaderCache;

        vk::Buffer cameraUBO;
        vk::DeviceMemory cameraUBOMemory;

        // Current camera matrices for AABB rendering
        mutable glm::mat4 currentView{1.0f};
        mutable glm::mat4 currentProjection{1.0f};
        mutable glm::vec3 currentCameraPos{0.0f};
        mutable float currentTime{0.0f};

        std::unique_ptr<MaterialCacheManager> materialCacheManager;

        // Prepare textures for frame rendering
        void prepareTexturesForFrame(const std::vector<MeshRenderData>& meshDrawList) const;

        bool usingDefaultTextures = false;
        std::unique_ptr<ibl::DefaultIBLTextureFactory> defaultIBLFactory;

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

        // GPU-driven rendering support
        vk::DescriptorSetLayout getIBLDescriptorSetLayout() const { return descriptorSetLayout; }
        vk::DescriptorSet getIBLDescriptorSet(uint32_t /*imageIndex*/) const { return descriptorSet; }
        const MeshGPUCache& getMeshGPUCache() const { return *meshCache; }
        MaterialTextureCache& getMaterialTextureCache() { return *textureCache; }

        // Render pass control for GPU-driven integration
        void beginRenderPass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void endRenderPass(const vk::CommandBuffer& commandBuffer) const;

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

        // Update the default descriptor set with material textures (for preview rendering)
        void updatePreviewTextureDescriptors(
            const std::array<vk::ImageView, material::MAX_MATERIAL_TEXTURES>& imageViews,
            const std::array<vk::Sampler, material::MAX_MATERIAL_TEXTURES>& samplers);

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

        const math::AABB* getMeshBoundingBox(const std::string& meshId) const;

        material::BlendMode getMaterialBlendMode(const std::string& materialPath) const;

        std::vector<std::string> getLoadedMeshIds() const;

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                 uint32_t imageIndex,
                                 const std::vector<MeshRenderData>& meshDrawList,
                                 const math::Frustum* frustum,
                                 render::DebugRenderer* debugRenderer = nullptr,
                                 const glm::mat4& debugView = glm::mat4(1.0f),
                                 const glm::mat4& debugProjection = glm::mat4(1.0f)) const;

    private:
        void loadShaders();
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

        // Texture descriptor set methods (set 1)
        void createTextureDescriptorSetLayout();
        void createTextureDescriptorPool();
        void initializeDefaultTextureDescriptors();

        // Rendering helper structures and methods
        struct SortedSubmesh
        {
            const MeshRenderData* meshData;
            const SubMeshGPUData* subMesh;
            size_t subMeshIndex;
            std::string materialPath;
        };

        struct RenderState
        {
            vk::Pipeline currentPipeline = nullptr;
            vk::DescriptorSet currentMaterialDescriptorSet = nullptr;
        };

        void collectSortedSubmeshes(
            const std::vector<MeshRenderData>& meshDrawList,
            const math::Frustum* frustum,
            const std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& materialCache,
            std::vector<SortedSubmesh>& opaqueSubmeshes,
            std::vector<SortedSubmesh>& maskedSubmeshes) const;

        void renderSubmesh(
            const vk::CommandBuffer& commandBuffer,
            const MeshRenderData& meshData,
            const SubMeshGPUData& subMesh,
            size_t subMeshIndex,
            material::BlendMode targetBlendMode,
            const std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& materialCache,
            RenderState& state) const;
    };
}
