#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace water
{
    // VK-1605: the water-depth signal the ocean never had.
    //
    // A camera-following window of waterDepth = waterHeight - terrainHeight, baked on the CPU and
    // uploaded to an R32F texture the water vertex shader samples. Everything here is deliberately
    // free of engine dependencies (no services/, no Vulkan) so the bake, the sampling and the
    // rebake policy are unit-testable without a GPU or a running scene.
    //
    // The field is the single source of truth for both the shader and CPU buoyancy: the renderer
    // keeps this exact object and feeds it to getOceanHeightAt, so physics and pixels agree.

    inline constexpr uint32_t SHORE_FIELD_RESOLUTION = 256;
    inline constexpr float SHORE_FIELD_WINDOW = 1024.0f;

    // "No bottom anywhere near here". Any depth this large makes every shoaling factor exactly 1
    // (a band stops feeling the bottom at depth = wavelength/2, and no wavelength approaches this),
    // which is what keeps deep-ocean and terrain-less scenes byte-identical.
    inline constexpr float SHORE_FIELD_DEEP = 1.0e4f;

    inline constexpr uint32_t SHORE_FIELD_ROWS_PER_TICK = 32;

    // Rebake once the camera has drifted this fraction of the window away from the field centre.
    // A QUARTER, not a half: at half the camera sits exactly on the window border, where
    // clamp-to-edge sampling would be extrapolating the shoreline in front of it. A quarter keeps
    // >= 256 m of real data in every direction at all times.
    inline constexpr float SHORE_FIELD_REBAKE_FRACTION = 0.25f;

    // ------------------------------------------------------------------------------------------
    // Non-owning view over an events::terrain::TerrainHeightfieldResult snapshot.
    //
    // Utilities sits below Services in the dependency graph and must not include its headers, so
    // this takes the snapshot's fields raw. The sampling is a deliberate mirror of
    // TerrainService::getTerrainHeightAt (services/impl/scene/TerrainService.cpp:519-597): locate
    // the tile by floor(world / tileWorldSize), clamp into the tile, bilinear over the four
    // surrounding vertices. Keep the two in step - a divergence would put the shoreline in a
    // different place than the terrain the player walks on.
    // ------------------------------------------------------------------------------------------
    struct TerrainHeightGrid
    {
        float worldOriginX = 0.0f;      // world X of the min tile's origin
        float worldOriginZ = 0.0f;
        float tileWorldSize = 32.0f;
        float vertexSpacing = 1.0f;
        int32_t gridCountX = 0;
        int32_t gridCountZ = 0;
        uint32_t verticesPerTile = 33;  // per side, = quadCount + 1

        const float* heights = nullptr; // packed tile-by-tile, row-major within a tile (z*vpt + x)
        std::size_t heightCount = 0;

        // Optional per-tile "this tile actually has height data" flags, gridCountX*gridCountZ
        // entries. Without it an un-streamed tile reads as height 0 and would be mistaken for a
        // sea-level beach, killing the waves over it.
        const uint8_t* tileValid = nullptr;
        std::size_t tileValidCount = 0;

        [[nodiscard]] bool isValid() const
        {
            return heights != nullptr && heightCount > 0 && gridCountX > 0 && gridCountZ > 0 &&
                   verticesPerTile >= 2 && tileWorldSize > 0.0f && vertexSpacing > 0.0f;
        }

        // Returns false when there is no terrain at (worldX, worldZ) - outside the grid, or on a
        // tile with no height data. Callers treat that as "open water, no bottom".
        [[nodiscard]] bool sample(float worldX, float worldZ, float& outHeight) const;
    };

    // Bilinear, clamp-to-edge lookup into a raw field buffer - the CPU twin of shoreDepthAt() in
    // resources/shaders/water/water_shoaling.glsl. Free-standing because the renderer keeps only a
    // flat copy of the data (for the GPU upload) and has to sample it for buoyancy without owning a
    // whole ShoreDepthField; ShoreDepthField::sample() forwards here so there is one implementation.
    [[nodiscard]] float sampleShoreDepth(const std::vector<float>& data, uint32_t resolution,
                                         const glm::vec2& origin, float windowSize,
                                         const glm::vec2& worldXZ);

    // ------------------------------------------------------------------------------------------
    // ShoreDepthField
    // ------------------------------------------------------------------------------------------
    class ShoreDepthField
    {
    public:
        // Returns false when there is no terrain at that position (-> treated as SHORE_FIELD_DEEP).
        using HeightSampler = std::function<bool(float worldX, float worldZ, float& outHeight)>;

        ShoreDepthField();

        // Resolution/window are fixed for the lifetime of the GPU texture, so this is only for
        // tests. Resets the field to "deep everywhere".
        void configure(uint32_t resolution, float windowSize);

        [[nodiscard]] bool needsRebake(const glm::vec2& cameraXZ) const;

        // Starts a bake centred on cameraXZ. Fills the BACK buffer; the front buffer (what sample()
        // reads and what the GPU holds) keeps the previous contents until the bake completes, so a
        // sample can never mix old and new rows. Restarting an in-flight bake is allowed and simply
        // discards the partial work.
        void beginRebake(const glm::vec2& cameraXZ, float waterHeight, HeightSampler sampler);

        // Bakes up to rowCount rows. Returns true on the tick that COMPLETES the bake, which is
        // also when the buffers swap, the origin is committed and version() increments.
        bool bakeRows(uint32_t rowCount);

        // Abandon an in-flight bake without committing it. The front buffer, its origin and
        // version() are untouched - only the pending work is dropped.
        //
        // A bake spans SHORE_FIELD_RESOLUTION / SHORE_FIELD_ROWS_PER_TICK ticks, and its sampler
        // holds a reference to the caller's terrain snapshot. If the owner stops ticking mid-bake
        // (scene cleared, the last water in the scene deleted), `baking` would otherwise stay true
        // forever: the snapshot stays pinned, the !isBaking() guard blocks every future rebake, and
        // the next tick that does arrive finishes and commits the PREVIOUS scene's bathymetry.
        void cancelBake();

        // Bilinear, clamp-to-edge. Always safe to call - returns SHORE_FIELD_DEEP before any bake.
        [[nodiscard]] float sample(const glm::vec2& worldXZ) const;

        // Central difference of sample(); points OFFSHORE (depth increases seaward).
        [[nodiscard]] glm::vec2 gradient(const glm::vec2& worldXZ, float eps) const;

        // 1 well inside the window, falling to 0 at the border. Multiplying every shoreline effect
        // by this is what makes re-centring invisible: at the border the field contributes nothing,
        // so whatever the next bake puts there cannot pop.
        [[nodiscard]] float edgeFade(const glm::vec2& worldXZ, float fadeStart) const;

        [[nodiscard]] uint32_t version() const { return fieldVersion; }
        [[nodiscard]] uint32_t resolution() const { return fieldResolution; }
        [[nodiscard]] float windowSize() const { return fieldWindowSize; }
        [[nodiscard]] glm::vec2 origin() const { return fieldOrigin; }   // window min corner
        [[nodiscard]] glm::vec2 center() const;
        [[nodiscard]] const std::vector<float>& data() const { return front; }

        [[nodiscard]] bool hasBakedOnce() const { return fieldVersion > 0; }
        [[nodiscard]] bool isBaking() const { return baking; }
        [[nodiscard]] float bakeProgress() const;

    private:
        uint32_t fieldResolution = SHORE_FIELD_RESOLUTION;
        float fieldWindowSize = SHORE_FIELD_WINDOW;

        std::vector<float> front;
        std::vector<float> back;

        glm::vec2 fieldOrigin{0.0f};        // committed (front) window min corner
        glm::vec2 pendingOrigin{0.0f};      // window min corner the in-flight bake is filling
        float pendingWaterHeight = 0.0f;
        HeightSampler pendingSampler;

        uint32_t nextRow = 0;
        bool baking = false;
        uint32_t fieldVersion = 0;
    };
}
