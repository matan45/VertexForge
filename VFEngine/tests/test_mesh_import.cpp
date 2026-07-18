#include <doctest.h>
#include <controllers/Import.hpp>
#include <registry/builtin/BuiltinImporters.hpp>
#include <config/Config.hpp>
#include <resource/MeshStreamHandle.hpp>

#include <cstdint>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// ============================================================
// Multi-mesh import layouts (Import.dll)
//
// Driven end-to-end through the public controllers::Import facade. Split mode
// preserves the existing one-asset-per-mesh behavior. Opt-in combined mode
// emits one multi-submesh asset that one MeshComponent can render in full.
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

    // One geometry instanced by two nodes. The second instance is mirrored,
    // exercising accumulated transforms, duplicate-name disambiguation, and
    // winding correction in combined mode.
    constexpr const char* transformedInstancesDae = R"dae(<?xml version="1.0"?><COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
  <asset><unit name="meter" meter="1"/><up_axis>Y_UP</up_axis></asset>
  <library_geometries>
    <geometry id="SharedTri" name="SharedTri"><mesh>
      <source id="SharedTri-positions">
        <float_array id="SharedTri-positions-array" count="9">0 0 0  1 0 0  0 1 0</float_array>
        <technique_common><accessor source="#SharedTri-positions-array" count="3" stride="3">
          <param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/>
        </accessor></technique_common>
      </source>
      <source id="SharedTri-normals">
        <float_array id="SharedTri-normals-array" count="9">0 0 1  0 0 1  0 0 1</float_array>
        <technique_common><accessor source="#SharedTri-normals-array" count="3" stride="3">
          <param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/>
        </accessor></technique_common>
      </source>
      <vertices id="SharedTri-vertices"><input semantic="POSITION" source="#SharedTri-positions"/></vertices>
      <triangles count="1">
        <input semantic="VERTEX" source="#SharedTri-vertices" offset="0"/>
        <input semantic="NORMAL" source="#SharedTri-normals" offset="1"/>
        <p>0 0 1 1 2 2</p>
      </triangles>
    </mesh></geometry>
  </library_geometries>
  <library_visual_scenes><visual_scene id="Scene" name="Scene">
    <node id="InstanceA" name="InstanceA"><translate sid="location">10 0 0</translate><instance_geometry url="#SharedTri"/></node>
    <node id="InstanceB" name="InstanceB"><matrix>-1 0 0 20  0 1 0 0  0 0 1 0  0 0 0 1</matrix><instance_geometry url="#SharedTri"/></node>
  </visual_scene></library_visual_scenes>
  <scene><instance_visual_scene url="#Scene"/></scene>
</COLLADA>)dae";

    fs::path makeScratchDir(const char* name)
    {
        fs::path dir = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        return dir;
    }

    std::string makeAnimatedInstancesDae()
    {
        constexpr std::string_view animationLibrary = R"dae(
  <library_animations>
    <animation id="InstanceA-location-animation">
      <source id="InstanceA-location-input">
        <float_array id="InstanceA-location-input-array" count="2">0 1</float_array>
        <technique_common><accessor source="#InstanceA-location-input-array" count="2">
          <param name="TIME" type="float"/>
        </accessor></technique_common>
      </source>
      <source id="InstanceA-location-output">
        <float_array id="InstanceA-location-output-array" count="6">10 0 0  11 0 0</float_array>
        <technique_common><accessor source="#InstanceA-location-output-array" count="2" stride="3">
          <param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/>
        </accessor></technique_common>
      </source>
      <source id="InstanceA-location-interpolation">
        <Name_array id="InstanceA-location-interpolation-array" count="2">LINEAR LINEAR</Name_array>
        <technique_common><accessor source="#InstanceA-location-interpolation-array" count="2">
          <param name="INTERPOLATION" type="Name"/>
        </accessor></technique_common>
      </source>
      <sampler id="InstanceA-location-sampler">
        <input semantic="INPUT" source="#InstanceA-location-input"/>
        <input semantic="OUTPUT" source="#InstanceA-location-output"/>
        <input semantic="INTERPOLATION" source="#InstanceA-location-interpolation"/>
      </sampler>
      <channel source="#InstanceA-location-sampler" target="InstanceA/location"/>
    </animation>
  </library_animations>
)dae";

        std::string document = transformedInstancesDae;
        const size_t insertionPoint = document.find("<library_visual_scenes>");
        REQUIRE(insertionPoint != std::string::npos);
        document.insert(insertionPoint, animationLibrary);
        return document;
    }

    void writeText(const fs::path& path, std::string_view text)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
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

    controllers::ImportResult importModel(const fs::path& dir, const fs::path& src, bool combineMeshes = false)
    {
        import::builtin::ensureRegistered();
        controllers::Import::setLocation(dir.string());
        importConfig::ImportConfig config;
        config.customOptions["combineMeshes"] = combineMeshes;
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

        auto result = importModel(dir, src);

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

        auto result = importModel(dir, src);

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

    TEST_CASE("combined mode exports one multi-submesh .vfMesh")
    {
        fs::path dir = makeScratchDir("vf_combined_multi");
        fs::path src = dir / "twomesh.obj";
        writeText(src, twoMeshObj);

        auto result = importModel(dir, src, true);

        CHECK(result.failureCount == 0);
        REQUIRE(result.fileResults.size() == 1);
        CHECK(result.successCount == 1);

        const fs::path expected = dir / ("twomesh." + FileExtension::mesh);
        REQUIRE(fs::exists(result.fileResults[0].outputPath));
        CHECK(fs::equivalent(fs::path(result.fileResults[0].outputPath), expected));
        CHECK(readHeaderNumMeshes(result.fileResults[0].outputPath) == 2u);

        resource::MeshStreamHandle stream;
        REQUIRE(stream.openStream(result.fileResults[0].outputPath));
        const auto& header = stream.getHeader();
        REQUIRE(header.submeshes.size() == 2);
        CHECK(header.submeshes[0].name != header.submeshes[1].name);

        fs::path meta = result.fileResults[0].outputPath;
        meta += "." + FileExtension::assetMeta;
        CHECK(fs::exists(meta));

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("combined mode bakes node transforms and preserves mirrored winding")
    {
        fs::path dir = makeScratchDir("vf_combined_transforms");
        fs::path src = dir / "instances.dae";
        writeText(src, transformedInstancesDae);

        auto result = importModel(dir, src, true);

        CHECK(result.failureCount == 0);
        REQUIRE(result.fileResults.size() == 1);

        resource::MeshStreamHandle stream;
        REQUIRE(stream.openStream(result.fileResults[0].outputPath));
        const auto& header = stream.getHeader();
        REQUIRE(header.submeshes.size() == 2);
        CHECK(header.submeshes[0].name == "SharedTri");
        CHECK(header.submeshes[1].name == "SharedTri_1");

        std::vector<float> centroidX;
        for (uint32_t submesh = 0; submesh < 2; ++submesh)
        {
            std::vector<resource::Vertex> vertices;
            std::vector<uint32_t> indices;
            REQUIRE(stream.readLODLevel(submesh, 0, vertices, indices));
            REQUIRE(vertices.size() == 3);
            REQUIRE(indices.size() == 3);

            centroidX.push_back((vertices[0].position.x + vertices[1].position.x +
                                 vertices[2].position.x) / 3.0f);

            const auto& p0 = vertices[indices[0]].position;
            const auto& p1 = vertices[indices[1]].position;
            const auto& p2 = vertices[indices[2]].position;
            const glm::vec3 geometricNormal = glm::cross(p1 - p0, p2 - p0);
            CHECK(glm::dot(geometricNormal, vertices[indices[0]].normal) > 0.0f);
        }

        std::ranges::sort(centroidX);
        CHECK(centroidX[0] == doctest::Approx(10.3333f).epsilon(0.01));
        CHECK(centroidX[1] == doctest::Approx(19.6667f).epsilon(0.01));

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("combined mode falls back to split output for animated models")
    {
        fs::path dir = makeScratchDir("vf_combined_animated_fallback");
        fs::path src = dir / "animated_instances.dae";
        writeText(src, makeAnimatedInstancesDae());

        auto result = importModel(dir, src, true);

        CHECK(result.failureCount == 0);
        REQUIRE(result.fileResults.size() == 1);
        CHECK(readHeaderNumMeshes(result.fileResults[0].outputPath) == 1u);

        resource::MeshStreamHandle stream;
        REQUIRE(stream.openStream(result.fileResults[0].outputPath));
        CHECK(stream.getHeader().submeshes.size() == 1);

        std::error_code ec;
        fs::remove_all(dir, ec);
    }
}
