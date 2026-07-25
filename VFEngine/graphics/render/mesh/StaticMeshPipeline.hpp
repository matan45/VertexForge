#pragma once

#include "MeshTypes.hpp"
#include "../ibl/IBLTypes.hpp"
#include "../probe/ReflectionProbeTypes.hpp"
#include "material/MaterialTypes.hpp"
#include "material/MaterialManager.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include "../../../services/data/PipelineWarmupTypes.hpp"
#include <array>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
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
    struct ExtractedPBRValues;
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
    class MaterialParameterBufferCache;
    class MaterialPipelineWarmup;
}

namespace render::mesh
{
    class StaticMeshPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        std::shared_ptr<core::Shader> meshShader;

        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        vk::DescriptorSetLayout textureDescriptorSetLayout;
        vk::DescriptorPool textureDescriptorPool;
        std::vector<vk::DescriptorSet> textureDescriptorSets;
        bool textureDescriptorsInitialized = false;

        std::unique_ptr<MeshGPUCache> meshCache;
        std::unique_ptr<MaterialTextureCache> textureCache;
        std::unique_ptr<MaterialShaderCache> materialShaderCache;
        std::unique_ptr<MaterialParameterBufferCache> parameterBufferCache;

        vk::Buffer cameraUBO;
        core::VulkanAllocation cameraUBOAllocation;
        bool externalCameraBuffer = false;

        ibl::ImageData cachedIrradianceMap;
        ibl::ImageData cachedPrefilterMap;
        ibl::ImageData cachedBrdfLUT;
        uint64_t iblDescriptorVersion = 0;

        // VK-1577 — reflection probes at set 0, bindings 4 (cube array of MAX probes) and 5 (SSBO).
        //
        // `emptyProbeBuffer` is a zero-filled, full-size probe buffer that always exists. It is what
        // binding 5 points at before any probe manager appears, AND what every EXTERNAL set gets
        // (preview / render-texture / probe-capture) — its count of 0 is what structurally prevents
        // a probe capture from sampling the very cubes it is rendering into.
        //
        // Unset entries in `cachedProbeCubes` fall back to `cachedPrefilterMap` (the global
        // environment) rather than a flat default, so an un-baked probe degrades to the sky instead
        // of sampling garbage — and that image already has the right format, mip count and sampler.
        vk::Buffer emptyProbeBuffer;
        core::VulkanAllocation emptyProbeAllocation;
        vk::Buffer cachedProbeBuffer;  // null => emptyProbeBuffer
        std::array<ibl::ImageData, probe::MAX_REFLECTION_PROBES> cachedProbeCubes{};

        mutable float currentTime{0.0f};

        std::unique_ptr<MaterialCacheManager> materialCacheManager;
        material::CallbackId materialChangeCallbackId{};

        // VK-1532: async material-pipeline warm-up for this (scene) pipeline instance.
        std::unique_ptr<MaterialPipelineWarmup> pipelineWarmup;

        void registerMaterialChangeCallback();
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

        vk::DescriptorSetLayout getIBLDescriptorSetLayout() const { return descriptorSetLayout; }
        vk::DescriptorSet getIBLDescriptorSet(uint32_t /*imageIndex*/) const { return descriptorSet; }
        MaterialTextureCache& getMaterialTextureCache() { return *textureCache; }

        void beginRenderPass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        // Begin render pass with secondary command buffer support for parallel recording
        void beginRenderPassForSecondary(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void beginVFXRenderPass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void beginTransparencyPass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void beginWaterContinuePass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void endRenderPass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        // Graph-managed variants — identical to originals but without scene color image transitions
        // (the render graph handles layout transitions externally)
        void recordCommandBufferGraphManaged(const vk::CommandBuffer& commandBuffer,
                                          uint32_t imageIndex,
                                          const std::vector<MeshRenderData>& meshDrawList,
                                          const math::Frustum* frustum,
                                          render::DebugRenderer* debugRenderer,
                                          const glm::mat4& debugView,
                                          const glm::mat4& debugProjection) const;
        void beginRenderPassGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void beginRenderPassForSecondaryGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void beginVFXRenderPassGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void restoreDepthAfterVFX(const vk::CommandBuffer& commandBuffer) const;
        // VK-1604: water-only scope with depth bound read-only so water.glsl can sample it (set 9 b1).
        void beginWaterReadOnlyDepthPassGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void restoreDepthAfterWater(const vk::CommandBuffer& commandBuffer) const;
        void beginWaterContinuePassGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void endRenderPassGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        void injectMaterialForPreview(const std::string& materialPath,
                                      std::shared_ptr<material::MaterialData> materialData);

        std::string getLastShaderCompilationError() const;

        // VK-1532: kick off async warm-up of this scene's material pipelines (plus any extra
        // paths from a Phase-2 PSO manifest). Call on the main thread after scene load.
        void beginPipelineWarmup(std::vector<std::string> extraPaths = {});
        services::PipelineWarmupStats getPipelineWarmupStats() const;
        // Toggle the render-thread mid-frame guard on the material shader cache.
        void setFrameRecording(bool recording) const;

        void updatePreviewTextureDescriptors(
            uint32_t imageIndex,
            const std::array<vk::ImageView, material::MAX_MATERIAL_TEXTURES>& imageViews,
            const std::array<vk::Sampler, material::MAX_MATERIAL_TEXTURES>& samplers);

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time = 0.0f) const;

        void setExternalCameraBuffer(vk::Buffer buffer)
        {
            cameraUBO = buffer;
            externalCameraBuffer = true;
        }

        // VK-1334: build an additional IBL descriptor set that binds an external CameraUBO at
        // binding 0 (matching `descriptorSetLayout`), reusing the cached IBL images for bindings
        // 1-3. Used by RenderTextureViewPort so RTT passes don't write the shared CameraUBO.
        //
        // VK-1577: these sets always get the EMPTY probe buffer at binding 5. Off-screen passes must
        // not sample reflection probes — most importantly the probe capture itself, which renders the
        // scene into the very cubes bound at binding 4.
        vk::DescriptorSet createExternalIBLDescriptorSet(vk::Buffer externalCameraUBO,
                                                         vk::DescriptorPool externalPool) const;
        uint64_t getIBLDescriptorVersion() const { return iblDescriptorVersion; }

        // VK-1577: point set 0 bindings 4/5 at the probe manager's live cubes + SSBO. Pass a null
        // buffer to revert to the empty (count = 0) buffer. Cube slots left with a null imageView
        // fall back to the global prefilter map. Rewrites the main descriptor set in place; the
        // image VIEWS are stable for the manager's lifetime, so this is called on
        // allocation/teardown, never per frame.
        void setReflectionProbeResources(
            vk::Buffer probeBuffer,
            const std::array<ibl::ImageData, probe::MAX_REFLECTION_PROBES>& cubes);

        std::string loadMesh(std::string_view meshPath);
        std::string uploadMesh(const std::string& meshId, const resource::MeshesData& meshData);

        void unloadMesh(const std::string& meshId);

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

        void renderMeshList(const vk::CommandBuffer& commandBuffer,
                           uint32_t imageIndex,
                           const std::vector<MeshRenderData>& meshDrawList,
                           const math::Frustum* frustum) const;

    private:
        void unloadAllMeshes();

        void loadShaders();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createDescriptorSet(const ibl::ImageData& irradianceMap,
                                 const ibl::ImageData& prefilterMap,
                                 const ibl::ImageData& brdfLUT);
        void createCameraUBO();
        // VK-1577: the always-valid zero-filled probe buffer. Must run before any descriptor set is
        // written, since every set binds it (directly, or as the fallback for binding 5).
        void createEmptyProbeBuffer();
        // VK-1577: THE single place bindings 4 and 5 are written. Both descriptor-set allocation
        // paths funnel through it so the two can never drift apart.
        void writeProbeBindings(vk::DescriptorSet target, vk::Buffer probeBuffer) const;
        void createPipelineLayout();
        void createGraphicsPipeline();

        void createTextureDescriptorSetLayout();
        void createTextureDescriptorPool();
        void initializeDefaultTextureDescriptors();
        vk::DescriptorSet getTextureDescriptorSet(uint32_t imageIndex) const;

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
            vk::DescriptorSet defaultMaterialDescriptorSet = nullptr;
            uint32_t imageIndex = 0;
            std::string lastParameterMaterialPath;
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

        void bindSubmeshPipeline(
            const vk::CommandBuffer& commandBuffer,
            const ExtractedPBRValues& pbrValues,
            const std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& materialCache,
            RenderState& state) const;

        void bindSubmeshMaterial(
            const vk::CommandBuffer& commandBuffer,
            const ExtractedPBRValues& pbrValues,
            RenderState& state) const;

        void bindSubmeshParameters(
            const vk::CommandBuffer& commandBuffer,
            const ExtractedPBRValues& pbrValues,
            const std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& materialCache,
            RenderState& state) const;

        MeshPushConstants buildSubmeshPushConstants(
            const MeshRenderData& meshData,
            const SubMeshGPUData& subMesh,
            size_t subMeshIndex,
            const ExtractedPBRValues& pbrValues) const;
    };
}
