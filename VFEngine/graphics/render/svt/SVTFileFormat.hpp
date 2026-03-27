#pragma once

#include "SVTTypes.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstring>
#include "resource/VirtualFileSystem.hpp"

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
        mutable std::ifstream file;
        SVTFileHeader header{};
        std::vector<SVTTileDirectoryEntry> directory;
        uint32_t totalTiles = 0;
        uint64_t baseOffset = 0;
        bool valid = false;

        uint32_t getTileIndex(const VirtualTileCoord& coord) const
        {
            uint32_t mipOffset = computePageTableMipOffset(coord.mipLevel,
                header.virtualSizeLog2, header.tileSizeLog2);
            uint32_t tilesPerSide = computeTilesPerMipSide(coord.mipLevel,
                header.virtualSizeLog2, header.tileSizeLog2);
            if (tilesPerSide == 0) tilesPerSide = 1;
            return mipOffset + coord.y * tilesPerSide + coord.x;
        }

    public:
        bool open(const std::string& path)
        {
            if (resource::VirtualFileSystem::instance().isArchiveMode())
            {
                auto region = resource::VirtualFileSystem::instance().openStream(path);
                if (region)
                {
                    baseOffset = static_cast<uint64_t>(region->baseOffset);
                    file = std::move(region->stream);
                }
                else
                {
                    return false;
                }
            }
            else
            {
                file.open(path, std::ios::binary);
            }
            if (!file.is_open()) return false;

            file.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (header.magic[0] != 'S' || header.magic[1] != 'V' || header.magic[2] != 'T')
            {
                file.close();
                return false;
            }

            totalTiles = computeTotalPageTableEntries(header.virtualSizeLog2, header.tileSizeLog2);
            directory.resize(totalTiles);
            file.read(reinterpret_cast<char*>(directory.data()),
                       totalTiles * sizeof(SVTTileDirectoryEntry));

            valid = file.good();
            return valid;
        }

        void close()
        {
            file.close();
            valid = false;
        }

        bool readTile(const VirtualTileCoord& coord, std::vector<uint8_t>& outData) const
        {
            if (!valid) return false;

            uint32_t idx = getTileIndex(coord);
            if (idx >= totalTiles) return false;

            const auto& entry = directory[idx];
            if (entry.fileOffset == 0 || entry.compressedSize == 0) return false;

            outData.resize(entry.compressedSize);
            file.seekg(static_cast<std::streamoff>(baseOffset + entry.fileOffset));
            file.read(reinterpret_cast<char*>(outData.data()), entry.compressedSize);

            return file.good();
        }

        const SVTFileHeader& getHeader() const { return header; }
        uint32_t getTotalTiles() const { return totalTiles; }
        bool isValid() const { return valid; }

        uint32_t countPresentEntries() const
        {
            uint32_t count = 0;
            for (const auto& entry : directory)
            {
                if (entry.fileOffset != 0 && entry.compressedSize != 0)
                    ++count;
            }
            return count;
        }
    };

    // Writes a .vfSVT file during import
    class SVTFileWriter
    {
    private:
        std::ofstream file;
        SVTFileHeader header{};
        std::vector<SVTTileDirectoryEntry> directory;
        uint32_t totalTiles = 0;
        uint64_t currentDataOffset = 0;
        bool valid = false;

        uint32_t getTileIndex(const VirtualTileCoord& coord) const
        {
            uint32_t mipOffset = computePageTableMipOffset(coord.mipLevel,
                header.virtualSizeLog2, header.tileSizeLog2);
            uint32_t tilesPerSide = computeTilesPerMipSide(coord.mipLevel,
                header.virtualSizeLog2, header.tileSizeLog2);
            if (tilesPerSide == 0) tilesPerSide = 1;
            return mipOffset + coord.y * tilesPerSide + coord.x;
        }

    public:
        bool create(const std::string& path, uint32_t virtualSizeLog2,
                     uint32_t tileSizeLog2 = 7, uint32_t borderSize = 4)
        {
            file.open(path, std::ios::binary);
            if (!file.is_open()) return false;

            header.virtualSizeLog2 = virtualSizeLog2;
            header.tileSizeLog2 = tileSizeLog2;
            header.borderSize = borderSize;
            header.mipLevelCount = computeMipLevelCount(virtualSizeLog2, tileSizeLog2);

            totalTiles = computeTotalPageTableEntries(virtualSizeLog2, tileSizeLog2);
            directory.resize(totalTiles);
            std::memset(directory.data(), 0, totalTiles * sizeof(SVTTileDirectoryEntry));

            file.write(reinterpret_cast<const char*>(&header), sizeof(header));
            file.write(reinterpret_cast<const char*>(directory.data()),
                        totalTiles * sizeof(SVTTileDirectoryEntry));

            currentDataOffset = sizeof(header) + totalTiles * sizeof(SVTTileDirectoryEntry);
            valid = file.good();
            return valid;
        }

        bool writeTile(const VirtualTileCoord& coord, const void* data, uint32_t dataSize)
        {
            if (!valid || !data || dataSize == 0) return false;

            uint32_t idx = getTileIndex(coord);
            if (idx >= totalTiles) return false;

            directory[idx].fileOffset = currentDataOffset;
            directory[idx].compressedSize = dataSize;

            file.write(reinterpret_cast<const char*>(data), dataSize);
            currentDataOffset += dataSize;

            return file.good();
        }

        bool finalize()
        {
            if (!valid) return false;

            file.seekp(0);
            file.write(reinterpret_cast<const char*>(&header), sizeof(header));
            file.write(reinterpret_cast<const char*>(directory.data()),
                        totalTiles * sizeof(SVTTileDirectoryEntry));

            file.close();
            valid = false;
            return true;
        }
    };
}
