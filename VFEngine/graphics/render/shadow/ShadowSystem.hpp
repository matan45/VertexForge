#pragma once

#include "ShadowTypes.hpp"
#include "VSMTypes.hpp"
#include "VSMPhysicalTilePool.hpp"
#include "VSMPageTable.hpp"
#include "ShadowPassPipeline.hpp"
#include "ShadowGPUDataManager.hpp"
#include "ShadowPassRecorder.hpp"
#include "TerrainShadowPipeline.hpp"
#include "VSMFeedbackPipeline.hpp"
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
    class ThreadCommandPoolManager;
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

            // Shadow streaming: per-frame tile allocation budget
            static constexpr uint32_t MAX_NEW_PAGES_PER_FRAME = 32;
            uint32_t newPagesAllocatedThisFrame = 0;

            std::unique_ptr<VSMPhysicalTilePool> tilePool;
            std::unique_ptr<VSMPageTable> pageTable;
            std::unique_ptr<ShadowPassPipeline> shadowPassPipeline;
            std::unique_ptr<TerrainShadowPipeline> terrainShadowPipeline;
            std::unique_ptr<ShadowGPUDataManager> gpuDataManager;
            std::unique_ptr<ShadowPassRecorder> passRecorder;
            std::unique_ptr<VSMFeedbackPipeline> feedbackPipeline;
            std::unique_ptr<core::ThreadCommandPoolManager> threadPoolManager;

            // Feedback state
            std::vector<uint32_t> prevFrameFeedback;
            bool feedbackEnabled = true;
            bool feedbackHasResults = false;
            static constexpr uint32_t EVICTION_THRESHOLD = 60; // frames before freeing unused page (~1 second at 60fps)

            std::unordered_map<uint32_t, LightShadowData> lightShadowData;

            std::vector<ShadowView> directionalShadowViews;
            std::vector<ShadowView> pointShadowViews;
            std::vector<ShadowView> spotShadowViews;

            std::unordered_map<uint32_t, int32_t> entityToShadowIndex;

            // VSM page render lists (built each frame)
            std::vector<shadow::PageRenderEntry> pageRenderList;          // legacy: static lights (all objects)
            std::vector<shadow::PageRenderEntry> staticPageRenderList;    // dual-layer: static-only objects
            std::vector<shadow::PageRenderEntry> dynamicPageRenderList;   // dual-layer: dynamic-only objects
            std::vector<shadow::TileCopyEntry> tileCopyList;              // dual-layer: static->dynamic tile copies

            // A1: bounds of dynamic (non-ShadowStatic) shadow casters, refilled each frame by the
            // renderer (mutableDynamicCasterBounds) before beginFrame. determineDynamicPages marks
            // only the VSM pages these overlap as dynamic, instead of every page whenever any
            // dynamic object exists. Reused across frames (clear + fill, no per-frame realloc).
            struct DynamicCasterBound
            {
                glm::vec3 localMin;
                glm::vec3 localMax;
                glm::mat4 modelMatrix;   // clipFromBox = lightViewProjection * modelMatrix
            };
            std::vector<DynamicCasterBound> dynamicCasterBounds;

            bool shadowsEnabled = true;
            ShadowQuality globalQuality = ShadowQuality::High;
            bool globalSoftShadows = true;

            // When true, RT directional shadows are overriding the directional VSM clipmap
            // full-screen (the fragment shaders sample the RT mask and never read the clipmap),
            // so rendering the clipmap pages is pure waste — buildPageRenderList() skips
            // directional lights. Set per-frame by the renderer from the GPU light buffer's
            // runtime rtShadowActive flag (true only when RT actually produced a mask).
            bool directionalRTOverrideActive = false;

            // VK-1479 B1: page-binned directional shadow cull. When on, the directional clipmap
            // renders as single-layer "All" bin pages (per-page GPU cull) instead of dual-layer.
            // Default OFF; legacy path is the byte-identical fallback.
            bool shadowCullEnabled = false;
            bool binActiveThisFrame = false;         // a directional light produced bin pages this frame
            bool binBuiltThisFrame = false;          // B1 bins the first directional light only this frame
            uint32_t binPagesPerLevel = 0;           // directional pagesPerLevel (grid width)
            uint32_t binLevelCount = 0;              // directional clipmap level count
            std::vector<uint32_t> binPageBase;       // pageBinBase[pageLinear] = renderSlot or INVALID

            // Directional clipmap settings (applied at directional-light registration).
            uint32_t clipmapLevelCount = ShadowConstants::DEFAULT_CLIPMAP_LEVELS;
            float clipmapBaseExtent = ShadowConstants::DEFAULT_CLIPMAP_BASE_EXTENT;
            float clipmapDepthRange = ShadowConstants::DEFAULT_CLIPMAP_DEPTH_RANGE;
            float globalDepthBias = 0.005f;
            float globalSlopeBias = 1.5f;
            float globalNormalBias = 0.02f;
            bool initialized = false;
            bool needsUpdate = true;
            bool poolFirstUse = true;
            uint32_t frameCounter = 0;

            // Camera tracking for shadow priority
            glm::vec3 lastCameraPosition{0.0f};
            static constexpr float CAMERA_MOVE_EPSILON = 0.001f;

            // VSM light index counter
            uint32_t nextVSMLightIndex = 0;

            struct ShadowCacheStats
            {
                uint32_t totalStaticLights = 0;
                uint32_t cachedShadowMaps = 0;
                uint32_t renderedThisFrame = 0;
                uint32_t skippedThisFrame = 0;
                // Page-level cache stats
                uint32_t totalPages = 0;
                uint32_t renderedPages = 0;
                uint32_t cachedPages = 0;
                // Dual-layer stats
                uint32_t staticPagesRendered = 0;
                uint32_t dynamicPagesRendered = 0;
                uint32_t tileCopiesThisFrame = 0;
                uint32_t dynamicTilesAllocated = 0;
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

            // See directionalRTOverrideActive. Call before beginFrame() each frame; when true,
            // the directional clipmap pages are not rendered (RT overrides them full-screen).
            void setDirectionalRTOverrideActive(bool active) { directionalRTOverrideActive = active; }

            // A1: the renderer fills this reused buffer with dynamic shadow-caster bounds
            // (resolved GPU object data) each frame BEFORE beginFrame(); determineDynamicPages
            // consumes it to mark only the pages a dynamic caster overlaps.
            [[nodiscard]] std::vector<DynamicCasterBound>& mutableDynamicCasterBounds() { return dynamicCasterBounds; }

            // VK-1479 B1 page-binned cull. Toggling invalidates all pages so they re-render (and
            // the page table is repointed at the static tile) on the next frame.
            void setShadowCullEnabled(bool enabled);
            [[nodiscard]] bool isShadowCullEnabled() const { return shadowCullEnabled; }
            // Per-frame bin outputs consumed by the renderer's ShadowPageBinner (valid when active).
            [[nodiscard]] bool isShadowBinActive() const { return binActiveThisFrame; }
            [[nodiscard]] uint32_t getShadowBinPagesPerLevel() const { return binPagesPerLevel; }
            [[nodiscard]] uint32_t getShadowBinLevelCount() const { return binLevelCount; }
            [[nodiscard]] const std::vector<uint32_t>& getShadowBinPageBase() const { return binPageBase; }

            [[nodiscard]] ShadowQuality getGlobalQuality() const { return globalQuality; }

            [[nodiscard]] float getGlobalDepthBias() const { return globalDepthBias; }
            [[nodiscard]] float getGlobalNormalBias() const { return globalNormalBias; }
            [[nodiscard]] uint32_t getClipmapLevelCount() const { return clipmapLevelCount; }
            [[nodiscard]] float getClipmapBaseExtent() const { return clipmapBaseExtent; }
            [[nodiscard]] float getClipmapDepthRange() const { return clipmapDepthRange; }
            void applyRenderSettings(const types::RenderSettings& settings);

            // Shadow caching for static lights
            void invalidateStaticShadow(uint32_t entityId);
            void invalidateAllStaticShadows();
            void updateStaticFlags();

            // Scene change notification — marks ALL shadow pages dirty
            void notifySceneChanged();

            [[nodiscard]] ShadowCacheStats getShadowCacheStats() const;

            [[nodiscard]] uint32_t getActiveShadowCasterCount() const;
            [[nodiscard]] uint32_t getActiveShadowViewCount() const;
            [[nodiscard]] float getPoolUtilization() const;

            [[nodiscard]] VSMPhysicalTilePool* getTilePool() const { return tilePool.get(); }
            [[nodiscard]] bool isInitialized() const { return initialized; }

            void setLightBufferManager(lighting::GPULightBufferManager* manager) { lightBufferManager = manager; }

            [[nodiscard]] const std::vector<ShadowView>& getDirectionalShadowViews() const { return directionalShadowViews; }
            [[nodiscard]] const std::vector<ShadowView>& getPointShadowViews() const { return pointShadowViews; }
            [[nodiscard]] const std::vector<ShadowView>& getSpotShadowViews() const { return spotShadowViews; }

            [[nodiscard]] std::vector<ShadowDebugInfo> getShadowDebugInfo() const;
            [[nodiscard]] ShadowRecordingStats getShadowRecordingStats() const;

            struct PerLightStats
            {
                uint32_t entityId = 0;
                uint32_t type = 0; // 1=spot, 2=point
                uint32_t pagesAllocated = 0;
                uint32_t pagesDirty = 0;
                uint32_t pagesCached = 0;
            };
            [[nodiscard]] std::vector<PerLightStats> getPerLightStats() const;

            // GPU Feedback (Phase 2)
            void dispatchFeedback(vk::CommandBuffer cmd, vk::ImageView depthView,
                                  const glm::mat4& invViewProjection,
                                  uint32_t screenWidth, uint32_t screenHeight);
            void copyFeedbackToStaging(vk::CommandBuffer cmd);
            void markFeedbackReady();
            void readBackFeedback();
            [[nodiscard]] bool isFeedbackEnabled() const { return feedbackEnabled && initialized; }

            struct PageDimensions
            {
                uint32_t x = 0;
                uint32_t y = 0;
                bool valid = false;
            };

        private:
            void applyFeedbackAllocations();
            bool allocateVSMPages(LightShadowData& data);
            void freeVSMPages(LightShadowData& data);

            // Shadow streaming: evict lowest-priority page to make room (O(1) via pre-built cache)
            struct EvictionCandidate
            {
                float priority;
                uint32_t lastUsedFrame;
                uint32_t entityId;
                uint32_t pageIdx;
                bool operator>(const EvictionCandidate& o) const
                {
                    if (priority != o.priority) return priority > o.priority;
                    return lastUsedFrame > o.lastUsedFrame;
                }
            };
            std::vector<EvictionCandidate> evictionHeap;
            // A2: the eviction heap is rebuilt lazily. beginFrame only marks it dirty; the O(all
            // resident pages) rebuild happens on the first eviction request of a frame (reached
            // only when the physical tile pool is exhausted), so steady-state cost is zero.
            bool evictionHeapDirty = true;
            void markEvictionHeapDirty() { evictionHeapDirty = true; }
            void ensureEvictionHeap();
            void buildEvictionHeap();
            uint32_t evictLowestPriorityPage(float requestingPriority);

            // Per-type VSM page allocation helpers
            PageDimensions allocateSpotPages(LightShadowData& data, uint32_t maxPages);
            PageDimensions allocatePointPages(LightShadowData& data, uint32_t maxPages);
            bool allocatePhysicalTiles(LightShadowData& data, uint32_t pagesX, uint32_t pagesY);
            // Directional clipmap reserves a page-table block but defers physical tiles to
            // feedback (a full clipmap is too large to allocate eagerly).
            bool allocateDirectionalBlock(LightShadowData& data);
            [[nodiscard]] uint32_t clipmapPagesPerLevel() const;

            // Debug info helpers
            void addSingleViewDebugInfo(std::vector<ShadowDebugInfo>& infos, ShadowMapType type) const;

            // beginFrame helpers
            struct PointLightRef { LightShadowData* data; glm::mat4 worldMatrix; float radius; };
            struct SpotLightRef { LightShadowData* data; glm::mat4 worldMatrix; float outerAngle; float range; };
            struct DirectionalLightRef { LightShadowData* data; glm::vec3 direction; };

            void classifyLightsForUpdate(
                std::vector<PointLightRef>& pointLights,
                std::vector<SpotLightRef>& spotLights,
                std::vector<DirectionalLightRef>& directionalLights);
            void updateShadowCacheAfterRender();

            void updatePointCubeShadowMatrices(LightShadowData& data, uint32_t entityId);
            void updatePointCubeShadowMatricesFromData(LightShadowData& data,
                                                        const glm::mat4& worldMatrix, float radius);
            void updateSpotShadowMatrices(LightShadowData& data, uint32_t entityId);
            void updateSpotShadowMatricesFromData(LightShadowData& data,
                                                    const glm::mat4& worldMatrix,
                                                    float outerAngle, float range);
            void updateDirectionalShadowMatricesFromData(LightShadowData& data,
                                                         const glm::vec3& lightDirection);
            void collectShadowViewsForGPU(const std::unordered_set<uint32_t>* visibleLightIds);
            void buildPageRenderList();
            void determineDynamicPages();
            void markDynamicPagesForLight(LightShadowData& data);
            void buildSingleViewPageRenderList(LightShadowData& data);
            void buildPointPageRenderList(LightShadowData& data);
            void buildClipmapPageRenderList(LightShadowData& data);
            void addPageToRenderLists(LightShadowData& data, uint32_t pageIdx,
                                      const glm::mat4& cropViewProjection,
                                      const ShadowView& view, bool isDirty);
            void addStaticLightPage(LightShadowData& data, uint32_t pageIdx,
                                     const glm::mat4& cropVP, const ShadowView& view,
                                     bool isDirty, bool forceRender);
            void addDualLayerPage(LightShadowData& data, uint32_t pageIdx,
                                   const glm::mat4& cropVP, const ShadowView& view,
                                   bool isDirty, bool forceRender);
            // B1: single-layer "All" bin page. Renders into the static tile (repoints the page
            // table there), records the page for bin-slot assignment. Returns true if it rendered
            // (not cached), pushing pageLinear + the pageRenderList index into the out vectors.
            bool addBinPage(LightShadowData& data, uint32_t pageIdx, uint32_t pageLinear,
                            const glm::mat4& cropVP, const ShadowView& view, bool isDirty,
                            std::vector<uint32_t>& outPageLinears, std::vector<size_t>& outEntryIndices);
            void allocateDynamicTile(LightShadowData& data, uint32_t pageIdx);
            void freeDynamicTileIfExpired(LightShadowData& data, uint32_t pageIdx, uint32_t physTile);
            void allocateNonStaticLightPages(LightShadowData& data);
            void allocateStaticLightPages(LightShadowData& data, bool allowEviction);
        };
    }
}
