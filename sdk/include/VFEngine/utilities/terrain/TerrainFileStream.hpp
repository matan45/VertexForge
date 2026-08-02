#pragma once

#include "TerrainFileAccess.hpp"
#include <fstream>
#include <limits>
#include <optional>

namespace terrain::detail
{
    struct TerrainInputFile
    {
        TerrainFileLocation location;
        std::ifstream stream;

        [[nodiscard]] std::optional<uint64_t> logicalPosition()
        {
            const auto position = stream.tellg();
            if (position < 0) return std::nullopt;
            const uint64_t physicalPosition = static_cast<uint64_t>(position);
            if (physicalPosition < location.baseOffset) return std::nullopt;
            const uint64_t logicalPosition = physicalPosition - location.baseOffset;
            if (logicalPosition > location.size) return std::nullopt;
            return logicalPosition;
        }
    };

    inline std::optional<TerrainInputFile> openTerrainInputFile(const std::string& path,
                                                                 uint64_t logicalOffset)
    {
        auto location = locateTerrainFile(path);
        if (!location || logicalOffset > location->size ||
            location->baseOffset > std::numeric_limits<uint64_t>::max() - logicalOffset)
            return std::nullopt;

        const uint64_t physicalOffset = location->baseOffset + logicalOffset;
        if (physicalOffset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
            return std::nullopt;

        TerrainInputFile input;
        input.location = std::move(*location);
        input.stream.open(input.location.filePath, std::ios::binary);
        if (!input.stream.is_open()) return std::nullopt;
        input.stream.seekg(static_cast<std::streamoff>(physicalOffset));
        if (!input.stream.good()) return std::nullopt;
        return input;
    }
}
