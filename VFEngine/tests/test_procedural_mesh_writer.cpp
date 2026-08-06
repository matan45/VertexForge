#include <doctest.h>

#include <resource/MeshStreamHandle.hpp>
#include <resource/Types.hpp>
#include <terrain/RoadMeshBuilder.hpp>
#include <types/ProceduralMeshWriter.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// VK-1621 — ProceduralMeshWriter turns CPU vertex/index data into a real .vfMesh, which is what
// lets a generated road be an ordinary mesh asset (rendering, culling, LOD, streaming and physics
// all key on MeshComponent::meshRef resolving to a readable file) with no renderer changes.
//
// These tests write a road and read it straight back through the SAME reader the engine uses,
// which is the only way to know the bytes are actually right.
namespace
{
    [[nodiscard]] std::filesystem::path scratchDir()
    {
        return std::filesystem::temp_directory_path() / "vf_procedural_mesh_writer";
    }

    // meshopt_encodeIndexBuffer / decodeIndexBuffer is lossless about TOPOLOGY but not about the
    // literal index array: it is free to emit each triangle starting at a different corner. A
    // rotation keeps the same three vertices in the same cyclic order, so it preserves winding —
    // which is the property the road actually depends on. Comparing rotation-canonical triangles
    // tests that; comparing the raw arrays would only test the codec's internal choices.
    [[nodiscard]] std::array<uint32_t, 3> canonicalTriangle(uint32_t a, uint32_t b, uint32_t c)
    {
        if (a <= b && a <= c)
            return {a, b, c};
        if (b <= a && b <= c)
            return {b, c, a};
        return {c, a, b};
    }

    [[nodiscard]] std::vector<std::array<uint32_t, 3>> canonicalTriangles(const std::vector<uint32_t>& indices)
    {
        std::vector<std::array<uint32_t, 3>> triangles;
        triangles.reserve(indices.size() / 3);
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
            triangles.push_back(canonicalTriangle(indices[i], indices[i + 1], indices[i + 2]));
        std::sort(triangles.begin(), triangles.end());
        return triangles;
    }

    [[nodiscard]] types::LODMeshData toImportLOD(const resource::LODLevel& level)
    {
        types::LODMeshData out;
        out.vertices = level.vertices;
        out.indices = level.indices;
        return out;
    }

    [[nodiscard]] terrain::RoadMeshData buildTestRoad()
    {
        const terrain::RoadHeightFn flat = [](float, float) { return 0.0f; };
        // Tile size 8 over a 20 m road gives three chunks, so the multi-submesh path is covered.
        return terrain::buildRoadMesh({{0.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 0.0f}},
                                      terrain::makeDefaultRoadProfile(), 8.0f, flat);
    }

    [[nodiscard]] std::vector<types::ProceduralSubmesh> toSubmeshes(const terrain::RoadMeshData& road)
    {
        std::vector<types::ProceduralSubmesh> submeshes;
        submeshes.reserve(road.chunks.size());
        for (size_t i = 0; i < road.chunks.size(); ++i)
        {
            types::ProceduralSubmesh submesh;
            submesh.name = "Road_" + std::to_string(i);
            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
                submesh.lods[lod] = toImportLOD(road.chunks[i].lods[lod]);
            submeshes.push_back(std::move(submesh));
        }
        return submeshes;
    }
}

TEST_SUITE("ProceduralMeshWriter")
{
    TEST_CASE("a generated road round-trips through the engine's own .vfMesh reader")
    {
        const terrain::RoadMeshData road = buildTestRoad();
        REQUIRE(road.valid);
        REQUIRE(road.chunks.size() == 3);

        const std::vector<types::ProceduralSubmesh> submeshes = toSubmeshes(road);

        std::error_code ec;
        std::filesystem::create_directories(scratchDir(), ec);
        const std::string path = (scratchDir() / "roundtrip.vfMesh").string();
        std::filesystem::remove(path, ec);

        const types::ProceduralMeshWriteResult written =
            types::ProceduralMeshWriter::write(path, submeshes);

        REQUIRE(written.success);
        CHECK(written.submeshCount == 3);
        CHECK(written.totalVertices == road.chunks[0].lods[0].vertices.size()
                                     + road.chunks[1].lods[0].vertices.size()
                                     + road.chunks[2].lods[0].vertices.size());
        REQUIRE(std::filesystem::exists(path));
        // The temp file must not survive a successful write.
        CHECK_FALSE(std::filesystem::exists(path + ".tmp"));

        resource::MeshStreamHandle handle;
        REQUIRE(handle.openStream(path));

        const resource::MeshStreamHeader& header = handle.getHeader();
        CHECK(header.headerFileType == resource::FileType::MESH);
        CHECK(header.numSubmeshes == 3);
        REQUIRE(header.submeshes.size() == 3);
        CHECK(handle.isCompressed());
        CHECK(handle.hasMeshletData());

        for (uint32_t s = 0; s < 3; ++s)
        {
            CHECK(header.submeshes[s].name == "Road_" + std::to_string(s));

            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                const resource::LODLevel& expected = road.chunks[s].lods[lod];
                CHECK(header.submeshes[s].lods[lod].vertexCount == expected.vertices.size());
                CHECK(header.submeshes[s].lods[lod].indexCount == expected.indices.size());

                std::vector<resource::Vertex> vertices;
                std::vector<uint32_t> indices;
                REQUIRE(handle.readLODLevel(s, lod, vertices, indices));
                REQUIRE(vertices.size() == expected.vertices.size());
                REQUIRE(indices.size() == expected.indices.size());

                // Positions are 16-bit-quantised over the submesh AABB, so ~0.2 mm over an 11 m
                // chunk. Indices must be exact.
                for (size_t v = 0; v < vertices.size(); ++v)
                {
                    CHECK(std::abs(vertices[v].position.x - expected.vertices[v].position.x) < 1e-3f);
                    CHECK(std::abs(vertices[v].position.y - expected.vertices[v].position.y) < 1e-3f);
                    CHECK(std::abs(vertices[v].position.z - expected.vertices[v].position.z) < 1e-3f);
                    CHECK(vertices[v].normal.y > 0.99f); // flat road: normals survive oct-encoding
                }

                // Same triangles, same cyclic order (see canonicalTriangle).
                CHECK(canonicalTriangles(indices) == canonicalTriangles(expected.indices));

                // And the thing that would actually break the road: every decoded triangle must
                // still face +Y. A codec that reflected a triangle instead of rotating it would
                // pass the multiset check above but render the road back-faced.
                for (size_t t = 0; t + 2 < indices.size(); t += 3)
                {
                    const glm::vec3& a = vertices[indices[t]].position;
                    const glm::vec3& b = vertices[indices[t + 1]].position;
                    const glm::vec3& c = vertices[indices[t + 2]].position;
                    CHECK(glm::cross(b - a, c - a).y > 0.0f);
                }
            }

            // The GPU-driven path needs meshlets; a road without them would simply not draw.
            resource::SubmeshMeshletData meshletData;
            REQUIRE(handle.readMeshletData(s, meshletData));
            CHECK(header.submeshes[s].meshletLods[0].meshletCount > 0);
            CHECK_FALSE(meshletData.meshlets.empty());
        }

        handle.close();
        std::filesystem::remove(path, ec);
    }

    TEST_CASE("collider LOD 2 is written and still describes the road surface")
    {
        // ColliderShape::TriangleMesh loads LOD 2, not LOD 0 (PhysicsShapeFactory.cpp:168,222).
        // If that level were missing or degenerate the collider silently falls back to a box.
        const terrain::RoadMeshData road = buildTestRoad();
        REQUIRE(road.valid);

        std::error_code ec;
        std::filesystem::create_directories(scratchDir(), ec);
        const std::string path = (scratchDir() / "collider_lod.vfMesh").string();

        REQUIRE(types::ProceduralMeshWriter::write(path, toSubmeshes(road)).success);

        resource::MeshStreamHandle handle;
        REQUIRE(handle.openStream(path));

        std::vector<resource::Vertex> vertices;
        std::vector<uint32_t> indices;
        REQUIRE(handle.readLODLevel(0, 2, vertices, indices));
        REQUIRE(indices.size() >= 3);

        float minZ = vertices[0].position.z;
        float maxZ = vertices[0].position.z;
        for (const resource::Vertex& v : vertices)
        {
            minZ = std::min(minZ, v.position.z);
            maxZ = std::max(maxZ, v.position.z);
        }
        // Full authored width (2 * (4 + 1.5)) survives to LOD 2 — the whole point of decimating
        // by ring rather than through meshopt_simplifySloppy.
        CHECK(maxZ - minZ == doctest::Approx(11.0f).epsilon(0.01));

        handle.close();
        std::filesystem::remove(path, ec);
    }

    TEST_CASE("malformed input is rejected without leaving a file behind")
    {
        std::error_code ec;
        std::filesystem::create_directories(scratchDir(), ec);
        const std::string path = (scratchDir() / "rejected.vfMesh").string();
        std::filesystem::remove(path, ec);

        SUBCASE("no submeshes")
        {
            const auto result = types::ProceduralMeshWriter::write(path, {});
            CHECK_FALSE(result.success);
            CHECK_FALSE(result.message.empty());
            CHECK_FALSE(std::filesystem::exists(path));
        }

        SUBCASE("a missing LOD level")
        {
            const terrain::RoadMeshData road = buildTestRoad();
            REQUIRE(road.valid);
            std::vector<types::ProceduralSubmesh> submeshes = toSubmeshes(road);
            submeshes[0].lods[3].vertices.clear();
            submeshes[0].lods[3].indices.clear();

            const auto result = types::ProceduralMeshWriter::write(path, submeshes);
            CHECK_FALSE(result.success);
            CHECK_FALSE(std::filesystem::exists(path));
        }

        SUBCASE("an index count that is not a multiple of three")
        {
            const terrain::RoadMeshData road = buildTestRoad();
            REQUIRE(road.valid);
            std::vector<types::ProceduralSubmesh> submeshes = toSubmeshes(road);
            submeshes[0].lods[0].indices.pop_back();

            const auto result = types::ProceduralMeshWriter::write(path, submeshes);
            CHECK_FALSE(result.success);
            CHECK_FALSE(std::filesystem::exists(path));
        }

        std::filesystem::remove(path, ec);
    }
}
