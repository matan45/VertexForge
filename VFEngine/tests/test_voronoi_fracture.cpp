#include <doctest.h>
#include <destruction/DestructionTypes.hpp>
#include <destruction/MeshClipper.hpp>
#include <destruction/VoronoiFracture.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>
#include <unordered_set>

namespace
{
    // Build a unit cube mesh centered at origin: [-0.5, 0.5]^3
    // 8 vertices, 12 triangles (2 per face)
    resource::MeshData makeUnitCube()
    {
        resource::MeshData mesh;
        mesh.name = "cube";

        resource::LODLevel lod;

        // 8 corner vertices (normals approximate, UVs simple)
        glm::vec3 corners[8] = {
            {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
            { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
            {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
            { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}
        };

        // For simplicity, use per-face vertices (24 vertices, 6 faces x 4 verts)
        auto addFace = [&](glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3 v3, glm::vec3 normal)
        {
            uint32_t base = static_cast<uint32_t>(lod.vertices.size());
            resource::Vertex verts[4];
            glm::vec3 positions[4] = {v0, v1, v2, v3};
            glm::vec2 uvs[4] = {{0,0},{1,0},{1,1},{0,1}};

            for (int i = 0; i < 4; ++i)
            {
                verts[i].position = positions[i];
                verts[i].normal = normal;
                verts[i].texCoords = uvs[i];
                verts[i].boneIndices = glm::ivec4(-1);
                verts[i].boneWeights = glm::vec4(0.0f);
                lod.vertices.push_back(verts[i]);
            }

            // Two triangles: 0-1-2, 0-2-3
            lod.indices.push_back(base);
            lod.indices.push_back(base + 1);
            lod.indices.push_back(base + 2);
            lod.indices.push_back(base);
            lod.indices.push_back(base + 2);
            lod.indices.push_back(base + 3);
        };

        // Front  (+Z)
        addFace(corners[4], corners[5], corners[6], corners[7], {0,0,1});
        // Back   (-Z)
        addFace(corners[1], corners[0], corners[3], corners[2], {0,0,-1});
        // Right  (+X)
        addFace(corners[5], corners[1], corners[2], corners[6], {1,0,0});
        // Left   (-X)
        addFace(corners[0], corners[4], corners[7], corners[3], {-1,0,0});
        // Top    (+Y)
        addFace(corners[7], corners[6], corners[2], corners[3], {0,1,0});
        // Bottom (-Y)
        addFace(corners[0], corners[1], corners[5], corners[4], {0,-1,0});

        mesh.lodLevels.push_back(std::move(lod));
        return mesh;
    }

    // Build a simple tetrahedron mesh
    resource::MeshData makeTetrahedron()
    {
        resource::MeshData mesh;
        mesh.name = "tetrahedron";

        resource::LODLevel lod;

        glm::vec3 v0(0.0f, 1.0f, 0.0f);
        glm::vec3 v1(-0.5f, 0.0f, 0.5f);
        glm::vec3 v2(0.5f, 0.0f, 0.5f);
        glm::vec3 v3(0.0f, 0.0f, -0.5f);

        auto addTri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c)
        {
            uint32_t base = static_cast<uint32_t>(lod.vertices.size());
            glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));

            for (const auto& pos : {a, b, c})
            {
                resource::Vertex vert;
                vert.position = pos;
                vert.normal = normal;
                vert.texCoords = {0.0f, 0.0f};
                vert.boneIndices = glm::ivec4(-1);
                vert.boneWeights = glm::vec4(0.0f);
                lod.vertices.push_back(vert);
            }

            lod.indices.push_back(base);
            lod.indices.push_back(base + 1);
            lod.indices.push_back(base + 2);
        };

        addTri(v0, v1, v2);
        addTri(v0, v2, v3);
        addTri(v0, v3, v1);
        addTri(v1, v3, v2);

        mesh.lodLevels.push_back(std::move(lod));
        return mesh;
    }
}

// ============================================================
// MeshClipper tests
// ============================================================

TEST_SUITE("MeshClipper") {

TEST_CASE("clipToHalfSpace: mesh entirely on positive side returns unchanged") {
    auto cube = makeUnitCube();
    const auto& lod = cube.lodLevels[0];

    // Plane at x = -5 (everything is positive)
    glm::vec4 plane(1.0f, 0.0f, 0.0f, 5.0f);
    auto result = destruction::MeshClipper::clipToHalfSpace(lod.vertices, lod.indices, plane);

    CHECK_FALSE(result.didClip);
    CHECK(result.vertices.size() == lod.vertices.size());
    CHECK(result.indices.size() == lod.indices.size());
    CHECK(result.cutEdges.empty());
}

TEST_CASE("clipToHalfSpace: mesh entirely on negative side returns empty") {
    auto cube = makeUnitCube();
    const auto& lod = cube.lodLevels[0];

    // Plane at x = 5 pointing +x (everything is negative)
    glm::vec4 plane(1.0f, 0.0f, 0.0f, -5.0f);
    auto result = destruction::MeshClipper::clipToHalfSpace(lod.vertices, lod.indices, plane);

    CHECK(result.didClip);
    CHECK(result.vertices.empty());
    CHECK(result.indices.empty());
}

TEST_CASE("clipToHalfSpace: clip cube in half produces geometry on positive side") {
    auto cube = makeUnitCube();
    const auto& lod = cube.lodLevels[0];

    // Plane at x = 0, keeping +x side
    glm::vec4 plane(1.0f, 0.0f, 0.0f, 0.0f);
    auto result = destruction::MeshClipper::clipToHalfSpace(lod.vertices, lod.indices, plane);

    CHECK(result.didClip);
    CHECK(result.vertices.size() > 0);
    CHECK(result.indices.size() > 0);
    CHECK(result.indices.size() % 3 == 0);
    CHECK(result.cutEdges.size() > 0);

    // All resulting vertices should have x >= -epsilon
    for (const auto& v : result.vertices)
    {
        CHECK(v.position.x >= -1e-5f);
    }
}

TEST_CASE("clipToHalfSpace: clip preserves vertex attributes") {
    auto cube = makeUnitCube();
    const auto& lod = cube.lodLevels[0];

    // Clip at x = 0
    glm::vec4 plane(1.0f, 0.0f, 0.0f, 0.0f);
    auto result = destruction::MeshClipper::clipToHalfSpace(lod.vertices, lod.indices, plane);

    for (const auto& v : result.vertices)
    {
        // Normals should be unit length
        float len = glm::length(v.normal);
        CHECK(len == doctest::Approx(1.0f).epsilon(0.01f));

        // Bone indices should be -1 (no skinning)
        CHECK(v.boneIndices.x == -1);
    }
}

TEST_CASE("clipToHalfSpace: empty input returns empty result") {
    std::vector<resource::Vertex> emptyVerts;
    std::vector<uint32_t> emptyIndices;

    glm::vec4 plane(1.0f, 0.0f, 0.0f, 0.0f);
    auto result = destruction::MeshClipper::clipToHalfSpace(emptyVerts, emptyIndices, plane);

    CHECK(result.vertices.empty());
    CHECK(result.indices.empty());
    CHECK_FALSE(result.didClip);
}

TEST_CASE("clipToHalfSpace: clip at different positions produces different amounts of geometry") {
    auto cube = makeUnitCube();
    const auto& lod = cube.lodLevels[0];

    // Clip keeping most of the cube (x > -0.4)
    glm::vec4 planeMost(1.0f, 0.0f, 0.0f, 0.4f);
    auto resultMost = destruction::MeshClipper::clipToHalfSpace(lod.vertices, lod.indices, planeMost);

    // Clip keeping less of the cube (x > 0.4)
    glm::vec4 planeLess(1.0f, 0.0f, 0.0f, -0.4f);
    auto resultLess = destruction::MeshClipper::clipToHalfSpace(lod.vertices, lod.indices, planeLess);

    // The "most" clip should keep more triangles than the "less" clip
    CHECK(resultMost.indices.size() >= resultLess.indices.size());
}

TEST_CASE("clipToHalfSpace: clipping tetrahedron produces valid geometry") {
    auto tet = makeTetrahedron();
    const auto& lod = tet.lodLevels[0];

    // Horizontal clip at y = 0.5
    glm::vec4 plane(0.0f, 1.0f, 0.0f, -0.5f);
    auto result = destruction::MeshClipper::clipToHalfSpace(lod.vertices, lod.indices, plane);

    CHECK(result.didClip);
    CHECK(result.vertices.size() > 0);
    CHECK(result.indices.size() % 3 == 0);

    // All resulting vertices should have y >= 0.5 - epsilon
    for (const auto& v : result.vertices)
    {
        CHECK(v.position.y >= 0.5f - 1e-4f);
    }
}

} // TEST_SUITE MeshClipper

// ============================================================
// VoronoiFracture tests
// ============================================================

TEST_SUITE("VoronoiFracture") {

TEST_CASE("fracture: empty mesh returns error") {
    resource::MeshData emptyMesh;
    emptyMesh.name = "empty";

    destruction::FractureConfig config;
    config.cellCount = 5;

    auto result = destruction::VoronoiFracture::fracture(emptyMesh, config);

    CHECK_FALSE(result.success);
    CHECK_FALSE(result.errorMessage.empty());
}

TEST_CASE("fracture: cellCount < 2 returns error") {
    auto cube = makeUnitCube();

    destruction::FractureConfig config;
    config.cellCount = 1;

    auto result = destruction::VoronoiFracture::fracture(cube, config);

    CHECK_FALSE(result.success);
    CHECK_FALSE(result.errorMessage.empty());
}

TEST_CASE("fracture: cube with 2 cells produces 2 fragments") {
    auto cube = makeUnitCube();

    destruction::FractureConfig config;
    config.cellCount = 2;
    config.randomSeed = 42;
    config.generateConvexHulls = false;

    auto result = destruction::VoronoiFracture::fracture(cube, config);

    CHECK(result.success);
    CHECK(result.fragments.size() == 2);

    for (const auto& frag : result.fragments)
    {
        CHECK_FALSE(frag.mesh.lodLevels.empty());
        CHECK(frag.mesh.lodLevels[0].vertices.size() > 0);
        CHECK(frag.mesh.lodLevels[0].indices.size() >= 3);
        CHECK(frag.mesh.lodLevels[0].indices.size() % 3 == 0);
    }
}

TEST_CASE("fracture: cube with 5 cells produces multiple fragments") {
    auto cube = makeUnitCube();

    destruction::FractureConfig config;
    config.cellCount = 5;
    config.randomSeed = 123;
    config.generateConvexHulls = false;

    auto result = destruction::VoronoiFracture::fracture(cube, config);

    CHECK(result.success);
    CHECK(result.fragments.size() >= 2);
    CHECK(result.fragments.size() <= 5);
}

TEST_CASE("fracture: fragments have finite center of mass") {
    auto cube = makeUnitCube();

    destruction::FractureConfig config;
    config.cellCount = 4;
    config.randomSeed = 7;
    config.generateConvexHulls = false;

    auto result = destruction::VoronoiFracture::fracture(cube, config);

    CHECK(result.success);

    for (const auto& frag : result.fragments)
    {
        CHECK(std::isfinite(frag.centerOfMass.x));
        CHECK(std::isfinite(frag.centerOfMass.y));
        CHECK(std::isfinite(frag.centerOfMass.z));
    }
}

TEST_CASE("fracture: fragments have positive volume") {
    auto cube = makeUnitCube();

    destruction::FractureConfig config;
    config.cellCount = 3;
    config.randomSeed = 99;
    config.generateConvexHulls = false;

    auto result = destruction::VoronoiFracture::fracture(cube, config);

    CHECK(result.success);

    for (const auto& frag : result.fragments)
    {
        CHECK(frag.volume > 0.0f);
    }
}

TEST_CASE("fracture: fragments are named sequentially") {
    auto cube = makeUnitCube();

    destruction::FractureConfig config;
    config.cellCount = 4;
    config.randomSeed = 42;
    config.generateConvexHulls = false;

    auto result = destruction::VoronoiFracture::fracture(cube, config);

    CHECK(result.success);

    for (size_t i = 0; i < result.fragments.size(); ++i)
    {
        CHECK(result.fragments[i].mesh.name == "fragment_" + std::to_string(i));
    }
}

TEST_CASE("fracture: connectivity graph has bidirectional neighbors") {
    auto cube = makeUnitCube();

    destruction::FractureConfig config;
    config.cellCount = 5;
    config.randomSeed = 42;
    config.generateConvexHulls = false;

    auto result = destruction::VoronoiFracture::fracture(cube, config);

    CHECK(result.success);

    // If fragment A lists B as neighbor, B should list A as neighbor
    for (uint32_t i = 0; i < static_cast<uint32_t>(result.fragments.size()); ++i)
    {
        for (const auto& neighbor : result.fragments[i].neighbors)
        {
            uint32_t j = neighbor.neighborIndex;
            CHECK(j < result.fragments.size());

            bool found = false;
            for (const auto& reverseNeighbor : result.fragments[j].neighbors)
            {
                if (reverseNeighbor.neighborIndex == i)
                {
                    found = true;
                    break;
                }
            }
            CHECK(found);
        }
    }
}

TEST_CASE("fracture: deterministic with same random seed") {
    auto cube = makeUnitCube();

    destruction::FractureConfig config;
    config.cellCount = 4;
    config.randomSeed = 42;
    config.generateConvexHulls = false;

    auto result1 = destruction::VoronoiFracture::fracture(cube, config);
    auto result2 = destruction::VoronoiFracture::fracture(cube, config);

    CHECK(result1.success);
    CHECK(result2.success);
    CHECK(result1.fragments.size() == result2.fragments.size());

    for (size_t i = 0; i < result1.fragments.size(); ++i)
    {
        const auto& lod1 = result1.fragments[i].mesh.lodLevels[0];
        const auto& lod2 = result2.fragments[i].mesh.lodLevels[0];

        CHECK(lod1.vertices.size() == lod2.vertices.size());
        CHECK(lod1.indices.size() == lod2.indices.size());
    }
}

TEST_CASE("fracture: different seeds produce different results") {
    auto cube = makeUnitCube();

    destruction::FractureConfig config1;
    config1.cellCount = 4;
    config1.randomSeed = 42;
    config1.generateConvexHulls = false;

    destruction::FractureConfig config2;
    config2.cellCount = 4;
    config2.randomSeed = 999;
    config2.generateConvexHulls = false;

    auto result1 = destruction::VoronoiFracture::fracture(cube, config1);
    auto result2 = destruction::VoronoiFracture::fracture(cube, config2);

    CHECK(result1.success);
    CHECK(result2.success);

    // With different seeds, at least one fragment should differ in vertex count
    bool anyDifferent = false;
    if (result1.fragments.size() != result2.fragments.size())
    {
        anyDifferent = true;
    }
    else
    {
        for (size_t i = 0; i < result1.fragments.size(); ++i)
        {
            if (result1.fragments[i].mesh.lodLevels[0].vertices.size() !=
                result2.fragments[i].mesh.lodLevels[0].vertices.size())
            {
                anyDifferent = true;
                break;
            }
        }
    }
    CHECK(anyDifferent);
}

TEST_CASE("fracture: cancellation stops early") {
    auto cube = makeUnitCube();

    destruction::FractureConfig config;
    config.cellCount = 20;
    config.randomSeed = 42;
    config.generateConvexHulls = false;

    std::atomic<bool> cancel{true};
    auto result = destruction::VoronoiFracture::fracture(cube, config, nullptr, &cancel);

    CHECK_FALSE(result.success);
    CHECK(result.errorMessage == "Cancelled");
}

TEST_CASE("fracture: progress callback is invoked") {
    auto cube = makeUnitCube();

    destruction::FractureConfig config;
    config.cellCount = 3;
    config.randomSeed = 42;
    config.generateConvexHulls = false;

    float lastProgress = -1.0f;
    int callCount = 0;

    auto callback = [&](float progress, std::string_view)
    {
        CHECK(progress >= lastProgress);
        lastProgress = progress;
        ++callCount;
    };

    auto result = destruction::VoronoiFracture::fracture(cube, config, callback);

    CHECK(result.success);
    CHECK(callCount > 0);
    CHECK(lastProgress == doctest::Approx(1.0f));
}

TEST_CASE("fracture: clustered seed distribution works") {
    auto cube = makeUnitCube();

    destruction::FractureConfig config;
    config.cellCount = 6;
    config.randomSeed = 42;
    config.seedDistribution = destruction::SeedDistribution::Clustered;
    config.clusterParams.clusterCount = 2;
    config.clusterParams.clusterRadius = 0.3f;
    config.generateConvexHulls = false;

    auto result = destruction::VoronoiFracture::fracture(cube, config);

    CHECK(result.success);
    CHECK(result.fragments.size() >= 2);
}

TEST_CASE("fracture: artist-placed seeds work") {
    auto cube = makeUnitCube();

    destruction::FractureConfig config;
    config.cellCount = 3;
    config.seedDistribution = destruction::SeedDistribution::ArtistPlaced;
    config.artistSeeds = {
        {-0.2f, 0.0f, 0.0f},
        { 0.2f, 0.0f, 0.0f},
        { 0.0f, 0.3f, 0.0f}
    };
    config.generateConvexHulls = false;

    auto result = destruction::VoronoiFracture::fracture(cube, config);

    CHECK(result.success);
    CHECK(result.fragments.size() >= 2);
}

} // TEST_SUITE VoronoiFracture

// ============================================================
// toMeshesData tests
// ============================================================

TEST_SUITE("VoronoiFracture::toMeshesData") {

TEST_CASE("toMeshesData: converts FractureResult to MeshesData") {
    auto cube = makeUnitCube();

    destruction::FractureConfig config;
    config.cellCount = 3;
    config.randomSeed = 42;
    config.generateConvexHulls = false;

    auto result = destruction::VoronoiFracture::fracture(cube, config);
    CHECK(result.success);

    auto meshesData = destruction::VoronoiFracture::toMeshesData(result);

    CHECK(meshesData.numberOfMeshes == result.fragments.size());
    CHECK(meshesData.meshes.size() == result.fragments.size());
    CHECK(meshesData.headerFileType == resource::FileType::MESH);
    CHECK_FALSE(meshesData.hasSkinning);

    for (size_t i = 0; i < result.fragments.size(); ++i)
    {
        CHECK(meshesData.meshes[i].name == result.fragments[i].mesh.name);
        CHECK_FALSE(meshesData.meshes[i].lodLevels.empty());
    }
}

} // TEST_SUITE toMeshesData

// ============================================================
// FractureResult tests
// ============================================================

TEST_SUITE("FractureResult") {

TEST_CASE("getTotalVertexCount: returns sum across all fragments") {
    destruction::FractureResult result;
    result.success = true;

    destruction::FragmentData frag1;
    resource::LODLevel lod1;
    lod1.vertices.resize(10);
    frag1.mesh.lodLevels.push_back(lod1);

    destruction::FragmentData frag2;
    resource::LODLevel lod2;
    lod2.vertices.resize(15);
    frag2.mesh.lodLevels.push_back(lod2);

    result.fragments.push_back(frag1);
    result.fragments.push_back(frag2);

    CHECK(result.getTotalVertexCount() == 25);
}

TEST_CASE("getTotalIndexCount: returns sum across all fragments") {
    destruction::FractureResult result;
    result.success = true;

    destruction::FragmentData frag;
    resource::LODLevel lod;
    lod.indices.resize(36);
    frag.mesh.lodLevels.push_back(lod);

    result.fragments.push_back(frag);

    CHECK(result.getTotalIndexCount() == 36);
}

TEST_CASE("getTotalVertexCount: empty result returns 0") {
    destruction::FractureResult result;
    CHECK(result.getTotalVertexCount() == 0);
    CHECK(result.getTotalIndexCount() == 0);
}

} // TEST_SUITE FractureResult
