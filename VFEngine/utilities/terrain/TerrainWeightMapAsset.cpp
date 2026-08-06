#include "TerrainWeightMapAsset.hpp"
#include "TerrainFileAccess.hpp"
#include "../print/Log.hpp"
#include "../resource/EndianUtils.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <array>

namespace terrain
{
    namespace fs = std::filesystem;
    using namespace resource::endian;

    static constexpr std::array<char, 4> WEIGHT_MAP_MAGIC = {'V', 'F', 'W', 'M'};
    static constexpr uint32_t FORMAT_VERSION_MAJOR = 2;
    static constexpr uint32_t FORMAT_VERSION_MINOR = 0;
    static constexpr uint32_t FORMAT_VERSION_PATCH = 0;
    static constexpr uint32_t MAX_REASONABLE_TILE_COUNT = 10000;

    bool TerrainWeightMapAsset::save(
        std::string_view path,
        const std::unordered_map<TileCoord, TileWeightMapData, TileCoordHash>& tileWeights,
        uint32_t resolution,
        const std::string& materialPath)
    {
        if (terrainArchiveMode())
        {
            vfLogError("TerrainWeightMapAsset: Cannot save in archive mode");
            return false;
        }
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

            file.write(WEIGHT_MAP_MAGIC.data(), 4);

            writeLE(file, FORMAT_VERSION_MAJOR);
            writeLE(file, FORMAT_VERSION_MINOR);
            writeLE(file, FORMAT_VERSION_PATCH);

            writeLE(file, static_cast<uint32_t>(tileWeights.size()));
            writeLE(file, resolution);

            uint32_t pathLen = static_cast<uint32_t>(materialPath.size());
            writeLE(file, pathLen);
            if (pathLen > 0)
                file.write(materialPath.data(), pathLen);

            for (const auto& [coord, weightData] : tileWeights)
            {
                writeLE(file, coord.x);
                writeLE(file, coord.z);

                for (uint8_t i = 0; i < WEIGHT_CHANNELS; ++i)
                    writeLE<uint8_t>(file, weightData.layerIndices[i]);

                for (uint8_t ch = 0; ch < WEIGHT_CHANNELS; ++ch)
                {
                    if (ch < weightData.layerWeights.size())
                    {
                        writeVectorLE(file, weightData.layerWeights[ch]);
                    }
                    else
                    {
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
        std::string_view path,
        std::string* outMaterialPath)
    {
        std::unordered_map<TileCoord, TileWeightMapData, TileCoordHash> result;

        try
        {
            auto fileData = readTerrainFileBytes(std::string(path));
            if (fileData.empty())
            {
                vfLogWarning("TerrainWeightMapAsset: Failed to read file: {}", path);
                return result;
            }

            std::string dataStr(fileData.begin(), fileData.end());
            std::istringstream file(dataStr, std::ios::binary);

            std::array<char, 4> magic{};
            file.read(magic.data(), 4);
            if (magic != WEIGHT_MAP_MAGIC)
            {
                vfLogError("TerrainWeightMapAsset: Invalid magic bytes in {}", path);
                return result;
            }

            uint32_t major = readLE<uint32_t>(file);
            uint32_t minor = readLE<uint32_t>(file);
            uint32_t patch = readLE<uint32_t>(file);

            if (major != FORMAT_VERSION_MAJOR || minor != FORMAT_VERSION_MINOR || patch != FORMAT_VERSION_PATCH)
            {
                vfLogError("TerrainWeightMapAsset: Incompatible version {}.{}.{}, expected {}.{}.{}. Re-import required.",
                           major, minor, patch,
                           FORMAT_VERSION_MAJOR, FORMAT_VERSION_MINOR, FORMAT_VERSION_PATCH);
                return result;
            }

            uint32_t tileCount = readLE<uint32_t>(file);
            uint32_t resolution = readLE<uint32_t>(file);

            {
                uint32_t pathLen = readLE<uint32_t>(file);
                if (pathLen > 0)
                {
                    std::string matPath(pathLen, '\0');
                    file.read(matPath.data(), pathLen);
                    if (outMaterialPath)
                        *outMaterialPath = std::move(matPath);
                }
            }

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

                TileWeightMapData weightData;
                weightData.resolution = resolution;

                for (uint8_t li = 0; li < WEIGHT_CHANNELS; ++li)
                    weightData.layerIndices[li] = readLE<uint8_t>(file);

                weightData.layerWeights.resize(WEIGHT_CHANNELS);
                for (uint8_t ch = 0; ch < WEIGHT_CHANNELS; ++ch)
                {
                    readVectorLE(file, weightData.layerWeights[ch], texelCount);
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
