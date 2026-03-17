#pragma once

#include "ShadowTypes.hpp"
#include "VSMTypes.hpp"
#include "VSMPhysicalTilePool.hpp"
#include "VSMPageTable.hpp"
#include "ShadowResourcePool.hpp"
#include "ShadowPassPipeline.hpp"
#include "ShadowGPUDataManager.hpp"
#include "ShadowPassRecorder.hpp"
#include "TerrainShadowPipeline.hpp"
#include "types/RenderSettings.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace core
{
    class Device;
    class DeferredDeletionQueue;
}

namespace render
{
    namespace lighting
    {
        class GPULightBufferManager;
    }

    namespace shadow
    {
        class ShadowSystem
        {
        private:
            core::Device& device;

            static constexpr float FRAME_BUDGET_WARNING_MS = 16.0f;

            std::unique_ptr<VSMPhysicalTilePool> tilePool;
            std::unique_ptr<VSMPageTable> pageTable;
            std::unique_ptr<ShadowResourcePool> resourcePool;
            std::unique_ptr<ShadowPassPipeline> shadowPassPipeline;
            std::unique_ptr<TerrainShadowPipeline> terrainShadowPipeline;
            std::unique_ptr<ShadowGPUDataManager> gpuDataManager;
            std::unique_ptr<ShadowPassRecorder> passRecorder;

            std::unordered_map<uint32_t, LightShadowData> lightShadowData;

            std::vector<ShadowView> directionalShadowViews;
            std::vector<ShadowView> pointShadowViews;
            std::vector<ShadowView> spotShadowViews;

            std::unordered_map<uint32_t, int32_t> entityToShadowIndex;

            // VSM page render list (built each frame)
            std::vector<shadow::PageRenderEntry> pageRenderList;

            bool shadowsEnabled = true;
            ShadowQuality globalQuality = ShadowQuality::High;
            bool globalSoftShadows = true;
            float globalDepthBias = 0.005f;
            float globalSlopeBias = 1.5f;
            float globalNormalBias = 0.02f;
            uint8_t globalCascadeCount = 4;
            types::CascadeSplitMode globalCascadeSplitMode = types::CascadeSplitMode::Practical;

            bool initialized = false;
            bool needsUpdate = true;
            bool poolFirstUse = true;
            uint32_t frameCounter = 0;

            // VSM light index counter
            uint32_t nextVSMLightIndex = 0;

            struct ShadowCacheStats
            {
                uint32_t totalStaticLights = 0;
                uint32_t cachedShadowMaps = 0;
                uint32_t renderedThisFrame = 0;
                uint32_t skippedThisFrame = 0;
            };

            mutable ShadowCacheStats lastCacheStats{};

            lighting::GPULightBufferManager* lightBufferManager = nullptr;

        public:
            explicit ShadowSystem(core::Device& device);
            ~ShadowSystem();

            ShadowSystem(const ShadowSystem&) = delete;
            ShadowSystem& operator=(const ShadowSystem&) = delete;

            void init();
            void cleanup();

            void initShadowPass(vk::DescriptorSetLayout perDrawLayout,
                                vk::DescriptorSetLayout meshletDataLayout,
                                vk::DescriptorSetLayout vertexDataLayout,
                                vk::DescriptorSetLayout boneMatrixLayout);

            void updateCameraDescriptor(vk::Buffer cameraBuffer, vk::DeviceSize bufferSize);
            [[nodiscard]] vk::DescriptorSet getShadowCameraDescSet() const;

            void initTerrainShadowPass(vk::DescriptorSetLayout terrainDataLayout,
                                        vk::DescriptorSetLayout terrainMeshletLayout,
                                        vk::DescriptorSetLayout terrainVertexLayout);

            [[nodiscard]] bool registerLight(uint32_t entityId, ShadowMapType type, const ShadowSettings& settings = {});
            void unregisterLight(uint32_t entityId);

            [[nodiscard]] int32_t getShadowViewIndex(uint32_t entityId) const;

            void beginFrame(const glm::mat4& cameraView,
                            const glm::mat4& cameraProjection,
                            float cameraNear,
                            float cameraFar,
                            const std::unordered_set<uint32_t>* visibleLightIds = nullptr);

            void uploadToGPU(vk::CommandBuffer cmd);

            void recordShadowPass(vk::CommandBuffer cmd,
                                   const ShadowPassParams& params,
                                   const TerrainShadowPassParams* terrainParams = nullptr);

            [[nodiscard]] vk::DescriptorSetLayout getShadowDataLayout() const;
            [[nodiscard]] vk::DescriptorSet getShadowDataDescSet() const;
            [[nodiscard]] vk::DescriptorSetLayout getShadowTextureLayout() const;
            [[nodiscard]] vk::DescriptorSet getShadowTextureDescSet() const;

            [[nodiscard]] bool isShadowsEnabled() const { return shadowsEnabled; }

            [[nodiscard]] ShadowQuality getGlobalQuality() const { return globalQuality; }

            [[nodiscard]] float getGlobalDepthBias() const { return globalDepthBias; }
            [[nodiscard]] float getGlobalNormalBias() const { return globalNormalBias; }
            [[nodiscard]] uint8_t getGlobalCascadeCount() const { return globalCascadeCount; }

            void applyRenderSettings(const types::RenderSettings& settings);

            // Shadow caching for static lights
            void invalidateStaticShadow(uint32_t entityId);
            void invalidateAllStaticShadows();
            void updateStaticFlags();

            [[nodiscard]] ShadowCacheStats getShadowCacheStats() const;

            [[nodiscard]] uint32_t getActiveShadowCasterCount() const;
            [[nodiscard]] uint32_t getActiveShadowViewCount() const;
            [[nodiscard]] float getPoolUtilization() const;

            [[nodiscard]] VSMPhysicalTilePool* getTilePool() const { return tilePool.get(); }
            [[nodiscard]] bool isInitialized() const { return initialized; }

            void setLightBufferManager(lighting::GPULightBufferManager* manager) { lightBufferManager = manager; }
            void setDeletionQueue(core::DeferredDeletionQueue* queue);

            [[nodiscard]] const std::vector<ShadowView>& getDirectionalShadowViews() const { return directionalShadowViews; }
            [[nodiscard]] const std::vector<ShadowView>& getPointShadowViews() const { return pointShadowViews; }
            [[nodiscard]] const std::vector<ShadowView>& getSpotShadowViews() const { return spotShadowViews; }

            [[nodiscard]] std::vector<ShadowDebugInfo> getShadowDebugInfo() const;

        private:
            bool allocateVSMPages(LightShadowData& data);
            void freeVSMPages(LightShadowData& data);

            void updatePointCubeShadowMatrices(LightShadowData& data, uint32_t entityId);
            void updatePointCubeShadowMatricesFromData(LightShadowData& data,
                                                        const glm::mat4& worldMatrix, float radius);
            void updateSpotShadowMatrices(LightShadowData& data, uint32_t entityId);
            void updateSpotShadowMatricesFromData(LightShadowData& data,
                                                    const glm::mat4& worldMatrix,
                                                    float outerAngle, float range);
            void updateDirectionalCSMMatrices(LightShadowData& data, uint32_t entityId,
                                               const glm::mat4& cameraView, const glm::mat4& cameraProjection,
                                               float cameraNear, float cameraFar);
            void updateDirectionalCSMMatricesFromData(LightShadowData& data,
                                                       const glm::mat4& worldMatrix,
                                                       const glm::mat4& cameraView, const glm::mat4& cameraProjection,
                                                       float cameraNear, float cameraFar);
            void collectShadowViewsForGPU(const std::unordered_set<uint32_t>* visibleLightIds);
            void buildPageRenderList();
        };
    }
}
