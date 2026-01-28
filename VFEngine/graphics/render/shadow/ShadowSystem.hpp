#pragma once

#include "ShadowTypes.hpp"
#include "ShadowAtlasManager.hpp"
#include "ShadowResourcePool.hpp"
#include "ShadowPassPipeline.hpp"
#include "ShadowGPUDataManager.hpp"
#include "ShadowPassRecorder.hpp"
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

            std::unique_ptr<ShadowAtlasManager> atlasManager;
            std::unique_ptr<ShadowResourcePool> resourcePool;
            std::unique_ptr<ShadowPassPipeline> shadowPassPipeline;
            std::unique_ptr<ShadowGPUDataManager> gpuDataManager;
            std::unique_ptr<ShadowPassRecorder> passRecorder;

            std::unordered_map<uint32_t, LightShadowData> lightShadowData;

            std::vector<ShadowView> directionalShadowViews;
            std::vector<ShadowView> pointShadowViews;
            std::vector<ShadowView> spotShadowViews;

            std::unordered_map<uint32_t, int32_t> entityToShadowIndex;

            bool shadowsEnabled = true;
            ShadowQuality globalQuality = ShadowQuality::High;
            uint8_t globalPcfKernel = 1;
            bool globalSoftShadowsEnabled = true;
            float globalDepthBias = 0.005f;
            float globalSlopeBias = 1.5f;
            float globalNormalBias = 0.02f;
            uint8_t globalCascadeCount = 4;
            types::CascadeSplitMode globalCascadeSplitMode = types::CascadeSplitMode::Practical;

            bool initialized = false;
            bool needsUpdate = true;

            lighting::GPULightBufferManager* lightBufferManager = nullptr;

        public:
            explicit ShadowSystem(core::Device& device);
            ~ShadowSystem();

            ShadowSystem(const ShadowSystem&) = delete;
            ShadowSystem& operator=(const ShadowSystem&) = delete;

            void init();
            void cleanup();
            void recreate();

            void initShadowPass(vk::DescriptorSetLayout perDrawLayout,
                                vk::DescriptorSetLayout meshletDataLayout,
                                vk::DescriptorSetLayout vertexDataLayout,
                                vk::DescriptorSetLayout boneMatrixLayout);

            // ===== Light Shadow Registration =====

            [[nodiscard]] bool registerLight(uint32_t entityId, ShadowMapType type, const ShadowSettings& settings = {});
            void unregisterLight(uint32_t entityId);

            [[nodiscard]] bool hasLightShadow(uint32_t entityId) const;
            [[nodiscard]] const LightShadowData* getLightShadowData(uint32_t entityId) const;
            [[nodiscard]] LightShadowData* getLightShadowData(uint32_t entityId);

            [[nodiscard]] int32_t getShadowViewIndex(uint32_t entityId) const;

            // ===== Frame Update =====

            void beginFrame(const glm::mat4& cameraView,
                            const glm::mat4& cameraProjection,
                            float cameraNear,
                            float cameraFar,
                            const std::unordered_set<uint32_t>* visibleLightIds = nullptr);

            void uploadToGPU(vk::CommandBuffer cmd);

            // ===== Shadow Pass Recording =====

            void recordShadowPass(vk::CommandBuffer cmd, const ShadowPassParams& params);

            // ===== Descriptor Access =====

            [[nodiscard]] vk::DescriptorSetLayout getAtlasDescriptorLayout() const;
            [[nodiscard]] vk::DescriptorSet getAtlasDescriptorSet() const;
            [[nodiscard]] vk::DescriptorSetLayout getShadowDataLayout() const;
            [[nodiscard]] vk::DescriptorSet getShadowDataDescSet() const;
            [[nodiscard]] vk::DescriptorSetLayout getShadowTextureLayout() const;
            [[nodiscard]] vk::DescriptorSet getShadowTextureDescSet() const;

            // ===== Global Settings =====

            void setShadowsEnabled(bool enabled) { shadowsEnabled = enabled; needsUpdate = true; }
            [[nodiscard]] bool isShadowsEnabled() const { return shadowsEnabled; }

            [[nodiscard]] ShadowQuality getGlobalQuality() const { return globalQuality; }

            [[nodiscard]] float getGlobalDepthBias() const { return globalDepthBias; }
            [[nodiscard]] float getGlobalSlopeBias() const { return globalSlopeBias; }
            [[nodiscard]] float getGlobalNormalBias() const { return globalNormalBias; }
            [[nodiscard]] uint8_t getGlobalCascadeCount() const { return globalCascadeCount; }

            void applyRenderSettings(const types::RenderSettings& settings);

            // ===== Statistics =====

            [[nodiscard]] uint32_t getActiveShadowCasterCount() const;
            [[nodiscard]] uint32_t getActiveShadowViewCount() const;
            [[nodiscard]] float getAtlasUtilization() const;

            // ===== Accessors =====

            [[nodiscard]] ShadowAtlasManager* getAtlasManager() const { return atlasManager.get(); }
            [[nodiscard]] bool isInitialized() const { return initialized; }

            void setLightBufferManager(lighting::GPULightBufferManager* manager) { lightBufferManager = manager; }
            void setDeletionQueue(core::DeferredDeletionQueue* queue);

            [[nodiscard]] const std::vector<ShadowView>& getDirectionalShadowViews() const { return directionalShadowViews; }
            [[nodiscard]] const std::vector<ShadowView>& getPointShadowViews() const { return pointShadowViews; }
            [[nodiscard]] const std::vector<ShadowView>& getSpotShadowViews() const { return spotShadowViews; }

            [[nodiscard]] std::vector<ShadowDebugInfo> getShadowDebugInfo() const;

        private:
            bool allocateShadowMaps(LightShadowData& data);
            void freeShadowMaps(LightShadowData& data);
        };
    }
}
