#pragma once

// Compiled by the Serialization DLL (not the World DLL) — see premake5.lua removefiles in World project
#include "../serialization/SerializationExport.hpp"
#include "WorldSector.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace world
{
    using json = nlohmann::json;

    class VF_SERIALIZATION_API WorldSectorSerialization
    {
    public:
        // Binary format (default) - writes header + MessagePack
        static bool saveSector(WorldSector& sector, const std::string& filePath);

        // JSON format (debug fallback)
        static bool saveSectorJson(WorldSector& sector, const std::string& filePath);

        // Auto-detecting load (binary or JSON based on magic bytes).
        // outDataLayers (optional) receives v3 section payloads; v2 files leave it empty.
        static bool loadSector(const std::string& filePath,
                               std::vector<json>& outEntityData,
                               SectorDataLayers* outDataLayers = nullptr);

        // Header-only read for metadata caching (binary files only)
        static bool readSectorMetadata(const std::string& filePath,
                                       SectorMetadata& outMetadata);

        static json serializeSectorMetadata(const WorldSector& sector);

    private:
        static bool saveSectorBinary(WorldSector& sector, const std::string& filePath);
        static bool loadSectorBinary(std::ifstream& file, uint32_t version,
                                     std::vector<json>& outEntityData,
                                     SectorDataLayers* outDataLayers);
        static bool loadSectorJson(const std::string& filePath,
                                   std::vector<json>& outEntityData);
        static json buildSectorJson(WorldSector& sector);
        static math::AABB computeSectorAABB(WorldSector& sector);
        static void writeHeader(std::ostream& file, const SectorFileHeader& header);
        static bool readHeader(std::istream& file, SectorFileHeader& outHeader);
    };

} // namespace world
