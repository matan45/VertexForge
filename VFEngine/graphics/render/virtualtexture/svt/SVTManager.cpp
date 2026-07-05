#include "SVTManager.hpp"
#include "../VTPageExtract.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "resource/TextureStreamHandle.hpp"
#include "print/Log.hpp"

#include <cstring>
#include <algorithm>

namespace render::gpudriven
{
    using namespace render::vt;

    namespace
    {
        constexpr uint32_t kMaxPageTableEntries = 1u << 20; // 4 MB page table (shared by all SVT images)
        constexpr uint32_t kMinSvtDim = 512;                // smaller textures aren't worth paging
    }

    SVTManager::SVTManager(core::Device& device)
        : device(device)
    {
    }

    SVTManager::~SVTManager()
    {
        cleanup();
    }

    void SVTManager::init(const Config& cfg)
    {
        if (initialized)
            return;
        config = cfg;

        VTPoolDesc poolDesc;
        poolDesc.poolDim = vtPoolDimForBudget(config.poolBudgetMB, /*planes*/ 1, /*bytesPerTexel*/ 1); // BC7 ~1 B/texel
        poolDesc.planeFormats = {config.srgb ? vk::Format::eBc7SrgbBlock : vk::Format::eBc7UnormBlock};
        poolDesc.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        pool = std::make_unique<VTPhysicalPool>(device);
        pool->init(poolDesc);

        totalPageTableEntries = kMaxPageTableEntries;
        pageTable = std::make_unique<VTPageTable>(device);
        pageTable->init(totalPageTableEntries);

        feedback = std::make_unique<VTFeedbackReadback>(device);
        feedback->init(totalPageTableEntries);

        tileByteSize = vtTileByteSize(VT_FORMAT_BC7);
        createImageInfoBuffer();

        // Per-frame upload staging for pagesPerFrame BC7 tiles.
        const vk::DeviceSize stagingSize = static_cast<vk::DeviceSize>(tileByteSize) * config.pagesPerFrame;
        core::BufferInfoRequest req(device.getLogicalDevice(), device.getPhysicalDevice(), stagingSize,
                                    vk::BufferUsageFlagBits::eTransferSrc,
                                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        core::BufferUtilities::createBuffer(req, uploadStaging, uploadStagingAllocation, device.getMemoryManager());

        residency.clear();
        initialized = true;
        vfLogInfo("SVTManager: BC7 pool {}x{} ({} tiles), page table {} entries",
                  pool->getPoolDim(), pool->getPoolDim(), pool->maxTiles(), totalPageTableEntries);
    }

    void SVTManager::cleanup()
    {
        if (!initialized)
            return;
        const vk::Device vkDevice = device.getLogicalDevice();
        auto& mem = device.getMemoryManager();
        core::BufferUtilities::destroyBuffer(vkDevice, uploadStaging, uploadStagingAllocation, mem);
        core::BufferUtilities::destroyBuffer(vkDevice, imageInfoStaging, imageInfoStagingAllocation, mem);
        core::BufferUtilities::destroyBuffer(vkDevice, imageInfoBuffer, imageInfoAllocation, mem);
        feedback.reset();
        pageTable.reset();
        pool.reset();
        images.clear();
        pathToImage.clear();
        imageInfoCpu.clear();
        residency.clear();
        initialized = false;
    }

    void SVTManager::createImageInfoBuffer()
    {
        imageInfoCapacity = 1024; // grows lazily if exceeded
        const vk::DeviceSize size = sizeof(GPUVTImageInfo) * imageInfoCapacity;
        core::BufferInfoRequest devReq(device.getLogicalDevice(), device.getPhysicalDevice(), size,
                                       vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                       vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::BufferUtilities::createBuffer(devReq, imageInfoBuffer, imageInfoAllocation, device.getMemoryManager());

        core::BufferInfoRequest stgReq(device.getLogicalDevice(), device.getPhysicalDevice(), size,
                                       vk::BufferUsageFlagBits::eTransferSrc,
                                       vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        core::BufferUtilities::createBuffer(stgReq, imageInfoStaging, imageInfoStagingAllocation, device.getMemoryManager());
    }

    uint32_t SVTManager::registerTexture(const std::string& path, uint32_t fallbackBindlessIndex)
    {
        if (!initialized)
            return VT_INVALID_TILE;

        if (path.size() < 8 || path.rfind(".vfImage") != path.size() - 8)
            return VT_INVALID_TILE; // SVT only pages .vfImage

        if (auto it = pathToImage.find(path); it != pathToImage.end())
            return SVT_TAG_BIT | it->second;

        auto handle = resource::TextureStreamResource::openStream(path);
        if (!handle)
            return VT_INVALID_TILE;

        const auto& hdr = handle->getHeader();
        // Only BC7-compressed, sufficiently large textures are worth paging.
        if (hdr.compression != resource::TextureCompressionFormat::BC7)
            return VT_INVALID_TILE;
        if (hdr.width < kMinSvtDim || hdr.height < kMinSvtDim || hdr.mipLevels == 0)
            return VT_INVALID_TILE;

        SVTImage img;
        img.path = path;
        img.fallbackIndex = fallbackBindlessIndex;
        img.handle = std::move(handle);
        img.desc.pagesX0 = std::max(1u, (hdr.width + VT_PAGE_INTERIOR - 1u) / VT_PAGE_INTERIOR);
        img.desc.pagesY0 = std::max(1u, (hdr.height + VT_PAGE_INTERIOR - 1u) / VT_PAGE_INTERIOR);
        img.desc.mipCount = std::min(vtComputeMipCount(img.desc.pagesX0, img.desc.pagesY0), hdr.mipLevels);

        const uint32_t base = pageTable->allocateBlock(img.desc.pagesX0, img.desc.pagesY0, img.desc.mipCount);
        if (base == VT_INVALID_TILE)
        {
            vfLogWarning("SVTManager: page table full, '{}' falls back to bindless", path);
            return VT_INVALID_TILE;
        }
        img.desc.pageTableBase = base;

        const uint32_t imageId = static_cast<uint32_t>(images.size());
        images.push_back(std::move(img));
        pathToImage[path] = imageId;

        GPUVTImageInfo info;
        info.pagesX0 = images[imageId].desc.pagesX0;
        info.pagesY0 = images[imageId].desc.pagesY0;
        info.mipCount = images[imageId].desc.mipCount;
        info.pageTableBase = images[imageId].desc.pageTableBase;
        info.poolDim = pool->getPoolDim();
        info.pad0 = atlasBindlessIndex;      // BC7 atlas bindless slot (repatched in uploadImageInfo)
        info.pad1 = fallbackBindlessIndex;   // whole-image bindless slot (fallback while streaming)
        imageInfoCpu.push_back(info);
        imageInfoDirty = true;

        return SVT_TAG_BIT | imageId;
    }

    void SVTManager::beginFrameReadback()
    {
        requestedPages.clear();
        if (!feedback)
            return;
        const std::vector<uint32_t> bits = feedback->readback();
        if (bits.empty())
            return;

        // Decode set bits: find the owning image (block containing the global index), then the page.
        for (uint32_t i = 0; i < bits.size(); ++i)
        {
            if (bits[i] == 0u)
                continue;
            for (uint32_t imageId = 0; imageId < images.size(); ++imageId)
            {
                const VTImageDesc& d = images[imageId].desc;
                const uint32_t blockCount = d.blockEntryCount();
                if (i < d.pageTableBase || i >= d.pageTableBase + blockCount)
                    continue;
                uint32_t mip = 0, x = 0, y = 0;
                if (vtDecodeEntry(d.pagesX0, d.pagesY0, d.mipCount, i - d.pageTableBase, mip, x, y))
                    requestedPages.push_back(VTPageKey{imageId, mip, x, y});
                break;
            }
        }
        std::sort(requestedPages.begin(), requestedPages.end(),
                  [](const VTPageKey& a, const VTPageKey& b) { return a.mip < b.mip; });
    }

    bool SVTManager::extractAndStage(const SVTImage& img, uint32_t mip, uint32_t px, uint32_t py, uint32_t slot)
    {
        if (!img.handle)
            return false;
        resource::MipLevelData mipData;
        if (!const_cast<resource::TextureStreamHandle*>(img.handle.get())->readMipLevel(mip, mipData))
            return false;

        std::vector<uint8_t> tile;
        if (!vtExtractTile(mipData.data.data(), static_cast<uint32_t>(mipData.data.size()),
                           mipData.width, mipData.height, VT_FORMAT_BC7, px, py, tile))
            return false;

        std::memcpy(static_cast<uint8_t*>(uploadStagingAllocation.mappedPtr) + static_cast<size_t>(slot) * tileByteSize,
                    tile.data(), std::min<size_t>(tile.size(), tileByteSize));
        return true;
    }

    void SVTManager::updateAndUpload(vk::CommandBuffer cmd, uint32_t frame)
    {
        if (!initialized)
            return;

        const VTResidencyPlan plan = residency.planFrame(requestedPages, frame, pool->freeTileCount(),
                                                         config.pagesPerFrame, config.evictionAgeFrames);

        for (const auto& e : plan.toEvict)
        {
            const uint32_t tile = residency.commitEviction(e);
            if (tile != VT_INVALID_TILE)
            {
                pool->freeTile(tile);
                pageTable->unmapEntry(images[e.imageId].desc.pageTableBase
                                      + vtPageLinearIndex(images[e.imageId].desc.pagesX0, images[e.imageId].desc.pagesY0,
                                                          e.mip, e.x, e.y));
            }
        }

        // Extract + stage each new page, collecting its copy region.
        std::vector<vk::BufferImageCopy> copies;
        std::vector<VTPageKey> allocated;
        std::vector<uint32_t> allocatedTiles;
        uint32_t slot = 0;
        for (const auto& a : plan.toAllocate)
        {
            if (slot >= config.pagesPerFrame || a.imageId >= images.size())
                break;
            const uint32_t tile = pool->allocateTile();
            if (tile == VT_INVALID_TILE)
                break;
            if (!extractAndStage(images[a.imageId], a.mip, a.x, a.y, slot))
            {
                pool->freeTile(tile);
                continue;
            }
            copies.push_back(pool->getTileBufferCopy(tile, 0, static_cast<vk::DeviceSize>(slot) * tileByteSize));
            allocated.push_back(a);
            allocatedTiles.push_back(tile);
            ++slot;
        }

        if (!copies.empty())
        {
            pool->transition(cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal,
                             vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer,
                             vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eTransferWrite);
            cmd.copyBufferToImage(uploadStaging, pool->planeImage(0), vk::ImageLayout::eTransferDstOptimal,
                                  static_cast<uint32_t>(copies.size()), copies.data());
            pool->transition(cmd, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                             vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                             vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead);

            const uint32_t per = pool->getTilesPerSide();
            for (uint32_t k = 0; k < allocated.size(); ++k)
            {
                const VTPageKey& a = allocated[k];
                const VTImageDesc& d = images[a.imageId].desc;
                pageTable->mapEntry(d.pageTableBase + vtPageLinearIndex(d.pagesX0, d.pagesY0, a.mip, a.x, a.y),
                                    allocatedTiles[k] % per, allocatedTiles[k] / per);
                residency.commitAllocation(a, allocatedTiles[k], frame, /*pinned*/ false);
            }
        }

        pageTable->uploadToGPU(cmd);
        uploadImageInfo(cmd);
    }

    void SVTManager::uploadImageInfo(vk::CommandBuffer cmd)
    {
        if (!imageInfoDirty || imageInfoCpu.empty())
            return;
        // Keep the atlas bindless slot current in every image's info (it may be assigned after some
        // textures were already registered).
        for (auto& info : imageInfoCpu)
            info.pad0 = atlasBindlessIndex;

        const size_t count = std::min<size_t>(imageInfoCpu.size(), imageInfoCapacity);
        const vk::DeviceSize bytes = static_cast<vk::DeviceSize>(count) * sizeof(GPUVTImageInfo);
        std::memcpy(imageInfoStagingAllocation.mappedPtr, imageInfoCpu.data(), bytes);

        vk::BufferCopy copy{};
        copy.size = bytes;
        cmd.copyBuffer(imageInfoStaging, imageInfoBuffer, 1, &copy);

        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = imageInfoBuffer;
        barrier.offset = 0;
        barrier.size = bytes;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                            {}, 0, nullptr, 1, &barrier, 0, nullptr);

        imageInfoDirty = false;
    }
}
