#pragma once

#include "SVTTypes.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstring>

namespace render::svt
{
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
        uint64_t fileOffset = 0;
        uint32_t compressedSize = 0;
        uint32_t padding = 0;
    };

    // Reads tiles from a .vfSVT file on demand
    class SVTFileReader
    {
    private:
        mutable std::ifstream file_;
        SVTFileHeader header_{};
        std::vector<SVTTileDirectoryEntry> directory_;
        uint32_t totalTiles_ = 0;
        bool valid_ = false;

        uint32_t getTileIndex(const VirtualTileCoord& coord) const
        {
            uint32_t mipOffset = computePageTableMipOffset(coord.mipLevel,
                header_.virtualSizeLog2, header_.tileSizeLog2);
            uint32_t tilesPerSide = computeTilesPerMipSide(coord.mipLevel,
                header_.virtualSizeLog2, header_.tileSizeLog2);
            if (tilesPerSide == 0) tilesPerSide = 1;
            return mipOffset + coord.y * tilesPerSide + coord.x;
        }

    public:
        bool open(const std::string& path)
        {
            file_.open(path, std::ios::binary);
            if (!file_.is_open()) return false;

            file_.read(reinterpret_cast<char*>(&header_), sizeof(header_));
            if (header_.magic[0] != 'S' || header_.magic[1] != 'V' || header_.magic[2] != 'T')
            {
                file_.close();
                return false;
            }

            totalTiles_ = computeTotalPageTableEntries(header_.virtualSizeLog2, header_.tileSizeLog2);
            directory_.resize(totalTiles_);
            file_.read(reinterpret_cast<char*>(directory_.data()),
                       totalTiles_ * sizeof(SVTTileDirectoryEntry));

            valid_ = file_.good();
            return valid_;
        }

        void close()
        {
            file_.close();
            valid_ = false;
        }

        bool readTile(const VirtualTileCoord& coord, std::vector<uint8_t>& outData) const
        {
            if (!valid_) return false;

            uint32_t idx = getTileIndex(coord);
            if (idx >= totalTiles_) return false;

            const auto& entry = directory_[idx];
            if (entry.fileOffset == 0 || entry.compressedSize == 0) return false;

            outData.resize(entry.compressedSize);
            file_.seekg(static_cast<std::streamoff>(entry.fileOffset));
            file_.read(reinterpret_cast<char*>(outData.data()), entry.compressedSize);

            return file_.good();
        }

        const SVTFileHeader& getHeader() const { return header_; }
        uint32_t getTotalTiles() const { return totalTiles_; }
        bool isValid() const { return valid_; }
    };

    // Writes a .vfSVT file during import
    class SVTFileWriter
    {
    private:
        std::ofstream file_;
        SVTFileHeader header_{};
        std::vector<SVTTileDirectoryEntry> directory_;
        uint32_t totalTiles_ = 0;
        uint64_t currentDataOffset_ = 0;
        bool valid_ = false;

        uint32_t getTileIndex(const VirtualTileCoord& coord) const
        {
            uint32_t mipOffset = computePageTableMipOffset(coord.mipLevel,
                header_.virtualSizeLog2, header_.tileSizeLog2);
            uint32_t tilesPerSide = computeTilesPerMipSide(coord.mipLevel,
                header_.virtualSizeLog2, header_.tileSizeLog2);
            if (tilesPerSide == 0) tilesPerSide = 1;
            return mipOffset + coord.y * tilesPerSide + coord.x;
        }

    public:
        bool create(const std::string& path, uint32_t virtualSizeLog2,
                     uint32_t tileSizeLog2 = 7, uint32_t borderSize = 4)
        {
            file_.open(path, std::ios::binary);
            if (!file_.is_open()) return false;

            header_.virtualSizeLog2 = virtualSizeLog2;
            header_.tileSizeLog2 = tileSizeLog2;
            header_.borderSize = borderSize;
            header_.mipLevelCount = computeMipLevelCount(virtualSizeLog2, tileSizeLog2);

            totalTiles_ = computeTotalPageTableEntries(virtualSizeLog2, tileSizeLog2);
            directory_.resize(totalTiles_);
            std::memset(directory_.data(), 0, totalTiles_ * sizeof(SVTTileDirectoryEntry));

            file_.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
            file_.write(reinterpret_cast<const char*>(directory_.data()),
                        totalTiles_ * sizeof(SVTTileDirectoryEntry));

            currentDataOffset_ = sizeof(header_) + totalTiles_ * sizeof(SVTTileDirectoryEntry);
            valid_ = file_.good();
            return valid_;
        }

        bool writeTile(const VirtualTileCoord& coord, const void* data, uint32_t dataSize)
        {
            if (!valid_ || !data || dataSize == 0) return false;

            uint32_t idx = getTileIndex(coord);
            if (idx >= totalTiles_) return false;

            directory_[idx].fileOffset = currentDataOffset_;
            directory_[idx].compressedSize = dataSize;

            file_.write(reinterpret_cast<const char*>(data), dataSize);
            currentDataOffset_ += dataSize;

            return file_.good();
        }

        bool finalize()
        {
            if (!valid_) return false;

            file_.seekp(0);
            file_.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
            file_.write(reinterpret_cast<const char*>(directory_.data()),
                        totalTiles_ * sizeof(SVTTileDirectoryEntry));

            file_.close();
            valid_ = false;
            return true;
        }
    };
}
