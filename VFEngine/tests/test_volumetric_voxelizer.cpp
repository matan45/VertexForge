#include <doctest.h>
#include <navigation/volumetric/VolumetricVoxelizer.hpp>
#include <threading/CancellationToken.hpp>

#include <string>
#include <vector>

// ============================================================
// VK-1660: the volumetric bake (volumetric::voxelize) - surface voxels, sealed interiors, terrain columns,
// analytic spheres and capsules, clearance dilation, the voxel cap, cancellation and input validation.
// ============================================================

namespace
{
    namespace vol = volumetric;

    vol::VolumetricBakeInput makeInput(const glm::vec3& boundsMin, const glm::vec3& boundsMax, float voxelSize,
                                       float clearance = 0.0f, uint8_t connectivity = 26)
    {
        vol::VolumetricBakeInput input;
        input.boundsMin = boundsMin;
        input.boundsMax = boundsMax;
        input.settings.voxelSize = voxelSize;
        input.settings.clearance = clearance;
        input.settings.connectivity = connectivity;
        return input;
    }

    void addQuad(std::vector<float>& vertices, std::vector<uint32_t>& indices, const glm::vec3& a, const glm::vec3& b,
                 const glm::vec3& c, const glm::vec3& d)
    {
        const uint32_t base = static_cast<uint32_t>(vertices.size() / 3);
        for (const glm::vec3& p : {a, b, c, d})
        {
            vertices.push_back(p.x);
            vertices.push_back(p.y);
            vertices.push_back(p.z);
        }
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    // An axis-aligned box as 12 triangles; without its top (+Y) face when `open`.
    void addBox(vol::VolumetricBakeInput& input, const glm::vec3& lo, const glm::vec3& hi, bool open = false)
    {
        auto& v = input.vertices;
        auto& i = input.indices;
        addQuad(v, i, {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z}); // bottom
        if (!open)
            addQuad(v, i, {lo.x, hi.y, lo.z}, {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}, {hi.x, hi.y, lo.z}); // top
        addQuad(v, i, {lo.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, lo.y, lo.z}); // -Z
        addQuad(v, i, {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}); // +Z
        addQuad(v, i, {lo.x, lo.y, lo.z}, {lo.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {lo.x, hi.y, lo.z}); // -X
        addQuad(v, i, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {hi.x, lo.y, hi.z}); // +X
    }

    bool navigableAt(const vol::VoxelGrid& grid, int x, int y, int z)
    {
        return grid.isNavigable(vol::VoxelCoord{x, y, z});
    }
}

TEST_SUITE("VolumetricVoxelizer")
{
    TEST_CASE("grid dimensions round up per axis and tolerate float noise")
    {
        CHECK(vol::gridDimensionsFor(glm::vec3(0.0f), glm::vec3(10.0f, 2.5f, 1.0f), 1.0f) == glm::ivec3(10, 3, 1));
        CHECK(vol::gridDimensionsFor(glm::vec3(0.0f), glm::vec3(10.0f), 0.1f) == glm::ivec3(100));
        CHECK(vol::gridDimensionsFor(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 1.0f), 1.0f) == glm::ivec3(0));
        CHECK(vol::gridDimensionsFor(glm::vec3(0.0f), glm::vec3(1.0f), 0.0f) == glm::ivec3(0));
        CHECK(vol::voxelCountFor(glm::vec3(0.0f), glm::vec3(4.0f, 5.0f, 6.0f), 1.0f) == 120);
    }

    TEST_CASE("an empty scene bakes a fully navigable grid")
    {
        const auto result = vol::voxelize(makeInput(glm::vec3(0.0f), glm::vec3(4.0f), 1.0f));
        REQUIRE(result.grid.get() != nullptr);
        CHECK(result.error.empty());
        CHECK(result.grid->dims() == glm::ivec3(4));
        CHECK(result.voxels == 64);
        CHECK(result.navigable == 64);
        CHECK(result.grid->origin() == glm::vec3(0.0f));
    }

    TEST_CASE("a closed box blocks its surface and its sealed interior")
    {
        auto input = makeInput(glm::vec3(0.0f), glm::vec3(10.0f), 1.0f);
        addBox(input, glm::vec3(3.5f), glm::vec3(6.5f));
        const auto result = vol::voxelize(input);
        REQUIRE(result.grid.get() != nullptr);
        const vol::VoxelGrid& grid = *result.grid;

        CHECK_FALSE(navigableAt(grid, 3, 5, 5)); // surface
        CHECK_FALSE(navigableAt(grid, 6, 6, 6)); // corner
        CHECK_FALSE(navigableAt(grid, 5, 5, 5)); // sealed interior
        CHECK_FALSE(navigableAt(grid, 4, 4, 4));
        CHECK(navigableAt(grid, 2, 5, 5));
        CHECK(navigableAt(grid, 7, 5, 5));
        CHECK(navigableAt(grid, 0, 0, 0));
        CHECK(result.navigable == 1000 - 64); // the 4x4x4 block (shell + interior) is blocked
    }

    TEST_CASE("an open box stays reachable inside")
    {
        auto input = makeInput(glm::vec3(0.0f), glm::vec3(10.0f), 1.0f);
        addBox(input, glm::vec3(3.5f), glm::vec3(6.5f), true);
        const auto result = vol::voxelize(input);
        REQUIRE(result.grid.get() != nullptr);

        CHECK(navigableAt(*result.grid, 5, 5, 5)); // reachable through the missing top
        CHECK(navigableAt(*result.grid, 4, 6, 4));
        CHECK_FALSE(navigableAt(*result.grid, 5, 3, 5)); // the bottom face
        CHECK_FALSE(navigableAt(*result.grid, 3, 5, 5)); // a side wall
    }

    TEST_CASE("terrain blocks the voxels below its surface")
    {
        auto input = makeInput(glm::vec3(0.0f), glm::vec3(8.0f), 1.0f);
        addQuad(input.terrainVertices, input.terrainIndices, {-1.0f, 3.2f, -1.0f}, {-1.0f, 3.2f, 9.0f},
                {9.0f, 3.2f, 9.0f}, {9.0f, 3.2f, -1.0f});
        const auto result = vol::voxelize(input);
        REQUIRE(result.grid.get() != nullptr);

        CHECK_FALSE(navigableAt(*result.grid, 4, 0, 4));
        CHECK_FALSE(navigableAt(*result.grid, 4, 2, 4)); // center 2.5 below the surface
        CHECK_FALSE(navigableAt(*result.grid, 4, 3, 4)); // the surface passes through it
        CHECK(navigableAt(*result.grid, 4, 4, 4));
        CHECK(navigableAt(*result.grid, 0, 7, 0));
        CHECK(result.navigable == 8 * 8 * 4); // layers y = 4..7

        SUBCASE("terrain above the whole volume blocks every column")
        {
            auto buried = makeInput(glm::vec3(0.0f), glm::vec3(4.0f), 1.0f);
            addQuad(buried.terrainVertices, buried.terrainIndices, {-1.0f, 10.0f, -1.0f}, {-1.0f, 10.0f, 5.0f},
                    {5.0f, 10.0f, 5.0f}, {5.0f, 10.0f, -1.0f});
            const auto underground = vol::voxelize(buried);
            REQUIRE(underground.grid.get() != nullptr);
            CHECK(underground.navigable == 0);
        }
    }

    TEST_CASE("spheres and capsules are solid")
    {
        auto input = makeInput(glm::vec3(0.0f), glm::vec3(10.0f), 1.0f);
        input.spheres.push_back({glm::vec3(5.5f, 5.5f, 5.5f), 1.2f});
        input.capsules.push_back({glm::vec3(1.5f, 2.5f, 2.5f), glm::vec3(8.5f, 2.5f, 2.5f), 0.4f});
        const auto result = vol::voxelize(input);
        REQUIRE(result.grid.get() != nullptr);
        const vol::VoxelGrid& grid = *result.grid;

        CHECK_FALSE(navigableAt(grid, 5, 5, 5));
        CHECK_FALSE(navigableAt(grid, 5, 5, 6)); // 0.5 from the center
        CHECK(navigableAt(grid, 5, 5, 7));       // 1.5 away: outside the 1.2 radius
        CHECK_FALSE(navigableAt(grid, 1, 2, 2));
        CHECK_FALSE(navigableAt(grid, 8, 2, 2));
        CHECK(navigableAt(grid, 5, 3, 2)); // 0.5 above the segment's line: outside the 0.4 radius
        CHECK(navigableAt(grid, 9, 2, 2)); // past the end cap
    }

    TEST_CASE("clearance grows the blocked voxels by whole voxels and never walls the boundary")
    {
        auto input = makeInput(glm::vec3(0.0f), glm::vec3(10.0f), 1.0f, 1.0f);
        input.spheres.push_back({glm::vec3(5.5f), 0.3f}); // blocks voxel (5,5,5) only
        const auto result = vol::voxelize(input);
        REQUIRE(result.grid.get() != nullptr);

        CHECK_FALSE(navigableAt(*result.grid, 5, 5, 5));
        CHECK_FALSE(navigableAt(*result.grid, 4, 5, 5));
        CHECK_FALSE(navigableAt(*result.grid, 6, 6, 6)); // a cube kernel
        CHECK(navigableAt(*result.grid, 7, 5, 5));
        CHECK(navigableAt(*result.grid, 0, 0, 0));
        CHECK(result.navigable == 1000 - 27);

        SUBCASE("half a voxel of clearance still rounds up to one voxel")
        {
            auto half = makeInput(glm::vec3(0.0f), glm::vec3(10.0f), 1.0f, 0.5f);
            half.spheres.push_back({glm::vec3(5.5f), 0.3f});
            const auto halfResult = vol::voxelize(half);
            REQUIRE(halfResult.grid.get() != nullptr);
            CHECK(halfResult.navigable == 1000 - 27);
        }
    }

    TEST_CASE("more voxels than the cap is an error that names a voxel size that fits")
    {
        const auto result = vol::voxelize(makeInput(glm::vec3(0.0f), glm::vec3(1000.0f), 1.0f));
        CHECK(result.grid.get() == nullptr);
        CHECK(result.error.find("raise voxelSize to >= 3.91") != std::string::npos);
        CHECK(vol::voxelCountFor(glm::vec3(0.0f), glm::vec3(1000.0f), 3.91f) <= vol::kMaxVoxels);

        const float minimum = vol::minVoxelSizeFor(glm::vec3(0.0f), glm::vec3(1000.0f));
        CHECK(vol::voxelCountFor(glm::vec3(0.0f), glm::vec3(1000.0f), minimum) <= vol::kMaxVoxels);
        CHECK(vol::voxelCountFor(glm::vec3(0.0f), glm::vec3(1000.0f), minimum * 0.99f) > vol::kMaxVoxels);
    }

    TEST_CASE("a cancelled bake returns no grid")
    {
        auto token = threading::CancellationToken::create();
        token->cancel();
        auto input = makeInput(glm::vec3(0.0f), glm::vec3(8.0f), 1.0f);
        addBox(input, glm::vec3(2.5f), glm::vec3(5.5f));
        const auto result = vol::voxelize(input, token.get());
        CHECK(result.cancelled);
        CHECK(result.grid.get() == nullptr);
    }

    TEST_CASE("invalid input is refused with a reason")
    {
        CHECK(vol::voxelize(makeInput(glm::vec3(0.0f), glm::vec3(0.0f, 4.0f, 4.0f), 1.0f)).grid.get() == nullptr);
        CHECK(vol::voxelize(makeInput(glm::vec3(0.0f), glm::vec3(4.0f), 0.0f)).grid.get() == nullptr);
        CHECK(vol::voxelize(makeInput(glm::vec3(0.0f), glm::vec3(4.0f), 1.0f, -1.0f)).grid.get() == nullptr);
        const auto badConnectivity = vol::voxelize(makeInput(glm::vec3(0.0f), glm::vec3(4.0f), 1.0f, 0.0f, 8));
        CHECK(badConnectivity.grid.get() == nullptr);
        CHECK(badConnectivity.error.find("connectivity") != std::string::npos);
    }

    TEST_CASE("progress rises to 1")
    {
        auto input = makeInput(glm::vec3(0.0f), glm::vec3(6.0f), 1.0f, 1.0f);
        addBox(input, glm::vec3(1.5f), glm::vec3(4.5f));
        std::vector<float> reports;
        const auto result = vol::voxelize(input, nullptr, [&reports](float value) { reports.push_back(value); });
        REQUIRE(result.grid.get() != nullptr);
        REQUIRE_FALSE(reports.empty());
        for (size_t i = 1; i < reports.size(); ++i)
            CHECK(reports[i] > reports[i - 1]);
        CHECK(reports.back() == doctest::Approx(1.0f));
    }
}
