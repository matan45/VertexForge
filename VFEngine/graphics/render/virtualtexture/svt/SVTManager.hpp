#pragma once

#include "../VTTypes.hpp"
#include "../VTPageTable.hpp"
#include "../VTPhysicalPool.hpp"
#include "../VTFeedbackReadback.hpp"
#include "../VTResidencyCore.hpp"
#include "../../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace core
{
    class Device;
}
namespace resource
{
    class TextureStreamHandle;
}

// ============================================================================
// Virtual Texturing (VK-1209, Phase 2) — Streamed Virtual Textures for material
// textures. A single BC7 physical page atlas + page table + feedback + residency
// (the shared VT substrate), fed from disk: pages are extracted at load time from
// the existing .vfImage per-mip data (TextureStreamHandle::readMipLevel +
// vtExtractTile) — NO .vfImage format change. A texture registered as SVT gets a
// bit-31-tagged index that the mesh shader resolves through this system's page table
// instead of the bindless array (SVT_TAG | imageId); the low bits also index the
// GPUVTImageInfo SSBO. Uploads reuse the existing synchronous graphics-queue copy
// pattern, bounded per frame. Default OFF.
// ============================================================================

namespace render::gpudriven
{
    // Bit 31 tags a value in PerDrawData.textureIndices as SVT-backed (bindless indices only
    // ever use the low 12 bits — the mesh shader rejects >= 4096 — so bit 31 is free).
    inline constexpr uint32_t SVT_TAG_BIT = 0x80000000u;

    class SVTManager
    {
    public:
        struct Config
        {
            uint32_t poolBudgetMB = 512;
            uint32_t pagesPerFrame = 32;
            uint32_t evictionAgeFrames = 60;
            bool srgb = true; // BC7 sRGB atlas for albedo (UNORM for data maps)
        };

        explicit SVTManager(core::Device& device);
        ~SVTManager();

        SVTManager(const SVTManager&) = delete;
        SVTManager& operator=(const SVTManager&) = delete;

        void init(const Config& cfg);
        void cleanup();
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // Register a .vfImage as SVT-backed. Returns SVT_TAG_BIT | imageId, or
        // vt::VT_INVALID_TILE if it can't be paged (not .vfImage / too small / registry full).
        // fallbackBindlessIndex is the whole-image slot sampled while a page streams in.
        uint32_t registerTexture(const std::string& path, uint32_t fallbackBindlessIndex);

        // Frame lifecycle (mirrors the RVT loop; upload replaces bake).
        void markFeedbackReady() { if (feedback) feedback->markReady(); }
        void beginFrameReadback();
        void updateAndUpload(vk::CommandBuffer cmd, uint32_t frame); // residency + extract + upload + page-table
        void clearFeedback(vk::CommandBuffer cmd) { if (feedback) feedback->clear(cmd); }
        void copyFeedbackToStaging(vk::CommandBuffer cmd) { if (feedback) feedback->copyToStaging(cmd); }

        // The BC7 atlas is registered once in the bindless heap (UE5-style); its bindless index is
        // baked into every image's info (pad0) so the shader samples bindlessTextures[atlasIndex].
        void setAtlasBindlessIndex(uint32_t idx) { atlasBindlessIndex = idx; imageInfoDirty = true; }

        // Shader binding.
        [[nodiscard]] vk::Buffer getPageTableBuffer() const { return pageTable ? pageTable->getBuffer() : nullptr; }
        [[nodiscard]] vk::Buffer getFeedbackBuffer() const { return feedback ? feedback->getBuffer() : nullptr; }
        [[nodiscard]] vk::Buffer getImageInfoBuffer() const { return imageInfoBuffer; }
        [[nodiscard]] const vt::VTPhysicalPool* getPool() const { return pool.get(); }
        [[nodiscard]] uint32_t imageCount() const { return static_cast<uint32_t>(images.size()); }

    private:
        struct SVTImage
        {
            vt::VTImageDesc desc;
            uint32_t fallbackIndex = vt::VT_INVALID_TILE;
            std::string path;
            std::unique_ptr<resource::TextureStreamHandle> handle;
        };

        void createImageInfoBuffer();
        void uploadImageInfo(vk::CommandBuffer cmd);
        bool extractAndStage(const SVTImage& img, uint32_t mip, uint32_t px, uint32_t py,
                             uint32_t stagingSlot); // fills the staging buffer slot; false on failure

        core::Device& device;
        Config config;

        std::unique_ptr<vt::VTPageTable> pageTable;
        std::unique_ptr<vt::VTPhysicalPool> pool;
        std::unique_ptr<vt::VTFeedbackReadback> feedback;
        vt::VTResidencyCore residency;

        std::vector<SVTImage> images;
        std::unordered_map<std::string, uint32_t> pathToImage;

        // GPUVTImageInfo[] SSBO (one per registered image) + CPU mirror. Device-local (read per
        // material fragment) with a host-visible staging buffer for the upload copy.
        vk::Buffer imageInfoBuffer;
        core::VulkanAllocation imageInfoAllocation;
        vk::Buffer imageInfoStaging;
        core::VulkanAllocation imageInfoStagingAllocation;
        std::vector<vt::GPUVTImageInfo> imageInfoCpu;
        bool imageInfoDirty = false;
        uint32_t imageInfoCapacity = 0;
        uint32_t atlasBindlessIndex = 0; // BC7 atlas slot in the bindless heap

        // Per-frame upload staging (pagesPerFrame BC7 tiles).
        vk::Buffer uploadStaging;
        core::VulkanAllocation uploadStagingAllocation;
        uint32_t tileByteSize = 0;

        std::vector<vt::VTPageKey> requestedPages;
        uint32_t totalPageTableEntries = 0;
        bool initialized = false;
    };
}
