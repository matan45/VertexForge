#include "AssetMetadataSerializer.hpp"
#include "../print/Log.hpp"
#include "../resource/VFSHelpers.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <limits>
#include <variant>

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

            // Plugin-registered asset types (VK-1449): persist the precise
            // type id so the asset survives in DB/search/export even when the
            // owning plugin is currently absent. Absent for built-in types.
            if (!metadata.pluginTypeId.empty())
            {
                j["pluginTypeId"] = metadata.pluginTypeId;
            }

            // VK-1629: the options the asset was imported with, so a reimport can
            // replay them. Written only when non-empty, like the fields above.
            // Each value keeps its variant alternative's natural JSON type, which
            // is what load() discriminates on.
            if (!metadata.importOptions.empty())
            {
                json optionsJson = json::object();
                for (const auto& entry : metadata.importOptions)
                {
                    const std::string key = entry.first;
                    std::visit([&optionsJson, key](const auto& v) { optionsJson[key] = v; },
                               entry.second);
                }
                j["importOptions"] = optionsJson;
            }

            if (!metadata.dependencies.empty())
            {
                // Sorted for deterministic output (stable diffs under VCS)
                auto sorted = metadata.dependencies;
                std::sort(sorted.begin(), sorted.end());

                json depsArr = json::array();
                for (const auto& dep : sorted)
                {
                    depsArr.push_back(dep.toString());
                }
                j["dependencies"] = depsArr;
            }

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
            metadata.pluginTypeId = j.value("pluginTypeId", "");

            if (!metadata.guid.isValid())
            {
                vfLogWarning("Invalid GUID in meta file: {}", metaPath.string());
                return std::nullopt;
            }

            // VK-1629. The JSON type carries the variant alternative: a bool stays
            // a bool, an integral number an int32_t, a real number a float. An
            // out-of-int32 integer or a nested object/array is dropped rather than
            // silently truncated — the importer then falls back to its default.
            if (j.contains("importOptions") && j["importOptions"].is_object())
            {
                const auto& optionsJson = j["importOptions"];
                for (auto it = optionsJson.begin(); it != optionsJson.end(); ++it)
                {
                    const std::string& key = it.key();
                    const auto& value = it.value();

                    if (value.is_boolean())
                    {
                        metadata.importOptions[key] = value.get<bool>();
                    }
                    else if (value.is_number_integer())
                    {
                        const auto raw = value.get<int64_t>();
                        if (raw < std::numeric_limits<int32_t>::min() ||
                            raw > std::numeric_limits<int32_t>::max())
                        {
                            vfLogWarning("Import option '{}' out of int32 range in {}", key,
                                         metaPath.string());
                            continue;
                        }
                        metadata.importOptions[key] = static_cast<int32_t>(raw);
                    }
                    else if (value.is_number_float())
                    {
                        metadata.importOptions[key] = value.get<float>();
                    }
                    else if (value.is_string())
                    {
                        metadata.importOptions[key] = value.get<std::string>();
                    }
                    else
                    {
                        vfLogWarning("Unsupported import option '{}' in {}", key, metaPath.string());
                    }
                }
            }

            if (j.contains("dependencies") && j["dependencies"].is_array())
            {
                for (const auto& depJson : j["dependencies"])
                {
                    if (!depJson.is_string()) continue;
                    std::string depStr = depJson.get<std::string>();
                    if (!AssetGUID::isStrictHex16(depStr)) continue;

                    AssetGUID dep = AssetGUID::fromString(depStr);
                    if (dep.isValid()) metadata.dependencies.push_back(dep);
                }
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
        for (uint8_t i = 0; i < static_cast<uint8_t>(resource::AssetType::COUNT); ++i)
        {
            auto type = static_cast<resource::AssetType>(i);
            if (str == resource::assetTypeName(type)) return type;
        }
        return resource::AssetType::COUNT;
    }
}
