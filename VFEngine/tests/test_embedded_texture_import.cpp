#include <doctest.h>

#include <config/Config.hpp>
#include <resource/EndianUtils.hpp>
#include <types/Texture.hpp>

#include <cstdint>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

// ============================================================
// VK-55: Extract Embedded Textures During Mesh Import.
//
// Texture::saveEmbeddedTexture is the reusable seam Mesh.cpp calls per
// aiScene->mTextures entry. It decodes the embedded blob (compressed file blob
// or raw BGRA aiTexel array) and writes a .vfImage through the standard mip +
// compression pipeline. These tests exercise the raw-BGRA path end to end and
// validate the resulting .vfImage header. (The compressed-blob path delegates
// to the well-tested stbi_load_from_memory and is covered by in-editor import.)
// ============================================================

namespace
{
    struct VfImageHeader
    {
        uint8_t fileType = 0xFF;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t channels = 0;
        uint32_t mipLevels = 0;
        uint8_t compressionFormat = 0xFF;
        // First mip header.
        uint32_t mip0Width = 0;
        uint32_t mip0Height = 0;
        uint32_t mip0DataSize = 0;
    };

    // Reads the .vfImage header laid down by Texture::saveToFileTextureWithMips.
    bool readVfImageHeader(const std::filesystem::path& path, VfImageHeader& out)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;

        out.fileType = resource::endian::readLE<uint8_t>(in);
        resource::endian::readLE<uint32_t>(in); // version major
        resource::endian::readLE<uint32_t>(in); // version minor
        resource::endian::readLE<uint32_t>(in); // version patch
        out.width = resource::endian::readLE<uint32_t>(in);
        out.height = resource::endian::readLE<uint32_t>(in);
        out.channels = resource::endian::readLE<uint32_t>(in);
        out.mipLevels = resource::endian::readLE<uint32_t>(in);
        out.compressionFormat = resource::endian::readLE<uint8_t>(in);
        out.mip0Width = resource::endian::readLE<uint32_t>(in);
        out.mip0Height = resource::endian::readLE<uint32_t>(in);
        out.mip0DataSize = resource::endian::readLE<uint32_t>(in);
        return static_cast<bool>(in);
    }

    // Builds a width*height BGRA8 buffer (Assimp aiTexel layout) with a flat color.
    std::vector<unsigned char> makeBGRA(uint32_t width, uint32_t height,
                                        unsigned char b, unsigned char g, unsigned char r, unsigned char a)
    {
        std::vector<unsigned char> data(static_cast<size_t>(width) * height * 4);
        for (size_t i = 0; i < data.size(); i += 4)
        {
            data[i + 0] = b;
            data[i + 1] = g;
            data[i + 2] = r;
            data[i + 3] = a;
        }
        return data;
    }

    std::filesystem::path makeTempDir(const char* tag)
    {
        std::filesystem::path dir =
            std::filesystem::temp_directory_path() / (std::string("vf_vk55_") + tag);
        std::filesystem::create_directories(dir);
        return dir;
    }
}

TEST_CASE("VK-55 embedded texture: raw BGRA uncompressed round-trips to .vfImage")
{
    const std::filesystem::path dir = makeTempDir("raw_uncompressed");
    const std::string stem = "embedded_raw";

    importConfig::ImportConfig config;
    config.compressionMode = importConfig::TextureCompressionMode::Uncompressed;

    // 4x4 BGRA texels: blue=10, green=20, red=30, alpha=255.
    const uint32_t w = 4;
    const uint32_t h = 4;
    const std::vector<unsigned char> bgra = makeBGRA(w, h, 10, 20, 30, 255);

    types::Texture texture;
    const bool ok = texture.saveEmbeddedTexture(stem, dir.string(), bgra.data(), bgra.size(),
                                                /*isCompressed=*/false, w, h, config);
    REQUIRE(ok);

    const std::filesystem::path out = dir / (stem + "." + FileExtension::textrue);
    REQUIRE(std::filesystem::exists(out));

    VfImageHeader hdr;
    REQUIRE(readVfImageHeader(out, hdr));
    CHECK(hdr.fileType == 0);            // FileType::TEXTURE
    CHECK(hdr.width == w);
    CHECK(hdr.height == h);
    CHECK(hdr.channels == 4);
    CHECK(hdr.mipLevels >= 1);          // 4x4 -> at least mips 4,2,1
    CHECK(hdr.compressionFormat == 0);  // Uncompressed
    CHECK(hdr.mip0Width == w);
    CHECK(hdr.mip0Height == h);

    // Uncompressed mip0 is stored as BGRA (TGA-style); verify the first texel
    // survives the BGRA -> RGBA -> BGRA round-trip.
    // Header = 1 (fileType) + 12 (version) + 16 (w,h,ch,mips) + 1 (compression)
    //        = 30 bytes; mip0 header = 12 bytes; pixel data starts at offset 42.
    std::ifstream in(out, std::ios::binary);
    REQUIRE(in);
    in.seekg(static_cast<std::streamoff>(42), std::ios::beg);
    unsigned char texel[4] = {0, 0, 0, 0};
    in.read(reinterpret_cast<char*>(texel), 4);
    REQUIRE(in);
    CHECK(texel[0] == 10);  // B
    CHECK(texel[1] == 20);  // G
    CHECK(texel[2] == 30);  // R
    CHECK(texel[3] == 255); // A

    std::filesystem::remove_all(dir);
}

TEST_CASE("VK-55 embedded texture: BC compression mode produces a compressed .vfImage")
{
    const std::filesystem::path dir = makeTempDir("raw_bc");
    const std::string stem = "embedded_bc";

    importConfig::ImportConfig config;
    config.compressionMode = importConfig::TextureCompressionMode::BC;
    config.compressionQuality = importConfig::TextureCompressionQuality::Fast;

    const uint32_t w = 8;
    const uint32_t h = 8;
    const std::vector<unsigned char> bgra = makeBGRA(w, h, 200, 100, 50, 255);

    types::Texture texture;
    const bool ok = texture.saveEmbeddedTexture(stem, dir.string(), bgra.data(), bgra.size(),
                                                /*isCompressed=*/false, w, h, config);
    REQUIRE(ok);

    const std::filesystem::path out = dir / (stem + "." + FileExtension::textrue);
    REQUIRE(std::filesystem::exists(out));

    VfImageHeader hdr;
    REQUIRE(readVfImageHeader(out, hdr));
    CHECK(hdr.width == w);
    CHECK(hdr.height == h);
    CHECK(hdr.mipLevels >= 1);
    CHECK(hdr.compressionFormat == 1); // BC7
    CHECK(hdr.mip0DataSize > 0);       // compressed payload size is explicit

    std::filesystem::remove_all(dir);
}

TEST_CASE("VK-55 embedded texture: invalid inputs are rejected")
{
    const std::filesystem::path dir = makeTempDir("invalid");
    importConfig::ImportConfig config;
    config.compressionMode = importConfig::TextureCompressionMode::Uncompressed;

    types::Texture texture;

    // Null / empty data.
    CHECK_FALSE(texture.saveEmbeddedTexture("none", dir.string(), nullptr, 0, false, 4, 4, config));

    // Raw buffer too small for the claimed dimensions.
    const std::vector<unsigned char> tooSmall(8, 0); // need 4*4*4 = 64 bytes
    CHECK_FALSE(texture.saveEmbeddedTexture("small", dir.string(), tooSmall.data(), tooSmall.size(),
                                            /*isCompressed=*/false, 4, 4, config));

    std::filesystem::remove_all(dir);
}
