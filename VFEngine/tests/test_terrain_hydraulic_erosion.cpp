#include <doctest.h>

#include "test_repo_scan_helpers.hpp"

#include <terrain/BrushTypes.hpp>
#include <terrain/TerrainHydraulicErosion.hpp>
#include <terrain/TerrainTypes.hpp>
#include <export/ShaderCompiler.hpp>
#include <resource/ShaderResource.hpp>
#include <resource/Types.hpp>
#include "render/gpudriven/brush/HydraulicErosionPipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// ============================================================
// VK-1616 - hydraulic erosion brush (virtual pipe model, Mei/Decaudin/Hu 2007).
//
// The brush itself runs on the GPU and its only observable is the height field it hands back, so
// nothing here can exercise the real dispatch. What CAN be pinned without a Vulkan device is
// everything that would be miserable to debug through the viewport:
//
//   * the global-vertex <-> (tile, local slot) mapping, which is what lets one dispatch span
//     several tiles without cracking the seams between them,
//   * the numeric invariants of the solver (mass conservation, water non-negativity, the rim
//     guarantee, no NaN), via the CPU twin in utilities/terrain/TerrainHydraulicErosion.hpp,
//   * that the shader still compiles, and that the handful of expressions the CPU twin cannot
//     reach are still present in it.
//
// The CPU twin is a line-by-line mirror of resources/shaders/terrain/hydraulic_erosion.glsl. A
// divergence between them is silent, so treat a failure here as evidence about the shader too.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    fs::path shaderRoot()
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");
        return *root / "resources" / "shaders";
    }

    std::string readShaderSource(const fs::path& path)
    {
        std::ifstream in(path);
        REQUIRE_MESSAGE(in.is_open(), "missing " << path.string());
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    size_t countOccurrences(const std::string& haystack, const std::string& needle)
    {
        size_t count = 0;
        for (size_t at = haystack.find(needle); at != std::string::npos;
             at = haystack.find(needle, at + needle.size()))
        {
            ++count;
        }
        return count;
    }

    // A square region with a linear slope down +X plus a small cross-slope ripple, which is what
    // gives the flow something to organise itself around instead of running perfectly parallel.
    terrain::HydraulicRegion makeRegion(uint32_t side)
    {
        terrain::HydraulicRegion region;
        region.originX = 0;
        region.originZ = 0;
        region.width = side;
        region.height = side;
        return region;
    }

    // Height drops with +X, so +X is downhill. `flatFrom` is the column where the ramp levels off
    // into a basin; the default puts it past the far edge, giving a pure constant-gradient slope.
    void seedSlope(terrain::HydraulicState& state, const terrain::HydraulicRegion& region,
                   float dropPerCell = 0.15f, uint32_t flatFrom = ~0u)
    {
        state.resize(region.cellCount());
        for (uint32_t z = 0; z < region.height; ++z)
        {
            for (uint32_t x = 0; x < region.width; ++x)
            {
                const uint32_t idx = region.index(x, z);
                const float ripple = 0.05f * std::sin(static_cast<float>(z) * 0.7f);
                const float ramp = static_cast<float>(std::min(x, flatFrom));
                const float height = 20.0f - dropPerCell * ramp + ripple;
                state.terrain[idx] = height;
                state.original[idx] = height;
                state.valid[idx] = 1.0f;
            }
        }
    }

    terrain::HydraulicBrushShape makeBrush(const terrain::HydraulicRegion& region, float radius,
                                           terrain::BrushFalloff falloff = terrain::BrushFalloff::Smooth)
    {
        terrain::HydraulicBrushShape brush;
        brush.center = glm::vec2(static_cast<float>(region.width) * 0.5f,
                                 static_cast<float>(region.height) * 0.5f);
        brush.radius = radius;
        brush.falloff = falloff;
        brush.shape = terrain::BrushShape::Circle;
        brush.vertexSpacing = 1.0f;
        return brush;
    }

    terrain::HydraulicParams makeParams(float cellSize = 1.0f, uint32_t iterations = 32)
    {
        terrain::HydraulicParams params;
        params.iterations = iterations;
        params.cellSize = cellSize;
        params.validate();
        return params;
    }

    bool allFinite(const std::vector<float>& values)
    {
        return std::all_of(values.begin(), values.end(),
                           [](float v) { return std::isfinite(v); });
    }
}

TEST_SUITE("TerrainHydraulicErosion")
{
    // --------------------------------------------------------------------------------------
    // Global vertex mapping. Highest-risk code in the feature and the only part that can crack
    // terrain seams, which is exactly the failure the whole scheme exists to avoid.
    // --------------------------------------------------------------------------------------

    TEST_CASE("floored division places negative coordinates in the correct tile")
    {
        // The trap: C++ integer division truncates toward zero, so -1/32 is 0, which would put a
        // vertex just left of the origin into tile 0 instead of tile -1.
        CHECK(terrain::hydraulicFloorDiv(-1, 32) == -1);
        CHECK(terrain::hydraulicFloorDiv(-32, 32) == -1);
        CHECK(terrain::hydraulicFloorDiv(-33, 32) == -2);
        CHECK(terrain::hydraulicFloorDiv(0, 32) == 0);
        CHECK(terrain::hydraulicFloorDiv(31, 32) == 0);
        CHECK(terrain::hydraulicFloorDiv(32, 32) == 1);

        CHECK(terrain::tileIndexForGlobalVertex(-1, 32) == -1);
        CHECK(terrain::localIndexForGlobalVertex(-1, 32) == 31u);
        CHECK(terrain::tileIndexForGlobalVertex(-32, 32) == -1);
        CHECK(terrain::localIndexForGlobalVertex(-32, 32) == 0u);
    }

    TEST_CASE("global vertex <-> tile-local round-trips at every resolution")
    {
        for (uint32_t quads : {32u, 64u, 128u})
        {
            for (int32_t g = -3 * static_cast<int32_t>(quads); g <= 3 * static_cast<int32_t>(quads); ++g)
            {
                const int32_t tile = terrain::tileIndexForGlobalVertex(g, quads);
                const uint32_t local = terrain::localIndexForGlobalVertex(g, quads);

                CHECK(local < quads);
                CHECK(terrain::globalVertexForTileLocal(tile, local, quads) == g);
            }
        }
    }

    TEST_CASE("seam vertices have two owners and corners have four")
    {
        constexpr uint32_t quads = 32;

        SUBCASE("interior vertex has a single owner")
        {
            int owners = 0;
            terrain::forEachVertexOwner(5, 7, quads, [&](const terrain::TileCoord&, uint32_t, uint32_t)
            {
                ++owners;
            });
            CHECK(owners == 1);
        }

        SUBCASE("a vertex on the X seam is owned by both tiles that share it")
        {
            std::vector<std::pair<terrain::TileCoord, std::pair<uint32_t, uint32_t>>> hits;
            terrain::forEachVertexOwner(32, 7, quads,
                                        [&](const terrain::TileCoord& c, uint32_t lx, uint32_t lz)
                                        {
                                            hits.push_back({c, {lx, lz}});
                                        });
            REQUIRE(hits.size() == 2);
            // Tile 1 holds it as its first column; tile 0 holds the very same vertex as its last.
            const bool asFirst = std::any_of(hits.begin(), hits.end(), [](const auto& h)
            {
                return h.first.x == 1 && h.second.first == 0u && h.second.second == 7u;
            });
            const bool asLast = std::any_of(hits.begin(), hits.end(), [](const auto& h)
            {
                return h.first.x == 0 && h.second.first == quads && h.second.second == 7u;
            });
            CHECK(asFirst);
            CHECK(asLast);
        }

        SUBCASE("a corner vertex is owned by all four tiles that meet there")
        {
            int owners = 0;
            terrain::forEachVertexOwner(64, 32, quads, [&](const terrain::TileCoord&, uint32_t, uint32_t)
            {
                ++owners;
            });
            CHECK(owners == 4);
        }

        SUBCASE("negative seams behave the same way")
        {
            int owners = 0;
            terrain::forEachVertexOwner(-32, -32, quads, [&](const terrain::TileCoord&, uint32_t, uint32_t)
            {
                ++owners;
            });
            CHECK(owners == 4);
        }
    }

    TEST_CASE("the tile span covers every owner of every region vertex")
    {
        constexpr uint32_t quads = 32;

        // Slid across a whole tile so the region edge lands on, before and after a tile boundary.
        for (int32_t originX = -40; originX <= 40; ++originX)
        {
            terrain::HydraulicRegion region;
            region.originX = originX;
            region.originZ = originX;
            region.width = 20;
            region.height = 20;

            const terrain::HydraulicTileSpan span = terrain::hydraulicTileSpan(region, quads);

            for (uint32_t z = 0; z < region.height; ++z)
            {
                for (uint32_t x = 0; x < region.width; ++x)
                {
                    const int32_t gx = region.originX + static_cast<int32_t>(x);
                    const int32_t gz = region.originZ + static_cast<int32_t>(z);
                    terrain::forEachVertexOwner(gx, gz, quads,
                                                [&](const terrain::TileCoord& c, uint32_t, uint32_t)
                                                {
                                                    CHECK(c.x >= span.minX);
                                                    CHECK(c.x <= span.maxX);
                                                    CHECK(c.z >= span.minZ);
                                                    CHECK(c.z <= span.maxZ);
                                                });
                }
            }
        }
    }

    TEST_CASE("a gather/scatter round trip through tiles reproduces the field exactly")
    {
        // Stands in for TerrainService::applyHydraulicErosion's two loops without needing a grid:
        // the property that matters is that both sides of a seam end up holding the same bytes, so
        // syncBrushBoundaryHeights afterwards is a no-op rather than a correction.
        constexpr uint32_t quads = 8;
        constexpr uint32_t vertexCount = quads + 1;

        struct FakeTile
        {
            terrain::TileCoord coord;
            std::vector<float> heights;
        };

        std::vector<FakeTile> tiles;
        for (int32_t tz = 0; tz <= 1; ++tz)
            for (int32_t tx = 0; tx <= 1; ++tx)
            {
                FakeTile tile;
                tile.coord = terrain::TileCoord(tx, tz);
                tile.heights.resize(vertexCount * vertexCount);
                for (uint32_t lz = 0; lz < vertexCount; ++lz)
                    for (uint32_t lx = 0; lx < vertexCount; ++lx)
                    {
                        // Value is a pure function of the GLOBAL vertex, so duplicated seam
                        // vertices already agree - the invariant the real grid maintains.
                        const int32_t gx = terrain::globalVertexForTileLocal(tx, lx, quads);
                        const int32_t gz = terrain::globalVertexForTileLocal(tz, lz, quads);
                        tile.heights[lz * vertexCount + lx] =
                            static_cast<float>(gx) * 3.0f + static_cast<float>(gz);
                    }
                tiles.push_back(std::move(tile));
            }

        terrain::HydraulicRegion region;
        region.originX = 2;
        region.originZ = 3;
        region.width = 11;
        region.height = 9;

        std::vector<float> field(region.cellCount(), 0.0f);
        std::vector<uint32_t> valid(region.cellCount(), 0u);

        auto forEachRegionVertex = [&](const terrain::TileCoord& coord, auto&& fn)
        {
            for (uint32_t lz = 0; lz <= quads; ++lz)
            {
                const int32_t gz = terrain::globalVertexForTileLocal(coord.z, lz, quads);
                if (gz < region.originZ || gz >= region.originZ + static_cast<int32_t>(region.height))
                    continue;
                for (uint32_t lx = 0; lx <= quads; ++lx)
                {
                    const int32_t gx = terrain::globalVertexForTileLocal(coord.x, lx, quads);
                    if (gx < region.originX || gx >= region.originX + static_cast<int32_t>(region.width))
                        continue;
                    fn(region.index(static_cast<uint32_t>(gx - region.originX),
                                    static_cast<uint32_t>(gz - region.originZ)),
                       lz * vertexCount + lx);
                }
            }
        };

        for (const auto& tile : tiles)
            forEachRegionVertex(tile.coord, [&](uint32_t cell, uint32_t slot)
            {
                field[cell] = tile.heights[slot];
                valid[cell] = 1u;
            });

        CHECK(std::all_of(valid.begin(), valid.end(), [](uint32_t v) { return v == 1u; }));

        // Modify the field the way the solver would, then scatter it back.
        for (auto& v : field)
            v += 1.0f;

        auto before = tiles;
        for (auto& tile : tiles)
            forEachRegionVertex(tile.coord, [&](uint32_t cell, uint32_t slot)
            {
                if (valid[cell] != 0u)
                    tile.heights[slot] = field[cell];
            });

        // Every vertex inside the region moved by exactly 1, and every one outside is untouched.
        for (size_t t = 0; t < tiles.size(); ++t)
        {
            for (uint32_t lz = 0; lz < vertexCount; ++lz)
                for (uint32_t lx = 0; lx < vertexCount; ++lx)
                {
                    const int32_t gx = terrain::globalVertexForTileLocal(tiles[t].coord.x, lx, quads);
                    const int32_t gz = terrain::globalVertexForTileLocal(tiles[t].coord.z, lz, quads);
                    const uint32_t slot = lz * vertexCount + lx;
                    const float delta = tiles[t].heights[slot] - before[t].heights[slot];
                    CHECK(delta == (region.contains(gx, gz) ? 1.0f : 0.0f));
                }
        }

        // The seam column is the point of the exercise: tile (0,z)'s last column must equal tile
        // (1,z)'s first column BIT-EXACTLY, so syncBrushBoundaryHeights' (a + b) * 0.5 is identity.
        const FakeTile& left = tiles[0];
        const FakeTile& right = tiles[1];
        REQUIRE(left.coord.x == 0);
        REQUIRE(right.coord.x == 1);
        for (uint32_t lz = 0; lz < vertexCount; ++lz)
        {
            const float a = left.heights[lz * vertexCount + quads];
            const float b = right.heights[lz * vertexCount + 0];
            CHECK(a == b);
            CHECK((a + b) * 0.5f == a);
        }
    }

    // --------------------------------------------------------------------------------------
    // Region and budget
    // --------------------------------------------------------------------------------------

    TEST_CASE("the region covers the brush disc with padding on every side")
    {
        const float radius = 7.0f;
        const float spacing = 0.25f;
        const glm::vec2 center(13.3f, -4.8f);

        const terrain::HydraulicRegion region = terrain::computeHydraulicRegion(center, radius, spacing);
        REQUIRE(region.valid());

        const int32_t pad = static_cast<int32_t>(terrain::hydraulicPadCells(radius, spacing));
        const int32_t minInside = static_cast<int32_t>(std::floor((center.x - radius) / spacing));
        const int32_t maxInside = static_cast<int32_t>(std::ceil((center.x + radius) / spacing));

        CHECK(region.originX <= minInside - pad);
        CHECK(region.originX + static_cast<int32_t>(region.width) - 1 >= maxInside + pad);
        CHECK(region.width <= terrain::HYDRAULIC_MAX_REGION_SIDE);
        CHECK(region.height <= terrain::HYDRAULIC_MAX_REGION_SIDE);
    }

    TEST_CASE("an oversized brush clamps the region while staying centred")
    {
        // High tiles at 0.25 m spacing make a radius-100 brush ~800 cells across, past the cap.
        const terrain::HydraulicRegion region =
            terrain::computeHydraulicRegion(glm::vec2(0.0f), 100.0f, 0.25f);
        REQUIRE(region.valid());
        CHECK(region.width == terrain::HYDRAULIC_MAX_REGION_SIDE);
        CHECK(region.height == terrain::HYDRAULIC_MAX_REGION_SIDE);

        // Still straddling the brush centre rather than having drifted to one side.
        const int32_t centreX = region.originX + static_cast<int32_t>(region.width) / 2;
        CHECK(std::abs(centreX) <= 1);
    }

    TEST_CASE("the iteration budget trades depth against region size")
    {
        // A small region keeps everything the user asked for.
        CHECK(terrain::budgetHydraulicIterations(64, 2000) == 64u);

        // A large one degrades instead of hitching, and never to zero.
        const uint32_t huge = terrain::HYDRAULIC_MAX_REGION_SIDE * terrain::HYDRAULIC_MAX_REGION_SIDE;
        const uint32_t budgeted = terrain::budgetHydraulicIterations(128, huge);
        CHECK(budgeted >= 1u);
        CHECK(budgeted <= 128u);
        CHECK(static_cast<uint64_t>(budgeted) * huge <= terrain::HYDRAULIC_CELL_STEP_BUDGET);

        CHECK(terrain::budgetHydraulicIterations(32, 0) == 1u);
    }

    // --------------------------------------------------------------------------------------
    // Parameters
    // --------------------------------------------------------------------------------------

    TEST_CASE("validate clamps every field and derives a CFL-safe timestep")
    {
        terrain::HydraulicParams params;
        params.rainRate = 99.0f;
        params.sedimentCapacity = -5.0f;
        params.evaporation = 3.0f;
        params.hardness = 7.0f;
        params.smoothing = -1.0f;
        params.iterations = 100000;
        params.talusAngle = 0.0f;
        params.cellSize = 0.25f;
        params.validate();

        CHECK(params.rainRate == doctest::Approx(2.0f));
        CHECK(params.sedimentCapacity == doctest::Approx(0.1f));
        CHECK(params.evaporation == doctest::Approx(0.2f));
        CHECK(params.hardness == doctest::Approx(1.0f));
        CHECK(params.smoothing == doctest::Approx(0.0f));
        CHECK(params.iterations == 128u);
        CHECK(params.talusAngle == doctest::Approx(5.0f));

        // dt is derived, never a slider: the CFL bound is what keeps the solver stable, and the
        // paper's own timings scale dt linearly with cell size for exactly this reason.
        CHECK(params.dt > 0.0f);
        CHECK(params.dt * params.maxVelocity() == doctest::Approx(terrain::HYDRAULIC_CFL * params.cellSize));

        terrain::HydraulicParams coarse = params;
        coarse.cellSize = 1.0f;
        coarse.validate();
        CHECK(coarse.dt == doctest::Approx(params.dt * 4.0f));
    }

    TEST_CASE("brush params defaults survive validation and hydraulic fields clamp")
    {
        terrain::BrushParams params;
        const terrain::BrushParams defaults = params;
        params.validate();

        CHECK(params.hydraulicRainRate == doctest::Approx(defaults.hydraulicRainRate));
        CHECK(params.hydraulicSedimentCapacity == doctest::Approx(defaults.hydraulicSedimentCapacity));
        CHECK(params.hydraulicEvaporation == doctest::Approx(defaults.hydraulicEvaporation));
        CHECK(params.hydraulicHardness == doctest::Approx(defaults.hydraulicHardness));
        CHECK(params.hydraulicSmoothing == doctest::Approx(defaults.hydraulicSmoothing));
        CHECK(params.hydraulicIterations == defaults.hydraulicIterations);

        params.hydraulicRainRate = -1.0f;
        params.hydraulicIterations = 0;
        params.hydraulicSmoothing = 5.0f;
        params.validate();
        CHECK(params.hydraulicRainRate == doctest::Approx(0.0f));
        CHECK(params.hydraulicIterations == 1u);
        CHECK(params.hydraulicSmoothing == doctest::Approx(1.0f));
    }

    TEST_CASE("Hydraulic is appended to BrushType, never inserted")
    {
        // hydraulic_erosion.glsl and brush_compute.glsl both switch on the raw integer, so
        // renumbering an existing brush would silently repoint it at another kernel.
        CHECK(static_cast<int>(terrain::BrushType::Raise) == 0);
        CHECK(static_cast<int>(terrain::BrushType::Erosion) == 6);
        CHECK(static_cast<int>(terrain::BrushType::Ramp) == 8);
        CHECK(static_cast<int>(terrain::BrushType::Hydraulic) == 9);
    }

    TEST_CASE("invert biases toward deposition without disabling dissolving outright")
    {
        terrain::HydraulicParams params = makeParams();
        const float dissolve = params.dissolveRate();
        const float deposit = params.depositRate();
        CHECK(dissolve > 0.0f);

        params.depositBias = true;
        // Strictly positive: zeroing it would make Shift a no-op, because nothing would ever enter
        // suspension for the deposition branch to drop.
        CHECK(params.dissolveRate() > 0.0f);
        CHECK(params.dissolveRate() < dissolve);
        CHECK(params.depositRate() > deposit);
        CHECK(params.depositRate() <= 1.0f);
    }

    // --------------------------------------------------------------------------------------
    // Solver invariants
    // --------------------------------------------------------------------------------------

    TEST_CASE("the erosion pass conserves terrain plus suspended sediment")
    {
        // This is the invariant the whole feature rests on: erosion does not create or destroy
        // ground, it moves it between b and s. If this drifts, the terrain drifts with it.
        const terrain::HydraulicRegion region = makeRegion(24);
        terrain::HydraulicState state;
        seedSlope(state, region);
        const terrain::HydraulicParams params = makeParams();
        const terrain::HydraulicBrushShape brush = makeBrush(region, 10.0f);

        for (uint32_t i = 0; i < 8; ++i)
        {
            terrain::hydraulicPassFlux(state, region, params, brush);
            terrain::hydraulicPassWater(state, region, params, brush);

            double before = 0.0;
            for (uint32_t c = 0; c < region.cellCount(); ++c)
                before += static_cast<double>(state.terrain[c]) + state.sediment[c];

            terrain::hydraulicPassErosion(state, region, params);

            double after = 0.0;
            for (uint32_t c = 0; c < region.cellCount(); ++c)
                after += static_cast<double>(state.terrain[c]) + state.sediment[c];

            // Tolerance is float rounding on ~600 cells, not slack: a real leak moves a whole
            // delta per cell and lands orders of magnitude outside this.
            CHECK(after == doctest::Approx(before).epsilon(1e-6));

            terrain::hydraulicPassAdvect(state, region, params);
        }
    }

    TEST_CASE("resolve settles residual sediment back into the terrain")
    {
        // Regression guard for the mass-loss bug: sediment still in suspension when the loop ends
        // was subtracted from the terrain, so dropping it from the resolve blend drains the ground
        // under the cursor at one dab per frame. `+ sediment` is not cosmetic.
        const terrain::HydraulicRegion region = makeRegion(8);
        terrain::HydraulicState state;
        state.resize(region.cellCount());
        for (uint32_t c = 0; c < region.cellCount(); ++c)
        {
            state.valid[c] = 1.0f;
            state.original[c] = 10.0f;
            state.terrain[c] = 9.0f;  // one metre eroded away...
            state.sediment[c] = 0.4f; // ...of which 0.4 is still in the water
        }

        terrain::HydraulicBrushShape brush = makeBrush(region, 100.0f, terrain::BrushFalloff::Constant);
        terrain::hydraulicPassResolve(state, region, brush, 1.0f, -1000.0f, 1000.0f);

        // 10 + ((9 + 0.4) - 10) * 1 == 9.4, not 9.0.
        for (uint32_t c = 0; c < region.cellCount(); ++c)
            CHECK(state.terrain[c] == doctest::Approx(9.4f));
    }

    TEST_CASE("the delta is exactly zero outside the brush radius")
    {
        const terrain::HydraulicRegion region = makeRegion(48);
        terrain::HydraulicState state;
        seedSlope(state, region);
        const std::vector<float> original = state.terrain;

        terrain::HydraulicParams params = makeParams(1.0f, 24);
        params.rainRate = 2.0f;
        const terrain::HydraulicBrushShape brush = makeBrush(region, 12.0f);

        terrain::simulateHydraulicErosion(state, region, params, brush, 1.0f, -1000.0f, 1000.0f);

        size_t outsideChecked = 0;
        for (uint32_t z = 0; z < region.height; ++z)
        {
            for (uint32_t x = 0; x < region.width; ++x)
            {
                const uint32_t idx = region.index(x, z);
                if (terrain::hydraulicInfluence(brush, region.originX + static_cast<int32_t>(x),
                                                region.originZ + static_cast<int32_t>(z)) > 0.0f)
                    continue;

                // Bit-exact, not approximate: at weight 0 the blend is base + delta*0 == base.
                CHECK(state.terrain[idx] == original[idx]);
                ++outsideChecked;
            }
        }
        CHECK(outsideChecked > 0);
    }

    TEST_CASE("cells no tile owns are never modified")
    {
        const terrain::HydraulicRegion region = makeRegion(32);
        terrain::HydraulicState state;
        seedSlope(state, region);

        // Punch a hole, as an unstreamed or out-of-world tile would.
        for (uint32_t z = 10; z < 20; ++z)
            for (uint32_t x = 10; x < 20; ++x)
                state.valid[region.index(x, z)] = 0.0f;

        const std::vector<float> original = state.terrain;

        terrain::HydraulicParams params = makeParams(1.0f, 24);
        params.rainRate = 2.0f;
        const terrain::HydraulicBrushShape brush = makeBrush(region, 20.0f);

        terrain::simulateHydraulicErosion(state, region, params, brush, 1.0f, -1000.0f, 1000.0f);

        for (uint32_t z = 10; z < 20; ++z)
            for (uint32_t x = 10; x < 20; ++x)
                CHECK(state.terrain[region.index(x, z)] == original[region.index(x, z)]);
    }

    TEST_CASE("water never goes negative and nothing turns into NaN")
    {
        // The K limiter is what guarantees the first property; without it a cell drains more water
        // than it holds, d goes negative, and the velocity divide produces an inf that never
        // washes back out of the field.
        const terrain::HydraulicRegion region = makeRegion(32);
        terrain::HydraulicState state;
        seedSlope(state, region, 3.0f); // a deliberately violent slope

        terrain::HydraulicParams params = makeParams(0.25f, 128);
        params.rainRate = 2.0f;
        params.sedimentCapacity = 5.0f;
        params.evaporation = 0.0f;
        params.validate();

        const terrain::HydraulicBrushShape brush = makeBrush(region, 16.0f);

        for (uint32_t i = 0; i < params.iterations; ++i)
        {
            terrain::hydraulicPassFlux(state, region, params, brush);
            terrain::hydraulicPassWater(state, region, params, brush);
            terrain::hydraulicPassErosion(state, region, params);
            terrain::hydraulicPassAdvect(state, region, params);

            for (uint32_t c = 0; c < region.cellCount(); ++c)
                REQUIRE(state.water[c] >= 0.0f);
        }

        CHECK(allFinite(state.terrain));
        CHECK(allFinite(state.water));
        CHECK(allFinite(state.sediment));
    }

    TEST_CASE("a flat field still produces a usable tilt instead of dividing by zero")
    {
        const terrain::HydraulicRegion region = makeRegion(16);
        terrain::HydraulicState state;
        state.resize(region.cellCount());
        for (uint32_t c = 0; c < region.cellCount(); ++c)
        {
            state.valid[c] = 1.0f;
            state.terrain[c] = 5.0f;
            state.original[c] = 5.0f;
        }

        const terrain::HydraulicParams params = makeParams();
        const terrain::HydraulicBrushShape brush = makeBrush(region, 8.0f);

        terrain::hydraulicPassFlux(state, region, params, brush);
        terrain::hydraulicPassWater(state, region, params, brush);

        for (uint32_t c = 0; c < region.cellCount(); ++c)
        {
            // The paper calls the vanishing capacity on flat ground out as the model's blind spot;
            // the floor is what stops the brush doing visibly nothing on gentle terrain.
            CHECK(state.velocity[c].z >= terrain::HYDRAULIC_MIN_TILT_SIN);
            CHECK(std::isfinite(state.velocity[c].z));
        }
    }

    TEST_CASE("erosion carves the slope and deposits in the basin, unlike thermal")
    {
        // The terrain is a ramp that levels off into a basin. That shape is deliberate: on a
        // CONSTANT-gradient slope draining off the region edge, flow only ever accelerates, so
        // capacity (Kc * sin(alpha) * |v|) rises all the way down and the sediment leaves through
        // the absorbing rim without ever settling. Deposition needs flow that SLOWS -- which is
        // exactly what the flat does, as the gradient collapses to the sin(alpha) floor.
        const terrain::HydraulicRegion region = makeRegion(48);
        const uint32_t flatFrom = 18;
        const terrain::HydraulicBrushShape brush = makeBrush(region, 18.0f, terrain::BrushFalloff::Constant);

        terrain::HydraulicParams params = makeParams(1.0f, 48);
        params.rainRate = 2.0f;
        params.smoothing = 0.0f; // isolate the fluvial behaviour from the talus sub-pass
        params.validate();

        terrain::HydraulicState hydraulic;
        seedSlope(hydraulic, region, 0.15f, flatFrom);
        const std::vector<float> original = hydraulic.terrain;
        terrain::simulateHydraulicErosion(hydraulic, region, params, brush, 1.0f, -1000.0f, 1000.0f);

        int lowered = 0;
        int raised = 0;
        float maxChange = 0.0f;
        double slopeDelta = 0.0;
        double basinDelta = 0.0;
        for (uint32_t z = 0; z < region.height; ++z)
        {
            for (uint32_t x = 0; x < region.width; ++x)
            {
                const uint32_t c = region.index(x, z);
                const float delta = hydraulic.terrain[c] - original[c];
                if (delta < -1e-5f) ++lowered;
                if (delta > 1e-5f) ++raised;
                maxChange = std::max(maxChange, std::abs(delta));
                (x < flatFrom ? slopeDelta : basinDelta) += delta;
            }
        }

        // Both signs must appear: ground is dissolved on the ramp and dropped again in the basin.
        // That is the AC's "carves flow channels and deposits sediment", made executable.
        CHECK(lowered > 0);
        CHECK(raised > 0);
        CHECK(maxChange > 1e-4f);
        CHECK(basinDelta > slopeDelta);

        // Thermal relaxation on the SAME terrain (same profile, or the comparison would only be
        // measuring the difference between two starting shapes). A 0.15/cell ramp is far below the
        // 45-degree talus threshold, so thermal finds no violations and leaves it untouched, while
        // hydraulic erosion reshapes it. This is the AC's "visually distinct from thermal erosion
        // on a test slope", made executable.
        terrain::HydraulicState thermal;
        seedSlope(thermal, region, 0.15f, flatFrom);
        terrain::HydraulicParams thermalParams = params;
        thermalParams.smoothing = 1.0f;
        for (uint32_t i = 0; i < params.iterations; ++i)
            terrain::hydraulicPassThermal(thermal, region, thermalParams);

        double divergence = 0.0;
        for (uint32_t c = 0; c < region.cellCount(); ++c)
            divergence += std::abs(hydraulic.terrain[c] - thermal.terrain[c]);
        CHECK(divergence > 1e-3);
    }

    TEST_CASE("invert removes materially less ground than a normal stroke")
    {
        const terrain::HydraulicRegion region = makeRegion(32);
        const terrain::HydraulicBrushShape brush = makeBrush(region, 14.0f, terrain::BrushFalloff::Constant);

        auto erodedVolume = [&](bool invert)
        {
            terrain::HydraulicParams params = makeParams(1.0f, 32);
            params.rainRate = 2.0f;
            params.smoothing = 0.0f;
            params.validate();
            params.depositBias = invert;

            terrain::HydraulicState state;
            seedSlope(state, region);
            const std::vector<float> original = state.terrain;
            terrain::simulateHydraulicErosion(state, region, params, brush, 1.0f, -1000.0f, 1000.0f);

            double removed = 0.0;
            for (uint32_t c = 0; c < region.cellCount(); ++c)
                removed += std::max(0.0f, original[c] - state.terrain[c]);
            return removed;
        };

        const double carved = erodedVolume(false);
        const double silted = erodedVolume(true);

        REQUIRE(carved > 0.0);
        CHECK(silted < carved);
    }

    TEST_CASE("the result is clamped to the tile height range")
    {
        const terrain::HydraulicRegion region = makeRegion(24);
        terrain::HydraulicState state;
        seedSlope(state, region, 2.0f);

        terrain::HydraulicParams params = makeParams(1.0f, 32);
        params.rainRate = 2.0f;
        const terrain::HydraulicBrushShape brush = makeBrush(region, 12.0f, terrain::BrushFalloff::Constant);

        terrain::simulateHydraulicErosion(state, region, params, brush, 1.0f, 0.0f, 15.0f);

        for (uint32_t c = 0; c < region.cellCount(); ++c)
        {
            CHECK(state.terrain[c] >= 0.0f);
            CHECK(state.terrain[c] <= 15.0f);
        }
    }

    // --------------------------------------------------------------------------------------
    // Shader
    // --------------------------------------------------------------------------------------

    TEST_CASE("hydraulic_erosion.glsl compiles to SPIR-V")
    {
        // The only automated proof the shader is valid GLSL. A broken compute shader is nearly
        // silent at runtime: the pipeline fails to create, simulate() returns false, and the brush
        // just does nothing.
        const fs::path shaderPath = shaderRoot() / "terrain" / "hydraulic_erosion.glsl";
        REQUIRE_MESSAGE(fs::exists(shaderPath), "missing " << shaderPath.string());

        const auto stages = resource::ShaderResource::readShaderFile(shaderPath.string());
        REQUIRE_MESSAGE(!stages.empty(), "no #type stages parsed from " << shaderPath.string());
        REQUIRE(stages.size() == 1);
        CHECK(stages[0].type == resource::ShaderType::COMPUTE);

        shaderCompiler::CompileOptions options;
        options.includeBasePath = shaderPath.parent_path();

        const auto spirv = shaderCompiler::compile(
            stages[0].source,
            shaderCompiler::shaderTypeToVulkanStage(static_cast<uint8_t>(stages[0].type)),
            "hydraulic_erosion.glsl", options);
        CHECK_MESSAGE(!spirv.empty(), "hydraulic_erosion.glsl failed to compile");
    }

    TEST_CASE("the shader still carries the expressions the CPU twin cannot reach")
    {
        const std::string source = readShaderSource(shaderRoot() / "terrain" / "hydraulic_erosion.glsl");

        SUBCASE("all six passes are dispatched")
        {
            CHECK(source.find("switch (pc.passIndex)") != std::string::npos);
            for (const char* label : {"case 0u: passFlux", "case 1u: passWater", "case 2u: passErosion",
                                      "case 3u: passAdvect", "case 4u: passThermal",
                                      "case 5u: passResolve"})
                CHECK_MESSAGE(source.find(label) != std::string::npos, "missing " << label);
        }

        SUBCASE("resolve settles residual sediment")
        {
            // The mass-conservation term. Losing it drains the terrain one dab at a time.
            CHECK(source.find("s[pingPongSlot(idx, pc.sedimentParity)]") != std::string::npos);
        }

        SUBCASE("the velocity is CFL-clamped")
        {
            CHECK(countOccurrences(source, "-pc.maxVelocity, pc.maxVelocity") == 2);
        }

        SUBCASE("the K limiter is present")
        {
            CHECK(source.find("min(1.0, (d1 * cellArea) / (total * pc.dt))") != std::string::npos);
        }

        SUBCASE("the erosion pass never reads a neighbour")
        {
            // If it did, it would be reading heights while other threads write theirs - a race
            // whose symptom is non-deterministic speckle that reads as a tuning problem. The tilt
            // is cached by the water pass precisely so this stays cell-local.
            const size_t begin = source.find("void passErosion(uint idx)");
            REQUIRE(begin != std::string::npos);
            const size_t end = source.find("float sampleSediment(vec2 pos)", begin);
            REQUIRE(end != std::string::npos);
            const std::string body = source.substr(begin, end - begin);
            CHECK(body.find("cellIndex(") == std::string::npos);
        }
    }

    TEST_CASE("the push-constant block matches the shader's declared layout")
    {
        using PC = render::gpudriven::HydraulicErosionPushConstants;
        static_assert(sizeof(PC) == 120, "keep in step with hydraulic_erosion.glsl");

        // Both vec2s land on 8-byte boundaries and every scalar after them is 4-byte aligned, so
        // the C++ struct and the std430 push block agree with no padding on either side.
        CHECK(offsetof(PC, regionOriginWorld) == 0);
        CHECK(offsetof(PC, brushCenter) == 8);
        CHECK(offsetof(PC, regionWidth) == 16);
        CHECK(offsetof(PC, passIndex) == 24);
        CHECK(offsetof(PC, terrainParity) == 36);
        CHECK(offsetof(PC, sedimentParity) == 40);
        CHECK(offsetof(PC, cellCount) == 44);
        CHECK(offsetof(PC, cellSize) == 48);
        CHECK(offsetof(PC, dt) == 56);
        CHECK(offsetof(PC, evaporation) == 76);
        CHECK(offsetof(PC, maxVelocity) == 88);
        CHECK(offsetof(PC, talusThreshold) == 104);
        CHECK(offsetof(PC, minHeight) == 112);
        CHECK(offsetof(PC, maxHeight) == 116);

        // Comfortably inside the 128-byte guaranteed minimum push-constant size, and separate from
        // BrushComputePushConstants, which is pinned at 96 and shared by the other nine brushes.
        CHECK(sizeof(PC) <= 128);
    }
}
