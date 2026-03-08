#include "NavmeshTileCache.hpp"
#include "../print/Log.hpp"
#include <fstream>
#include <filesystem>

namespace navigation
{
    NavmeshTileCache::NavmeshTileCache(const std::string& directory)
        : directory(directory)
    {
    }

    std::string NavmeshTileCache::getTilePath(const NavmeshTileCoord& coord) const
    {
        return directory + "/tile_" + std::to_string(coord.x) + "_" + std::to_string(coord.z) + ".vfNavTile";
    }

    std::string NavmeshTileCache::getIndexPath() const
    {
        return directory + "/index.vfNavIndex";
    }

    bool NavmeshTileCache::saveTile(const NavmeshTileCoord& coord, const NavmeshTileData& data)
    {
        std::filesystem::create_directories(directory);

        std::string path = getTilePath(coord);
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("NavmeshTileCache: Failed to write tile ({}, {})", coord.x, coord.z);
            return false;
        }

        file.write(reinterpret_cast<const char*>(&data.x), sizeof(data.x));
        file.write(reinterpret_cast<const char*>(&data.y), sizeof(data.y));
        file.write(reinterpret_cast<const char*>(&data.dataSize), sizeof(data.dataSize));
        if (data.dataSize > 0)
        {
            file.write(reinterpret_cast<const char*>(data.data.data()), data.dataSize);
        }

        knownTiles.insert(coord);
        return file.good();
    }

    bool NavmeshTileCache::loadTile(const NavmeshTileCoord& coord, NavmeshTileData& outData)
    {
        std::string path = getTilePath(coord);
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        file.read(reinterpret_cast<char*>(&outData.x), sizeof(outData.x));
        file.read(reinterpret_cast<char*>(&outData.y), sizeof(outData.y));
        file.read(reinterpret_cast<char*>(&outData.dataSize), sizeof(outData.dataSize));
        if (outData.dataSize > 0)
        {
            outData.data.resize(outData.dataSize);
            file.read(reinterpret_cast<char*>(outData.data.data()), outData.dataSize);
        }

        return file.good();
    }

    bool NavmeshTileCache::hasTile(const NavmeshTileCoord& coord) const
    {
        if (knownTiles.count(coord))
            return true;

        return std::filesystem::exists(getTilePath(coord));
    }

    bool NavmeshTileCache::removeTile(const NavmeshTileCoord& coord)
    {
        knownTiles.erase(coord);
        std::string path = getTilePath(coord);
        std::error_code ec;
        return std::filesystem::remove(path, ec);
    }

    bool NavmeshTileCache::saveIndex(const NavmeshTileIndex& index)
    {
        std::filesystem::create_directories(directory);

        std::string path = getIndexPath();
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("NavmeshTileCache: Failed to write index");
            return false;
        }

        file.write(reinterpret_cast<const char*>(&index.magic), sizeof(index.magic));
        file.write(reinterpret_cast<const char*>(&index.version), sizeof(index.version));
        file.write(reinterpret_cast<const char*>(&index.settings), sizeof(index.settings));
        file.write(reinterpret_cast<const char*>(&index.boundsMin), sizeof(index.boundsMin));
        file.write(reinterpret_cast<const char*>(&index.boundsMax), sizeof(index.boundsMax));

        uint32_t count = static_cast<uint32_t>(index.tileCoords.size());
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const auto& coord : index.tileCoords)
        {
            file.write(reinterpret_cast<const char*>(&coord.x), sizeof(coord.x));
            file.write(reinterpret_cast<const char*>(&coord.z), sizeof(coord.z));
        }

        return file.good();
    }

    bool NavmeshTileCache::loadIndex(NavmeshTileIndex& outIndex)
    {
        std::string path = getIndexPath();
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        file.read(reinterpret_cast<char*>(&outIndex.magic), sizeof(outIndex.magic));
        if (outIndex.magic != NAVMESH_FILE_MAGIC)
        {
            vfLogError("NavmeshTileCache: Invalid index file magic");
            return false;
        }

        file.read(reinterpret_cast<char*>(&outIndex.version), sizeof(outIndex.version));
        if (outIndex.version != NAVMESH_TILE_FILE_VERSION)
        {
            vfLogError("NavmeshTileCache: Unsupported index version {}", outIndex.version);
            return false;
        }

        file.read(reinterpret_cast<char*>(&outIndex.settings), sizeof(outIndex.settings));
        file.read(reinterpret_cast<char*>(&outIndex.boundsMin), sizeof(outIndex.boundsMin));
        file.read(reinterpret_cast<char*>(&outIndex.boundsMax), sizeof(outIndex.boundsMax));

        uint32_t count = 0;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        outIndex.tileCoords.resize(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            file.read(reinterpret_cast<char*>(&outIndex.tileCoords[i].x), sizeof(int32_t));
            file.read(reinterpret_cast<char*>(&outIndex.tileCoords[i].z), sizeof(int32_t));
        }

        knownTiles.clear();
        for (const auto& coord : outIndex.tileCoords)
        {
            knownTiles.insert(coord);
        }

        return file.good();
    }

    void NavmeshTileCache::forEachTile(const std::function<void(const NavmeshTileCoord&)>& callback) const
    {
        for (const auto& coord : knownTiles)
        {
            callback(coord);
        }
    }
}
