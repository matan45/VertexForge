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
                                 ShadowPushConstants& pc);

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
            std::vector<vk::CommandBuffer>& secondaryBuffers,
            std::vector<bool>& threadUsed);

        void dispatchPagesParallel(
            const ParallelDispatchArgs& args,
            const std::vector<PageRenderEntry>& pages,
            bool clearTiles);

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
