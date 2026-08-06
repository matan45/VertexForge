#include "TerrainFileAccess.hpp"
#include "../print/Log.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <shared_mutex>

namespace terrain
{
    namespace
    {
        std::vector<uint8_t> readLooseFile(const std::string& path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open()) return {};
            const auto end = file.tellg();
            if (end < 0) return {};
            const auto size = static_cast<size_t>(end);
            file.seekg(0);
            std::vector<uint8_t> bytes(size);
            if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()),
                                       static_cast<std::streamsize>(size)))
                return {};
            return bytes;
        }

        TerrainFileAccess makeDefaultAccess()
        {
            TerrainFileAccess access;
            access.locate = [](const std::string& path) -> std::optional<TerrainFileLocation>
            {
                std::error_code ec;
                const uint64_t size = std::filesystem::file_size(path, ec);
                if (ec) return std::nullopt;
                return TerrainFileLocation{path, 0, size};
            };
            access.readBytes = readLooseFile;
            access.exists = [](const std::string& path)
            {
                std::error_code ec;
                return std::filesystem::exists(path, ec) && !ec;
            };
            access.isArchiveMode = [] { return false; };
            return access;
        }

        std::shared_mutex accessMutex;
        TerrainFileAccess fileAccess = makeDefaultAccess();

        // Distinct from accessMutex, which only guards the callback bundle above. This one guards
        // the terrain files themselves — see the comment on terrainFileMutex().
        std::shared_mutex fileMutex;

        // Atomic rather than fileMutex-guarded: the reader side is a worker thread comparing the
        // value AFTER its locked reads have finished, precisely when it is holding no lock.
        std::atomic<uint64_t> relocationEpoch{0};
    }

    std::shared_mutex& terrainFileMutex()
    {
        return fileMutex;
    }

    uint64_t terrainFileRelocationEpoch()
    {
        return relocationEpoch.load();
    }

    void bumpTerrainFileRelocationEpoch()
    {
        relocationEpoch.fetch_add(1);
    }

    bool setTerrainFileAccess(TerrainFileAccess access)
    {
        if (!access.locate || !access.readBytes || !access.exists || !access.isArchiveMode)
        {
            vfLogError("TerrainFileAccess: rejected incomplete callback bundle");
            return false;
        }
        std::unique_lock lock(accessMutex);
        fileAccess = std::move(access);
        return true;
    }

    void resetTerrainFileAccess()
    {
        std::unique_lock lock(accessMutex);
        fileAccess = makeDefaultAccess();
    }

    std::optional<TerrainFileLocation> locateTerrainFile(const std::string& path)
    {
        decltype(TerrainFileAccess::locate) callback;
        {
            std::shared_lock lock(accessMutex);
            callback = fileAccess.locate;
        }
        try { return callback(path); }
        catch (const std::exception& e)
        {
            vfLogError("TerrainFileAccess: locate callback failed for {}: {}", path, e.what());
            return std::nullopt;
        }
        catch (...)
        {
            vfLogError("TerrainFileAccess: locate callback failed for {}", path);
            return std::nullopt;
        }
    }

    std::vector<uint8_t> readTerrainFileBytes(const std::string& path)
    {
        decltype(TerrainFileAccess::readBytes) callback;
        {
            std::shared_lock lock(accessMutex);
            callback = fileAccess.readBytes;
        }
        try { return callback(path); }
        catch (const std::exception& e)
        {
            vfLogError("TerrainFileAccess: read callback failed for {}: {}", path, e.what());
            return {};
        }
        catch (...)
        {
            vfLogError("TerrainFileAccess: read callback failed for {}", path);
            return {};
        }
    }

    bool terrainFileExists(const std::string& path)
    {
        decltype(TerrainFileAccess::exists) callback;
        {
            std::shared_lock lock(accessMutex);
            callback = fileAccess.exists;
        }
        try { return callback(path); }
        catch (...) { return false; }
    }

    bool terrainArchiveMode()
    {
        decltype(TerrainFileAccess::isArchiveMode) callback;
        {
            std::shared_lock lock(accessMutex);
            callback = fileAccess.isArchiveMode;
        }
        try { return callback(); }
        catch (...) { return true; }
    }
}
