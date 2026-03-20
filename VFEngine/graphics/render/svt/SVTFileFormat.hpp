#pragma once

#include "SVTTypes.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>

namespace render::svt
{
    // .vfSVT file format:
    //
    // Header (fixed):
    //   uint8_t  magic[4]        = "SVT\0"
    //   uint32_t version         = 1
    //   uint32_t virtualSizeLog2 = e.g. 12 (4096) or 13 (8192)
    //   uint32_t tileSizeLog2    = 7 (128)
    //   uint32_t borderSize      = 4
    //   uint32_t mipLevelCount
    //   uint32_t channelCount    = 1 (single channel: albedo, normal, or ORM)
    //   uint8_t  compressionFormat = 1 (BC7)
    //   uint8_t  reserved[3]
    //
    // Tile directory (one entry per tile across all mip levels):
    //   uint64_t fileOffset      (0 = tile not present)
    //   uint32_t compressedSize
    //   uint32_t padding
    //
    // Tile data (sequential, BC7 compressed):
    //   Raw BC7 block data for each tile

    struct SVTFileHeader
    {
        uint8_t magic[4] = {'S', 'V', 'T', '\0'};
        uint32_t version = 1;
        uint32_t virtualSizeLog2 = 0;
        uint32_t tileSizeLog2 = 7;
        uint32_t borderSize = 4;
        uint32_t mipLevelCount = 0;
        uint32_t channelCount = 1;
        uint8_t compressionFormat = 1; // 1 = BC7
        uint8_t reserved[3] = {};
    };

    struct SVTTileDirectoryEntry
    {
        uint64_t fileOffset = 0;        // 0 = not present
        uint32_t compressedSize = 0;
        uint32_t padding = 0;
    };

    // Reads tiles from a .vfSVT file on demand
    class SVTFileReader
    {
    private:
        std::ifstream file_;
        SVTFileHeader header_;
        std::vector<SVTTileDirectoryEntry> directory_;
        uint32_t totalTiles_ = 0;
        bool valid_ = false;

    public:
        bool open(const std::string& path);
        void close();

        bool readTile(const VirtualTileCoord& coord, std::vector<uint8_t>& outData) const;

        const SVTFileHeader& getHeader() const { return header_; }
        uint32_t getTotalTiles() const { return totalTiles_; }
        bool isValid() const { return valid_; }

    private:
        uint32_t getTileIndex(const VirtualTileCoord& coord) const;
    };

    // Writes a .vfSVT file during import
    class SVTFileWriter
    {
    private:
        std::ofstream file_;
        SVTFileHeader header_;
        std::vector<SVTTileDirectoryEntry> directory_;
        uint32_t totalTiles_ = 0;
        uint64_t currentDataOffset_ = 0;
        bool valid_ = false;

    public:
        bool create(const std::string& path, uint32_t virtualSizeLog2,
                     uint32_t tileSizeLog2 = 7, uint32_t borderSize = 4);

        bool writeTile(const VirtualTileCoord& coord, const void* data, uint32_t dataSize);

        bool finalize();

    private:
        uint32_t getTileIndex(const VirtualTileCoord& coord) const;
    };
}
