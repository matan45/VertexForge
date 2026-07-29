#include "TerrainMaterialAsset.hpp"
#include "../print/Log.hpp"
#include "../uuid/UUID.hpp"
#include "../asset/AssetRef.hpp"
#include "../serialization/AssetRefSerializationHelper.hpp"
#include "../resource/VFSHelpers.hpp"
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

        json j;
        try
        {
            j = resource::readJsonFile(std::string(path));
        }
        catch (const json::parse_error& e)
        {
            vfLogError("Terrain material file '{}' contains invalid JSON at byte {}: {}",
                       path, e.byte, e.what());
            return std::nullopt;
        }
        if (j.is_null())
        {
            vfLogError("Failed to open terrain material file: {}", path);
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
                            layer.materialRef = serialization::readAssetRef(layerJson, "materialRef");
                            layer.tilingScale = layerJson.value("tilingScale", 1.0f);
                            layer.blendMode = stringToLayerBlendMode(layerJson.value("blendMode", "Linear"));
                            // VK-1609. Purely additive key: a file written before this story simply
                            // takes the default, which is why no format-version bump is needed.
                            layer.heightContrast = layerJson.value("heightContrast", 4.0f);
                            // VK-1612. Additive too; absent keys read as "hex tiling off".
                            layer.hexTiling = layerJson.value("hexTiling", false);
                            layer.hexCellScale = std::clamp(
                                layerJson.value("hexCellScale", HEX_TILING_DEFAULT_CELL_SCALE),
                                MIN_HEX_TILING_CELL_SCALE, MAX_HEX_TILING_CELL_SCALE);
                            layer.hexContrast = std::clamp(
                                layerJson.value("hexContrast", HEX_TILING_DEFAULT_CONTRAST),
                                MIN_HEX_TILING_CONTRAST, MAX_HEX_TILING_CONTRAST);
                            layer.hexRotation = std::clamp(
                                layerJson.value("hexRotation", HEX_TILING_DEFAULT_ROTATION),
                                0.0f, MAX_HEX_TILING_ROTATION);
                            layer.enabled = layerJson.value("enabled", true);

                            if (layer.tilingScale <= 0.0f)
                            {
                                logWarningLimited(std::format(
                                    "Layer {} has invalid tilingScale {}, clamping to 0.01",
                                    i, layer.tilingScale));
                                layer.tilingScale = 0.01f;
                            }

                            if (layer.heightContrast < 0.0f || layer.heightContrast > MAX_HEIGHT_BLEND_CONTRAST)
                            {
                                logWarningLimited(std::format(
                                    "Layer {} has out-of-range heightContrast {}, clamping to [0, {}]",
                                    i, layer.heightContrast, MAX_HEIGHT_BLEND_CONTRAST));
                                layer.heightContrast = std::clamp(layer.heightContrast, 0.0f, MAX_HEIGHT_BLEND_CONTRAST);
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

            // VK-1611. Additive block, same discipline as heightContrast above: a file written
            // before this story has no "antiTiling" object, so every field takes its default and
            // both features read as OFF. No format-version bump.
            if (j.contains("antiTiling") && j["antiTiling"].is_object())
            {
                const auto& at = j["antiTiling"];
                auto& dst = material.antiTiling;
                dst.macroVariationStrength = std::clamp(
                    at.value("macroVariationStrength", 0.0f), 0.0f, MACRO_VARIATION_MAX_STRENGTH);
                dst.macroVariationSize0 = std::clamp(
                    at.value("macroVariationSize0", MACRO_VARIATION_DEFAULT_SIZE0),
                    MACRO_VARIATION_MIN_SIZE, MACRO_VARIATION_MAX_SIZE);
                dst.macroVariationSize1 = std::clamp(
                    at.value("macroVariationSize1", MACRO_VARIATION_DEFAULT_SIZE1),
                    MACRO_VARIATION_MIN_SIZE, MACRO_VARIATION_MAX_SIZE);
                dst.macroVariationSeed = at.value("macroVariationSeed", 0u);
                dst.distanceRescaleStrength = std::clamp(
                    at.value("distanceRescaleStrength", 0.0f), 0.0f, DISTANCE_RESCALE_MAX_STRENGTH);
                dst.distanceRescaleScale = std::clamp(
                    at.value("distanceRescaleScale", DISTANCE_RESCALE_DEFAULT_SCALE),
                    DISTANCE_RESCALE_MIN_SCALE, DISTANCE_RESCALE_MAX_SCALE);
                dst.distanceRescaleKnee = std::clamp(
                    at.value("distanceRescaleKnee", DISTANCE_RESCALE_DEFAULT_KNEE),
                    DISTANCE_RESCALE_MIN_KNEE, DISTANCE_RESCALE_MAX_KNEE);
                // Clamped away from zero because the shader feeds this straight to smoothstep,
                // whose behaviour with equal edges is undefined.
                dst.distanceRescaleWidth = std::clamp(
                    at.value("distanceRescaleWidth", DISTANCE_RESCALE_DEFAULT_WIDTH),
                    DISTANCE_RESCALE_MIN_WIDTH, DISTANCE_RESCALE_MAX_WIDTH);
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
            serialization::writeAssetRef(layerJson, "materialRef", layer.materialRef);
            layerJson["tilingScale"] = layer.tilingScale;
            layerJson["blendMode"] = blendModeToString(layer.blendMode);
            layerJson["heightContrast"] = layer.heightContrast;
            layerJson["hexTiling"] = layer.hexTiling;
            layerJson["hexCellScale"] = layer.hexCellScale;
            layerJson["hexContrast"] = layer.hexContrast;
            layerJson["hexRotation"] = layer.hexRotation;
            layerJson["enabled"] = layer.enabled;
            layersJson.push_back(layerJson);
        }
        j["layers"] = layersJson;

        // VK-1611 material-global anti-tiling.
        {
            json at;
            at["macroVariationStrength"] = material.antiTiling.macroVariationStrength;
            at["macroVariationSize0"] = material.antiTiling.macroVariationSize0;
            at["macroVariationSize1"] = material.antiTiling.macroVariationSize1;
            at["macroVariationSeed"] = material.antiTiling.macroVariationSeed;
            at["distanceRescaleStrength"] = material.antiTiling.distanceRescaleStrength;
            at["distanceRescaleScale"] = material.antiTiling.distanceRescaleScale;
            at["distanceRescaleKnee"] = material.antiTiling.distanceRescaleKnee;
            at["distanceRescaleWidth"] = material.antiTiling.distanceRescaleWidth;
            j["antiTiling"] = at;
        }

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
