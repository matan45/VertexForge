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
        static bool saveSector(WorldSector& sector, scene::SceneGraphSystem& sceneGraph,
                               const std::string& filePath);

        static bool loadSector(const std::string& filePath,
                               std::vector<json>& outEntityData);

        static json serializeSectorMetadata(const WorldSector& sector);
    };

} // namespace world
