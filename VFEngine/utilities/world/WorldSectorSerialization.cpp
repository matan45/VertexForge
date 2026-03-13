#include "WorldSectorSerialization.hpp"
#include "../serialization/SceneSerialization.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../scene/Entity.hpp"
#include "../scene/EntityRegistry.hpp"
#include "../components/Components.hpp"
#include "../print/Log.hpp"
#include <fstream>

namespace world
{
    bool WorldSectorSerialization::saveSector(WorldSector& sector, scene::SceneGraphSystem& sceneGraph,
                                              const std::string& filePath)
    {
        try
        {
            json sectorJson;
            sectorJson["version"] = "1.0";
            sectorJson["coord"] = {{"x", sector.coord.x}, {"z", sector.coord.z}};

            json entitiesJson = json::array();
            auto& registry = scene::EntityRegistry::getRegistry();

            for (uint64_t uuid : sector.entityUUIDs)
            {
                auto uuidView = registry.view<components::UUIDComponent>();
                for (auto entity : uuidView)
                {
                    const auto& uuidComp = uuidView.get<components::UUIDComponent>(entity);
                    if (uuidComp.id.getValue() == uuid)
                    {
                        scene::Entity sceneEntity(entity);
                        entitiesJson.push_back(serialization::SceneSerialization::serializeEntity(sceneEntity));
                        break;
                    }
                }
            }

            sectorJson["entities"] = entitiesJson;

            std::ofstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open sector file for writing: {}", filePath);
                return false;
            }

            file << sectorJson.dump(2);
            file.close();

            sector.dirty = false;
            sector.filePath = filePath;

            vfLogInfo("Sector ({},{}) saved to: {}", sector.coord.x, sector.coord.z, filePath);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save sector: {}", e.what());
            return false;
        }
    }

    bool WorldSectorSerialization::loadSector(const std::string& filePath,
                                               std::vector<json>& outEntityData)
    {
        try
        {
            std::ifstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open sector file for reading: {}", filePath);
                return false;
            }

            json sectorJson = json::parse(file);
            file.close();

            if (!sectorJson.contains("entities") || !sectorJson["entities"].is_array())
            {
                vfLogError("Invalid sector file: missing entities array");
                return false;
            }

            outEntityData.clear();
            for (const auto& entityJson : sectorJson["entities"])
            {
                outEntityData.push_back(entityJson);
            }

            return true;
        }
        catch (const json::parse_error& e)
        {
            vfLogError("JSON parse error in sector file: {}", e.what());
            return false;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to load sector file: {}", e.what());
            return false;
        }
    }

    json WorldSectorSerialization::serializeSectorMetadata(const WorldSector& sector)
    {
        json meta;
        meta["coord"] = {{"x", sector.coord.x}, {"z", sector.coord.z}};
        meta["entityCount"] = sector.entityUUIDs.size();
        meta["filePath"] = sector.filePath;
        return meta;
    }

} // namespace world
