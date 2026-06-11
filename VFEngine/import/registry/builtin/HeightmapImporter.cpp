#include "HeightmapImporter.hpp"
#include "resource/EndianUtils.hpp"
#include "string/StringUtil.hpp"
#include <stb_image.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace import::builtin
{
    namespace
    {
        constexpr unsigned char pngSig[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

        // PNG layout: 8 signature bytes, 4-byte chunk length, "IHDR",
        // 4-byte width, 4-byte height, bit depth (24), color type (25) —
        // all inside the 32-byte detection header.
        bool isPng16Grayscale(std::span<const unsigned char> header)
        {
            if (header.size() < 26) return false;
            if (!std::equal(std::begin(pngSig), std::end(pngSig), header.begin())) return false;
            if (std::memcmp(header.data() + 12, "IHDR", 4) != 0) return false;
            return header[24] == 16 && header[25] == 0; // 16-bit, grayscale
        }

        bool hasRawExtension(std::string_view path)
        {
            std::string ext = StringUtil::toLower(std::filesystem::path(path).extension().string());
            return ext == ".raw" || ext == ".r16";
        }

        // RAW16 has no signature: square 16-bit sample count is the heuristic.
        bool squareRaw16Dimension(uint64_t fileSize, uint32_t& dimension)
        {
            if (fileSize < 2 || (fileSize % 2) != 0) return false;
            uint64_t samples = fileSize / 2;
            auto root = static_cast<uint64_t>(std::llround(std::sqrt(static_cast<double>(samples))));
            if (root == 0 || root * root != samples) return false;
            dimension = static_cast<uint32_t>(root);
            return true;
        }

        void reportProgress(pipeline::ImportContext& context, float progress)
        {
            if (context.progressCallback)
            {
                context.progressCallback(context.fileName, context.fileIndex + 1,
                                         context.totalFiles, progress);
            }
        }

        // Standard uncompressed single-mip .vfImage (same layout as
        // procedural::VFImageWriter / Texture::saveToFileTextureWithMips),
        // with the 16-bit height split across the color and alpha planes.
        void writeHeightVfImage(const std::string& outputPath, uint32_t width, uint32_t height,
                                const std::vector<uint16_t>& samples)
        {
            std::ofstream outFile(outputPath, std::ios::binary);
            if (!outFile)
                throw std::runtime_error("Failed to open heightmap output: " + outputPath);

            constexpr uint8_t fileType = 0;          // TEXTURE
            constexpr uint8_t compressionFormat = 0; // Uncompressed
            constexpr uint32_t channels = 4;
            constexpr uint32_t mipLevels = 1;

            resource::endian::writeLE<uint8_t>(outFile, fileType);
            resource::endian::writeLE<uint32_t>(outFile, Version::major);
            resource::endian::writeLE<uint32_t>(outFile, Version::minor);
            resource::endian::writeLE<uint32_t>(outFile, Version::patch);
            resource::endian::writeLE<uint32_t>(outFile, width);
            resource::endian::writeLE<uint32_t>(outFile, height);
            resource::endian::writeLE<uint32_t>(outFile, channels);
            resource::endian::writeLE<uint32_t>(outFile, mipLevels);
            resource::endian::writeLE<uint8_t>(outFile, compressionFormat);

            uint32_t dataSize = width * height * channels;
            resource::endian::writeLE<uint32_t>(outFile, width);
            resource::endian::writeLE<uint32_t>(outFile, height);
            resource::endian::writeLE<uint32_t>(outFile, dataSize);

            // Pixel order in uncompressed .vfImage is BGRA.
            std::vector<uint8_t> pixels;
            pixels.reserve(samples.size() * 4);
            for (uint16_t sample : samples)
            {
                auto high = static_cast<uint8_t>(sample >> 8);
                auto low = static_cast<uint8_t>(sample & 0xFF);
                pixels.push_back(high); // B
                pixels.push_back(high); // G
                pixels.push_back(high); // R
                pixels.push_back(low);  // A
            }
            outFile.write(reinterpret_cast<const char*>(pixels.data()),
                          static_cast<std::streamsize>(pixels.size()));

            if (!outFile.good())
                throw std::runtime_error("Failed to write heightmap output: " + outputPath);
        }

        std::vector<uint16_t> decodePng16(const std::string& path, uint32_t& width, uint32_t& height)
        {
            int w = 0, h = 0, comp = 0;
            stbi_us* data = stbi_load_16(path.c_str(), &w, &h, &comp, 1);
            if (!data)
                throw std::runtime_error("Failed to decode 16-bit PNG heightmap: " + path);

            width = static_cast<uint32_t>(w);
            height = static_cast<uint32_t>(h);
            std::vector<uint16_t> samples(data, data + static_cast<size_t>(w) * h);
            stbi_image_free(data);
            return samples;
        }

        std::vector<uint16_t> decodeRaw16(const std::string& path, uint32_t& width, uint32_t& height)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
                throw std::runtime_error("Failed to open RAW heightmap: " + path);

            auto fileSize = static_cast<uint64_t>(file.tellg());
            uint32_t dimension = 0;
            if (!squareRaw16Dimension(fileSize, dimension))
                throw std::runtime_error("RAW heightmap is not a square 16-bit grid: " + path);

            std::vector<uint8_t> bytes(fileSize);
            file.seekg(0);
            file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(fileSize));
            if (!file.good())
                throw std::runtime_error("Failed to read RAW heightmap: " + path);

            width = dimension;
            height = dimension;
            std::vector<uint16_t> samples(static_cast<size_t>(dimension) * dimension);
            for (size_t i = 0; i < samples.size(); ++i)
            {
                // World Machine / Gaea RAW exports are little-endian
                samples[i] = static_cast<uint16_t>(bytes[i * 2] | (bytes[i * 2 + 1] << 8));
            }
            return samples;
        }
    }

    std::vector<FormatInfo> HeightmapImporter::formats() const
    {
        return {
            // Above TextureImporter's PNG (100): 16-bit grayscale PNGs are
            // heightmaps, 8-bit PNGs keep flowing to the texture path.
            {"PNG16", "Heightmap Files", {"png"}, FileExtension::textrue, resource::AssetType::Texture, 110},
            // Extension-gated, so it outranks the loose content heuristics
            // (a raw height grid can coincidentally look like TGA or TTF).
            {"RAW16", "Heightmap Files", {"raw", "r16"}, FileExtension::textrue, resource::AssetType::Texture, 105},
        };
    }

    bool HeightmapImporter::matches(const std::string& fileType, const DetectionInput& input) const
    {
        if (fileType == "PNG16")
            return isPng16Grayscale(input.header);

        if (fileType == "RAW16")
        {
            uint32_t dimension = 0;
            return hasRawExtension(input.path) && squareRaw16Dimension(input.fileSize, dimension);
        }

        return false;
    }

    void HeightmapImporter::process(pipeline::ImportContext& context)
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint16_t> samples = (context.fileType == "PNG16")
            ? decodePng16(context.file.path, width, height)
            : decodeRaw16(context.file.path, width, height);

        reportProgress(context, 0.5f);

        const std::string outputPath = (std::filesystem::path(context.location) /
                                        (context.fileName + "." + FileExtension::textrue)).string();
        writeHeightVfImage(outputPath, width, height, samples);

        reportProgress(context, 1.0f);
    }
}
