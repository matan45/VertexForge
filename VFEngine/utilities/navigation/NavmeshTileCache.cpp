#include "NavmeshTileCache.hpp"
#include "../print/Log.hpp"
#include "../resource/VirtualFileSystem.hpp"
#include <fstream>
#include <filesystem>
#include <cstring>

namespace navigation
{
    namespace
    {
        // Sequential reader over a byte buffer (pak entries arrive as whole buffers)
        struct BufferReader
        {
            const std::vector<uint8_t>& buf;
            size_t pos = 0;

            bool read(void* dst, size_t size)
            {
                if (pos + size > buf.size())
                    return false;
                std::memcpy(dst, buf.data() + pos, size);
                pos += size;
                return true;
            }
        };
    }

    NavmeshTileCache::NavmeshTileCache(const std::string& directory)
        : directory(directory)
    {
        readFileFn = [](const std::string& path)
        {
            return resource::VirtualFileSystem::instance().readFile(path);
        };
        fileExistsFn = [](const std::string& path)
        {
            return resource::VirtualFileSystem::instance().exists(path);
        };
    }

    void NavmeshTileCache::setFileAccess(FileReadFn read, FileExistsFn exists)
    {
        readFileFn = std::move(read);
        fileExistsFn = std::move(exists);
    }

    std::string NavmeshTileCache::getTilePath(const NavmeshTileCoord& coord) const
    {
        return directory + "/tile_" + std::to_string(coord.x) + "_" + std::to_string(coord.z) + ".vfNavTile";
    }

    std::string NavmeshTileCache::getTilePath(const NavmeshTileLodKey& key) const
    {
        if (key.lod == 0)
            return directory + "/tile_" + std::to_string(key.x) + "_" + std::to_string(key.z) + ".vfNavTile";

        return directory + "/tile_" + std::to_string(key.x) + "_" + std::to_string(key.z)
               + "_lod" + std::to_string(key.lod) + ".vfNavTile";
    }

    std::string NavmeshTileCache::getIndexPath() const
    {
        return directory + "/index.vfNavIndex";
    }

    bool NavmeshTileCache::saveTile(const NavmeshTileCoord& coord, const NavmeshTileData& data)
    {
        if (resource::VirtualFileSystem::instance().isArchiveMode())
            return false; // pak is read-only — on-demand tiles stay in-memory in shipped builds

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

    bool NavmeshTileCache::saveTile(const NavmeshTileLodKey& key, const NavmeshTileData& data)
    {
        if (resource::VirtualFileSystem::instance().isArchiveMode())
            return false;

        std::filesystem::create_directories(directory);

        std::string path = getTilePath(key);
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("NavmeshTileCache: Failed to write LOD tile ({}, {}, lod{})", key.x, key.z, key.lod);
            return false;
        }

        file.write(reinterpret_cast<const char*>(&data.x), sizeof(data.x));
        file.write(reinterpret_cast<const char*>(&data.y), sizeof(data.y));
        file.write(reinterpret_cast<const char*>(&data.lod), sizeof(data.lod));
        file.write(reinterpret_cast<const char*>(&data.dataSize), sizeof(data.dataSize));
        if (data.dataSize > 0)
        {
            file.write(reinterpret_cast<const char*>(data.data.data()), data.dataSize);
        }

        knownTileLods.insert(key);
        // Also track in knownTiles for LOD 0 backward compat
        if (key.lod == 0)
            knownTiles.insert({key.x, key.z});

        return file.good();
    }

    bool NavmeshTileCache::loadTile(const NavmeshTileCoord& coord, NavmeshTileData& outData)
    {
        auto bytes = readFileFn(getTilePath(coord));
        if (bytes.empty())
            return false;

        BufferReader reader{bytes};
        if (!reader.read(&outData.x, sizeof(outData.x)) ||
            !reader.read(&outData.y, sizeof(outData.y)) ||
            !reader.read(&outData.dataSize, sizeof(outData.dataSize)))
            return false;

        if (outData.dataSize > 0)
        {
            outData.data.resize(outData.dataSize);
            if (!reader.read(outData.data.data(), outData.dataSize))
                return false;
        }

        return true;
    }

    bool NavmeshTileCache::loadTile(const NavmeshTileLodKey& key, NavmeshTileData& outData)
    {
        auto bytes = readFileFn(getTilePath(key));
        if (bytes.empty())
            return false;

        BufferReader reader{bytes};
        if (!reader.read(&outData.x, sizeof(outData.x)) ||
            !reader.read(&outData.y, sizeof(outData.y)) ||
            !reader.read(&outData.lod, sizeof(outData.lod)) ||
            !reader.read(&outData.dataSize, sizeof(outData.dataSize)))
            return false;

        if (outData.dataSize > 0)
        {
            outData.data.resize(outData.dataSize);
            if (!reader.read(outData.data.data(), outData.dataSize))
                return false;
        }

        return true;
    }

    bool NavmeshTileCache::hasTile(const NavmeshTileCoord& coord) const
    {
        if (knownTiles.count(coord))
            return true;

        return fileExistsFn(getTilePath(coord));
    }

    bool NavmeshTileCache::hasTile(const NavmeshTileLodKey& key) const
    {
        if (knownTileLods.count(key))
            return true;

        return fileExistsFn(getTilePath(key));
    }

    bool NavmeshTileCache::removeTile(const NavmeshTileCoord& coord)
    {
        if (resource::VirtualFileSystem::instance().isArchiveMode())
            return false;

        knownTiles.erase(coord);
        std::string path = getTilePath(coord);
        std::error_code ec;
        return std::filesystem::remove(path, ec);
    }

    bool NavmeshTileCache::saveIndex(const NavmeshTileIndex& index)
    {
        if (resource::VirtualFileSystem::instance().isArchiveMode())
            return false;

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

        // Version 6: streaming block, written field-by-field (no struct padding)
        file.write(reinterpret_cast<const char*>(&index.streaming.enabled), sizeof(index.streaming.enabled));
        file.write(reinterpret_cast<const char*>(&index.streaming.loadRadius), sizeof(index.streaming.loadRadius));
        file.write(reinterpret_cast<const char*>(&index.streaming.unloadRadius), sizeof(index.streaming.unloadRadius));
        file.write(reinterpret_cast<const char*>(&index.streaming.maxLoadsPerFrame), sizeof(index.streaming.maxLoadsPerFrame));
        file.write(reinterpret_cast<const char*>(&index.streaming.maxUnloadsPerFrame), sizeof(index.streaming.maxUnloadsPerFrame));
        file.write(reinterpret_cast<const char*>(index.streaming.lodDistances), sizeof(index.streaming.lodDistances));

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
        auto bytes = readFileFn(getIndexPath());
        if (bytes.empty())
            return false;

        BufferReader reader{bytes};

        if (!reader.read(&outIndex.magic, sizeof(outIndex.magic)) || outIndex.magic != NAVMESH_FILE_MAGIC)
        {
            vfLogError("NavmeshTileCache: Invalid index file magic");
            return false;
        }

        if (!reader.read(&outIndex.version, sizeof(outIndex.version)) ||
            outIndex.version < NAVMESH_TILE_MIN_SUPPORTED_VERSION || outIndex.version > NAVMESH_TILE_FILE_VERSION)
        {
            vfLogError("NavmeshTileCache: Unsupported index version {}", outIndex.version);
            return false;
        }

        if (!reader.read(&outIndex.settings, sizeof(outIndex.settings)) ||
            !reader.read(&outIndex.boundsMin, sizeof(outIndex.boundsMin)) ||
            !reader.read(&outIndex.boundsMax, sizeof(outIndex.boundsMax)))
            return false;

        if (outIndex.version >= 6)
        {
            if (!reader.read(&outIndex.streaming.enabled, sizeof(outIndex.streaming.enabled)) ||
                !reader.read(&outIndex.streaming.loadRadius, sizeof(outIndex.streaming.loadRadius)) ||
                !reader.read(&outIndex.streaming.unloadRadius, sizeof(outIndex.streaming.unloadRadius)) ||
                !reader.read(&outIndex.streaming.maxLoadsPerFrame, sizeof(outIndex.streaming.maxLoadsPerFrame)) ||
                !reader.read(&outIndex.streaming.maxUnloadsPerFrame, sizeof(outIndex.streaming.maxUnloadsPerFrame)) ||
                !reader.read(outIndex.streaming.lodDistances, sizeof(outIndex.streaming.lodDistances)))
                return false;
        }
        else
        {
            // Version 5: no streaming block — defaults (streaming OFF)
            outIndex.streaming = NavmeshIndexStreamingSettings{};
        }

        uint32_t count = 0;
        if (!reader.read(&count, sizeof(count)))
            return false;
        outIndex.tileCoords.resize(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            if (!reader.read(&outIndex.tileCoords[i].x, sizeof(int32_t)) ||
                !reader.read(&outIndex.tileCoords[i].z, sizeof(int32_t)))
                return false;
        }

        knownTiles.clear();
        for (const auto& coord : outIndex.tileCoords)
        {
            knownTiles.insert(coord);
        }

        return true;
    }

    void NavmeshTileCache::forEachTile(const std::function<void(const NavmeshTileCoord&)>& callback) const
    {
        for (const auto& coord : knownTiles)
        {
            callback(coord);
        }
    }
}
