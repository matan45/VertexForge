#pragma once

#include "WaterTileGrid.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace water
{
    // VK-1607: bounded water bodies (lakes, pools) that sit at their own level alongside the single
    // infinite ocean. CPU twin of the tile decode and the body-clip discard in
    // resources/shaders/water/water.glsl - keep both in sync. The shader clips ocean PIXELS against
    // the same rectangles that findBodyAt/resolveSurfaceHeight use to answer "what is the water
    // height here", so any drift between them shows up as a boat floating on water that is not
    // being drawn (or vice versa).
    //
    // A body is an axis-aligned box in XZ. That is deliberate: containment is two comparisons, the
    // tile that draws it is exactly its rectangle (no fragment clip needed for the body itself),
    // and the whole thing is unit-testable without a GPU.

    // How many body rectangles the fragment stage can clip the ocean against in one frame. Matches
    // the bodyClipRects[] array in WaterExtendedParams / water_params.glsl.
    inline constexpr uint32_t MAX_WATER_BODY_CLIP_RECTS = 8u;

    // The per-tile flag bits (WATER_TILE_BAND_MASK_BITS / WATER_TILE_IS_BODY) live next to the struct
    // they are packed into, in WaterTileGrid.hpp.

    // Flat POD description of one water body, resolved from WaterBodyComponent + TransformComponent.
    // Crosses the services -> provider -> renderer boundary by value (same shape of contract as
    // water::WaterImpulse).
    struct WaterBodyDesc
    {
        glm::vec2 center{0.0f, 0.0f};        // world XZ centre
        glm::vec2 halfExtents{0.0f, 0.0f};   // world metres, entity scale deliberately ignored
        float surfaceHeight = 0.0f;          // absolute world Y of the surface
        uint32_t bandMask = 0u;              // 0 = flat; bits 0..2 = swell / agitation / ripples
        uint32_t physicsEnabled = 1u;
    };

    // Inclusive on all four edges. A point exactly on the border belongs to the body: the shader's
    // clip test is inclusive too (greaterThanEqual / lessThanEqual), so a boat at the rim floats on
    // the same surface that gets drawn there.
    [[nodiscard]] inline bool containsXZ(const WaterBodyDesc& body, const glm::vec2& worldXZ)
    {
        const float dx = std::abs(worldXZ.x - body.center.x);
        const float dz = std::abs(worldXZ.y - body.center.y);
        return dx <= body.halfExtents.x && dz <= body.halfExtents.y;
    }

    // xy = min corner XZ, zw = max corner XZ. Negative half extents would invert the rectangle and
    // make the shader's range test never fire, so they are clamped here rather than in every caller.
    [[nodiscard]] inline glm::vec4 clipRect(const WaterBodyDesc& body)
    {
        const float hx = std::max(body.halfExtents.x, 0.0f);
        const float hz = std::max(body.halfExtents.y, 0.0f);
        return {body.center.x - hx, body.center.y - hz, body.center.x + hx, body.center.y + hz};
    }

    [[nodiscard]] inline float bodyArea(const WaterBodyDesc& body)
    {
        return 4.0f * std::max(body.halfExtents.x, 0.0f) * std::max(body.halfExtents.y, 0.0f);
    }

    // Index of the body owning this point, or -1. Overlap precedence, in order:
    //   1. highest surface wins   - higher water physically covers lower water
    //   2. smaller area wins      - a pool carved into a lake at the same level
    //   3. lower index wins       - the final tie-break, so the answer never depends on the order
    //                              entt happened to hand the components back
    // The third rule is what makes this deterministic: without it a registry reshuffle would silently
    // change which surface a boat floats on.
    [[nodiscard]] inline int findBodyAt(const WaterBodyDesc* bodies, size_t count,
                                        const glm::vec2& worldXZ)
    {
        int best = -1;
        for (size_t i = 0; i < count; ++i)
        {
            if (!containsXZ(bodies[i], worldXZ))
                continue;

            if (best < 0)
            {
                best = static_cast<int>(i);
                continue;
            }

            const WaterBodyDesc& b = bodies[static_cast<size_t>(best)];
            const WaterBodyDesc& c = bodies[i];

            if (c.surfaceHeight > b.surfaceHeight)
                best = static_cast<int>(i);
            else if (c.surfaceHeight == b.surfaceHeight && bodyArea(c) < bodyArea(b))
                best = static_cast<int>(i);
        }
        return best;
    }

    // The single body-vs-ocean resolution rule. Bodies are checked FIRST; the ocean is the fallback.
    // outFound is false only when there is no water at all here, which is what lets buoyancy and the
    // underwater post-process distinguish "sea level 0" from "no water".
    [[nodiscard]] inline float resolveSurfaceHeight(const WaterBodyDesc* bodies, size_t count,
                                                    const glm::vec2& worldXZ, float oceanHeight,
                                                    bool oceanActive, bool& outFound)
    {
        const int idx = findBodyAt(bodies, count, worldXZ);
        if (idx >= 0)
        {
            outFound = true;
            return bodies[static_cast<size_t>(idx)].surfaceHeight;
        }

        outFound = oceanActive;
        return oceanActive ? oceanHeight : 0.0f;
    }

    // One tile covering exactly the body's rectangle. Non-square tiles are why bodies need no
    // fragment clip of their own: worldOriginAndSize.w is the size along X and .y (an unused origin
    // Y before this story) is the size along Z.
    //
    // LOD 0 always: the LOD field only selects which subdivided quad mesh is instanced, and bodies
    // are small enough that the finest one is the right call - a flat body pays nothing for it.
    [[nodiscard]] inline WaterTileGPUData makeBodyTile(const WaterBodyDesc& body)
    {
        const float hx = std::max(body.halfExtents.x, 0.0f);
        const float hz = std::max(body.halfExtents.y, 0.0f);

        const uint32_t flags = (body.bandMask & WATER_TILE_BAND_MASK_BITS) | WATER_TILE_IS_BODY;

        WaterTileGPUData tile;
        tile.worldOriginAndSize = glm::vec4(body.center.x - hx, 2.0f * hz,
                                            body.center.y - hz, 2.0f * hx);
        tile.heightAndWave = glm::vec4(body.surfaceHeight, 1.0f, 0.0f, static_cast<float>(flags));
        return tile;
    }

    // Trim the per-LOD tile counts so their sum fits the GPU instance budget, dropping the COARSEST
    // LODs first (they are the farthest tiles). Returns the surviving total, which is also the number
    // of leading entries of the LOD-sorted tile array that stay valid.
    //
    // This has to exist because WaterMeshBuffer::updateTileData clamps only the memcpy: leaving
    // lodTileCounts untrimmed makes renderMultiLOD issue instances whose firstInstance runs past the
    // end of the SSBO. Body tiles live at the head of LOD 0, so they are never the ones trimmed.
    inline uint32_t clampLodTileCounts(uint32_t lodCounts[WATER_TILE_LOD_COUNT], uint32_t maxTiles)
    {
        uint32_t total = 0;
        for (uint32_t lod = 0; lod < WATER_TILE_LOD_COUNT; ++lod)
            total += lodCounts[lod];

        if (total <= maxTiles)
            return total;

        uint32_t excess = total - maxTiles;
        for (uint32_t i = WATER_TILE_LOD_COUNT; i-- > 0 && excess > 0;)
        {
            const uint32_t drop = std::min(excess, lodCounts[i]);
            lodCounts[i] -= drop;
            excess -= drop;
        }

        return maxTiles;
    }
}
