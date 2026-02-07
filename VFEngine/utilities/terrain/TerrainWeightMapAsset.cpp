#include "TerrainWeightMapAsset.hpp"
#include "../print/EditorLogger.hpp"
#include "../resource/EndianUtils.hpp"
#include <fstream>
#include <filesystem>
#include <array>

namespace terrain
{
    namespace fs = std::filesystem;
    using namespace resource::endian;

    static constexpr std::array<char, 4> WEIGHT_MAP_MAGIC = {'V', 'F', 'W', 'M'};
    static constexpr uint32_t FORMAT_VERSION_MAJOR = 0;
    static constexpr uint32_t FORMAT_VERSION_MINOR = 0;
    static constexpr uint32_t FORMAT_VERSION_PATCH = 1;
    static constexpr uint32_t MAX_REASONABLE_TILE_COUNT = 10000;

    bool TerrainWeightMapAsset::save(
        std::string_view path,
        const std::unordered_map<TileCoord, TileWeightMapData, TileCoordHash>& tileWeights,
        uint32_t resolution)
    {
        if (tileWeights.empty())
        {
            vfLogWarning("TerrainWeightMapAsset: No weight data to save");
            return true;
        }

        try
        {
            fs::path filePath(path);
            fs::create_directories(filePath.parent_path());

            std::ofstream file(filePath, std::ios::binary);
            if (!file.is_open())
            {
                vfLogError("TerrainWeightMapAsset: Failed to create file: {}", path);
                return false;
            }

            // Magic
            file.write(WEIGHT_MAP_MAGIC.data(), 4);

            // Version
            writeLE(file, FORMAT_VERSION_MAJOR);
            writeLE(file, FORMAT_VERSION_MINOR);
            writeLE(file, FORMAT_VERSION_PATCH);

            // Tile count and resolution
            writeLE(file, static_cast<uint32_t>(tileWeights.size()));
            writeLE(file, resolution);

            // Per tile
            for (const auto& [coord, weightData] : tileWeights)
            {
                writeLE(file, coord.x);
                writeLE(file, coord.z);
                writeLE(file, weightData.activeLayerCount);

                for (uint8_t layer = 0; layer < weightData.activeLayerCount; ++layer)
                {
                    if (layer < weightData.layerWeights.size())
                    {
                        writeVectorLE(file, weightData.layerWeights[layer]);
                    }
                    else
                    {
                        // Pad with zeros if layer data is missing
                        std::vector<float> zeros(static_cast<size_t>(resolution) * resolution, 0.0f);
                        writeVectorLE(file, zeros);
                    }
                }
            }

            file.flush();
            if (!file.good())
            {
                vfLogError("TerrainWeightMapAsset: Failed to flush file: {}", path);
                return false;
            }

            file.close();
            if (file.fail())
            {
                vfLogError("TerrainWeightMapAsset: Failed to close file: {}", path);
                return false;
            }

            vfLogInfo("TerrainWeightMapAsset: Saved {} tiles to {}", tileWeights.size(), path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainWeightMapAsset: Failed to save {}: {}", path, e.what());
            return false;
        }
    }

    std::unordered_map<TileCoord, TileWeightMapData, TileCoordHash> TerrainWeightMapAsset::load(
        std::string_view path)
    {
        std::unordered_map<TileCoord, TileWeightMapData, TileCoordHash> result;

        fs::path filePath(path);
        if (!fs::exists(filePath))
        {
            vfLogWarning("TerrainWeightMapAsset: File not found: {}", path);
            return result;
        }

        try
        {
            std::ifstream file(filePath, std::ios::binary);
            if (!file.is_open())
            {
                vfLogError("TerrainWeightMapAsset: Failed to open file: {}", path);
                return result;
            }

            // Validate magic
            std::array<char, 4> magic{};
            file.read(magic.data(), 4);
            if (magic != WEIGHT_MAP_MAGIC)
            {
                vfLogError("TerrainWeightMapAsset: Invalid magic bytes in {}", path);
                return result;
            }

            // Read version
            uint32_t major = readLE<uint32_t>(file);
            uint32_t minor = readLE<uint32_t>(file);
            uint32_t patch = readLE<uint32_t>(file);

            if (major > FORMAT_VERSION_MAJOR)
            {
                vfLogError("TerrainWeightMapAsset: Unsupported version {}.{}.{} in {}",
                           major, minor, patch, path);
                return result;
            }

            uint32_t tileCount = readLE<uint32_t>(file);
            uint32_t resolution = readLE<uint32_t>(file);

            // Validate
            if (tileCount > MAX_REASONABLE_TILE_COUNT)
            {
                vfLogError("TerrainWeightMapAsset: Unreasonable tile count {} in {}", tileCount, path);
                return result;
            }

            if (resolution != 33 && resolution != 65 && resolution != 129)
            {
                vfLogError("TerrainWeightMapAsset: Invalid resolution {} in {}", resolution, path);
                return result;
            }

            size_t texelCount = static_cast<size_t>(resolution) * resolution;

            for (uint32_t i = 0; i < tileCount; ++i)
            {
                if (!file.good())
                {
                    vfLogError("TerrainWeightMapAsset: Unexpected EOF at tile {} in {}", i, path);
                    return result;
                }

                int32_t coordX = readLE<int32_t>(file);
                int32_t coordZ = readLE<int32_t>(file);
                uint8_t layerCount = readLE<uint8_t>(file);

                if (layerCount == 0 || layerCount > MAX_TERRAIN_LAYERS)
                {
                    vfLogWarning("TerrainWeightMapAsset: Invalid layer count {} for tile ({}, {}), skipping",
                                 layerCount, coordX, coordZ);
                    // Skip this tile's data
                    file.seekg(static_cast<std::streamoff>(layerCount) * texelCount * sizeof(float),
                               std::ios::cur);
                    continue;
                }

                TileWeightMapData weightData;
                weightData.resolution = resolution;
                weightData.activeLayerCount = layerCount;
                weightData.layerWeights.resize(layerCount);

                for (uint8_t layer = 0; layer < layerCount; ++layer)
                {
                    readVectorLE(file, weightData.layerWeights[layer], texelCount);
                }

                if (!file.good())
                {
                    vfLogError("TerrainWeightMapAsset: Read error at tile ({}, {}) in {}",
                               coordX, coordZ, path);
                    return result;
                }

                TileCoord coord(coordX, coordZ);
                result.emplace(coord, std::move(weightData));
            }

            vfLogInfo("TerrainWeightMapAsset: Loaded {} tiles from {}", result.size(), path);
            return result;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainWeightMapAsset: Failed to load {}: {}", path, e.what());
            return {};
        }
    }
}
