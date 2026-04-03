#include <doctest.h>
#include <destruction/DestructionTypes.hpp>
#include <destruction/VoronoiFracture.hpp>
#include "test_destruction_helpers.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>
#include <unordered_set>

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
