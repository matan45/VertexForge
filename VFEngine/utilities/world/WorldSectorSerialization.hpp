#pragma once

#include "WorldSector.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace scene
{
    class SceneGraphSystem;
}

namespace world
{
    using json = nlohmann::json;

    class WorldSectorSerialization
    {
    public:
        // Binary format (default) - writes header + MessagePack
        static bool saveSector(WorldSector& sector, scene::SceneGraphSystem& sceneGraph,
                               const std::string& filePath);

        // JSON format (debug fallback)
        static bool saveSectorJson(WorldSector& sector, scene::SceneGraphSystem& sceneGraph,
                                   const std::string& filePath);

        // Auto-detecting load (binary or JSON based on magic bytes)
        static bool loadSector(const std::string& filePath,
                               std::vector<json>& outEntityData);

        // Header-only read for metadata caching (binary files only)
        static bool readSectorMetadata(const std::string& filePath,
                                       SectorMetadata& outMetadata);

        static json serializeSectorMetadata(const WorldSector& sector);

    private:
        static bool saveSectorBinary(WorldSector& sector, scene::SceneGraphSystem& sceneGraph,
                                     const std::string& filePath);
        static bool loadSectorBinary(const std::string& filePath,
                                     std::vector<json>& outEntityData);
        static bool loadSectorJson(const std::string& filePath,
                                   std::vector<json>& outEntityData);
        static bool isBinaryFormat(const std::string& filePath);
        static json buildSectorJson(WorldSector& sector);
        static math::AABB computeSectorAABB(WorldSector& sector);
        static void writeHeader(std::ostream& file, const SectorFileHeader& header);
        static bool readHeader(std::istream& file, SectorFileHeader& outHeader);
    };

} // namespace world
