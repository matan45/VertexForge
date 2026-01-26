#pragma once

#include "ShadowTypes.hpp"
#include "ShadowAtlasManager.hpp"
#include "ShadowResourcePool.hpp"
#include "ShadowPassPipeline.hpp"
#include "CascadeShadowCalculator.hpp"
#include "PointShadowCalculator.hpp"
#include "SpotShadowCalculator.hpp"
#include "types/RenderSettings.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace core
{
    class Device;
    class SwapChain;
}

namespace render
{
    namespace lighting
    {
        class GPULightBufferManager;
    }

    namespace shadow
    {
        /**
         * ShadowSystem - Central coordinator for shadow rendering.
         *
         * Responsibilities:
         * - Shadow pass orchestration (when shadow passes are implemented)
         * - Shadow atlas management via ShadowAtlasManager
         * - Per-light shadow metadata tracking
         * - Integration point for shadow rendering pipelines
         *
         * This class provides the framework for future shadow implementation.
         * Actual shadow pass rendering will be added in subsequent tasks.
         */
        class ShadowSystem
        {
        private:
            core::Device& device;
            core::SwapChain& swapChain;

            // Sub-systems
            std::unique_ptr<ShadowAtlasManager> atlasManager;
            std::unique_ptr<ShadowResourcePool> resourcePool;
            std::unique_ptr<ShadowPassPipeline> shadowPassPipeline;

            // Per-light shadow data (entityId -> shadow data)
            std::unordered_map<uint32_t, LightShadowData> lightShadowData;

            // GPU buffer for shadow matrices (consumed by forward pass)
            vk::Buffer shadowDataBuffer;
            vk::DeviceMemory shadowDataMemory;
            vk::Buffer shadowDataStagingBuffer;
            vk::DeviceMemory shadowDataStagingMemory;
            void* shadowDataMapped = nullptr;

            // Descriptor resources for shadow data UBO
            vk::DescriptorSetLayout shadowDataLayout;
            vk::DescriptorPool shadowDataPool;
            vk::DescriptorSet shadowDataDescSet;

            // Descriptor resources for shadow textures (atlas + CSM array + point cubes)
            vk::DescriptorSetLayout shadowTextureLayout;
            vk::DescriptorPool shadowTexturePool;
            vk::DescriptorSet shadowTextureDescSet;

            // Collected shadow views for current frame (sorted by type for batching)
            std::vector<ShadowView> directionalShadowViews;
            std::vector<ShadowView> pointShadowViews;
            std::vector<ShadowView> spotShadowViews;

            // GPU shadow data array (for upload)
            std::vector<GPUShadowData> gpuShadowData;

            // Entity to shadow index mapping (updated each frame in buildGPUShadowData)
            // Maps entityId -> base index in gpuShadowData for that light's first view
            std::unordered_map<uint32_t, int32_t> entityToShadowIndex;

            // Global settings
            bool shadowsEnabled = true;
            ShadowFilterMode globalFilterMode = ShadowFilterMode::PCF;
            ShadowQuality globalQuality = ShadowQuality::High;
            uint8_t globalPcfKernel = 1;           // PCF kernel radius (0=none, 1=3x3, 2=5x5, 3=7x7)
            bool globalSoftShadowsEnabled = true;  // Global soft shadows toggle

            // State
            bool initialized = false;
            bool needsUpdate = true;
            bool atlasFirstUse = true;  // Track if atlas needs initial layout transition
            uint32_t maxShadowCasters = ShadowConstants::MAX_TOTAL_SHADOW_VIEWS;

            // Pending cube face framebuffers for deferred destruction (destroyed next frame)
            std::vector<vk::Framebuffer> pendingCubeFramebuffers;

            // External references (not owned)
            lighting::GPULightBufferManager* lightBufferManager = nullptr;

        public:
            explicit ShadowSystem(core::Device& device, core::SwapChain& swapChain);
            ~ShadowSystem();

            ShadowSystem(const ShadowSystem&) = delete;
            ShadowSystem& operator=(const ShadowSystem&) = delete;

            void init();
            void cleanup();
            void recreate();

            // Initialize shadow pass pipeline (call after init)
            void initShadowPass(vk::DescriptorSetLayout perDrawLayout,
                                vk::DescriptorSetLayout meshletDataLayout,
                                vk::DescriptorSetLayout vertexDataLayout,
                                vk::DescriptorSetLayout boneMatrixLayout);

            // ===== Light Shadow Registration =====

            // Register a light for shadow casting
            // Returns true if registration succeeded, false if allocation failed
            [[nodiscard]] bool registerLight(uint32_t entityId, ShadowMapType type, const ShadowSettings& settings = {});
            void unregisterLight(uint32_t entityId);
            void updateLightSettings(uint32_t entityId, const ShadowSettings& settings);

            // Check if a light has shadow data
            [[nodiscard]] bool hasLightShadow(uint32_t entityId) const;
            [[nodiscard]] const LightShadowData* getLightShadowData(uint32_t entityId) const;
            [[nodiscard]] LightShadowData* getLightShadowData(uint32_t entityId);

            // Get the base shadow view index for a light in the GPU shadow data buffer
            // Returns -1 if the light has no shadow or shadows are disabled
            [[nodiscard]] int32_t getShadowViewIndex(uint32_t entityId) const;

            // ===== Frame Update =====

            // Call each frame before shadow pass recording
            // Collects active shadow casters and updates matrices
            // Camera parameters are required for CSM cascade calculations
            // If visibleLightIds is provided, only collects shadows for visible lights
            // (directional lights are always included as they're global)
            void beginFrame(const glm::mat4& cameraView,
                            const glm::mat4& cameraProjection,
                            float cameraNear,
                            float cameraFar,
                            const std::unordered_set<uint32_t>* visibleLightIds = nullptr);

            // Update shadow view matrices for a specific light
            // Called when light transform or camera changes
            void updateLightShadowMatrices(uint32_t entityId,
                                           const glm::mat4& cameraView,
                                           const glm::mat4& cameraProjection,
                                           float cameraNear,
                                           float cameraFar);

            // Finalize and upload shadow data to GPU
            void uploadToGPU(vk::CommandBuffer cmd);

            // ===== Shadow Pass Recording =====

            // Parameters for shadow pass rendering
            struct ShadowPassParams
            {
                vk::DescriptorSet perDrawDataDescSet;
                vk::DescriptorSet meshletDataDescSet;
                vk::DescriptorSet vertexDataDescSet;
                vk::DescriptorSet boneMatrixDescSet;
                vk::Buffer drawCommandBuffer;
                vk::Buffer drawCountBuffer;
                uint32_t batchCount;
                uint32_t commandsPerSection;
                uint32_t shaderGroupCount;      // Number of shader groups to iterate
                uint32_t drawCountStructSize;   // Size of BatchDrawStats struct (for count offset calculation)
            };

            // Record shadow pass commands
            void recordShadowPass(vk::CommandBuffer cmd, const ShadowPassParams& params);

            // ===== Descriptor Access =====

            // For binding to forward shading pipeline
            [[nodiscard]] vk::DescriptorSetLayout getAtlasDescriptorLayout() const;
            [[nodiscard]] vk::DescriptorSet getAtlasDescriptorSet() const;
            [[nodiscard]] vk::DescriptorSetLayout getShadowDataLayout() const { return shadowDataLayout; }
            [[nodiscard]] vk::DescriptorSet getShadowDataDescSet() const { return shadowDataDescSet; }
            [[nodiscard]] vk::DescriptorSetLayout getShadowTextureLayout() const { return shadowTextureLayout; }
            [[nodiscard]] vk::DescriptorSet getShadowTextureDescSet() const { return shadowTextureDescSet; }

            // ===== Global Settings =====

            void setShadowsEnabled(bool enabled) { shadowsEnabled = enabled; needsUpdate = true; }
            [[nodiscard]] bool isShadowsEnabled() const { return shadowsEnabled; }

            void setGlobalFilterMode(ShadowFilterMode mode) { globalFilterMode = mode; needsUpdate = true; }
            [[nodiscard]] ShadowFilterMode getGlobalFilterMode() const { return globalFilterMode; }

            void setGlobalQuality(ShadowQuality quality);
            [[nodiscard]] ShadowQuality getGlobalQuality() const { return globalQuality; }

            // Apply render settings (may trigger atlas resize and reallocation)
            void applyRenderSettings(const types::RenderSettings& settings);

            // ===== Statistics =====

            [[nodiscard]] uint32_t getActiveShadowCasterCount() const;
            [[nodiscard]] uint32_t getActiveShadowViewCount() const;
            [[nodiscard]] float getAtlasUtilization() const;

            // ===== Accessors =====

            [[nodiscard]] ShadowAtlasManager* getAtlasManager() const { return atlasManager.get(); }
            [[nodiscard]] ShadowResourcePool* getResourcePool() const { return resourcePool.get(); }
            [[nodiscard]] bool isInitialized() const { return initialized; }

            void setLightBufferManager(lighting::GPULightBufferManager* manager) { lightBufferManager = manager; }

            // Get shadow views for rendering (sorted by type)
            [[nodiscard]] const std::vector<ShadowView>& getDirectionalShadowViews() const { return directionalShadowViews; }
            [[nodiscard]] const std::vector<ShadowView>& getPointShadowViews() const { return pointShadowViews; }
            [[nodiscard]] const std::vector<ShadowView>& getSpotShadowViews() const { return spotShadowViews; }

            // Get debug visualization info for all active shadow casters
            [[nodiscard]] std::vector<ShadowDebugInfo> getShadowDebugInfo() const;

        private:
            void createShadowDataBuffer();
            void destroyShadowDataBuffer();
            void createDescriptorResources();
            void updateDescriptorSet();
            void createShadowTextureDescriptor();
            void updateShadowTextureDescriptor();
            void destroyShadowTextureDescriptor();

            // Allocate shadow map tiles in atlas for a light
            bool allocateShadowMaps(LightShadowData& data);
            void freeShadowMaps(LightShadowData& data);

            // Render point light cube map shadows
            void renderPointLightCubeShadows(vk::CommandBuffer cmd, const ShadowPassParams& params);

            // Build GPU shadow data array from current shadow views
            void buildGPUShadowData();
        };
    }
}
