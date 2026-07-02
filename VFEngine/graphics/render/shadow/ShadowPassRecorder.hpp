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
        // A4: per-section (batch*shaderGroupCount + sg) occupancy superset from the batch manager.
        // When non-null, the legacy shadow loop skips sections with no candidates — occupancy is
        // camera-independent so it is valid for shadow views. null => draw every section (legacy).
        const std::vector<uint8_t>* sectionOccupancy = nullptr;

        // VK-1479 B1: page-binned shadow cull. When shadowCullEnabled, a PageRenderEntry with a
        // valid binSlot is drawn from binCommandBuffer/binCountBuffer (one indirect-count draw at
        // offset binSlot*binCapacity) with binPerDrawDataDescSet bound at set 0 (b0 -> bin
        // PerDrawData). Legacy fields above stay the fallback for INVALID binSlots / flag off.
        bool shadowCullEnabled = false;
        vk::DescriptorSet binPerDrawDataDescSet;
        vk::Buffer binCommandBuffer;
        vk::Buffer binCountBuffer;
        uint32_t binCapacity = 0;
    };

    enum class ShadowLayer : uint8_t
    {
        All,
        Static,
        Dynamic
    };

    // Matches render::gpudriven::INVALID_SHADOW_BIN_SLOT. A PageRenderEntry with this binSlot is
    // drawn via the legacy path; any other value selects its GPU bin (B1).
    inline constexpr uint32_t INVALID_BIN_SLOT = 0xFFFFFFFFu;

    struct PageRenderEntry
    {
        uint32_t physicalTileIndex;
        glm::mat4 cropViewProjection;
        float depthBias;
        float slopeBias;
        float normalBias;
        ShadowLayer layer = ShadowLayer::All;
        // VK-1479 B1: when != INVALID_BIN_SLOT and the page-binned cull flag is on, this page is
        // drawn from its GPU bin (baseDrawIndex = binSlot * binCapacity) instead of the legacy
        // per-page replay. Directional pages only in B1; INVALID => legacy path.
        uint32_t binSlot = INVALID_BIN_SLOT;
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

        void dispatchLegacyMeshBatches(vk::CommandBuffer cmd,
                                       const ShadowPassContext& ctx,
                                       ShadowPushConstants& pc);

        // VK-1479 B1: draw one page from its GPU bin (one indirect-count draw at the page's bin
        // region). Set 0 (bin PerDrawData / instance / objects) is bound by recordTileCommands.
        void dispatchBinnedPage(vk::CommandBuffer cmd,
                                const ShadowPassContext& ctx,
                                const PageRenderEntry& page);

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
