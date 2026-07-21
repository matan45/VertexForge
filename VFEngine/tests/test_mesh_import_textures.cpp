#include <doctest.h>
#include <controllers/Import.hpp>
#include <registry/builtin/BuiltinImporters.hpp>
#include <config/Config.hpp>

#include <cstdint>
#include <fstream>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// ============================================================
// Material-referenced texture import (VK-1641, Import.dll)
//
// With importMaterialTextures ON, the mesh importer walks the scene's materials and
// imports every referenced texture as a .vfImage next to the model. Materials
// themselves are NOT created. Driven end-to-end through controllers::Import.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    // One object whose material references an external diffuse map (map_Kd).
    constexpr const char* texturedMtl =
        "newmtl Textured\nKd 1 1 1\nmap_Kd surface.tga\n";

    constexpr const char* texturedObj =
        "mtllib textured.mtl\n"
        "o Quad\n"
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "vt 0 0\nvt 1 0\nvt 0 1\n"
        "vn 0 0 1\n"
        "usemtl Textured\n"
        "f 1/1/1 2/2/1 3/3/1\n";

    fs::path makeScratchDir(const char* name)
    {
        fs::path dir = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        return dir;
    }

    void writeText(const fs::path& path, std::string_view text)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    // Writes a minimal uncompressed 24-bit BGR TGA (18-byte header + raw pixels).
    // stb_image (used by the texture importer) decodes this directly — no PNG CRC/zlib
    // authoring needed. 16x16 is a clean multiple of 4 and below the 512px mip floor.
    void writeSolidTga(const fs::path& path, uint16_t w, uint16_t h)
    {
        std::vector<unsigned char> b;
        const auto put16 = [&](uint16_t v)
        {
            b.push_back(static_cast<unsigned char>(v & 0xFF));
            b.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
        };
        b.push_back(0);   // ID length
        b.push_back(0);   // color map type (none)
        b.push_back(2);   // image type: uncompressed true-color
        put16(0);         // color map first entry
        put16(0);         // color map length
        b.push_back(0);   // color map entry size
        put16(0);         // x-origin
        put16(0);         // y-origin
        put16(w);         // width
        put16(h);         // height
        b.push_back(24);  // bits per pixel
        b.push_back(0);   // image descriptor
        for (int i = 0; i < static_cast<int>(w) * static_cast<int>(h); ++i)
        {
            b.push_back(0x20); // B
            b.push_back(0x40); // G
            b.push_back(0x80); // R
        }

        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    }

    // A material that references a DDS diffuse map (VK-1642 routing through the gate).
    constexpr const char* ddsMtl =
        "newmtl Textured\nKd 1 1 1\nmap_Kd surface.dds\n";

    // Minimal uncompressed A8R8G8B8 DDS (128-byte header + BGRA pixels). Exercises
    // Texture::loadDdsFile via the material-import gate.
    void writeSolidDds(const fs::path& path, uint32_t w, uint32_t h)
    {
        std::vector<unsigned char> f;
        const auto put32 = [&](uint32_t v)
        {
            f.push_back(static_cast<unsigned char>(v & 0xFF));
            f.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
            f.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
            f.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
        };
        f.push_back('D'); f.push_back('D'); f.push_back('S'); f.push_back(' ');
        put32(124); put32(0x1007); put32(h); put32(w); put32(w * 4); put32(0); put32(1);
        for (int i = 0; i < 11; ++i) put32(0);
        put32(32); put32(0x41); put32(0); put32(32);
        put32(0x00FF0000u); put32(0x0000FF00u); put32(0x000000FFu); put32(0xFF000000u);
        put32(0x1000); put32(0); put32(0); put32(0); put32(0);
        for (uint32_t i = 0; i < w * h; ++i)
        {
            f.push_back(0x20); f.push_back(0x40); f.push_back(0x80); f.push_back(0xFF); // B,G,R,A
        }
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(f.data()), static_cast<std::streamsize>(f.size()));
    }

    controllers::ImportResult importModelWithTextures(const fs::path& dir, const fs::path& src,
                                                      bool importTextures)
    {
        import::builtin::ensureRegistered();
        controllers::Import::setLocation(dir.string());
        importConfig::ImportConfig config;
        config.customOptions["importMaterialTextures"] = importTextures;
        // Keep the test off the BC7 path — existence of the .vfImage is what we assert.
        config.compressionMode = importConfig::TextureCompressionMode::Uncompressed;
        return controllers::Import::importFiles({importConfig::ImportFiles(src.string(), config)});
    }
}

TEST_SUITE("MeshTextureImport")
{
    TEST_CASE("importMaterialTextures imports referenced textures as .vfImage")
    {
        fs::path dir = makeScratchDir("vf_texture_import");
        writeSolidTga(dir / "surface.tga", 16, 16);
        writeText(dir / "textured.mtl", texturedMtl);
        fs::path src = dir / "textured.obj";
        writeText(src, texturedObj);

        auto result = importModelWithTextures(dir, src, true);

        CHECK(result.failureCount == 0);
        REQUIRE(result.fileResults.size() >= 1);

        // The referenced texture is imported next to the model, namespaced by the model
        // stem: textured_surface.vfImage, with its own .vfmeta sidecar.
        const fs::path vfImage = dir / ("textured_surface." + FileExtension::textrue);
        CHECK(fs::exists(vfImage));

        fs::path vfImageMeta = vfImage;
        vfImageMeta += "." + FileExtension::assetMeta;
        CHECK(fs::exists(vfImageMeta));

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("importMaterialTextures imports a material-referenced DDS as .vfImage (VK-1642)")
    {
        fs::path dir = makeScratchDir("vf_texture_import_dds");
        writeSolidDds(dir / "surface.dds", 16, 16);
        writeText(dir / "textured.mtl", ddsMtl);
        fs::path src = dir / "textured.obj";
        writeText(src, texturedObj);

        auto result = importModelWithTextures(dir, src, true);

        CHECK(result.failureCount == 0);
        // The DDS is no longer skipped; it imports next to the model, namespaced.
        const fs::path vfImage = dir / ("textured_surface." + FileExtension::textrue);
        CHECK(fs::exists(vfImage));

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("importMaterialTextures off imports no textures")
    {
        fs::path dir = makeScratchDir("vf_texture_import_off");
        writeSolidTga(dir / "surface.tga", 16, 16);
        writeText(dir / "textured.mtl", texturedMtl);
        fs::path src = dir / "textured.obj";
        writeText(src, texturedObj);

        auto result = importModelWithTextures(dir, src, false);

        CHECK(result.failureCount == 0);
        // No .vfImage is produced when the option is off.
        CHECK_FALSE(fs::exists(dir / ("textured_surface." + FileExtension::textrue)));

        std::error_code ec;
        fs::remove_all(dir, ec);
    }
}
