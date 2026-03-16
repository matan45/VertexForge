#include "TerrainMaterialAsset.hpp"
#include "../print/Log.hpp"
#include "../uuid/UUID.hpp"
#include "../asset/AssetRef.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <format>
#include <algorithm>

namespace terrain
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;

    std::optional<TerrainMaterialData> TerrainMaterialAsset::load(std::string_view path)
    {
        fs::path filePath(path);

        if (!fs::exists(filePath))
        {
            vfLogError("Terrain material file not found: {}", path);
            return std::nullopt;
        }

        std::error_code ec;
        auto fileSize = fs::file_size(filePath, ec);
        if (ec)
        {
            vfLogError("Cannot read terrain material file size '{}': {}", path, ec.message());
            return std::nullopt;
        }
        constexpr size_t MAX_FILE_SIZE = 1 * 1024 * 1024; // 1 MB limit
        if (fileSize > MAX_FILE_SIZE)
        {
            vfLogError("Terrain material file '{}' is too large ({} bytes, max {} bytes)",
                       path, fileSize, MAX_FILE_SIZE);
            return std::nullopt;
        }

        std::ifstream file(filePath);
        if (!file.is_open())
        {
            vfLogError("Failed to open terrain material file: {}", path);
            return std::nullopt;
        }

        json j;
        try
        {
            file >> j;
        }
        catch (const json::parse_error& e)
        {
            vfLogError("Terrain material file '{}' contains invalid JSON at byte {}: {}",
                       path, e.byte, e.what());
            return std::nullopt;
        }

        if (!j.is_object())
        {
            vfLogError("Terrain material file '{}' must contain a JSON object at root level", path);
            return std::nullopt;
        }

        TerrainMaterialData material;
        int warningCount = 0;
        constexpr int MAX_WARNINGS = 20;

        auto logWarningLimited = [&](const std::string& msg)
        {
            if (warningCount < MAX_WARNINGS)
            {
                vfLogWarning("{}", msg);
                warningCount++;
                if (warningCount == MAX_WARNINGS)
                {
                    vfLogWarning("(suppressing further warnings for this file)");
                }
            }
        };

        try
        {
            std::string fileVersion = j.value("version", TERRAIN_MATERIAL_FORMAT_VERSION);
            if (fileVersion != TERRAIN_MATERIAL_FORMAT_VERSION)
            {
                vfLogError("Incompatible terrain material file version: {}, expected {}. Re-import required. path: {}",
                           fileVersion, TERRAIN_MATERIAL_FORMAT_VERSION, std::string(path));
            }

            material.uuid = j.value("uuid", std::to_string(uuid::UUID().getValue()));
            material.name = j.value("name", "Unnamed Terrain Material");

            if (material.name.empty())
            {
                material.name = "Unnamed Terrain Material";
                logWarningLimited("Terrain material has empty name, using default");
            }

            int rawLayerCount = j.value("activeLayerCount", 1);
            material.activeLayerCount = static_cast<uint8_t>(
                std::clamp(rawLayerCount, 1, static_cast<int>(MAX_TERRAIN_LAYERS)));

            if (j.contains("layers"))
            {
                if (!j["layers"].is_array())
                {
                    logWarningLimited("'layers' field is not an array, using defaults");
                }
                else
                {
                    const auto& layersJson = j["layers"];
                    size_t count = std::min(layersJson.size(), static_cast<size_t>(MAX_TERRAIN_LAYERS));

                    for (size_t i = 0; i < count; ++i)
                    {
                        const auto& layerJson = layersJson[i];

                        if (!layerJson.is_object())
                        {
                            logWarningLimited(std::format("Layer at index {} is not an object, skipping", i));
                            continue;
                        }

                        try
                        {
                            auto& layer = material.layers[i];
                            layer.name = layerJson.value("name", "Layer " + std::to_string(i));
                            layer.albedoTextureRef = asset::AssetRef::fromHexString(layerJson.value("albedoTextureRef", ""));
                            layer.normalTextureRef = asset::AssetRef::fromHexString(layerJson.value("normalTextureRef", ""));
                            layer.ormTextureRef = asset::AssetRef::fromHexString(layerJson.value("ormTextureRef", ""));
                            layer.tilingScale = layerJson.value("tilingScale", 1.0f);
                            layer.roughness = layerJson.value("roughness", 0.9f);
                            layer.metallic = layerJson.value("metallic", 0.0f);
                            layer.ao = layerJson.value("ao", 1.0f);
                            layer.emissionStrength = layerJson.value("emissionStrength", 0.0f);
                            layer.blendMode = stringToLayerBlendMode(layerJson.value("blendMode", "Linear"));
                            layer.enabled = layerJson.value("enabled", true);

                            if (layer.tilingScale <= 0.0f)
                            {
                                logWarningLimited(std::format(
                                    "Layer {} has invalid tilingScale {}, clamping to 0.01",
                                    i, layer.tilingScale));
                                layer.tilingScale = 0.01f;
                            }
                        }
                        catch (const std::exception& e)
                        {
                            logWarningLimited(std::format(
                                "Failed to parse layer at index {}: {}", i, e.what()));
                        }
                    }
                }
            }

            material.cachedMaterialSnippet = j.value("cachedMaterialSnippet", "");
            material.needsRecompile = material.cachedMaterialSnippet.empty();

            if (warningCount > 0)
            {
                vfLogWarning("Loaded terrain material '{}' with {} warning(s)", material.name, warningCount);
            }
            return material;
        }
        catch (const json::exception& e)
        {
            vfLogError("Failed to parse terrain material file '{}': {}", path, e.what());
            return std::nullopt;
        }
        catch (const std::exception& e)
        {
            vfLogError("Unexpected error loading terrain material '{}': {}", path, e.what());
            return std::nullopt;
        }
    }

    bool TerrainMaterialAsset::save(std::string_view path, const TerrainMaterialData& material)
    {
        json j;

        j["version"] = TERRAIN_MATERIAL_FORMAT_VERSION;
        j["uuid"] = material.uuid;
        j["name"] = material.name;
        j["activeLayerCount"] = material.activeLayerCount;

        json layersJson = json::array();
        for (int i = 0; i < MAX_TERRAIN_LAYERS; ++i)
        {
            const auto& layer = material.layers[i];
            json layerJson;
            layerJson["name"] = layer.name;
            layerJson["albedoTextureRef"] = layer.albedoTextureRef.toHexString();
            layerJson["normalTextureRef"] = layer.normalTextureRef.toHexString();
            layerJson["ormTextureRef"] = layer.ormTextureRef.toHexString();
            layerJson["tilingScale"] = layer.tilingScale;
            layerJson["roughness"] = layer.roughness;
            layerJson["metallic"] = layer.metallic;
            layerJson["ao"] = layer.ao;
            layerJson["emissionStrength"] = layer.emissionStrength;
            layerJson["blendMode"] = blendModeToString(layer.blendMode);
            layerJson["enabled"] = layer.enabled;
            layersJson.push_back(layerJson);
        }
        j["layers"] = layersJson;

        if (!material.cachedMaterialSnippet.empty())
        {
            j["cachedMaterialSnippet"] = material.cachedMaterialSnippet;
        }

        try
        {
            fs::path filePath(path);
            fs::create_directories(filePath.parent_path());

            std::ofstream file(filePath);
            if (!file.is_open())
            {
                vfLogError("Failed to create terrain material file: {}", path);
                return false;
            }

            file << j.dump(4);

            file.flush();
            if (!file.good())
            {
                vfLogError("Failed to flush terrain material file: {}", path);
                return false;
            }

            file.close();
            if (file.fail())
            {
                vfLogError("Failed to close terrain material file: {}", path);
                return false;
            }

            vfLogInfo("Saved terrain material: {} to {}", material.name, path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save terrain material file {}: {}", path, e.what());
            return false;
        }
    }

    TerrainMaterialData TerrainMaterialAsset::createDefault(const std::string& name)
    {
        TerrainMaterialData material;
        material.uuid = std::to_string(uuid::UUID().getValue());
        material.name = name;
        material.activeLayerCount = 1;
        material.layers[0].name = "Layer 0";
        material.needsRecompile = true;

        return material;
    }
}
