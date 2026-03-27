#include "AssetMetadataSerializer.hpp"
#include "../print/Log.hpp"
#include "../resource/VFSHelpers.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

namespace asset
{
    using json = nlohmann::json;

    bool AssetMetadataSerializer::save(const AssetMetadata& metadata, const std::filesystem::path& metaPath)
    {
        try
        {
            json j;
            j["guid"] = metadata.guid.toString();
            j["type"] = assetTypeToString(metadata.type);
            j["importSource"] = metadata.importSourcePath;
            j["importTimestamp"] = metadata.importTimestamp;
            j["formatVersion"] = metadata.formatVersion;

            std::ofstream file(metaPath);
            if (!file.is_open())
            {
                vfLogError("Failed to open meta file for writing: {}", metaPath.string());
                return false;
            }

            file << j.dump(2);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save meta file {}: {}", metaPath.string(), e.what());
            return false;
        }
    }

    std::optional<AssetMetadata> AssetMetadataSerializer::load(const std::filesystem::path& metaPath)
    {
        try
        {
            json j = resource::readJsonFile(metaPath.string());
            if (j.is_null())
            {
                return std::nullopt;
            }

            AssetMetadata metadata;
            metadata.guid = AssetGUID::fromString(j.value("guid", ""));
            metadata.type = stringToAssetType(j.value("type", ""));
            metadata.importSourcePath = j.value("importSource", "");
            metadata.importTimestamp = j.value("importTimestamp", "");
            metadata.formatVersion = j.value("formatVersion", 1u);

            if (!metadata.guid.isValid())
            {
                vfLogWarning("Invalid GUID in meta file: {}", metaPath.string());
                return std::nullopt;
            }

            return metadata;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to load meta file {}: {}", metaPath.string(), e.what());
            return std::nullopt;
        }
    }

    std::filesystem::path AssetMetadataSerializer::getMetaPath(const std::filesystem::path& assetPath)
    {
        auto path = assetPath;
        path += ".vfmeta";
        return path;
    }

    std::string AssetMetadataSerializer::assetTypeToString(resource::AssetType type)
    {
        return resource::assetTypeName(type);
    }

    resource::AssetType AssetMetadataSerializer::stringToAssetType(const std::string& str)
    {
        if (str == "Texture")          return resource::AssetType::Texture;
        if (str == "Mesh")             return resource::AssetType::Mesh;
        if (str == "Audio")            return resource::AssetType::Audio;
        if (str == "Animation")        return resource::AssetType::Animation;
        if (str == "Animator")         return resource::AssetType::Animator;
        if (str == "Material")         return resource::AssetType::Material;
        if (str == "MaterialInstance")  return resource::AssetType::MaterialInstance;
        if (str == "PhysicsShape")     return resource::AssetType::PhysicsShape;
        if (str == "VFX")              return resource::AssetType::VFX;
        if (str == "Script")           return resource::AssetType::Script;
        if (str == "HDR")              return resource::AssetType::HDR;
        if (str == "Font")             return resource::AssetType::Font;
        if (str == "Skeleton")         return resource::AssetType::Skeleton;
        if (str == "Navmesh")          return resource::AssetType::Navmesh;
        if (str == "InputMapping")     return resource::AssetType::InputMapping;
        if (str == "Terrain")          return resource::AssetType::Terrain;
        if (str == "World")            return resource::AssetType::World;
        return resource::AssetType::COUNT;
    }
}
