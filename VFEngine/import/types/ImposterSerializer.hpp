#pragma once
#include "ImposterAtlasGenerator.hpp"
#include <string>
#include <cstdint>
#include <array>

namespace importTypes
{
    static constexpr std::array<char, 4> IMPOSTER_MAGIC = { 'V', 'F', 'I', 'M' };
    static constexpr uint32_t IMPOSTER_FORMAT_VERSION = 1;

    // Sanity caps to guard against corrupt data
    static constexpr uint32_t MAX_IMPOSTER_ATLAS_DIM = 16384;
    static constexpr uint32_t MAX_IMPOSTER_VIEW_COUNT = 256;

    class ImposterSerializer
    {
    public:
        // Save imposter atlas to .vfImposter file
        static bool save(const std::string& filePath, const ImposterAtlasData& atlas);

        // Load imposter atlas from .vfImposter file
        static bool load(const std::string& filePath, ImposterAtlasData& atlas);
    };
}
