#pragma once

#include "SerializationExport.hpp"
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace serialization
{
    struct SerializationFileAccess
    {
        std::function<std::vector<uint8_t>(const std::string&)> readBytes;
        std::function<bool(const std::string&)> exists;
    };

    VF_SERIALIZATION_API bool setSerializationFileAccess(SerializationFileAccess access);
    VF_SERIALIZATION_API void resetSerializationFileAccess();
    VF_SERIALIZATION_API std::vector<uint8_t> readSerializationFileBytes(const std::string& path);
    VF_SERIALIZATION_API nlohmann::json readSerializationJsonFile(const std::string& path);
    VF_SERIALIZATION_API bool serializationFileExists(const std::string& path);
}
