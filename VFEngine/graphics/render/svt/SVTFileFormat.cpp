#include "SVTFileFormat.hpp"
#include "print/Log.hpp"
#include <cstring>

namespace render::svt
{
    // ---- SVTFileReader ----

    bool SVTFileReader::open(const std::string& path)
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

    void SVTFileReader::close()
    {
        file_.close();
        valid_ = false;
    }

    bool SVTFileReader::readTile(const VirtualTileCoord& coord, std::vector<uint8_t>& outData) const
    {
        if (!valid_) return false;

        uint32_t idx = getTileIndex(coord);
        if (idx >= totalTiles_) return false;

        const auto& entry = directory_[idx];
        if (entry.fileOffset == 0 || entry.compressedSize == 0) return false;

        outData.resize(entry.compressedSize);

        // Use const_cast since ifstream doesn't have const seekg/read
        auto& mutableFile = const_cast<std::ifstream&>(file_);
        mutableFile.seekg(static_cast<std::streamoff>(entry.fileOffset));
        mutableFile.read(reinterpret_cast<char*>(outData.data()), entry.compressedSize);

        return mutableFile.good();
    }

    uint32_t SVTFileReader::getTileIndex(const VirtualTileCoord& coord) const
    {
        uint32_t mipOffset = computePageTableMipOffset(coord.mipLevel,
            header_.virtualSizeLog2, header_.tileSizeLog2);
        uint32_t tilesPerSide = computeTilesPerMipSide(coord.mipLevel,
            header_.virtualSizeLog2, header_.tileSizeLog2);
        if (tilesPerSide == 0) tilesPerSide = 1;
        return mipOffset + coord.y * tilesPerSide + coord.x;
    }

    // ---- SVTFileWriter ----

    bool SVTFileWriter::create(const std::string& path, uint32_t virtualSizeLog2,
                                uint32_t tileSizeLog2, uint32_t borderSize)
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

        // Write placeholder header and directory (will be rewritten in finalize)
        file_.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
        file_.write(reinterpret_cast<const char*>(directory_.data()),
                    totalTiles_ * sizeof(SVTTileDirectoryEntry));

        currentDataOffset_ = sizeof(header_) + totalTiles_ * sizeof(SVTTileDirectoryEntry);
        valid_ = file_.good();
        return valid_;
    }

    bool SVTFileWriter::writeTile(const VirtualTileCoord& coord, const void* data, uint32_t dataSize)
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

    bool SVTFileWriter::finalize()
    {
        if (!valid_) return false;

        // Rewrite header and directory with correct offsets
        file_.seekp(0);
        file_.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
        file_.write(reinterpret_cast<const char*>(directory_.data()),
                    totalTiles_ * sizeof(SVTTileDirectoryEntry));

        file_.close();
        valid_ = false;
        return true;
    }

    uint32_t SVTFileWriter::getTileIndex(const VirtualTileCoord& coord) const
    {
        uint32_t mipOffset = computePageTableMipOffset(coord.mipLevel,
            header_.virtualSizeLog2, header_.tileSizeLog2);
        uint32_t tilesPerSide = computeTilesPerMipSide(coord.mipLevel,
            header_.virtualSizeLog2, header_.tileSizeLog2);
        if (tilesPerSide == 0) tilesPerSide = 1;
        return mipOffset + coord.y * tilesPerSide + coord.x;
    }
}
