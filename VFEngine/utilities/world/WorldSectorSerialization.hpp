#pragma once

// Compiled by the Serialization DLL (not the World DLL) — see premake5.lua removefiles in World project
#include "../serialization/SerializationExport.hpp"
#include "WorldSector.hpp"
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
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
        // Reads through the serialization file-access bridge, so a packed .vfsector inside a
        // .vfpak resolves exactly like a loose file.
        // outDataLayers (optional) receives v3 section payloads; v2 files leave it empty.
        static bool loadSector(const std::string& filePath,
                               std::vector<json>& outEntityData,
                               SectorDataLayers* outDataLayers = nullptr);

        // Same parse, over bytes the caller already holds (one read, one parse, no re-copy).
        // sourceLabel only names the buffer in error logs.
        static bool loadSectorFromMemory(std::span<const uint8_t> bytes,
                                         std::vector<json>& outEntityData,
                                         SectorDataLayers* outDataLayers = nullptr,
                                         std::string_view sourceLabel = {});

        // Header-only read for metadata caching (binary files only)
        static bool readSectorMetadata(const std::string& filePath,
                                       SectorMetadata& outMetadata);

        static bool readSectorMetadataFromMemory(std::span<const uint8_t> bytes,
                                                 SectorMetadata& outMetadata);

        static json serializeSectorMetadata(const WorldSector& sector);

    private:
        static bool saveSectorBinary(WorldSector& sector, const std::string& filePath);
        static bool parseSectorPayload(std::span<const uint8_t> payload, uint32_t version,
                                       std::vector<json>& outEntityData,
                                       SectorDataLayers* outDataLayers);
        static bool parseSectorJson(std::span<const uint8_t> bytes,
                                    std::vector<json>& outEntityData);
        static json buildSectorJson(WorldSector& sector);
        static math::AABB computeSectorAABB(WorldSector& sector);
        static void writeHeader(std::ostream& file, const SectorFileHeader& header);
        static bool parseHeader(std::span<const uint8_t> bytes, SectorFileHeader& outHeader);
    };

} // namespace world
