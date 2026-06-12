#include "../print/Log.hpp"
#include "../resource/VirtualFileSystem.hpp"
#include "NavmeshSerializer.hpp"
#include <fstream>
#include <cstring>

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
        // Reads go through the VFS so the navmesh resolves from the .vfpak in
        // shipped builds (falls back to the filesystem in dev mode)
        auto bytes = resource::VirtualFileSystem::instance().readFile(filePath);
        if (bytes.empty())
        {
            vfLogError("NavmeshSerializer: Failed to open file for reading: {}", filePath);
            return false;
        }

        size_t pos = 0;
        auto read = [&](void* dst, size_t size)
        {
            if (pos + size > bytes.size())
                return false;
            std::memcpy(dst, bytes.data() + pos, size);
            pos += size;
            return true;
        };

        if (!read(&outHeader, sizeof(NavmeshFileHeader)))
            return false;

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
            if (!read(&tile.x, sizeof(tile.x)) ||
                !read(&tile.y, sizeof(tile.y)) ||
                !read(&tile.dataSize, sizeof(tile.dataSize)))
                return false;
            if (tile.dataSize > 0)
            {
                tile.data.resize(tile.dataSize);
                if (!read(tile.data.data(), tile.dataSize))
                    return false;
            }
        }

        return true;
    }
}
