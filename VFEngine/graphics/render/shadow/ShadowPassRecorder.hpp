#pragma once

#include "ShadowTypes.hpp"
#include "ShadowPassPipeline.hpp"
#include "VSMTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace core
{
    class Device;
    class ThreadCommandPoolManager;
}

namespace render::shadow
{
    class VSMPhysicalTilePool;
    class ShadowPassPipeline;
    class TerrainShadowPipeline;

    struct ShadowPassParams
    {
        vk::DescriptorSet perDrawDataDescSet;
        vk::DescriptorSet meshletDataDescSet;
        vk::DescriptorSet vertexDataDescSet;
        vk::DescriptorSet boneMatrixDescSet;
        vk::DescriptorSet cameraDescSet;
        vk::Buffer drawCommandBuffer;
        vk::Buffer drawCountBuffer;
        uint32_t batchCount;
        uint32_t commandsPerSection;
        uint32_t shaderGroupCount;
        uint32_t transparentGroupIndex;
        uint32_t drawCountStructSize;

        // Tier 4: per-shadow-view GPU culling. When usePerViewShadowCull is set, perDrawDataDescSet
        // is the shadow-cull variant (b0 -> compacted shadow perDrawData), and each page issues a
        // single indirect-count draw over its view's region of shadowCullDrawCommandBuffer instead
        // of looping batch x shaderGroup sections. Pages with viewSlot >= shadowCullActiveViews
        // (overflow beyond SHADOW_CULL_MAX_VIEWS, or an unmapped view) fall back to the legacy
        // main-camera buffer + legacyPerDrawDataDescSet so their shadows are never dropped.
        bool usePerViewShadowCull = false;
        vk::Buffer shadowCullDrawCommandBuffer;
        vk::Buffer shadowCullDrawCountBuffer;
        uint32_t shadowCullDrawsPerView = 0;
        uint32_t shadowCullActiveViews = 0;
        // Main-camera-culled perDrawData set (pairs with drawCommandBuffer/drawCountBuffer above).
        // In per-view mode this is the set-0 the recorder rebinds for the legacy fallback of views
        // that were not GPU-culled into the compacted buffer. In legacy mode it equals
        // perDrawDataDescSet.
        vk::DescriptorSet legacyPerDrawDataDescSet;
    };

    enum class ShadowLayer : uint8_t
    {
        All,
        Static,
        Dynamic
    };

    struct PageRenderEntry
    {
        uint32_t physicalTileIndex;
        glm::mat4 cropViewProjection;
        float depthBias;
        float slopeBias;
        float normalBias;
        ShadowLayer layer = ShadowLayer::All;
        // Tier 4: global shadow-view index (directional levels, then point faces, then spot) used
        // to select this page's region of the compacted shadow buffer. UINT32_MAX = unknown.
        uint32_t viewSlot = UINT32_MAX;
    };

    struct TileCopyEntry
    {
        uint32_t srcTileIndex;
        uint32_t dstTileIndex;
    };

    struct ShadowRecordingStats
    {
        float recordingUs = 0.0f;
        uint32_t tileCount = 0;
        uint32_t threadsUsed = 0;
        bool usedParallel = false;
    };

    struct ShadowPassContext
    {
        const ShadowPassParams& params;
        const TerrainShadowPassParams* terrainParams;
        VSMPhysicalTilePool* tilePool;
        ShadowPassPipeline* shadowPassPipeline;
        TerrainShadowPipeline* terrainShadowPipeline;
        const std::vector<PageRenderEntry>& pageRenderList;
        const std::vector<PageRenderEntry>& staticPageRenderList;
        const std::vector<PageRenderEntry>& dynamicPageRenderList;
        const std::vector<TileCopyEntry>& tileCopyList;
        std::unordered_map<uint32_t, LightShadowData>& lightShadowData;
        bool shadowsEnabled;
        bool poolFirstUse;
    };

    struct ShadowPassPrerequisites
    {
        bool hasTerrainShadows = false;
        bool hasPageViews = false;
        bool hasMeshBatches = false;
    };

    class ShadowPassRecorder
    {
    private:
        core::Device& device;
        ShadowRecordingStats lastStats{};

    public:
        explicit ShadowPassRecorder(core::Device& device);
        ~ShadowPassRecorder() = default;

        ShadowPassRecorder(const ShadowPassRecorder&) = delete;
        ShadowPassRecorder& operator=(const ShadowPassRecorder&) = delete;

        void recordShadowPass(vk::CommandBuffer cmd, const ShadowPassContext& ctx);

        void recordShadowPassParallel(
            vk::CommandBuffer cmd,
            const ShadowPassContext& ctx,
            core::ThreadCommandPoolManager* threadPoolManager,
            uint32_t frameIndex);

        const ShadowRecordingStats& getLastStats() const { return lastStats; }

    private:
        bool validatePrerequisites(const ShadowPassContext& ctx,
                                   ShadowPassPrerequisites& out) const;

        void beginDynamicShadowPass(vk::CommandBuffer cmd,
                                    VSMPhysicalTilePool* tilePool,
                                    bool clearDepth);

        void endDynamicShadowPass(vk::CommandBuffer cmd);

        void bindShadowPipelineAndSets(vk::CommandBuffer cmd,
                                       const ShadowPassContext& ctx);

        static void executeTileCopies(vk::CommandBuffer cmd,
                                      VSMPhysicalTilePool* tilePool,
                                      const std::vector<TileCopyEntry>& copies);

        void recordTileCommands(vk::CommandBuffer cmd,
                                const PageRenderEntry& page,
                                const ShadowPassContext& ctx,
                                bool clearTile);

        static void clearTileDepth(vk::CommandBuffer cmd,
                                   const vk::Rect2D& scissor);

        static ShadowPushConstants buildTilePushConstants(
            const PageRenderEntry& page);

        void recordStaticPhase(vk::CommandBuffer cmd,
                               const ShadowPassContext& ctx,
                               const ShadowPassPrerequisites& prereq,
                               bool clearDepth);

        void recordDynamicPhase(vk::CommandBuffer cmd,
                                const ShadowPassContext& ctx,
                                const ShadowPassPrerequisites& prereq);

        void dispatchMeshBatches(vk::CommandBuffer cmd,
                                 const ShadowPassContext& ctx,
                                 ShadowPushConstants& pc,
                                 const PageRenderEntry& page);

        void dispatchLegacyMeshBatches(vk::CommandBuffer cmd,
                                       const ShadowPassContext& ctx,
                                       ShadowPushConstants& pc);

        void bindPerDrawDataSet(vk::CommandBuffer cmd,
                                const ShadowPassContext& ctx,
                                vk::DescriptorSet set);

        void dispatchTerrainShadow(vk::CommandBuffer cmd,
                                   const ShadowPassContext& ctx,
                                   const glm::mat4& viewProj,
                                   float depthBias,
                                   float slopeBias);

        struct ParallelDispatchArgs
        {
            vk::CommandBuffer primaryCmd;
            const ShadowPassContext* ctx;
            const ShadowPassPrerequisites* prereq;
            core::ThreadCommandPoolManager* threadPoolManager;
            uint32_t frameIndex;
        };

        void recordSecondaryTileCommands(
            const ParallelDispatchArgs& args,
            const std::vector<PageRenderEntry>& pages,
            bool clearTiles,
            uint32_t slot,
            std::vector<vk::CommandBuffer>& secondaryBuffers,
            std::vector<bool>& threadUsed);

        void dispatchPagesParallel(
            const ParallelDispatchArgs& args,
            const std::vector<PageRenderEntry>& pages,
            bool clearTiles,
            uint32_t slot);

        void recordStaticPhaseParallel(
            const ParallelDispatchArgs& args,
            bool clearDepth);

        void recordDynamicPhaseParallel(
            const ParallelDispatchArgs& args);

        static void transitionPoolToDepthAttachment(vk::CommandBuffer cmd,
                                                     VSMPhysicalTilePool* tilePool,
                                                     bool poolFirstUse);
        static void transitionPoolToShaderRead(vk::CommandBuffer cmd,
                                                VSMPhysicalTilePool* tilePool);
    };
}
