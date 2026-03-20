#include "SVTPageTable.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"
#include <cstring>

namespace render::svt
{
    SVTPageTable::SVTPageTable(core::Device& device)
        : device_(device)
    {
    }

    SVTPageTable::~SVTPageTable()
    {
        cleanup();
    }

    void SVTPageTable::init(const SVTConfig& config)
    {
        if (initialized_) return;
        config_ = config;

        mipLevelCount_ = computeMipLevelCount(config_.virtualTextureSizeLog2, config_.tileSizeLog2);
        totalEntries_ = computeTotalPageTableEntries(config_.virtualTextureSizeLog2, config_.tileSizeLog2);

        // Build per-mip metadata
        mipInfos_.resize(mipLevelCount_);
        for (uint32_t m = 0; m < mipLevelCount_; ++m)
        {
            mipInfos_[m].offset = computePageTableMipOffset(m, config_.virtualTextureSizeLog2, config_.tileSizeLog2);
            mipInfos_[m].tilesPerSide = computeTilesPerMipSide(m, config_.virtualTextureSizeLog2, config_.tileSizeLog2);
            if (mipInfos_[m].tilesPerSide == 0) mipInfos_[m].tilesPerSide = 1;
        }

        // Initialize CPU entries to invalid
        entries_.resize(totalEntries_);
        std::memset(entries_.data(), 0, totalEntries_ * sizeof(SVTPageTableEntry));

        // Create GPU buffer
        createBuffer();

        // Upload initial (all-invalid) state
        flushToGPU();

        initialized_ = true;
        vfLogInfo("SVT PageTable initialized: {} total entries across {} mip levels, buffer size = {} KB",
                     totalEntries_, mipLevelCount_,
                     (totalEntries_ * sizeof(SVTPageTableEntry)) / 1024);
    }

    void SVTPageTable::cleanup()
    {
        if (!initialized_) return;

        auto dev = device_.getLogicalDevice();
        if (mapped_)
        {
            dev.unmapMemory(memory_);
            mapped_ = nullptr;
        }
        core::BufferUtilities::destroyBuffer(dev, buffer_, memory_);

        entries_.clear();
        mipInfos_.clear();
        initialized_ = false;
    }

    void SVTPageTable::setEntry(const VirtualTileCoord& coord, const SVTPageTableEntry& entry)
    {
        uint32_t idx = getFlatIndex(coord);
        if (idx < totalEntries_)
        {
            entries_[idx] = entry;
            dirty_ = true;
        }
    }

    void SVTPageTable::clearEntry(const VirtualTileCoord& coord)
    {
        uint32_t idx = getFlatIndex(coord);
        if (idx < totalEntries_)
        {
            entries_[idx] = SVTPageTableEntry::invalid();
            dirty_ = true;
        }
    }

    const SVTPageTableEntry& SVTPageTable::getEntry(const VirtualTileCoord& coord) const
    {
        uint32_t idx = getFlatIndex(coord);
        static const SVTPageTableEntry invalidEntry{};
        if (idx >= totalEntries_) return invalidEntry;
        return entries_[idx];
    }

    void SVTPageTable::flushToGPU()
    {
        if (!mapped_ || !dirty_) return;
        std::memcpy(mapped_, entries_.data(), totalEntries_ * sizeof(SVTPageTableEntry));
        dirty_ = false;
    }

    void SVTPageTable::clearAll()
    {
        std::memset(entries_.data(), 0, totalEntries_ * sizeof(SVTPageTableEntry));
        dirty_ = true;
    }

    uint32_t SVTPageTable::getFlatIndex(const VirtualTileCoord& coord) const
    {
        if (coord.mipLevel >= mipLevelCount_) return totalEntries_; // Out of bounds

        const auto& mip = mipInfos_[coord.mipLevel];
        if (coord.x >= mip.tilesPerSide || coord.y >= mip.tilesPerSide) return totalEntries_;

        return mip.offset + coord.y * mip.tilesPerSide + coord.x;
    }

    void SVTPageTable::createBuffer()
    {
        auto dev = device_.getLogicalDevice();
        auto physDev = device_.getPhysicalDevice();

        vk::DeviceSize bufferSize = totalEntries_ * sizeof(SVTPageTableEntry);

        core::BufferInfoRequest bufReq(dev, physDev,
            bufferSize,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(bufReq, buffer_, memory_);

        // Persistently map
        mapped_ = dev.mapMemory(memory_, 0, bufferSize);
    }
}
