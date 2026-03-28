#include "PrefabSerialization.hpp"
#include "../print/Log.hpp"
#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../components/Components.hpp"
#include "../resource/VFSHelpers.hpp"
#include <fstream>

namespace serialization
{
    bool PrefabSerialization::savePrefab(const scene::Entity& entity, std::string_view filename)
    {
        try
        {
            json prefabJson;
            prefabJson["version"] = "1.0";
            prefabJson["prefab"]["name"] = entity.getName();
            prefabJson["prefab"]["entity"] = serializeEntityTree(entity);

            std::string filePath{filename};
            std::ofstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open file for writing: {}", filename);
                return false;
            }

            file << prefabJson.dump(2);
            if (!file.good())
            {
                vfLogError("Failed to write prefab data to: {}", filename);
                return false;
            }
            file.close();

            vfLogInfo("Prefab saved successfully to: {}", filename);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save prefab: {}", e.what());
            return false;
        }
    }

    std::optional<json> PrefabSerialization::parsePrefabJson(std::string_view filename)
    {
        std::string filePath{filename};
        json prefabJson = resource::readJsonFile(filePath);
        if (prefabJson.is_null())
        {
            vfLogError("Failed to open prefab file: {}", filename);
            return std::nullopt;
        }

        if (!prefabJson.is_object())
        {
            vfLogError("Invalid prefab file: root is not a JSON object");
            return std::nullopt;
        }

        if (!prefabJson.contains("prefab") || !prefabJson["prefab"].is_object())
        {
            vfLogError("Invalid prefab file: missing 'prefab' object");
            return std::nullopt;
        }

        if (!prefabJson["prefab"].contains("entity") || !prefabJson["prefab"]["entity"].is_object())
        {
            vfLogError("Invalid prefab file: missing 'entity' object");
            return std::nullopt;
        }

        if (prefabJson.contains("version") && prefabJson["version"].is_string())
        {
            std::string version = prefabJson["version"].get<std::string>();
            vfLogInfo("Loading prefab version: {}", version);
        }

        return prefabJson;
    }

    std::optional<scene::Entity> PrefabSerialization::loadPrefab(
        std::string_view filename,
        scene::Entity& parent,
        scene::SceneGraphSystem& sceneGraph)
    {
        try
        {
            auto prefabJsonOpt = parsePrefabJson(filename);
            if (!prefabJsonOpt.has_value())
            {
                return std::nullopt;
            }

            scene::Entity rootEntity = deserializeEntityTree(
                (*prefabJsonOpt)["prefab"]["entity"],
                parent,
                sceneGraph
            );

            if (!rootEntity.isValid())
            {
                vfLogError("Failed to instantiate prefab from: {}", filename);
                return std::nullopt;
            }

            vfLogInfo("Prefab loaded successfully from: {}", filename);
            return rootEntity;
        }
        catch (const json::parse_error& e)
        {
            vfLogError("JSON parse error while loading prefab: {}", e.what());
            return std::nullopt;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to load prefab: {}", e.what());
            return std::nullopt;
        }
    }

    bool PrefabSerialization::validatePrefab(std::string_view filename)
    {
        try
        {
            std::string filePath{filename};
            json prefabJson = resource::readJsonFile(filePath);
            if (prefabJson.is_null())
            {
                return false;
            }

            if (!prefabJson.is_object()) return false;
            if (!prefabJson.contains("prefab")) return false;
            if (!prefabJson["prefab"].contains("entity")) return false;

            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}
