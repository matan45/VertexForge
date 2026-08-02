#include "SerializationFileAccess.hpp"
#include "../print/Log.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <shared_mutex>

namespace serialization
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

        SerializationFileAccess makeDefaultAccess()
        {
            return SerializationFileAccess{
                readLooseFile,
                [](const std::string& path)
                {
                    std::error_code ec;
                    return std::filesystem::exists(path, ec) && !ec;
                }};
        }

        std::shared_mutex accessMutex;
        SerializationFileAccess fileAccess = makeDefaultAccess();
    }

    bool setSerializationFileAccess(SerializationFileAccess access)
    {
        if (!access.readBytes || !access.exists)
        {
            vfLogError("SerializationFileAccess: rejected incomplete callback bundle");
            return false;
        }
        std::unique_lock lock(accessMutex);
        fileAccess = std::move(access);
        return true;
    }

    void resetSerializationFileAccess()
    {
        std::unique_lock lock(accessMutex);
        fileAccess = makeDefaultAccess();
    }

    std::vector<uint8_t> readSerializationFileBytes(const std::string& path)
    {
        decltype(SerializationFileAccess::readBytes) callback;
        {
            std::shared_lock lock(accessMutex);
            callback = fileAccess.readBytes;
        }
        try { return callback(path); }
        catch (const std::exception& e)
        {
            vfLogError("SerializationFileAccess: read callback failed for {}: {}", path, e.what());
            return {};
        }
        catch (...) { return {}; }
    }

    nlohmann::json readSerializationJsonFile(const std::string& path)
    {
        const auto bytes = readSerializationFileBytes(path);
        if (bytes.empty()) return {};
        return nlohmann::json::parse(bytes.begin(), bytes.end());
    }

    bool serializationFileExists(const std::string& path)
    {
        decltype(SerializationFileAccess::exists) callback;
        {
            std::shared_lock lock(accessMutex);
            callback = fileAccess.exists;
        }
        try { return callback(path); }
        catch (...) { return false; }
    }
}
