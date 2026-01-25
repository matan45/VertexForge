#pragma once

#include "ShadowTypes.hpp"
#include "ShadowAtlasManager.hpp"
#include "ShadowResourcePool.hpp"
#include "ShadowPassPipeline.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <unordered_map>

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

            // Collected shadow views for current frame (sorted by type for batching)
            std::vector<ShadowView> directionalShadowViews;
            std::vector<ShadowView> pointShadowViews;
            std::vector<ShadowView> spotShadowViews;

            // GPU shadow data array (for upload)
            std::vector<GPUShadowData> gpuShadowData;

            // Global settings
            bool shadowsEnabled = true;
            ShadowFilterMode globalFilterMode = ShadowFilterMode::PCF;
            ShadowQuality globalQuality = ShadowQuality::High;

            // State
            bool initialized = false;
            bool needsUpdate = true;
            uint32_t maxShadowCasters = ShadowConstants::MAX_TOTAL_SHADOW_VIEWS;

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
            void registerLight(uint32_t entityId, ShadowMapType type, const ShadowSettings& settings = {});
            void unregisterLight(uint32_t entityId);
            void updateLightSettings(uint32_t entityId, const ShadowSettings& settings);

            // Check if a light has shadow data
            [[nodiscard]] bool hasLightShadow(uint32_t entityId) const;
            [[nodiscard]] const LightShadowData* getLightShadowData(uint32_t entityId) const;
            [[nodiscard]] LightShadowData* getLightShadowData(uint32_t entityId);

            // ===== Frame Update =====

            // Call each frame before shadow pass recording
            // Collects active shadow casters and updates matrices
            void beginFrame();

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
            };

            // Record shadow pass commands
            void recordShadowPass(vk::CommandBuffer cmd, const ShadowPassParams& params);

            // ===== Descriptor Access =====

            // For binding to forward shading pipeline
            [[nodiscard]] vk::DescriptorSetLayout getAtlasDescriptorLayout() const;
            [[nodiscard]] vk::DescriptorSet getAtlasDescriptorSet() const;
            [[nodiscard]] vk::DescriptorSetLayout getShadowDataLayout() const { return shadowDataLayout; }
            [[nodiscard]] vk::DescriptorSet getShadowDataDescSet() const { return shadowDataDescSet; }

            // ===== Global Settings =====

            void setShadowsEnabled(bool enabled) { shadowsEnabled = enabled; needsUpdate = true; }
            [[nodiscard]] bool isShadowsEnabled() const { return shadowsEnabled; }

            void setGlobalFilterMode(ShadowFilterMode mode) { globalFilterMode = mode; needsUpdate = true; }
            [[nodiscard]] ShadowFilterMode getGlobalFilterMode() const { return globalFilterMode; }

            void setGlobalQuality(ShadowQuality quality);
            [[nodiscard]] ShadowQuality getGlobalQuality() const { return globalQuality; }

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

        private:
            void createShadowDataBuffer();
            void destroyShadowDataBuffer();
            void createDescriptorResources();
            void updateDescriptorSet();

            // Allocate shadow map tiles in atlas for a light
            bool allocateShadowMaps(LightShadowData& data);
            void freeShadowMaps(LightShadowData& data);

            // Build GPU shadow data array from current shadow views
            void buildGPUShadowData();
        };
    }
}
