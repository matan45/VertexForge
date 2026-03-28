#include "SVTPageTable.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"
#include <cstring>

namespace render::svt
{
    SVTPageTable::SVTPageTable(core::Device& device)
        : device(device)
    {
    }

    SVTPageTable::~SVTPageTable()
    {
        cleanup();
    }

    void SVTPageTable::init(const SVTConfig& config)
    {
        if (initialized) return;
        this->config = config;

        mipLevelCount = computeMipLevelCount(config.virtualTextureSizeLog2, config.tileSizeLog2);
        totalEntries = computeTotalPageTableEntries(config.virtualTextureSizeLog2, config.tileSizeLog2);

        // Build per-mip metadata
        mipInfos.resize(mipLevelCount);
        for (uint32_t m = 0; m < mipLevelCount; ++m)
        {
            mipInfos[m].offset = computePageTableMipOffset(m, config.virtualTextureSizeLog2, config.tileSizeLog2);
            mipInfos[m].tilesPerSide = computeTilesPerMipSide(m, config.virtualTextureSizeLog2, config.tileSizeLog2);
            if (mipInfos[m].tilesPerSide == 0) mipInfos[m].tilesPerSide = 1;
        }

        // Initialize CPU entries to invalid
        entries.resize(totalEntries);
        std::memset(entries.data(), 0, totalEntries * sizeof(SVTPageTableEntry));

        // Create GPU buffer
        createBuffer();

        // Upload initial (all-invalid) state
        flushToGPU();

        initialized = true;
        vfLogInfo("SVT PageTable initialized: {} total entries across {} mip levels, buffer size = {} KB",
                     totalEntries, mipLevelCount,
                     (totalEntries * sizeof(SVTPageTableEntry)) / 1024);
    }

    void SVTPageTable::cleanup()
    {
        if (!initialized) return;

        auto dev = device.getLogicalDevice();
        mapped = nullptr;
        core::BufferUtilities::destroyBuffer(dev, buffer, allocation, device.getMemoryManager());

        entries.clear();
        mipInfos.clear();
        initialized = false;
    }

    void SVTPageTable::setEntry(const VirtualTileCoord& coord, const SVTPageTableEntry& entry)
    {
        uint32_t idx = getFlatIndex(coord);
        if (idx < totalEntries)
        {
            entries[idx] = entry;
            dirty = true;
        }
    }

    void SVTPageTable::clearEntry(const VirtualTileCoord& coord)
    {
        uint32_t idx = getFlatIndex(coord);
        if (idx < totalEntries)
        {
            entries[idx] = SVTPageTableEntry::invalid();
            dirty = true;
        }
    }

    const SVTPageTableEntry& SVTPageTable::getEntry(const VirtualTileCoord& coord) const
    {
        uint32_t idx = getFlatIndex(coord);
        static const SVTPageTableEntry invalidEntry{};
        if (idx >= totalEntries) return invalidEntry;
        return entries[idx];
    }

    void SVTPageTable::flushToGPU()
    {
        if (!mapped || !dirty) return;
        std::memcpy(mapped, entries.data(), totalEntries * sizeof(SVTPageTableEntry));
        dirty = false;
    }

    void SVTPageTable::clearAll()
    {
        std::memset(entries.data(), 0, totalEntries * sizeof(SVTPageTableEntry));
        dirty = true;
    }

    uint32_t SVTPageTable::getFlatIndex(const VirtualTileCoord& coord) const
    {
        if (coord.mipLevel >= mipLevelCount) return totalEntries; // Out of bounds

        const auto& mip = mipInfos[coord.mipLevel];
        if (coord.x >= mip.tilesPerSide || coord.y >= mip.tilesPerSide) return totalEntries;

        return mip.offset + coord.y * mip.tilesPerSide + coord.x;
    }

    void SVTPageTable::createBuffer()
    {
        auto dev = device.getLogicalDevice();
        auto physDev = device.getPhysicalDevice();

        vk::DeviceSize bufferSize = totalEntries * sizeof(SVTPageTableEntry);

        core::BufferInfoRequest bufReq(dev, physDev,
            bufferSize,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(bufReq, buffer, allocation, device.getMemoryManager());

        // Persistently map
        mapped = allocation.mappedPtr;
    }
}
