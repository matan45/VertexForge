#include <doctest.h>

#include <config/Config.hpp>
#include <resource/EndianUtils.hpp>
#include <types/Texture.hpp>
#include <types/TextureCompressor.hpp>
#include <controllers/Import.hpp>
#include <registry/builtin/BuiltinImporters.hpp>

#include <cstdint>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

// ============================================================
// VK-1642: KTX/KTX2 texture import (Import.dll).
//
// KTX1 containers are fully synthesizable in-test (fixed header), so they cover
// the shared machinery: header parse, mip loop, block passthrough, uncompressed
// read, the write-assembly, and the full controllers::Import -> .vfImage/.vfmeta
// path. The KTX2 (Basis Universal) decode path needs a real ETC1S/UASTC fixture
// (only the transcoder, not the encoder, is vendored) and is exercised by a
// fixture-guarded case.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    // GL enums used by KTX1 headers.
    constexpr uint32_t GL_UNSIGNED_BYTE = 0x1401;
    constexpr uint32_t GL_RGBA = 0x1908;
    constexpr uint32_t GL_RGBA8 = 0x8058;
    constexpr uint32_t GL_COMPRESSED_RGBA_BPTC_UNORM = 0x8E8C;

    struct VfImageHeader
    {
        uint8_t fileType = 0xFF;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t channels = 0;
        uint32_t mipLevels = 0;
        uint8_t compressionFormat = 0xFF;
        uint32_t mip0Width = 0;
        uint32_t mip0Height = 0;
        uint32_t mip0DataSize = 0;
    };

    bool readVfImageHeader(const fs::path& path, VfImageHeader& out)
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

    std::vector<unsigned char> solidRgba8(uint32_t w, uint32_t h,
                                          unsigned char r, unsigned char g, unsigned char b, unsigned char a)
    {
        std::vector<unsigned char> d(static_cast<size_t>(w) * h * 4);
        for (size_t i = 0; i < d.size(); i += 4)
        {
            d[i + 0] = r; d[i + 1] = g; d[i + 2] = b; d[i + 3] = a;
        }
        return d;
    }

    void writeFile(const fs::path& path, const std::vector<unsigned char>& bytes)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    // Assembles a little-endian KTX1 file with a single 2D mip level.
    std::vector<unsigned char> buildKtx1(uint32_t glType, uint32_t glTypeSize, uint32_t glFormat,
                                         uint32_t glInternalFormat, uint32_t glBaseInternalFormat,
                                         uint32_t w, uint32_t h, const std::vector<unsigned char>& levelData)
    {
        std::vector<unsigned char> f;
        auto put32 = [&](uint32_t v)
        {
            f.push_back(static_cast<unsigned char>(v & 0xFF));
            f.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
            f.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
            f.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
        };
        const unsigned char id[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
        f.insert(f.end(), id, id + 12);
        put32(0x04030201);           // endianness (little-endian)
        put32(glType);
        put32(glTypeSize);
        put32(glFormat);
        put32(glInternalFormat);
        put32(glBaseInternalFormat);
        put32(w);                    // pixelWidth
        put32(h);                    // pixelHeight
        put32(0);                    // pixelDepth
        put32(0);                    // numberOfArrayElements
        put32(1);                    // numberOfFaces
        put32(1);                    // numberOfMipmapLevels
        put32(0);                    // bytesOfKeyValueData
        put32(static_cast<uint32_t>(levelData.size())); // imageSize
        f.insert(f.end(), levelData.begin(), levelData.end());
        const size_t pad = (3 - ((levelData.size() + 3) & 3)); // mip padding to 4 bytes
        for (size_t i = 0; i < pad; ++i) f.push_back(0);
        return f;
    }

    fs::path makeTempDir(const char* tag)
    {
        fs::path dir = fs::temp_directory_path() / (std::string("vf_vk1642_ktx_") + tag);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        return dir;
    }
}

TEST_SUITE("KtxImport")
{
    TEST_CASE("KTX1 uncompressed RGBA8 imports to .vfImage")
    {
        const fs::path dir = makeTempDir("rgba8");
        const auto rgba = solidRgba8(4, 4, 30, 20, 10, 255); // R,G,B,A
        writeFile(dir / "solid.ktx", buildKtx1(GL_UNSIGNED_BYTE, 1, GL_RGBA, GL_RGBA8, GL_RGBA, 4, 4, rgba));

        importConfig::ImportConfig config;
        config.compressionMode = importConfig::TextureCompressionMode::Uncompressed;

        types::Texture texture;
        const auto result = texture.loadKtxFile(
            importConfig::ImportFiles((dir / "solid.ktx").string(), config), "solid", dir.string());
        CHECK(result == types::TextureImportResult::WroteVfImage);

        const fs::path out = dir / ("solid." + FileExtension::textrue);
        REQUIRE(fs::exists(out));

        VfImageHeader hdr;
        REQUIRE(readVfImageHeader(out, hdr));
        CHECK(hdr.fileType == 0);           // FileType::TEXTURE
        CHECK(hdr.width == 4);
        CHECK(hdr.height == 4);
        CHECK(hdr.channels == 4);
        CHECK(hdr.compressionFormat == 0);  // Uncompressed

        // Uncompressed mip0 is stored BGRA (TGA-style) at offset 42.
        std::ifstream in(out, std::ios::binary);
        in.seekg(42, std::ios::beg);
        unsigned char texel[4] = {0, 0, 0, 0};
        in.read(reinterpret_cast<char*>(texel), 4);
        CHECK(texel[0] == 10);  // B
        CHECK(texel[1] == 20);  // G
        CHECK(texel[2] == 30);  // R
        CHECK(texel[3] == 255); // A

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("KTX1 BC7 blocks pass through to .vfImage without recompression")
    {
        const fs::path dir = makeTempDir("bc7");
        const auto rgba = solidRgba8(8, 8, 200, 100, 50, 255);
        const auto bc7 = types::TextureCompressor::compressBC7(rgba.data(), 8, 8,
                                                               importConfig::TextureCompressionQuality::Fast);
        REQUIRE(bc7.size() == 4u * 16u); // 2x2 blocks

        writeFile(dir / "bc7.ktx",
                  buildKtx1(0, 1, 0, GL_COMPRESSED_RGBA_BPTC_UNORM, GL_RGBA, 8, 8, bc7));

        importConfig::ImportConfig config; // BC mode; passthrough ignores it anyway
        types::Texture texture;
        const auto result = texture.loadKtxFile(
            importConfig::ImportFiles((dir / "bc7.ktx").string(), config), "bc7", dir.string());
        CHECK(result == types::TextureImportResult::WroteVfImage);

        const fs::path out = dir / ("bc7." + FileExtension::textrue);
        REQUIRE(fs::exists(out));

        VfImageHeader hdr;
        REQUIRE(readVfImageHeader(out, hdr));
        CHECK(hdr.width == 8);
        CHECK(hdr.height == 8);
        CHECK(hdr.compressionFormat == 1); // BC7
        CHECK(hdr.mipLevels == 1);         // source had 1 level; passthrough preserves it
        CHECK(hdr.mip0DataSize == 64);

        // Passthrough correctness: stored blocks are byte-identical to the source.
        std::ifstream in(out, std::ios::binary);
        in.seekg(42, std::ios::beg);
        std::vector<unsigned char> stored(64);
        in.read(reinterpret_cast<char*>(stored.data()), 64);
        CHECK(stored == bc7);

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("KTX import through controllers::Import writes .vfImage + .vfmeta")
    {
        const fs::path dir = makeTempDir("pipeline");
        const auto rgba = solidRgba8(4, 4, 10, 20, 30, 255);
        writeFile(dir / "pipe.ktx", buildKtx1(GL_UNSIGNED_BYTE, 1, GL_RGBA, GL_RGBA8, GL_RGBA, 4, 4, rgba));

        import::builtin::ensureRegistered();
        controllers::Import::setLocation(dir.string());
        importConfig::ImportConfig config;
        config.compressionMode = importConfig::TextureCompressionMode::Uncompressed;

        auto result = controllers::Import::importFiles(
            {importConfig::ImportFiles((dir / "pipe.ktx").string(), config)});
        CHECK(result.failureCount == 0);

        const fs::path out = dir / ("pipe." + FileExtension::textrue);
        CHECK(fs::exists(out));
        fs::path meta = out;
        meta += "." + FileExtension::assetMeta;
        CHECK(fs::exists(meta)); // .vfImage.vfmeta sidecar

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("truncated KTX is rejected, no output written")
    {
        const fs::path dir = makeTempDir("invalid");
        // Valid KTX1 identifier but no header body (< 64 bytes) -> rejected.
        std::vector<unsigned char> bad = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
        writeFile(dir / "bad.ktx", bad);

        importConfig::ImportConfig config;
        types::Texture texture;
        const auto result = texture.loadKtxFile(
            importConfig::ImportFiles((dir / "bad.ktx").string(), config), "bad", dir.string());
        CHECK(result == types::TextureImportResult::Failed);
        CHECK_FALSE(fs::exists(dir / ("bad." + FileExtension::textrue)));

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("KTX2 Basis fixture imports to .vfImage" * doctest::skip(true))
    {
        // Enable by removing skip(true) and dropping a small ETC1S/UASTC .ktx2 at
        // this path (e.g. `toktx --encode etc1s --t2 test.ktx2 img.png`). The Basis
        // encoder is not vendored, so the fixture must be generated offline.
        const fs::path fixture = fs::path(__FILE__).parent_path() / "fixtures" / "test.ktx2";
        if (!fs::exists(fixture))
        {
            MESSAGE("KTX2 fixture absent (" << fixture.string() << "); skipping basis-decode test");
            return;
        }

        const fs::path dir = makeTempDir("ktx2");
        importConfig::ImportConfig config;
        types::Texture texture;
        const auto result = texture.loadKtxFile(
            importConfig::ImportFiles(fixture.string(), config), "basis", dir.string());
        CHECK((result == types::TextureImportResult::WroteVfImage ||
               result == types::TextureImportResult::WroteVfHdr));

        std::error_code ec;
        fs::remove_all(dir, ec);
    }
}
