#pragma once

#include "SerializationExport.hpp"
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace serialization
{
    // Seekable physical bytes behind a logical path. Compressed archive entries deliberately
    // have no location — a header-only read of one is impossible without inflating it.
    struct SerializationFileLocation
    {
        std::string filePath;
        uint64_t baseOffset = 0;
        uint64_t size = 0;
    };

    struct SerializationFileAccess
    {
        std::function<std::optional<SerializationFileLocation>(const std::string&)> locate;
        std::function<std::vector<uint8_t>(const std::string&)> readBytes;
        std::function<bool(const std::string&)> exists;
        std::function<bool()> isArchiveMode;
    };

    VF_SERIALIZATION_API bool setSerializationFileAccess(SerializationFileAccess access);
    VF_SERIALIZATION_API void resetSerializationFileAccess();
    VF_SERIALIZATION_API std::optional<SerializationFileLocation>
        locateSerializationFile(const std::string& path);
    VF_SERIALIZATION_API std::vector<uint8_t> readSerializationFileBytes(const std::string& path);
    VF_SERIALIZATION_API nlohmann::json readSerializationJsonFile(const std::string& path);
    VF_SERIALIZATION_API bool serializationFileExists(const std::string& path);

    // VK-1648. Same reason the reads go through this bridge: a header compiled into the
    // Serialization DLL that asks resource::VirtualFileSystem::instance() directly gets the DLL's
    // OWN singleton, which nothing ever mounts the .vfpak into — so the answer is always "loose
    // files", in a packaged build where it is always false.
    VF_SERIALIZATION_API bool serializationArchiveMode();
}
