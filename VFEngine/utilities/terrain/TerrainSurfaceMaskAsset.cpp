#include "TerrainSurfaceMaskAsset.hpp"
#include "TerrainFileAccess.hpp"

#include "../config/Config.hpp"
#include "../print/Log.hpp"
#include "../resource/BC7Decoder.hpp"
#include "../resource/EndianUtils.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace terrain
{
    namespace fs = std::filesystem;

    namespace
    {
        // The `.vfImage` header, exactly as HeightmapLoader::loadVFImage parses it and
        // procedural::VFImageWriter::write emits it. There is no magic number in this format; the
        // extension is the only tag, so a wrong file reads as nonsense dimensions rather than
        // failing cleanly — hence the sanity checks below.
        constexpr uint8_t VFIMAGE_FILE_TYPE_TEXTURE = 0;
        constexpr uint8_t VFIMAGE_UNCOMPRESSED = 0;
        constexpr uint8_t VFIMAGE_BC7 = 1;
        constexpr uint32_t VFIMAGE_MIP_LEVELS = 1;

        // One BC7 block covers 4x4 texels in 16 bytes, for every mode.
        constexpr size_t BC7_BLOCK_BYTES = 16;

        [[nodiscard]] bool plausibleDimension(uint32_t value)
        {
            return value >= SURFACE_MASK_MIN_RESOLUTION && value <= SURFACE_MASK_MAX_RESOLUTION;
        }
    }

    float TerrainSurfaceMaskData::getChannel(uint32_t x, uint32_t z, uint32_t channel) const
    {
        if (!isValid() || x >= width || z >= height || channel >= SURFACE_MASK_CHANNELS)
            return 0.0f;

        const size_t idx = (static_cast<size_t>(z) * width + x) * SURFACE_MASK_CHANNELS + channel;
        return static_cast<float>(rgba[idx]) / 255.0f;
    }

    void TerrainSurfaceMaskData::setChannel(uint32_t x, uint32_t z, uint32_t channel, float value)
    {
        if (!isValid() || x >= width || z >= height || channel >= SURFACE_MASK_CHANNELS)
            return;

        const size_t idx = (static_cast<size_t>(z) * width + x) * SURFACE_MASK_CHANNELS + channel;
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        // Round-to-nearest, matching the weight-map packer in TerrainGPUAdapter so a value that
        // survives a save/load round trip is the one the artist painted.
        rgba[idx] = static_cast<uint8_t>(clamped * 255.0f + 0.5f);
    }

    float TerrainSurfaceMaskData::sample(float u, float v, uint32_t channel) const
    {
        if (!isValid() || channel >= SURFACE_MASK_CHANNELS)
            return 0.0f;

        // Clamp-to-edge, matching the sampler the mask texture is created with.
        const float fx = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(width - 1);
        const float fz = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(height - 1);

        const auto x0 = static_cast<uint32_t>(std::floor(fx));
        const auto z0 = static_cast<uint32_t>(std::floor(fz));
        const uint32_t x1 = (std::min)(x0 + 1, width - 1);
        const uint32_t z1 = (std::min)(z0 + 1, height - 1);
        const float tx = fx - static_cast<float>(x0);
        const float tz = fz - static_cast<float>(z0);

        const float c00 = getChannel(x0, z0, channel);
        const float c10 = getChannel(x1, z0, channel);
        const float c01 = getChannel(x0, z1, channel);
        const float c11 = getChannel(x1, z1, channel);

        const float top = c00 + (c10 - c00) * tx;
        const float bottom = c01 + (c11 - c01) * tx;
        return top + (bottom - top) * tz;
    }

    std::shared_ptr<TerrainSurfaceMaskData> TerrainSurfaceMaskAsset::createEmpty(uint32_t resolution)
    {
        uint32_t res = std::clamp(resolution, SURFACE_MASK_MIN_RESOLUTION, SURFACE_MASK_MAX_RESOLUTION);
        res &= ~3u; // whole 4x4 blocks, so a later BC7 pass needs no padding
        if (res < SURFACE_MASK_MIN_RESOLUTION)
            res = SURFACE_MASK_MIN_RESOLUTION;

        auto data = std::make_shared<TerrainSurfaceMaskData>();
        data->width = res;
        data->height = res;
        // 0 in every colour channel == no local wetness, no local snow. Alpha 255 (see the header).
        data->rgba.assign(static_cast<size_t>(res) * res * SURFACE_MASK_CHANNELS, 0);
        for (size_t i = 3; i < data->rgba.size(); i += SURFACE_MASK_CHANNELS)
            data->rgba[i] = 255;

        return data;
    }

    std::shared_ptr<TerrainSurfaceMaskData> TerrainSurfaceMaskAsset::load(const std::string& filePath)
    {
        const auto rawData = readTerrainFileBytes(filePath);
        if (rawData.empty())
        {
            vfLogError("TerrainSurfaceMaskAsset: Failed to read file: {}", filePath);
            return nullptr;
        }

        std::string dataStr(rawData.begin(), rawData.end());
        std::istringstream file(dataStr, std::ios::binary);

        const uint8_t fileType = resource::endian::readLE<uint8_t>(file);
        // Version triple is informational: this format is additive-only and every field the mask
        // needs has existed since the first .vfImage.
        (void)resource::endian::readLE<uint32_t>(file); // major
        (void)resource::endian::readLE<uint32_t>(file); // minor
        (void)resource::endian::readLE<uint32_t>(file); // patch
        const uint32_t width = resource::endian::readLE<uint32_t>(file);
        const uint32_t height = resource::endian::readLE<uint32_t>(file);
        (void)resource::endian::readLE<uint32_t>(file); // channels
        (void)resource::endian::readLE<uint32_t>(file); // mipLevels
        const uint8_t compressionFormat = resource::endian::readLE<uint8_t>(file);

        const uint32_t mipWidth = resource::endian::readLE<uint32_t>(file);
        const uint32_t mipHeight = resource::endian::readLE<uint32_t>(file);
        const uint32_t dataSize = resource::endian::readLE<uint32_t>(file);

        if (fileType != VFIMAGE_FILE_TYPE_TEXTURE)
        {
            vfLogError("TerrainSurfaceMaskAsset: {} is not a texture .vfImage (fileType {})",
                       filePath, static_cast<uint32_t>(fileType));
            return nullptr;
        }
        if (mipWidth != width || mipHeight != height)
        {
            vfLogError("TerrainSurfaceMaskAsset: {} mip0 {}x{} disagrees with header {}x{}",
                       filePath, mipWidth, mipHeight, width, height);
            return nullptr;
        }
        // There is no magic number to validate against, so the dimensions are the only guard against
        // parsing an unrelated file as a mask and then allocating whatever it happened to contain.
        if (!plausibleDimension(mipWidth) || !plausibleDimension(mipHeight))
        {
            vfLogError("TerrainSurfaceMaskAsset: {} has implausible dimensions {}x{} (expected "
                       "{}..{} per side)", filePath, mipWidth, mipHeight,
                       SURFACE_MASK_MIN_RESOLUTION, SURFACE_MASK_MAX_RESOLUTION);
            return nullptr;
        }

        const size_t pixelCount = static_cast<size_t>(mipWidth) * mipHeight;
        auto result = std::make_shared<TerrainSurfaceMaskData>();
        result->width = mipWidth;
        result->height = mipHeight;
        result->rgba.resize(pixelCount * SURFACE_MASK_CHANNELS);

        if (compressionFormat == VFIMAGE_UNCOMPRESSED)
        {
            const size_t payload = pixelCount * SURFACE_MASK_CHANNELS;
            std::vector<uint8_t> bgra(payload);
            file.read(reinterpret_cast<char*>(bgra.data()), static_cast<std::streamsize>(payload));
            if (static_cast<size_t>(file.gcount()) != payload)
            {
                vfLogError("TerrainSurfaceMaskAsset: {} is truncated ({} of {} payload bytes)",
                           filePath, static_cast<size_t>(file.gcount()), payload);
                return nullptr;
            }
            // Uncompressed .vfImage payloads are BGRA; we keep RGBA internally so the GPU upload is
            // a straight memcpy into an eR8G8B8A8Unorm image.
            for (size_t i = 0; i < payload; i += SURFACE_MASK_CHANNELS)
            {
                result->rgba[i + 0] = bgra[i + 2];
                result->rgba[i + 1] = bgra[i + 1];
                result->rgba[i + 2] = bgra[i + 0];
                result->rgba[i + 3] = 255; // never trust a stored alpha; see the header note
            }
        }
        else if (compressionFormat == VFIMAGE_BC7)
        {
            // dataSize is a raw 32-bit field out of the file and must be validated BEFORE it sizes
            // an allocation — the dimensions above go through plausibleDimension() for exactly this
            // reason. A BC7 payload is a fixed size for a given resolution, so anything else is a
            // file this loader has no business reading: too large asks for a multi-gigabyte
            // allocation, too small makes BC7Decoder walk blocks past the end of the buffer.
            const size_t expected = static_cast<size_t>((mipWidth + 3) / 4) *
                                    static_cast<size_t>((mipHeight + 3) / 4) * BC7_BLOCK_BYTES;
            if (dataSize != expected)
            {
                vfLogError("TerrainSurfaceMaskAsset: {} declares {} BC7 bytes but {}x{} needs "
                           "exactly {}", filePath, dataSize, mipWidth, mipHeight, expected);
                return nullptr;
            }

            std::vector<uint8_t> compressed(dataSize);
            file.read(reinterpret_cast<char*>(compressed.data()), dataSize);
            if (static_cast<size_t>(file.gcount()) != dataSize)
            {
                vfLogError("TerrainSurfaceMaskAsset: {} is truncated ({} of {} compressed bytes)",
                           filePath, static_cast<size_t>(file.gcount()), dataSize);
                return nullptr;
            }
            // BC7Decoder returns RGBA already. Note the vendored encoder emits mode 6 only, which
            // shares one index across all four channels — so an uncorrelated wetness/snow pair
            // quantizes poorly. The editor writes uncompressed for exactly that reason; this branch
            // exists so an externally-authored mask still loads.
            const auto decoded = resource::BC7Decoder::decompress(compressed.data(), mipWidth, mipHeight);
            if (decoded.size() != result->rgba.size())
            {
                vfLogError("TerrainSurfaceMaskAsset: Failed to decompress BC7 mask: {}", filePath);
                return nullptr;
            }
            std::copy(decoded.begin(), decoded.end(), result->rgba.begin());
            for (size_t i = 3; i < result->rgba.size(); i += SURFACE_MASK_CHANNELS)
                result->rgba[i] = 255;
        }
        else
        {
            vfLogError("TerrainSurfaceMaskAsset: Unsupported compression format {} in mask: {}",
                       static_cast<uint32_t>(compressionFormat), filePath);
            return nullptr;
        }

        vfLogInfo("TerrainSurfaceMaskAsset: Loaded {}x{} surface mask from {}",
                  mipWidth, mipHeight, filePath);
        return result;
    }

    bool TerrainSurfaceMaskAsset::save(const std::string& filePath, const TerrainSurfaceMaskData& data)
    {
        if (terrainArchiveMode())
        {
            vfLogError("TerrainSurfaceMaskAsset: Cannot save in archive mode");
            return false;
        }
        if (!data.isValid())
        {
            vfLogError("TerrainSurfaceMaskAsset: Refusing to save an invalid mask to {}", filePath);
            return false;
        }

        try
        {
            const fs::path path(filePath);
            if (path.has_parent_path())
                fs::create_directories(path.parent_path());

            std::ofstream outFile(path, std::ios::binary);
            if (!outFile)
            {
                vfLogError("TerrainSurfaceMaskAsset: Failed to open {} for writing", filePath);
                return false;
            }

            const uint32_t dataSize = data.width * data.height * SURFACE_MASK_CHANNELS;

            resource::endian::writeLE<uint8_t>(outFile, VFIMAGE_FILE_TYPE_TEXTURE);
            resource::endian::writeLE<uint32_t>(outFile, Version::major);
            resource::endian::writeLE<uint32_t>(outFile, Version::minor);
            resource::endian::writeLE<uint32_t>(outFile, Version::patch);
            resource::endian::writeLE<uint32_t>(outFile, data.width);
            resource::endian::writeLE<uint32_t>(outFile, data.height);
            resource::endian::writeLE<uint32_t>(outFile, SURFACE_MASK_CHANNELS);
            resource::endian::writeLE<uint32_t>(outFile, VFIMAGE_MIP_LEVELS);
            resource::endian::writeLE<uint8_t>(outFile, VFIMAGE_UNCOMPRESSED);

            resource::endian::writeLE<uint32_t>(outFile, data.width);
            resource::endian::writeLE<uint32_t>(outFile, data.height);
            resource::endian::writeLE<uint32_t>(outFile, dataSize);

            // Uncompressed .vfImage payloads are BGRA. Alpha is forced to 255 rather than copied:
            // a varying alpha would make HeightmapLoader decode this file as a 16-bit heightmap.
            for (size_t i = 0; i < data.rgba.size(); i += SURFACE_MASK_CHANNELS)
            {
                const uint8_t bgra[SURFACE_MASK_CHANNELS] = {
                    data.rgba[i + 2],
                    data.rgba[i + 1],
                    data.rgba[i + 0],
                    255
                };
                outFile.write(reinterpret_cast<const char*>(bgra), SURFACE_MASK_CHANNELS);
            }

            outFile.close();
            if (!outFile.good())
            {
                vfLogError("TerrainSurfaceMaskAsset: Write failed for {}", filePath);
                return false;
            }

            vfLogInfo("TerrainSurfaceMaskAsset: Saved {}x{} surface mask to {}",
                      data.width, data.height, filePath);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSurfaceMaskAsset: Exception saving {}: {}", filePath, e.what());
            return false;
        }
    }
}
