#include "WaterSerializer.hpp"
#include "../print/Log.hpp"
#include "../resource/EndianUtils.hpp"
#include <fstream>
#include <filesystem>

namespace water
{
    using namespace resource::endian;

    bool WaterSerializer::parseHeader(std::istream& file, WaterFileHeader& outHeader)
    {
        std::array<char, 4> magic{};
        file.read(magic.data(), 4);
        if (magic != WATER_MAGIC)
        {
            vfLogError("WaterSerializer: Invalid magic bytes");
            return false;
        }

        outHeader.versionMajor = readLE<uint32_t>(file);
        outHeader.versionMinor = readLE<uint32_t>(file);
        outHeader.versionPatch = readLE<uint32_t>(file);

        if (outHeader.versionMajor != WATER_FORMAT_VERSION_MAJOR)
        {
            vfLogError("WaterSerializer: Incompatible version {}.{}.{}, expected {}.{}.{}",
                       outHeader.versionMajor, outHeader.versionMinor, outHeader.versionPatch,
                       WATER_FORMAT_VERSION_MAJOR, WATER_FORMAT_VERSION_MINOR, WATER_FORMAT_VERSION_PATCH);
            return false;
        }

        outHeader.tileCount = readLE<uint32_t>(file);
        if (outHeader.tileCount > MAX_REASONABLE_WATER_TILES)
        {
            vfLogError("WaterSerializer: Unreasonable tile count {}", outHeader.tileCount);
            return false;
        }

        outHeader.worldTileSize = readLE<float>(file);

        outHeader.gridMinX = readLE<int32_t>(file);
        outHeader.gridMinZ = readLE<int32_t>(file);
        outHeader.gridMaxX = readLE<int32_t>(file);
        outHeader.gridMaxZ = readLE<int32_t>(file);

        outHeader.physicsEnabled = readLE<uint8_t>(file) != 0;

        // Global settings
        auto& s = outHeader.globalSettings;
        s.density = readLE<float>(file);
        s.drag = readLE<float>(file);
        s.buoyancyStrength = readLE<float>(file);
        s.waveSpeed = readLE<float>(file);
        s.waveAmplitude = readLE<float>(file);
        s.waveFrequency = readLE<float>(file);

        s.shallowColor.x = readLE<float>(file);
        s.shallowColor.y = readLE<float>(file);
        s.shallowColor.z = readLE<float>(file);
        s.shallowColor.w = readLE<float>(file);

        s.deepColor.x = readLE<float>(file);
        s.deepColor.y = readLE<float>(file);
        s.deepColor.z = readLE<float>(file);
        s.deepColor.w = readLE<float>(file);

        s.maxVisibleDepth = readLE<float>(file);
        s.fresnelPower = readLE<float>(file);
        s.dudvTiling = readLE<float>(file);
        s.dudvStrength = readLE<float>(file);
        s.waveDirectionDegrees = readLE<float>(file);

        // Ocean FFT settings (added in v1.1.0)
        if (outHeader.versionMinor >= 1)
        {
            auto& o = outHeader.oceanSettings;
            o.enabled = readLE<uint8_t>(file) != 0;
            o.resolution = readLE<uint32_t>(file);
            o.patchSize = readLE<float>(file);
            o.windSpeed = readLE<float>(file);
            o.windDirection = readLE<float>(file);
            o.amplitude = readLE<float>(file);
            o.choppiness = readLE<float>(file);
            o.foamThreshold = readLE<float>(file);
            if (file.good())
                o.displacementScale = readLE<float>(file);
        }

        return file.good();
    }

    bool WaterSerializer::parseTileData(std::istream& file, WaterTileData& outTile)
    {
        outTile.tileX = readLE<int32_t>(file);
        outTile.tileZ = readLE<int32_t>(file);
        outTile.waterHeight = readLE<float>(file);
        outTile.waveIntensity = readLE<float>(file);
        outTile.physicsEnabled = readLE<uint8_t>(file) != 0;
        if (file.good())
            outTile.isVisible = readLE<uint8_t>(file) != 0;

        return file.good();
    }

    bool WaterSerializer::loadAll(std::string_view path, WaterLoadResult& outResult)
    {
        outResult.success = false;

        std::ifstream file(std::filesystem::path(path), std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("WaterSerializer: Failed to open file for reading: {}", path);
            return false;
        }

        if (!parseHeader(file, outResult.header))
        {
            vfLogError("WaterSerializer: Failed to parse header from {}", path);
            return false;
        }

        outResult.tiles.resize(outResult.header.tileCount);
        for (uint32_t i = 0; i < outResult.header.tileCount; ++i)
        {
            if (!parseTileData(file, outResult.tiles[i]))
            {
                vfLogError("WaterSerializer: Failed to parse tile {} from {}", i, path);
                return false;
            }
        }

        outResult.success = true;
        vfLogInfo("WaterSerializer: Loaded {} tiles from {}", outResult.header.tileCount, path);
        return true;
    }

    bool WaterSerializer::readHeader(std::string_view path, WaterFileHeader& outHeader)
    {
        std::ifstream file(std::filesystem::path(path), std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("WaterSerializer: Failed to open file for reading: {}", path);
            return false;
        }

        return parseHeader(file, outHeader);
    }
}
