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
// VK-1642: DDS texture import (Import.dll).
//
// DDS containers are synthesizable in-test: uncompressed (channel masks) and
// BC7 via the DX10 header. Covers the parser, the passthrough path, and the
// full controllers::Import -> .vfImage/.vfmeta flow.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    struct VfImageHeader
    {
        uint8_t fileType = 0xFF;
        uint32_t width = 0, height = 0, channels = 0, mipLevels = 0;
        uint8_t compressionFormat = 0xFF;
        uint32_t mip0Width = 0, mip0Height = 0, mip0DataSize = 0;
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

    void writeFile(const fs::path& path, const std::vector<unsigned char>& bytes)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
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

    // 4-byte magic + 124-byte DDS_HEADER (128 total).
    std::vector<unsigned char> buildDdsBase(uint32_t w, uint32_t h, uint32_t linearOrPitch,
                                            uint32_t pfFlags, uint32_t fourCC, uint32_t bitCount,
                                            uint32_t rMask, uint32_t gMask, uint32_t bMask, uint32_t aMask)
    {
        std::vector<unsigned char> f;
        auto put32 = [&](uint32_t v)
        {
            f.push_back(static_cast<unsigned char>(v & 0xFF));
            f.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
            f.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
            f.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
        };
        f.push_back('D'); f.push_back('D'); f.push_back('S'); f.push_back(' ');
        put32(124);           // dwSize
        put32(0x1007);        // dwFlags (CAPS|HEIGHT|WIDTH|PIXELFORMAT)
        put32(h);             // dwHeight
        put32(w);             // dwWidth
        put32(linearOrPitch); // dwPitchOrLinearSize
        put32(0);             // dwDepth
        put32(1);             // dwMipMapCount
        for (int i = 0; i < 11; ++i) put32(0); // dwReserved1[11]
        put32(32);            // ddspf.dwSize
        put32(pfFlags);       // ddspf.dwFlags
        put32(fourCC);        // ddspf.dwFourCC
        put32(bitCount);      // ddspf.dwRGBBitCount
        put32(rMask); put32(gMask); put32(bMask); put32(aMask);
        put32(0x1000);        // dwCaps (TEXTURE)
        put32(0); put32(0); put32(0); // caps2/3/4
        put32(0);             // dwReserved2
        return f;
    }

    // A8R8G8B8 (bytes on disk are B,G,R,A per pixel).
    std::vector<unsigned char> buildDdsBgra8(uint32_t w, uint32_t h, const std::vector<unsigned char>& bgra)
    {
        auto f = buildDdsBase(w, h, w * 4, /*DDPF_RGB|ALPHA*/ 0x41, 0, 32,
                              0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
        f.insert(f.end(), bgra.begin(), bgra.end());
        return f;
    }

    std::vector<unsigned char> buildDdsDx10(uint32_t w, uint32_t h, uint32_t dxgiFormat, uint32_t linearSize,
                                            const std::vector<unsigned char>& data)
    {
        const uint32_t dx10 = uint32_t('D') | (uint32_t('X') << 8) | (uint32_t('1') << 16) | (uint32_t('0') << 24);
        auto f = buildDdsBase(w, h, linearSize, /*DDPF_FOURCC*/ 0x4, dx10, 0, 0, 0, 0, 0);
        auto put32 = [&](uint32_t v)
        {
            f.push_back(static_cast<unsigned char>(v & 0xFF));
            f.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
            f.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
            f.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
        };
        put32(dxgiFormat); // dxgiFormat
        put32(3);          // resourceDimension = TEXTURE2D
        put32(0);          // miscFlag
        put32(1);          // arraySize
        put32(0);          // miscFlags2
        f.insert(f.end(), data.begin(), data.end());
        return f;
    }

    fs::path makeTempDir(const char* tag)
    {
        fs::path dir = fs::temp_directory_path() / (std::string("vf_vk1642_dds_") + tag);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        return dir;
    }
}

TEST_SUITE("DdsImport")
{
    TEST_CASE("DDS uncompressed A8R8G8B8 imports to .vfImage")
    {
        const fs::path dir = makeTempDir("bgra8");
        // On-disk B,G,R,A. Target color R=30,G=20,B=10,A=255.
        std::vector<unsigned char> bgra(4u * 4u * 4u);
        for (size_t i = 0; i < bgra.size(); i += 4)
        {
            bgra[i + 0] = 10;  // B
            bgra[i + 1] = 20;  // G
            bgra[i + 2] = 30;  // R
            bgra[i + 3] = 255; // A
        }
        writeFile(dir / "solid.dds", buildDdsBgra8(4, 4, bgra));

        importConfig::ImportConfig config;
        config.compressionMode = importConfig::TextureCompressionMode::Uncompressed;

        types::Texture texture;
        const auto result = texture.loadDdsFile(
            importConfig::ImportFiles((dir / "solid.dds").string(), config), "solid", dir.string());
        CHECK(result == types::TextureImportResult::WroteVfImage);

        const fs::path out = dir / ("solid." + FileExtension::textrue);
        REQUIRE(fs::exists(out));

        VfImageHeader hdr;
        REQUIRE(readVfImageHeader(out, hdr));
        CHECK(hdr.fileType == 0);
        CHECK(hdr.width == 4);
        CHECK(hdr.height == 4);
        CHECK(hdr.compressionFormat == 0); // Uncompressed

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

    TEST_CASE("DDS BC7 (DX10) blocks pass through to .vfImage")
    {
        const fs::path dir = makeTempDir("bc7");
        const auto rgba = solidRgba8(8, 8, 200, 100, 50, 255);
        const auto bc7 = types::TextureCompressor::compressBC7(rgba.data(), 8, 8,
                                                               importConfig::TextureCompressionQuality::Fast);
        REQUIRE(bc7.size() == 4u * 16u);

        writeFile(dir / "bc7.dds", buildDdsDx10(8, 8, /*DXGI_FORMAT_BC7_UNORM*/ 98,
                                                static_cast<uint32_t>(bc7.size()), bc7));

        importConfig::ImportConfig config;
        types::Texture texture;
        const auto result = texture.loadDdsFile(
            importConfig::ImportFiles((dir / "bc7.dds").string(), config), "bc7", dir.string());
        CHECK(result == types::TextureImportResult::WroteVfImage);

        const fs::path out = dir / ("bc7." + FileExtension::textrue);
        REQUIRE(fs::exists(out));

        VfImageHeader hdr;
        REQUIRE(readVfImageHeader(out, hdr));
        CHECK(hdr.width == 8);
        CHECK(hdr.height == 8);
        CHECK(hdr.compressionFormat == 1); // BC7
        CHECK(hdr.mip0DataSize == 64);

        std::ifstream in(out, std::ios::binary);
        in.seekg(42, std::ios::beg);
        std::vector<unsigned char> stored(64);
        in.read(reinterpret_cast<char*>(stored.data()), 64);
        CHECK(stored == bc7);

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("DDS import through controllers::Import writes .vfImage + .vfmeta")
    {
        const fs::path dir = makeTempDir("pipeline");
        std::vector<unsigned char> bgra(4u * 4u * 4u, 0);
        for (size_t i = 0; i < bgra.size(); i += 4) { bgra[i + 3] = 255; }
        writeFile(dir / "pipe.dds", buildDdsBgra8(4, 4, bgra));

        import::builtin::ensureRegistered();
        controllers::Import::setLocation(dir.string());
        importConfig::ImportConfig config;
        config.compressionMode = importConfig::TextureCompressionMode::Uncompressed;

        auto result = controllers::Import::importFiles(
            {importConfig::ImportFiles((dir / "pipe.dds").string(), config)});
        CHECK(result.failureCount == 0);

        const fs::path out = dir / ("pipe." + FileExtension::textrue);
        CHECK(fs::exists(out));
        fs::path meta = out;
        meta += "." + FileExtension::assetMeta;
        CHECK(fs::exists(meta));

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("truncated DDS is rejected, no output written")
    {
        const fs::path dir = makeTempDir("invalid");
        writeFile(dir / "bad.dds", {'D', 'D', 'S', ' '}); // magic only, < 128 bytes

        importConfig::ImportConfig config;
        types::Texture texture;
        const auto result = texture.loadDdsFile(
            importConfig::ImportFiles((dir / "bad.dds").string(), config), "bad", dir.string());
        CHECK(result == types::TextureImportResult::Failed);
        CHECK_FALSE(fs::exists(dir / ("bad." + FileExtension::textrue)));

        std::error_code ec;
        fs::remove_all(dir, ec);
    }
}
