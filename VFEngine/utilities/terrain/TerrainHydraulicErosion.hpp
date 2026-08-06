#pragma once

#include "BrushFalloff.hpp"
#include "BrushTypes.hpp"
#include "TerrainTypes.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace terrain
{
    // VK-1616 - hydraulic erosion brush, virtual pipe model.
    //
    // CPU twin of resources/shaders/terrain/hydraulic_erosion.glsl. Keep both in sync; there is no
    // codegen between them. The GPU path is what the brush actually runs -- this exists so the
    // numerics, the region math and the tile gather/scatter can be unit-tested without a Vulkan
    // device (Tests cannot reach TerrainService at all: it needs a device, a grid and a physics
    // provider). Same arrangement as water/RippleSimMath.hpp vs water/ripple_sim.glsl.
    //
    // Model: Mei, Decaudin & Hu, "Fast Hydraulic Erosion Simulation and Visualization on GPU"
    // (Pacific Graphics 2007). Per cell we keep terrain height b, water depth d, suspended sediment
    // s, the four outflow fluxes f, and the velocity v. One iteration is
    //
    //   0 FLUX     rain, then f += dt*A*g*dh/l per pipe, then scale by K so outflow <= water present
    //   1 WATER    d += net flux volume / cell area; derive v from the flux; also cache sin(tilt)
    //   2 EROSION  C = Kc*sin(alpha)*|v|; C > s dissolves ground into the water, C <= s deposits
    //   3 ADVECT   move s along v (semi-Lagrangian backtrace), then evaporate
    //   4 THERMAL  every HYDRAULIC_THERMAL_INTERVAL-th iteration, one talus relaxation sweep
    //
    // and after the last iteration a single RESOLVE pass blends the result back through the brush
    // falloff. Unlike thermal erosion (brush_compute.glsl case 6) this is NOT a per-tile kernel:
    // water has to cross tile seams, so it runs over one brush-centred rect in *global vertex
    // space* that spans however many tiles the brush touches.
    //
    // Two things here are deliberate departures from the paper, both because a brush is not a
    // standalone terrain generator:
    //
    //   * The region boundary is ABSORBING, not the paper's no-slip. Water (and the sediment it
    //     carries) that leaves the rect is discarded. No-slip would pond water against the rim and
    //     ring the brush with a deposition ridge.
    //   * `rainRate` and `evaporation` are per-ITERATION amounts, not per-second rates multiplied
    //     by dt. dt is a function of the tile resolution, so rate*dt would make both sliders mean
    //     something different on a Low grid than on a High one.

    // ---------------------------------------------------------------------------------------
    // Constants
    // ---------------------------------------------------------------------------------------

    inline constexpr float HYDRAULIC_GRAVITY = 9.81f;

    // Courant number and the reference flow speed the timestep is sized against. dt scales
    // linearly with cell size, which is the same law the paper's own timings follow (dt 0.002 at
    // 256^2 down to 0.000125 at 4096^2 over a fixed extent).
    inline constexpr float HYDRAULIC_CFL = 0.5f;
    inline constexpr float HYDRAULIC_REF_SPEED = 8.0f;

    // Rain slider -> metres of water added per iteration at full brush influence. Keeps the
    // default (0.35) at ~0.0035 m/iteration, i.e. a sheet a few centimetres deep after 24
    // iterations -- deep enough to flow, shallow enough not to behave like a flood.
    inline constexpr float HYDRAULIC_RAIN_SCALE = 0.01f;

    // Below this depth a cell counts as dry: it neither erodes nor carries sediment, and whatever
    // it was carrying is dropped on the spot. This is what builds alluvial fans where flow runs out.
    inline constexpr float HYDRAULIC_MIN_WATER = 1.0e-4f;

    // sin(alpha) floor. The capacity term vanishes on flat ground, so without this the brush would
    // do visibly nothing on gentle terrain (the paper calls this out as the model's one blind spot).
    inline constexpr float HYDRAULIC_MIN_TILT_SIN = 0.05f;

    // Hardness slider endpoints. Soft ground dissolves readily and holds sediment in suspension;
    // hard ground resists dissolving and drops its load quickly. One slider, opposed ends, so the
    // brush always does *something* at either extreme.
    inline constexpr float HYDRAULIC_DISSOLVE_SOFT = 0.60f;
    inline constexpr float HYDRAULIC_DISSOLVE_HARD = 0.05f;
    inline constexpr float HYDRAULIC_DEPOSIT_SOFT = 0.05f;
    inline constexpr float HYDRAULIC_DEPOSIT_HARD = 0.60f;

    // Shift-invert keeps a little dissolving alive so there is something to deposit; see
    // HydraulicParams::depositBias.
    inline constexpr float HYDRAULIC_INVERT_DISSOLVE_SCALE = 0.15f;

    // Per-step ceiling on |db|, as a fraction of the cell size. Stops a single step from cutting
    // deeper than the cell is wide, which is what turns into a spike the mesh cannot represent.
    inline constexpr float HYDRAULIC_MAX_STEP_DELTA = 0.25f;

    // How often the thermal relaxation sub-pass runs. Unity interleaves thermal smoothing with its
    // hydraulic solver for the same reason: the pipe model carves clean channels but leaves the
    // ridges between them spiky.
    inline constexpr uint32_t HYDRAULIC_THERMAL_INTERVAL = 4;

    // Region padding, as a fraction of the brush radius, so channels have somewhere to run before
    // they meet the absorbing rim.
    inline constexpr float HYDRAULIC_PAD_FRACTION = 0.25f;
    inline constexpr uint32_t HYDRAULIC_MIN_PAD_CELLS = 4;

    // Hard ceiling on the simulated rect. A 512^2 region is ~15 MB of state and ~260k threads per
    // pass; past that a single dab stops being interactive. Radius is clamped against this rather
    // than silently simulating a sub-rect, so the brush never lies about its footprint.
    inline constexpr uint32_t HYDRAULIC_MAX_REGION_SIDE = 512;

    // Mirrors the `pass` push constant in hydraulic_erosion.glsl.
    enum class HydraulicPass : uint32_t
    {
        Flux = 0,
        Water = 1,
        Erosion = 2,
        Advect = 3,
        Thermal = 4,
        Resolve = 5
    };

    // ---------------------------------------------------------------------------------------
    // Parameters
    // ---------------------------------------------------------------------------------------

    struct HydraulicParams
    {
        // From BrushParams (user-facing).
        float rainRate = 0.35f;
        float sedimentCapacity = 1.2f;
        float evaporation = 0.015f;
        float hardness = 0.5f;
        float smoothing = 0.2f;
        uint32_t iterations = 24;
        float talusAngle = 45.0f;

        // From the tile config / brush placement (not user-facing).
        float cellSize = 1.0f;
        float dt = 0.0f;

        // Shift-invert. Negating the height delta would produce anti-gullies - ridges exactly where
        // the channels belong - which just reads as broken, so invert biases the sediment balance
        // toward deposition instead: material still mobilises, but settles again almost immediately,
        // so it migrates out of steep spots and silts up the hollows rather than carving.
        //
        // Note this is a BIAS, not a switch. Setting the dissolve rate to zero outright would make
        // Shift a literal no-op: with nothing dissolving, no sediment ever enters suspension, so
        // there is nothing for the deposition branch to deposit.
        bool depositBias = false;

        [[nodiscard]] float dissolveRate() const
        {
            const float base = HYDRAULIC_DISSOLVE_SOFT +
                (HYDRAULIC_DISSOLVE_HARD - HYDRAULIC_DISSOLVE_SOFT) * hardness;
            return depositBias ? base * HYDRAULIC_INVERT_DISSOLVE_SCALE : base;
        }

        [[nodiscard]] float depositRate() const
        {
            const float base = HYDRAULIC_DEPOSIT_SOFT + (HYDRAULIC_DEPOSIT_HARD - HYDRAULIC_DEPOSIT_SOFT) * hardness;
            return depositBias ? std::min(1.0f, base * 2.0f) : base;
        }

        // Largest velocity the advection backtrace may use before it would skip a cell.
        [[nodiscard]] float maxVelocity() const
        {
            return (dt > 0.0f) ? (HYDRAULIC_CFL * cellSize / dt) : 0.0f;
        }

        void validate()
        {
            rainRate = std::clamp(rainRate, 0.0f, 2.0f);
            sedimentCapacity = std::clamp(sedimentCapacity, 0.1f, 5.0f);
            evaporation = std::clamp(evaporation, 0.0f, 0.2f);
            hardness = std::clamp(hardness, 0.0f, 1.0f);
            smoothing = std::clamp(smoothing, 0.0f, 1.0f);
            iterations = std::clamp(iterations, 1u, 128u);
            talusAngle = std::clamp(talusAngle, 5.0f, 85.0f);
            cellSize = std::max(cellSize, 1.0e-4f);
            // The CFL bound is the whole reason dt is not a slider.
            dt = HYDRAULIC_CFL * cellSize / HYDRAULIC_REF_SPEED;
        }
    };

    inline HydraulicParams makeHydraulicParams(const BrushParams& brush, float cellSize)
    {
        HydraulicParams params;
        params.rainRate = brush.hydraulicRainRate;
        params.sedimentCapacity = brush.hydraulicSedimentCapacity;
        params.evaporation = brush.hydraulicEvaporation;
        params.hardness = brush.hydraulicHardness;
        params.smoothing = brush.hydraulicSmoothing;
        params.iterations = brush.hydraulicIterations;
        params.talusAngle = brush.talusAngle;
        params.cellSize = cellSize;
        params.validate();
        return params;
    }

    // ---------------------------------------------------------------------------------------
    // Global vertex space
    // ---------------------------------------------------------------------------------------
    //
    // Tiles duplicate their shared edge row/column: tile T's last column IS tile (T+1)'s first
    // column, which is exactly the invariant TerrainService::syncBrushBoundaryHeights maintains by
    // averaging. So with quads = vertexCount - 1, a tile-local vertex (lx, lz) of tile (tx, tz) is
    // the global vertex (tx*quads + lx, tz*quads + lz), and a seam vertex simply has two owners
    // (four at a corner). World position is global * vertexSpacing.

    // Floored division. Tile coordinates are signed and C++ integer division truncates toward zero,
    // which would put every vertex at negative coordinates in the wrong tile.
    inline int32_t hydraulicFloorDiv(int32_t value, int32_t divisor)
    {
        const int32_t q = value / divisor;
        const int32_t r = value % divisor;
        return (r != 0 && ((r < 0) != (divisor < 0))) ? q - 1 : q;
    }

    // Canonical owning tile of a global vertex. A seam vertex resolves to the tile that holds it at
    // local index 0, never to the one that holds it at local index `quads`.
    inline int32_t tileIndexForGlobalVertex(int32_t globalVertex, uint32_t quads)
    {
        return hydraulicFloorDiv(globalVertex, static_cast<int32_t>(quads));
    }

    // Local index within the canonical owner, always in [0, quads - 1].
    inline uint32_t localIndexForGlobalVertex(int32_t globalVertex, uint32_t quads)
    {
        const int32_t tile = tileIndexForGlobalVertex(globalVertex, quads);
        return static_cast<uint32_t>(globalVertex - tile * static_cast<int32_t>(quads));
    }

    inline int32_t globalVertexForTileLocal(int32_t tileCoord, uint32_t localIndex, uint32_t quads)
    {
        return tileCoord * static_cast<int32_t>(quads) + static_cast<int32_t>(localIndex);
    }

    // Every (tile, local) slot that stores this global vertex, along one axis: one normally, two on
    // a seam. Returns the count written into `tiles`/`locals`.
    inline uint32_t ownersForGlobalVertex(int32_t globalVertex, uint32_t quads,
                                          int32_t (&tiles)[2], uint32_t (&locals)[2])
    {
        const int32_t tile = tileIndexForGlobalVertex(globalVertex, quads);
        const uint32_t local = static_cast<uint32_t>(globalVertex - tile * static_cast<int32_t>(quads));
        tiles[0] = tile;
        locals[0] = local;
        if (local != 0)
            return 1;
        // The seam duplicate: this vertex is also the previous tile's last row/column.
        tiles[1] = tile - 1;
        locals[1] = quads;
        return 2;
    }

    // Visits every (tileCoord, localX, localZ) slot holding this global vertex - 1 in the interior,
    // 2 on a seam, 4 at a corner. Writing all of them is what keeps seams exact by construction and
    // makes the later syncBrushBoundaryHeights averaging a bit-identity no-op. It also covers the
    // diagonal corner owner, which that helper has never handled (it only walks +X and +Z).
    template <typename Fn>
    void forEachVertexOwner(int32_t globalX, int32_t globalZ, uint32_t quads, Fn&& fn)
    {
        int32_t tilesX[2];
        uint32_t localsX[2];
        int32_t tilesZ[2];
        uint32_t localsZ[2];
        const uint32_t countX = ownersForGlobalVertex(globalX, quads, tilesX, localsX);
        const uint32_t countZ = ownersForGlobalVertex(globalZ, quads, tilesZ, localsZ);

        for (uint32_t iz = 0; iz < countZ; ++iz)
            for (uint32_t ix = 0; ix < countX; ++ix)
                fn(TileCoord(tilesX[ix], tilesZ[iz]), localsX[ix], localsZ[iz]);
    }

    // The rect of global vertices the simulation runs over.
    struct HydraulicRegion
    {
        int32_t originX = 0;
        int32_t originZ = 0;
        uint32_t width = 0;
        uint32_t height = 0;

        [[nodiscard]] uint32_t cellCount() const { return width * height; }

        [[nodiscard]] uint32_t index(uint32_t x, uint32_t z) const { return z * width + x; }

        [[nodiscard]] bool contains(int32_t globalX, int32_t globalZ) const
        {
            return globalX >= originX && globalZ >= originZ &&
                globalX < originX + static_cast<int32_t>(width) &&
                globalZ < originZ + static_cast<int32_t>(height);
        }

        [[nodiscard]] bool valid() const { return width > 0 && height > 0; }
    };

    // Padding in cells for a given radius.
    inline uint32_t hydraulicPadCells(float radius, float vertexSpacing)
    {
        if (vertexSpacing <= 0.0f)
            return HYDRAULIC_MIN_PAD_CELLS;
        const float pad = radius * HYDRAULIC_PAD_FRACTION / vertexSpacing;
        return std::max(HYDRAULIC_MIN_PAD_CELLS, static_cast<uint32_t>(std::ceil(pad)));
    }

    // The brush-centred rect, padded, clamped to HYDRAULIC_MAX_REGION_SIDE.
    inline HydraulicRegion computeHydraulicRegion(const glm::vec2& brushCenter, float radius,
                                                  float vertexSpacing)
    {
        HydraulicRegion region;
        if (vertexSpacing <= 0.0f || radius <= 0.0f)
            return region;

        const int32_t pad = static_cast<int32_t>(hydraulicPadCells(radius, vertexSpacing));
        const int32_t minX = static_cast<int32_t>(std::floor((brushCenter.x - radius) / vertexSpacing)) - pad;
        const int32_t minZ = static_cast<int32_t>(std::floor((brushCenter.y - radius) / vertexSpacing)) - pad;
        const int32_t maxX = static_cast<int32_t>(std::ceil((brushCenter.x + radius) / vertexSpacing)) + pad;
        const int32_t maxZ = static_cast<int32_t>(std::ceil((brushCenter.y + radius) / vertexSpacing)) + pad;

        region.originX = minX;
        region.originZ = minZ;
        region.width = static_cast<uint32_t>(maxX - minX + 1);
        region.height = static_cast<uint32_t>(maxZ - minZ + 1);

        // Clamp around the centre rather than from the origin, so an over-large brush keeps its
        // footprint centred instead of drifting toward -X/-Z.
        if (region.width > HYDRAULIC_MAX_REGION_SIDE)
        {
            const int32_t excess = static_cast<int32_t>(region.width - HYDRAULIC_MAX_REGION_SIDE);
            region.originX += excess / 2;
            region.width = HYDRAULIC_MAX_REGION_SIDE;
        }
        if (region.height > HYDRAULIC_MAX_REGION_SIDE)
        {
            const int32_t excess = static_cast<int32_t>(region.height - HYDRAULIC_MAX_REGION_SIDE);
            region.originZ += excess / 2;
            region.height = HYDRAULIC_MAX_REGION_SIDE;
        }
        return region;
    }

    // The exact range of tiles holding any vertex of the region.
    //
    // Derived from the region rather than from BrushSampler::getAffectedTiles, because the two do
    // not agree at the edges: the region is snapped outward to the vertex lattice and can reach up
    // to one spacing beyond the padded world radius, and a region edge that lands exactly on a tile
    // boundary is also owned by the PREVIOUS tile at local index `quads`. Missing that tile would
    // write one side of a seam and not the other - the exact crack this whole global-vertex scheme
    // exists to avoid.
    struct HydraulicTileSpan
    {
        int32_t minX = 0;
        int32_t minZ = 0;
        int32_t maxX = 0;
        int32_t maxZ = 0;
    };

    inline HydraulicTileSpan hydraulicTileSpan(const HydraulicRegion& region, uint32_t quads)
    {
        HydraulicTileSpan span;
        if (!region.valid() || quads == 0)
            return span;

        const int32_t lastX = region.originX + static_cast<int32_t>(region.width) - 1;
        const int32_t lastZ = region.originZ + static_cast<int32_t>(region.height) - 1;

        span.minX = tileIndexForGlobalVertex(region.originX, quads);
        span.minZ = tileIndexForGlobalVertex(region.originZ, quads);
        if (localIndexForGlobalVertex(region.originX, quads) == 0)
            span.minX -= 1;
        if (localIndexForGlobalVertex(region.originZ, quads) == 0)
            span.minZ -= 1;

        span.maxX = tileIndexForGlobalVertex(lastX, quads);
        span.maxZ = tileIndexForGlobalVertex(lastZ, quads);
        return span;
    }

    // Per-frame work ceiling. `Iterations` is the user's quality knob, but a large radius on High
    // tiles can make the rect big enough that the requested count would stall the frame, so the
    // iteration count is traded off against the cell count. This is the AC's "iterations budgeted
    // per frame": at a small radius the full request survives; at a large one it degrades
    // gracefully instead of hitching.
    inline constexpr uint64_t HYDRAULIC_CELL_STEP_BUDGET = 32ull * 1000ull * 1000ull;

    inline uint32_t budgetHydraulicIterations(uint32_t requested, uint32_t cellCount)
    {
        if (cellCount == 0)
            return 1u;
        const uint64_t allowed = HYDRAULIC_CELL_STEP_BUDGET / static_cast<uint64_t>(cellCount);
        const uint32_t capped = static_cast<uint32_t>(std::min<uint64_t>(allowed, requested));
        return std::max(1u, capped);
    }

    // ---------------------------------------------------------------------------------------
    // Brush influence
    // ---------------------------------------------------------------------------------------

    // Placement of the brush relative to the region, in world units. Shared by the CPU twin and the
    // push-constant block so both evaluate influence identically.
    struct HydraulicBrushShape
    {
        glm::vec2 center{0.0f};
        float radius = 1.0f;
        BrushFalloff falloff = BrushFalloff::Smooth;
        BrushShape shape = BrushShape::Circle;
        float vertexSpacing = 1.0f;
    };

    // Mirrors the influence() helper in hydraulic_erosion.glsl. Returns 0 outside the brush.
    inline float hydraulicInfluence(const HydraulicBrushShape& brush, int32_t globalX, int32_t globalZ)
    {
        if (brush.radius <= 0.0f)
            return 0.0f;

        const glm::vec2 worldPos(static_cast<float>(globalX) * brush.vertexSpacing,
                                 static_cast<float>(globalZ) * brush.vertexSpacing);
        const glm::vec2 delta = worldPos - brush.center;

        const float dist = (brush.shape == BrushShape::Circle)
            ? std::sqrt(delta.x * delta.x + delta.y * delta.y) / brush.radius
            : std::max(std::abs(delta.x), std::abs(delta.y)) / brush.radius;

        if (dist >= 1.0f)
            return 0.0f;

        return std::clamp(applyFalloff(dist, brush.falloff), 0.0f, 1.0f);
    }

    // ---------------------------------------------------------------------------------------
    // Service <-> graphics boundary
    // ---------------------------------------------------------------------------------------

    // Everything the GPU pipeline needs, already resolved. The graphics layer stays completely
    // tile-ignorant: it receives a flat region and these scalars, never a TerrainTile or a grid.
    // Mirrors render::gpudriven::HydraulicErosionPushConstants, which is the struct that actually
    // crosses to the shader.
    struct HydraulicGPUParams
    {
        glm::vec2 regionOriginWorld{0.0f};
        glm::vec2 brushCenter{0.0f};
        uint32_t regionWidth = 0;
        uint32_t regionHeight = 0;
        BrushFalloff falloff = BrushFalloff::Smooth;
        BrushShape shape = BrushShape::Circle;
        float cellSize = 1.0f;
        float brushRadius = 0.0f;
        float dt = 0.0f;
        float rainAmount = 0.0f;
        float sedimentCapacity = 0.0f;
        float dissolveRate = 0.0f;
        float depositRate = 0.0f;
        float evaporation = 0.0f;
        float gravity = HYDRAULIC_GRAVITY;
        float minTiltSin = HYDRAULIC_MIN_TILT_SIN;
        float maxVelocity = 0.0f;
        float maxStepDelta = 0.0f;
        float minWater = HYDRAULIC_MIN_WATER;
        float smoothing = 0.0f;
        float talusThreshold = 0.0f;
        float strengthScale = 1.0f;
        float minHeight = 0.0f;
        float maxHeight = 0.0f;
        uint32_t iterations = 1;
        uint32_t thermalInterval = HYDRAULIC_THERMAL_INTERVAL;
    };

    inline HydraulicGPUParams makeHydraulicGPUParams(const HydraulicRegion& region,
                                                     const HydraulicParams& params,
                                                     const HydraulicBrushShape& brush,
                                                     float strengthScale,
                                                     float minHeight, float maxHeight)
    {
        HydraulicGPUParams gpu;
        gpu.regionOriginWorld = glm::vec2(static_cast<float>(region.originX) * brush.vertexSpacing,
                                          static_cast<float>(region.originZ) * brush.vertexSpacing);
        gpu.brushCenter = brush.center;
        gpu.regionWidth = region.width;
        gpu.regionHeight = region.height;
        gpu.falloff = brush.falloff;
        gpu.shape = brush.shape;
        gpu.cellSize = params.cellSize;
        gpu.brushRadius = brush.radius;
        gpu.dt = params.dt;
        gpu.rainAmount = params.rainRate * HYDRAULIC_RAIN_SCALE;
        gpu.sedimentCapacity = params.sedimentCapacity;
        gpu.dissolveRate = params.dissolveRate();
        gpu.depositRate = params.depositRate();
        gpu.evaporation = params.evaporation;
        gpu.maxVelocity = params.maxVelocity();
        gpu.maxStepDelta = HYDRAULIC_MAX_STEP_DELTA * params.cellSize;
        gpu.smoothing = params.smoothing;
        gpu.talusThreshold = std::tan(glm::radians(params.talusAngle)) * params.cellSize;
        gpu.strengthScale = strengthScale;
        gpu.minHeight = minHeight;
        gpu.maxHeight = maxHeight;
        gpu.iterations = budgetHydraulicIterations(params.iterations, region.cellCount());
        return gpu;
    }

    // ---------------------------------------------------------------------------------------
    // Simulation state
    // ---------------------------------------------------------------------------------------

    // The CPU mirror of the shader's SSBO set. The GPU side ping-pongs `terrain` and `sediment`
    // through a parity offset into a doubled buffer; here the same thing falls out of a swap and a
    // scratch vector, which is behaviourally identical and much easier to read.
    struct HydraulicState
    {
        std::vector<float> terrain;   // b, current
        std::vector<float> original;  // b0, pre-simulation, for the resolve blend
        std::vector<float> valid;     // 1.0 where a tile owns this cell, 0.0 = wall
        std::vector<float> water;     // d
        std::vector<float> sediment;  // s
        std::vector<glm::vec4> flux;  // fL, fR, fT, fB
        std::vector<glm::vec4> velocity; // u, w, sinAlpha, unused

        void resize(uint32_t cellCount)
        {
            terrain.assign(cellCount, 0.0f);
            original.assign(cellCount, 0.0f);
            valid.assign(cellCount, 0.0f);
            water.assign(cellCount, 0.0f);
            sediment.assign(cellCount, 0.0f);
            flux.assign(cellCount, glm::vec4(0.0f));
            velocity.assign(cellCount, glm::vec4(0.0f));
        }
    };

    namespace detail
    {
        inline bool cellValid(const HydraulicState& state, const HydraulicRegion& region,
                              int32_t x, int32_t z)
        {
            if (x < 0 || z < 0 || x >= static_cast<int32_t>(region.width) ||
                z >= static_cast<int32_t>(region.height))
                return false;
            return state.valid[region.index(static_cast<uint32_t>(x), static_cast<uint32_t>(z))] > 0.5f;
        }

        // Water surface (b + d) after this iteration's rain, for a neighbour. Rain is a closed-form
        // function of position, which is what lets the rain step fuse into the flux pass instead of
        // needing a pass and a barrier of its own.
        inline float surfaceWithRain(const HydraulicState& state, const HydraulicRegion& region,
                                     const HydraulicParams& params, const HydraulicBrushShape& brush,
                                     int32_t x, int32_t z)
        {
            const uint32_t idx = region.index(static_cast<uint32_t>(x), static_cast<uint32_t>(z));
            const float rain = params.rainRate * HYDRAULIC_RAIN_SCALE *
                hydraulicInfluence(brush, region.originX + x, region.originZ + z);
            return state.terrain[idx] + state.water[idx] + rain;
        }

        inline float rainAt(const HydraulicRegion& region, const HydraulicParams& params,
                            const HydraulicBrushShape& brush, int32_t x, int32_t z)
        {
            return params.rainRate * HYDRAULIC_RAIN_SCALE *
                hydraulicInfluence(brush, region.originX + x, region.originZ + z);
        }

        inline float sampleSediment(const HydraulicState& state, const HydraulicRegion& region,
                                    float x, float z)
        {
            // Outside the rect reads as zero rather than clamping to the edge: the boundary is
            // absorbing, and clamping would smear rim sediment inward as a visible band.
            if (x < 0.0f || z < 0.0f || x > static_cast<float>(region.width - 1) ||
                z > static_cast<float>(region.height - 1))
                return 0.0f;

            const uint32_t x0 = static_cast<uint32_t>(x);
            const uint32_t z0 = static_cast<uint32_t>(z);
            const uint32_t x1 = std::min(x0 + 1u, region.width - 1u);
            const uint32_t z1 = std::min(z0 + 1u, region.height - 1u);
            const float fx = x - static_cast<float>(x0);
            const float fz = z - static_cast<float>(z0);

            const float s00 = state.sediment[region.index(x0, z0)];
            const float s10 = state.sediment[region.index(x1, z0)];
            const float s01 = state.sediment[region.index(x0, z1)];
            const float s11 = state.sediment[region.index(x1, z1)];

            return (s00 * (1.0f - fx) + s10 * fx) * (1.0f - fz) +
                (s01 * (1.0f - fx) + s11 * fx) * fz;
        }
    }

    // Pass 0 - rain, outflow flux, and the K limiter that keeps water non-negative.
    inline void hydraulicPassFlux(HydraulicState& state, const HydraulicRegion& region,
                                  const HydraulicParams& params, const HydraulicBrushShape& brush)
    {
        const float cell = params.cellSize;
        const float cellArea = cell * cell;
        // A = cell^2 (pipe cross-section), l = cell (pipe length) => A*g/l collapses to cell*g.
        const float accel = params.dt * cell * HYDRAULIC_GRAVITY;

        for (int32_t z = 0; z < static_cast<int32_t>(region.height); ++z)
        {
            for (int32_t x = 0; x < static_cast<int32_t>(region.width); ++x)
            {
                const uint32_t idx = region.index(static_cast<uint32_t>(x), static_cast<uint32_t>(z));
                if (!detail::cellValid(state, region, x, z))
                {
                    state.flux[idx] = glm::vec4(0.0f);
                    continue;
                }

                const float rain = detail::rainAt(region, params, brush, x, z);
                const float d1 = state.water[idx] + rain;
                const float surface = state.terrain[idx] + d1;
                glm::vec4 f = state.flux[idx];

                const int32_t nx[4] = {x - 1, x + 1, x, x};
                const int32_t nz[4] = {z, z, z - 1, z + 1};
                for (int i = 0; i < 4; ++i)
                {
                    if (!detail::cellValid(state, region, nx[i], nz[i]))
                    {
                        // Two different non-cells, two different rules. A cell inside the rect that
                        // no tile owns (a hole in the grid) is a WALL: no outflow at all. The rect's
                        // own rim is ABSORBING: the neighbour's ground is taken as level with ours
                        // so dh collapses to d1 and the water simply runs off the edge. The paper's
                        // no-slip rim would instead pond water there and, since a wall has no
                        // slope, deposit a ring of sediment around the brush.
                        const bool outsideRect = nx[i] < 0 || nz[i] < 0 ||
                            nx[i] >= static_cast<int32_t>(region.width) ||
                            nz[i] >= static_cast<int32_t>(region.height);
                        f[i] = outsideRect ? std::max(0.0f, f[i] + accel * d1) : 0.0f;
                        continue;
                    }
                    const float dh = surface - detail::surfaceWithRain(state, region, params, brush,
                                                                       nx[i], nz[i]);
                    f[i] = std::max(0.0f, f[i] + accel * dh);
                }

                const float total = f.x + f.y + f.z + f.w;
                if (total > 0.0f)
                {
                    // The paper's K limiter. Without it a cell can send away more water than it
                    // holds, d goes negative, and the next division by dBar produces an inf that
                    // never washes out of the field.
                    const float k = std::min(1.0f, (d1 * cellArea) / (total * params.dt));
                    f *= k;
                }
                state.flux[idx] = f;
            }
        }
    }

    // Pass 1 - water depth from the net flux, then the velocity field and the cached tilt.
    //
    // sin(alpha) is computed HERE and not in the erosion pass, even though that is where it is
    // used. The tilt needs neighbouring heights, and the erosion pass writes heights; reading
    // neighbours of a buffer the same pass writes is a Jacobi/Gauss-Seidel race that would make the
    // GPU result non-deterministic and impossible to match against this twin. Nothing writes
    // terrain in this pass, so here it is exact.
    inline void hydraulicPassWater(HydraulicState& state, const HydraulicRegion& region,
                                   const HydraulicParams& params, const HydraulicBrushShape& brush)
    {
        const float cell = params.cellSize;
        const float cellArea = cell * cell;
        const float vMax = params.maxVelocity();

        for (int32_t z = 0; z < static_cast<int32_t>(region.height); ++z)
        {
            for (int32_t x = 0; x < static_cast<int32_t>(region.width); ++x)
            {
                const uint32_t idx = region.index(static_cast<uint32_t>(x), static_cast<uint32_t>(z));
                if (!detail::cellValid(state, region, x, z))
                {
                    state.water[idx] = 0.0f;
                    state.velocity[idx] = glm::vec4(0.0f);
                    continue;
                }

                const float d1 = state.water[idx] + detail::rainAt(region, params, brush, x, z);
                const glm::vec4 f = state.flux[idx];

                // Inflow: the left neighbour's flow to its right, etc.
                const float inLeft = detail::cellValid(state, region, x - 1, z)
                    ? state.flux[region.index(static_cast<uint32_t>(x - 1), static_cast<uint32_t>(z))].y
                    : 0.0f;
                const float inRight = detail::cellValid(state, region, x + 1, z)
                    ? state.flux[region.index(static_cast<uint32_t>(x + 1), static_cast<uint32_t>(z))].x
                    : 0.0f;
                const float inTop = detail::cellValid(state, region, x, z - 1)
                    ? state.flux[region.index(static_cast<uint32_t>(x), static_cast<uint32_t>(z - 1))].w
                    : 0.0f;
                const float inBottom = detail::cellValid(state, region, x, z + 1)
                    ? state.flux[region.index(static_cast<uint32_t>(x), static_cast<uint32_t>(z + 1))].z
                    : 0.0f;

                const float outTotal = f.x + f.y + f.z + f.w;
                const float deltaVolume = params.dt * (inLeft + inRight + inTop + inBottom - outTotal);
                const float d2 = std::max(0.0f, d1 + deltaVolume / cellArea);

                const float dBar = 0.5f * (d1 + d2);
                float u = 0.0f;
                float w = 0.0f;
                if (dBar > HYDRAULIC_MIN_WATER)
                {
                    const float deltaWX = 0.5f * (inLeft - f.x + f.y - inRight);
                    const float deltaWZ = 0.5f * (inTop - f.z + f.w - inBottom);
                    u = std::clamp(deltaWX / (cell * dBar), -vMax, vMax);
                    w = std::clamp(deltaWZ / (cell * dBar), -vMax, vMax);
                }

                // tan(alpha) = |grad b|, so sin(alpha) = |grad| / sqrt(1 + |grad|^2). Central
                // differences, falling back to a one-sided difference against a wall.
                const float hL = detail::cellValid(state, region, x - 1, z)
                    ? state.terrain[region.index(static_cast<uint32_t>(x - 1), static_cast<uint32_t>(z))]
                    : state.terrain[idx];
                const float hR = detail::cellValid(state, region, x + 1, z)
                    ? state.terrain[region.index(static_cast<uint32_t>(x + 1), static_cast<uint32_t>(z))]
                    : state.terrain[idx];
                const float hT = detail::cellValid(state, region, x, z - 1)
                    ? state.terrain[region.index(static_cast<uint32_t>(x), static_cast<uint32_t>(z - 1))]
                    : state.terrain[idx];
                const float hB = detail::cellValid(state, region, x, z + 1)
                    ? state.terrain[region.index(static_cast<uint32_t>(x), static_cast<uint32_t>(z + 1))]
                    : state.terrain[idx];

                const float gradX = (hR - hL) / (2.0f * cell);
                const float gradZ = (hB - hT) / (2.0f * cell);
                const float gradLen = std::sqrt(gradX * gradX + gradZ * gradZ);
                const float sinAlpha = std::max(HYDRAULIC_MIN_TILT_SIN,
                                                gradLen / std::sqrt(1.0f + gradLen * gradLen));

                state.water[idx] = d2;
                state.velocity[idx] = glm::vec4(u, w, sinAlpha, 0.0f);
            }
        }
    }

    // Pass 2 - erosion and deposition. Strictly cell-local, which is what pass 1's cached tilt buys.
    inline void hydraulicPassErosion(HydraulicState& state, const HydraulicRegion& region,
                                     const HydraulicParams& params)
    {
        const float maxDelta = HYDRAULIC_MAX_STEP_DELTA * params.cellSize;
        const float ks = params.dissolveRate();
        const float kd = params.depositRate();

        for (uint32_t idx = 0; idx < region.cellCount(); ++idx)
        {
            if (state.valid[idx] <= 0.5f)
                continue;

            const float d = state.water[idx];
            const float s = state.sediment[idx];

            if (d <= HYDRAULIC_MIN_WATER)
            {
                // Dried out: drop everything still in suspension. This is what deposits the fan at
                // the end of a run rather than leaving sediment stranded in the buffer.
                if (s > 0.0f)
                {
                    const float drop = std::min(s, maxDelta);
                    state.terrain[idx] += drop;
                    state.sediment[idx] = s - drop;
                }
                continue;
            }

            const glm::vec4 v = state.velocity[idx];
            const float speed = std::sqrt(v.x * v.x + v.y * v.y);
            const float capacity = params.sedimentCapacity * v.z * speed;

            float delta;
            if (capacity > s)
                delta = -ks * (capacity - s); // dissolve ground into the water
            else
                delta = kd * (s - capacity);  // settle sediment out of it

            delta = std::clamp(delta, -maxDelta, maxDelta);
            state.terrain[idx] += delta;
            state.sediment[idx] = std::max(0.0f, s - delta);
        }
    }

    // Pass 3 - move the suspended sediment along the flow, then evaporate.
    inline void hydraulicPassAdvect(HydraulicState& state, const HydraulicRegion& region,
                                    const HydraulicParams& params)
    {
        std::vector<float> advected(region.cellCount(), 0.0f);

        for (uint32_t z = 0; z < region.height; ++z)
        {
            for (uint32_t x = 0; x < region.width; ++x)
            {
                const uint32_t idx = region.index(x, z);
                if (state.valid[idx] <= 0.5f)
                    continue;

                const glm::vec4 v = state.velocity[idx];
                const float srcX = static_cast<float>(x) - v.x * params.dt / params.cellSize;
                const float srcZ = static_cast<float>(z) - v.y * params.dt / params.cellSize;
                advected[idx] = detail::sampleSediment(state, region, srcX, srcZ);
            }
        }

        const float keep = 1.0f - params.evaporation;
        for (uint32_t idx = 0; idx < region.cellCount(); ++idx)
        {
            if (state.valid[idx] <= 0.5f)
                continue;
            state.sediment[idx] = advected[idx];
            state.water[idx] *= keep;
        }
    }

    // Pass 4 - one talus relaxation sweep, the same rule case 6 of brush_compute.glsl applies.
    // Interleaved rather than run afterwards so the ridges never get a chance to sharpen.
    inline void hydraulicPassThermal(HydraulicState& state, const HydraulicRegion& region,
                                     const HydraulicParams& params)
    {
        if (params.smoothing <= 0.0f)
            return;

        const float talusThreshold = std::tan(glm::radians(params.talusAngle)) * params.cellSize;
        std::vector<float> relaxed = state.terrain;

        for (int32_t z = 0; z < static_cast<int32_t>(region.height); ++z)
        {
            for (int32_t x = 0; x < static_cast<int32_t>(region.width); ++x)
            {
                const uint32_t idx = region.index(static_cast<uint32_t>(x), static_cast<uint32_t>(z));
                if (state.valid[idx] <= 0.5f)
                    continue;

                const float current = state.terrain[idx];
                float targetSum = 0.0f;
                float violations = 0.0f;

                for (int32_t dz = -1; dz <= 1; ++dz)
                {
                    for (int32_t dx = -1; dx <= 1; ++dx)
                    {
                        if (dx == 0 && dz == 0)
                            continue;
                        if (!detail::cellValid(state, region, x + dx, z + dz))
                            continue;

                        const float neighbour = state.terrain[region.index(
                            static_cast<uint32_t>(x + dx), static_cast<uint32_t>(z + dz))];
                        if (current - neighbour > talusThreshold)
                        {
                            targetSum += neighbour + talusThreshold;
                            violations += 1.0f;
                        }
                    }
                }

                if (violations > 0.0f)
                {
                    const float target = std::min(current, targetSum / violations);
                    relaxed[idx] = current + (target - current) * params.smoothing;
                }
            }
        }

        state.terrain.swap(relaxed);
    }

    // Pass 5 - settle, then blend the simulated delta back through the brush falloff and clamp to
    // the tile's height range.
    //
    // The `+ sediment` is NOT optional. Erosion moves ground out of b and into s; whatever is still
    // in suspension when the loop ends was removed from the terrain and, if it is not returned
    // here, is destroyed. A dab fires every frame the mouse is held, so losing it means the terrain
    // quietly drains away under the cursor at ~60 dabs a second. Conservation is
    // `sum(b + s)` invariant, so settling s back into b is what closes the books.
    //
    // Blending the DELTA (rather than masking the rain alone) is what makes the change exactly zero
    // outside the radius no matter what the sim did out in the padded margin: at dist >= 1 the
    // weight is 0 and `base + delta*0 == base` bit-exactly.
    inline void hydraulicPassResolve(HydraulicState& state, const HydraulicRegion& region,
                                     const HydraulicBrushShape& brush, float strengthScale,
                                     float minHeight, float maxHeight)
    {
        for (uint32_t z = 0; z < region.height; ++z)
        {
            for (uint32_t x = 0; x < region.width; ++x)
            {
                const uint32_t idx = region.index(x, z);
                const float base = state.original[idx];
                if (state.valid[idx] <= 0.5f)
                {
                    state.terrain[idx] = base;
                    continue;
                }

                const float settled = state.terrain[idx] + state.sediment[idx];
                const float weight = hydraulicInfluence(brush, region.originX + static_cast<int32_t>(x),
                                                        region.originZ + static_cast<int32_t>(z)) *
                    strengthScale;
                const float blended = base + (settled - base) * weight;
                state.terrain[idx] = std::clamp(blended, minHeight, maxHeight);
            }
        }
    }

    // Full dab: `iterations` sim steps followed by one resolve. `state.terrain`, `state.original`
    // and `state.valid` must already be filled by the caller's gather.
    inline void simulateHydraulicErosion(HydraulicState& state, const HydraulicRegion& region,
                                         const HydraulicParams& params, const HydraulicBrushShape& brush,
                                         float strengthScale, float minHeight, float maxHeight)
    {
        if (!region.valid() || state.terrain.size() != region.cellCount())
            return;

        for (uint32_t i = 0; i < params.iterations; ++i)
        {
            hydraulicPassFlux(state, region, params, brush);
            hydraulicPassWater(state, region, params, brush);
            hydraulicPassErosion(state, region, params);
            hydraulicPassAdvect(state, region, params);
            if (params.smoothing > 0.0f && ((i + 1) % HYDRAULIC_THERMAL_INTERVAL) == 0)
                hydraulicPassThermal(state, region, params);
        }

        hydraulicPassResolve(state, region, brush, strengthScale, minHeight, maxHeight);
    }

    // The per-dab scale, matching how every other accumulating sculpt brush turns strength into a
    // step (brush_compute.glsl smooth/flatten/erosion/terrace all use this exact expression).
    //
    // deltaTime enters HERE, on the final delta, rather than on the rain. Scaling the rain instead
    // would make the simulation itself frame-rate dependent - a slow frame would flood the region
    // and change the character of the channels, not just the dose. Scaling the resolved delta keeps
    // the sim identical every frame for a given rect and makes only the amount applied per frame
    // proportional to elapsed time, which is what the other brushes do.
    inline float hydraulicStrengthScale(float brushStrength, float deltaTime)
    {
        return std::clamp(brushStrength * deltaTime, 0.0f, 1.0f);
    }

}
