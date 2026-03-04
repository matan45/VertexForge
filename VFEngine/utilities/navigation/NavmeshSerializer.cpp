#include "NavmeshSerializer.hpp"
#include <fstream>

namespace navigation
{
    bool NavmeshSerializer::save(const std::string& filePath,
                                  const NavmeshFileHeader& header,
                                  const std::vector<NavmeshTileData>& tiles)
    {
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("NavmeshSerializer: Failed to open file for writing: {}", filePath);
            return false;
        }

        file.write(reinterpret_cast<const char*>(&header), sizeof(NavmeshFileHeader));

        for (const auto& tile : tiles)
        {
            file.write(reinterpret_cast<const char*>(&tile.x), sizeof(tile.x));
            file.write(reinterpret_cast<const char*>(&tile.y), sizeof(tile.y));
            file.write(reinterpret_cast<const char*>(&tile.dataSize), sizeof(tile.dataSize));
            if (tile.dataSize > 0)
            {
                file.write(reinterpret_cast<const char*>(tile.data.data()), tile.dataSize);
            }
        }

        return file.good();
    }

    bool NavmeshSerializer::load(const std::string& filePath,
                                  NavmeshFileHeader& outHeader,
                                  std::vector<NavmeshTileData>& outTiles)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("NavmeshSerializer: Failed to open file for reading: {}", filePath);
            return false;
        }

        file.read(reinterpret_cast<char*>(&outHeader), sizeof(NavmeshFileHeader));

        if (outHeader.magic != NAVMESH_FILE_MAGIC)
        {
            vfLogError("NavmeshSerializer: Invalid file magic in: {}", filePath);
            return false;
        }

        if (outHeader.version != NAVMESH_FILE_VERSION)
        {
            vfLogError("NavmeshSerializer: Unsupported version {} in: {}", outHeader.version, filePath);
            return false;
        }

        outTiles.resize(outHeader.tileCount);
        for (uint32_t i = 0; i < outHeader.tileCount; ++i)
        {
            auto& tile = outTiles[i];
            file.read(reinterpret_cast<char*>(&tile.x), sizeof(tile.x));
            file.read(reinterpret_cast<char*>(&tile.y), sizeof(tile.y));
            file.read(reinterpret_cast<char*>(&tile.dataSize), sizeof(tile.dataSize));
            if (tile.dataSize > 0)
            {
                tile.data.resize(tile.dataSize);
                file.read(reinterpret_cast<char*>(tile.data.data()), tile.dataSize);
            }
        }

        return file.good();
    }
}
