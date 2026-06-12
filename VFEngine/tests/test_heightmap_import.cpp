#include <doctest.h>
#include <registry/ImporterRegistry.hpp>
#include <registry/builtin/BuiltinImporters.hpp>
#include <terrain/HeightmapLoader.hpp>
#include <resource/EndianUtils.hpp>
#include <config/Config.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// ============================================================
// Heightmap import (RAW16 / PNG16 -> .vfImage)
//
// 16-bit heights are packed into a standard uncompressed .vfImage as
// RGB = high byte, A = low byte; HeightmapLoader decodes the full 16 bits
// when the alpha plane varies and keeps the 8-bit luminance path otherwise.
// ============================================================

namespace
{
    constexpr size_t headerSize = 32;

    std::string detect(const std::vector<unsigned char>& header, uint64_t fileSize = 0,
                       std::string_view path = "")
    {
        import::builtin::ensureRegistered();
        const import::DetectionInput input{path, std::span<const unsigned char>(header), fileSize};
        return import::ImporterRegistry::instance().detect(input);
    }

    std::vector<unsigned char> pngHeader(uint8_t bitDepth, uint8_t colorType)
    {
        std::vector<unsigned char> header = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, // signature
            0x00, 0x00, 0x00, 0x0D,                          // IHDR length (13)
            'I', 'H', 'D', 'R',
            0x00, 0x00, 0x01, 0x00,                          // width 256 (BE)
            0x00, 0x00, 0x01, 0x00,                          // height 256 (BE)
            bitDepth, colorType,
        };
        header.resize(headerSize, 0);
        return header;
    }

    std::filesystem::path makeTempDir()
    {
        auto dir = std::filesystem::temp_directory_path() / "vf_heightmap_import_test";
        std::filesystem::create_directories(dir);
        return dir;
    }
}

TEST_SUITE("HeightmapImport")
{
    TEST_CASE("16-bit grayscale PNG detects as PNG16, everything else stays PNG")
    {
        CHECK(detect(pngHeader(16, 0)) == "PNG16");
        CHECK(detect(pngHeader(8, 0)) == "PNG");   // 8-bit grayscale
        CHECK(detect(pngHeader(8, 6)) == "PNG");   // 8-bit RGBA
        CHECK(detect(pngHeader(16, 2)) == "PNG");  // 16-bit RGB is not a heightmap
    }

    TEST_CASE("RAW16 detection needs the extension and a square 16-bit grid")
    {
        const std::vector<unsigned char> empty(headerSize, 0);

        CHECK(detect(empty, 256ull * 256 * 2, "terrain.raw") == "RAW16");
        CHECK(detect(empty, 512ull * 512 * 2, "terrain.r16") == "RAW16");
        CHECK(detect(empty, 100, "terrain.raw") == "Unknown");          // 50 samples, not square
        CHECK(detect(empty, 255, "terrain.raw") == "Unknown");          // odd byte count
        CHECK(detect(empty, 256ull * 256 * 2, "terrain.bin") == "Unknown"); // wrong extension
    }

    TEST_CASE("RAW16 import round-trips 16-bit heights through .vfImage")
    {
        import::builtin::ensureRegistered();
        auto dir = makeTempDir();
        auto rawPath = (dir / "roundtrip.raw").string();

        // 4x4 grid covering the 16-bit range, incl. adjacent values that an
        // 8-bit pipeline would collapse.
        const std::vector<uint16_t> samples = {
            0, 1, 255, 256,
            257, 4096, 32767, 32768,
            40000, 50000, 60000, 65000,
            65533, 65534, 65535, 12345,
        };

        {
            std::ofstream raw(rawPath, std::ios::binary);
            for (uint16_t sample : samples)
            {
                char bytes[2] = {static_cast<char>(sample & 0xFF), static_cast<char>(sample >> 8)};
                raw.write(bytes, 2);
            }
        }

        importConfig::ImportConfig config;
        pipeline::ImportContext context(importConfig::ImportFiles(rawPath, config), dir.string());
        context.fileName = "roundtrip";
        context.fileType = "RAW16";

        auto* importer = import::ImporterRegistry::instance().importerFor("RAW16");
        REQUIRE(importer != nullptr);
        importer->process(context);

        auto outputPath = (dir / "roundtrip.vfImage").string();
        REQUIRE(std::filesystem::exists(outputPath));

        auto heightmap = terrain::HeightmapLoader::load(outputPath);
        REQUIRE(heightmap != nullptr);
        REQUIRE(heightmap->isValid());
        CHECK(heightmap->width == 4);
        CHECK(heightmap->height == 4);

        for (size_t i = 0; i < samples.size(); ++i)
        {
            CAPTURE(i);
            CHECK(heightmap->heights[i] == doctest::Approx(samples[i] / 65535.0f).epsilon(1e-6));
        }

        // 16-bit precision survives: values 256 and 257 stay distinct.
        CHECK(heightmap->heights[3] != heightmap->heights[4]);

        std::filesystem::remove(rawPath);
        std::filesystem::remove(outputPath);
    }

    TEST_CASE("plain vfImage with constant alpha keeps the 8-bit luminance path")
    {
        auto dir = makeTempDir();
        auto imagePath = (dir / "plain.vfImage").string();

        {
            std::ofstream out(imagePath, std::ios::binary);
            resource::endian::writeLE<uint8_t>(out, 0);  // TEXTURE
            resource::endian::writeLE<uint32_t>(out, Version::major);
            resource::endian::writeLE<uint32_t>(out, Version::minor);
            resource::endian::writeLE<uint32_t>(out, Version::patch);
            resource::endian::writeLE<uint32_t>(out, 2); // width
            resource::endian::writeLE<uint32_t>(out, 2); // height
            resource::endian::writeLE<uint32_t>(out, 4); // channels
            resource::endian::writeLE<uint32_t>(out, 1); // mips
            resource::endian::writeLE<uint8_t>(out, 0);  // uncompressed
            resource::endian::writeLE<uint32_t>(out, 2);
            resource::endian::writeLE<uint32_t>(out, 2);
            resource::endian::writeLE<uint32_t>(out, 16);

            // BGRA gray pixels with the full-opacity alpha plane of a normal image
            const uint8_t values[] = {0, 64, 128, 255};
            for (uint8_t value : values)
            {
                uint8_t bgra[4] = {value, value, value, 255};
                out.write(reinterpret_cast<const char*>(bgra), 4);
            }
        }

        auto heightmap = terrain::HeightmapLoader::load(imagePath);
        REQUIRE(heightmap != nullptr);
        REQUIRE(heightmap->isValid());

        const uint8_t values[] = {0, 64, 128, 255};
        for (size_t i = 0; i < 4; ++i)
        {
            CAPTURE(i);
            CHECK(heightmap->heights[i] == doctest::Approx(values[i] / 255.0f).epsilon(1e-4));
        }

        std::filesystem::remove(imagePath);
    }
}
