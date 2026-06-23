#include <doctest.h>
#include <controllers/Import.hpp>
#include <registry/builtin/BuiltinImporters.hpp>
#include <config/Config.hpp>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

// ============================================================
// VK-194: one .vfMesh per mesh in an imported model (Import.dll)
//
// Driven end-to-end through the public controllers::Import facade: a model
// with several meshes must produce a SEPARATE .vfMesh (+ .vfmeta) per mesh,
// each with a file-header numMeshes == 1, and one ImportFileResult each. A
// single-mesh model keeps the bare <fileName>.vfMesh name (no suffix) so
// existing references stay valid.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    // Two distinct objects -> Assimp's OBJ importer yields two aiMeshes.
    // OBJ vertex/uv/normal indices are global and 1-based across the file.
    constexpr const char* twoMeshObj =
        "o TriA\n"
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "vt 0 0\nvt 1 0\nvt 0 1\n"
        "vn 0 0 1\n"
        "f 1/1/1 2/2/1 3/3/1\n"
        "o TriB\n"
        "v 2 0 0\nv 3 0 0\nv 2 1 0\n"
        "vt 0 0\nvt 1 0\nvt 0 1\n"
        "vn 0 0 1\n"
        "f 4/4/2 5/5/2 6/6/2\n";

    constexpr const char* oneMeshObj =
        "o Solo\n"
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "vt 0 0\nvt 1 0\nvt 0 1\n"
        "vn 0 0 1\n"
        "f 1/1/1 2/2/1 3/3/1\n";

    fs::path makeScratchDir(const char* name)
    {
        fs::path dir = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        return dir;
    }

    void writeText(const fs::path& path, const char* text)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(text, static_cast<std::streamsize>(std::strlen(text)));
    }

    // Reads the uint32 numMeshes field of the .vfMesh header. Layout
    // (see writeFileHeader in Mesh.cpp): uint8 fileType, uint32 major,
    // uint32 minor, uint32 patch, uint32 numMeshes, ... => offset 13.
    uint32_t readHeaderNumMeshes(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        in.seekg(13, std::ios::beg);
        unsigned char b[4] = {};
        in.read(reinterpret_cast<char*>(b), 4);
        REQUIRE(in.gcount() == 4);
        return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
               (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
    }

    controllers::ImportResult importObj(const fs::path& dir, const fs::path& src)
    {
        import::builtin::ensureRegistered();
        controllers::Import::setLocation(dir.string());
        importConfig::ImportConfig config;
        return controllers::Import::importFiles({importConfig::ImportFiles(src.string(), config)});
    }
}

TEST_SUITE("MeshImport")
{
    TEST_CASE("multi-mesh model exports one .vfMesh per mesh")
    {
        fs::path dir = makeScratchDir("vf_vk194_multi");
        fs::path src = dir / "twomesh.obj";
        writeText(src, twoMeshObj);

        auto result = importObj(dir, src);

        // Two meshes => two separate results, each a standalone .vfMesh asset.
        CHECK(result.failureCount == 0);
        REQUIRE(result.fileResults.size() == 2);
        CHECK(result.successCount == 2);

        const auto& a = result.fileResults[0];
        const auto& b = result.fileResults[1];
        CHECK(a.outputPath != b.outputPath);

        for (const auto& r : {a, b})
        {
            CAPTURE(r.outputPath);
            CHECK(r.success);
            CHECK(fs::path(r.outputPath).extension() == "." + FileExtension::mesh);
            CHECK(fs::exists(r.outputPath));
            // Distinct, base-prefixed names: twomesh_<meshName>.vfMesh.
            CHECK(fs::path(r.outputPath).stem().string().rfind("twomesh_", 0) == 0);
            // Each per-mesh file holds exactly one mesh.
            CHECK(readHeaderNumMeshes(r.outputPath) == 1u);
            // Each asset gets its own .vfmeta sidecar.
            fs::path meta = r.outputPath;
            meta += "." + FileExtension::assetMeta;
            CHECK(fs::exists(meta));
        }

        // The split uses the Assimp mesh names when available.
        const std::string both = a.outputPath + "|" + b.outputPath;
        CHECK(both.find("TriA") != std::string::npos);
        CHECK(both.find("TriB") != std::string::npos);

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("single-mesh model keeps the bare file name")
    {
        fs::path dir = makeScratchDir("vf_vk194_single");
        fs::path src = dir / "single.obj";
        writeText(src, oneMeshObj);

        auto result = importObj(dir, src);

        CHECK(result.failureCount == 0);
        REQUIRE(result.fileResults.size() == 1);
        CHECK(result.successCount == 1);

        const fs::path expected = dir / ("single." + FileExtension::mesh);
        REQUIRE(fs::exists(result.fileResults[0].outputPath));
        CHECK(fs::equivalent(fs::path(result.fileResults[0].outputPath), expected));
        CHECK(readHeaderNumMeshes(result.fileResults[0].outputPath) == 1u);

        std::error_code ec;
        fs::remove_all(dir, ec);
    }
}
