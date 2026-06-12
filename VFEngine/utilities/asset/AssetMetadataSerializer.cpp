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

            if (metadata.fractureData.has_value())
            {
                const auto& fd = metadata.fractureData.value();
                json fractureJson;
                fractureJson["fragmentCount"] = fd.fragmentCount;
                fractureJson["seedDistribution"] = fd.seedDistribution;
                fractureJson["randomSeed"] = fd.randomSeed;
                fractureJson["innerUVScale"] = fd.innerUVScale;

                json fragmentsJson = json::array();
                for (const auto& frag : fd.fragments)
                {
                    json f;
                    f["centerOfMass"] = {frag.centerOfMass.x, frag.centerOfMass.y, frag.centerOfMass.z};
                    f["volume"] = frag.volume;
                    f["bboxMin"] = {frag.bboxMin.x, frag.bboxMin.y, frag.bboxMin.z};
                    f["bboxMax"] = {frag.bboxMax.x, frag.bboxMax.y, frag.bboxMax.z};
                    fragmentsJson.push_back(f);
                }
                fractureJson["fragments"] = fragmentsJson;

                json connectivityJson = json::array();
                for (const auto& [a, b, area] : fd.connectivity)
                {
                    connectivityJson.push_back({a, b, area});
                }
                fractureJson["connectivity"] = connectivityJson;

                j["fractureData"] = fractureJson;
            }

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
            if (!std::filesystem::exists(metaPath))
            {
                return std::nullopt;
            }

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

            if (j.contains("fractureData"))
            {
                const auto& fj = j["fractureData"];
                FractureMetadata fd;
                fd.fragmentCount = fj.value("fragmentCount", 0u);
                fd.seedDistribution = fj.value("seedDistribution", 0u);
                fd.randomSeed = fj.value("randomSeed", 42u);
                fd.innerUVScale = fj.value("innerUVScale", 1.0f);

                if (fj.contains("fragments"))
                {
                    for (const auto& f : fj["fragments"])
                    {
                        FragmentPhysicsInfo info;
                        if (f.contains("centerOfMass"))
                        {
                            auto& c = f["centerOfMass"];
                            info.centerOfMass = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
                        }
                        info.volume = f.value("volume", 0.0f);
                        if (f.contains("bboxMin"))
                        {
                            auto& b = f["bboxMin"];
                            info.bboxMin = {b[0].get<float>(), b[1].get<float>(), b[2].get<float>()};
                        }
                        if (f.contains("bboxMax"))
                        {
                            auto& b = f["bboxMax"];
                            info.bboxMax = {b[0].get<float>(), b[1].get<float>(), b[2].get<float>()};
                        }
                        fd.fragments.push_back(info);
                    }
                }

                if (fj.contains("connectivity"))
                {
                    for (const auto& c : fj["connectivity"])
                    {
                        fd.connectivity.emplace_back(c[0].get<uint32_t>(), c[1].get<uint32_t>(), c[2].get<float>());
                    }
                }

                metadata.fractureData = fd;
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
        if (str == "Scene")            return resource::AssetType::Scene;
        if (str == "Theme")            return resource::AssetType::Theme;
        if (str == "Prefab")           return resource::AssetType::Prefab;
        return resource::AssetType::COUNT;
    }
}
