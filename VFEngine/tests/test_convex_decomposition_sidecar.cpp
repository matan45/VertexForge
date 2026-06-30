#include <doctest.h>
#include <resource/ConvexDecompositionSidecar.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{
    std::filesystem::path uniqueMeshPath()
    {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path()
            / ("vf_convex_sidecar_" + std::to_string(ticks) + ".vfMesh");
    }

    resource::ConvexDecompositionData makeConvexData(float x, bool shrinkWrap)
    {
        resource::ConvexDecompositionData data;
        data.hasDecomposition = true;
        data.params.maxConvexHulls = 4;
        data.params.resolution = 12345;
        data.params.maxVerticesPerHull = 32;
        data.params.minVolumePercentError = 2.5f;
        data.params.maxRecursionDepth = 7;
        data.params.shrinkWrap = shrinkWrap;

        resource::ConvexHull hull;
        hull.vertices = {
            {x, 0.0f, 0.0f},
            {x + 1.0f, 0.0f, 0.0f},
            {x, 1.0f, 0.0f}
        };
        hull.indices = {0, 1, 2};
        hull.center = {x + 0.33f, 0.33f, 0.0f};
        hull.volume = 0.5f;
        data.hulls.push_back(std::move(hull));
        return data;
    }

    void cleanupSidecar(const std::filesystem::path& meshPath)
    {
        std::error_code ec;
        std::filesystem::remove(resource::ConvexDecompositionSidecar::sidecarPathForMesh(meshPath.string()), ec);
        std::filesystem::remove(resource::ConvexDecompositionSidecar::sidecarPathForMesh(meshPath.string()).string() + ".tmp", ec);
        std::filesystem::remove(resource::ConvexDecompositionSidecar::sidecarPathForMesh(meshPath.string()).string() + ".bak", ec);
    }
}

TEST_SUITE("ConvexDecompositionSidecar")
{
    TEST_CASE("save and load preserves convex decomposition params and hulls")
    {
        const auto meshPath = uniqueMeshPath();
        cleanupSidecar(meshPath);

        resource::ConvexDecompositionSidecarEntry entry;
        entry.submeshIndex = 2;
        entry.data = makeConvexData(3.0f, false);

        REQUIRE(resource::ConvexDecompositionSidecar::save(meshPath.string(), {entry}));

        resource::ConvexDecompositionData loaded;
        REQUIRE(resource::ConvexDecompositionSidecar::loadForSubmesh(meshPath.string(), 2, loaded));
        CHECK(loaded.isValid());
        CHECK(loaded.params.resolution == 12345);
        CHECK(loaded.params.shrinkWrap == false);
        REQUIRE(loaded.hulls.size() == 1);
        REQUIRE(loaded.hulls[0].vertices.size() == 3);
        CHECK(loaded.hulls[0].vertices[0].x == doctest::Approx(3.0f));

        cleanupSidecar(meshPath);
    }

    TEST_CASE("upsert replaces one submesh while preserving others")
    {
        const auto meshPath = uniqueMeshPath();
        cleanupSidecar(meshPath);

        resource::ConvexDecompositionSidecarEntry entry0{0, makeConvexData(0.0f, true)};
        resource::ConvexDecompositionSidecarEntry entry1{1, makeConvexData(10.0f, true)};
        REQUIRE(resource::ConvexDecompositionSidecar::save(meshPath.string(), {entry0, entry1}));

        resource::ConvexDecompositionSidecarEntry replacement{1, makeConvexData(20.0f, false)};
        REQUIRE(resource::ConvexDecompositionSidecar::upsert(meshPath.string(), {replacement}));

        std::vector<resource::ConvexDecompositionSidecarEntry> loaded;
        REQUIRE(resource::ConvexDecompositionSidecar::load(meshPath.string(), loaded));
        REQUIRE(loaded.size() == 2);

        resource::ConvexDecompositionData loaded0;
        resource::ConvexDecompositionData loaded1;
        REQUIRE(resource::ConvexDecompositionSidecar::loadForSubmesh(meshPath.string(), 0, loaded0));
        REQUIRE(resource::ConvexDecompositionSidecar::loadForSubmesh(meshPath.string(), 1, loaded1));
        CHECK(loaded0.hulls[0].vertices[0].x == doctest::Approx(0.0f));
        CHECK(loaded1.hulls[0].vertices[0].x == doctest::Approx(20.0f));
        CHECK(loaded1.params.shrinkWrap == false);

        cleanupSidecar(meshPath);
    }
}
