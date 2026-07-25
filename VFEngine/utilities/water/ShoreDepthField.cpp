#include "ShoreDepthField.hpp"
#include "ShoalingMath.hpp"

#include <algorithm>
#include <cmath>

namespace water
{
    // ------------------------------------------------------------------------------------------
    // TerrainHeightGrid
    // ------------------------------------------------------------------------------------------

    bool TerrainHeightGrid::sample(float worldX, float worldZ, float& outHeight) const
    {
        if (!isValid())
            return false;

        // worldOriginX is the min tile's origin, so this recovers its integer tile coordinate.
        const int32_t minTileX = static_cast<int32_t>(std::lround(worldOriginX / tileWorldSize));
        const int32_t minTileZ = static_cast<int32_t>(std::lround(worldOriginZ / tileWorldSize));

        const int32_t tileX = static_cast<int32_t>(std::floor(worldX / tileWorldSize));
        const int32_t tileZ = static_cast<int32_t>(std::floor(worldZ / tileWorldSize));

        const int32_t offX = tileX - minTileX;
        const int32_t offZ = tileZ - minTileZ;
        if (offX < 0 || offX >= gridCountX || offZ < 0 || offZ >= gridCountZ)
            return false;

        const std::size_t tileIndex =
            static_cast<std::size_t>(offZ) * static_cast<std::size_t>(gridCountX) +
            static_cast<std::size_t>(offX);

        if (tileValid != nullptr)
        {
            if (tileIndex >= tileValidCount || tileValid[tileIndex] == 0)
                return false;
        }

        const std::size_t vpt = static_cast<std::size_t>(verticesPerTile);
        const std::size_t base = tileIndex * vpt * vpt;
        if (base + vpt * vpt > heightCount)
            return false;

        const uint32_t quadCount = verticesPerTile - 1;

        // Local position within the tile, then fractional grid coordinates - identical to
        // TerrainService::getTerrainHeightAt.
        const float localX = worldX - static_cast<float>(tileX) * tileWorldSize;
        const float localZ = worldZ - static_cast<float>(tileZ) * tileWorldSize;

        const float maxCoord = static_cast<float>(quadCount);
        const float gx = std::clamp(localX / vertexSpacing, 0.0f, maxCoord);
        const float gz = std::clamp(localZ / vertexSpacing, 0.0f, maxCoord);

        uint32_t ix = static_cast<uint32_t>(gx);
        uint32_t iz = static_cast<uint32_t>(gz);
        ix = std::min(ix, quadCount - 1);
        iz = std::min(iz, quadCount - 1);

        const float fx = gx - static_cast<float>(ix);
        const float fz = gz - static_cast<float>(iz);

        const float* tile = heights + base;
        auto at = [&](uint32_t x, uint32_t z) { return tile[static_cast<std::size_t>(z) * vpt + x]; };

        const float h00 = at(ix, iz);
        const float h10 = at(ix + 1, iz);
        const float h01 = at(ix, iz + 1);
        const float h11 = at(ix + 1, iz + 1);

        outHeight = h00 * (1.0f - fx) * (1.0f - fz)
                  + h10 * fx * (1.0f - fz)
                  + h01 * (1.0f - fx) * fz
                  + h11 * fx * fz;
        return true;
    }

    // ------------------------------------------------------------------------------------------
    // ShoreDepthField
    // ------------------------------------------------------------------------------------------

    ShoreDepthField::ShoreDepthField()
    {
        configure(SHORE_FIELD_RESOLUTION, SHORE_FIELD_WINDOW);
    }

    void ShoreDepthField::configure(uint32_t resolution, float windowSize)
    {
        fieldResolution = std::max(resolution, 2u);
        fieldWindowSize = std::max(windowSize, 1.0f);

        const std::size_t texels =
            static_cast<std::size_t>(fieldResolution) * static_cast<std::size_t>(fieldResolution);
        front.assign(texels, SHORE_FIELD_DEEP);
        back.assign(texels, SHORE_FIELD_DEEP);

        fieldOrigin = glm::vec2(0.0f);
        pendingOrigin = glm::vec2(0.0f);
        pendingSampler = nullptr;
        nextRow = 0;
        baking = false;
        fieldVersion = 0;
    }

    glm::vec2 ShoreDepthField::center() const
    {
        return fieldOrigin + glm::vec2(fieldWindowSize * 0.5f);
    }

    bool ShoreDepthField::needsRebake(const glm::vec2& cameraXZ) const
    {
        if (!hasBakedOnce())
            return true;

        // Chebyshev distance, because the window is a square: what matters is the smallest margin
        // in any axis, not the diagonal.
        const glm::vec2 delta = glm::abs(cameraXZ - center());
        return std::max(delta.x, delta.y) > SHORE_FIELD_REBAKE_FRACTION * fieldWindowSize;
    }

    void ShoreDepthField::beginRebake(const glm::vec2& cameraXZ, float waterHeight,
                                      HeightSampler sampler)
    {
        pendingOrigin = cameraXZ - glm::vec2(fieldWindowSize * 0.5f);
        pendingWaterHeight = waterHeight;
        pendingSampler = std::move(sampler);

        const std::size_t texels =
            static_cast<std::size_t>(fieldResolution) * static_cast<std::size_t>(fieldResolution);
        back.assign(texels, SHORE_FIELD_DEEP);

        nextRow = 0;
        baking = true;
    }

    bool ShoreDepthField::bakeRows(uint32_t rowCount)
    {
        if (!baking)
            return false;

        const float texelSize = fieldWindowSize / static_cast<float>(fieldResolution);
        const uint32_t lastRow = std::min(nextRow + rowCount, fieldResolution);

        for (; nextRow < lastRow; ++nextRow)
        {
            // Texel centres, matching how the GPU samples the texture (uv = (world-origin)/window
            // with linear filtering puts texel i's centre at (i+0.5)/resolution).
            const float worldZ = pendingOrigin.y + (static_cast<float>(nextRow) + 0.5f) * texelSize;
            float* row = back.data() + static_cast<std::size_t>(nextRow) * fieldResolution;

            for (uint32_t x = 0; x < fieldResolution; ++x)
            {
                const float worldX = pendingOrigin.x + (static_cast<float>(x) + 0.5f) * texelSize;

                float terrainHeight = 0.0f;
                if (pendingSampler && pendingSampler(worldX, worldZ, terrainHeight))
                    row[x] = pendingWaterHeight - terrainHeight;
                else
                    row[x] = SHORE_FIELD_DEEP;   // no terrain here: open water, no bottom
            }
        }

        if (nextRow < fieldResolution)
            return false;

        // Commit: swap in one step so no sample ever sees a half-rebaked window, and drop the
        // sampler so the caller's terrain snapshot can be released.
        front.swap(back);
        fieldOrigin = pendingOrigin;
        pendingSampler = nullptr;
        baking = false;
        ++fieldVersion;
        return true;
    }

    float sampleShoreDepth(const std::vector<float>& data, uint32_t resolution,
                           const glm::vec2& origin, float windowSize, const glm::vec2& worldXZ)
    {
        if (data.empty() || resolution < 2 ||
            data.size() < static_cast<std::size_t>(resolution) * resolution || windowSize <= 0.0f)
            return SHORE_FIELD_DEEP;

        const float res = static_cast<float>(resolution);
        const glm::vec2 uv = (worldXZ - origin) / windowSize;

        const float fx = uv.x * res - 0.5f;
        const float fy = uv.y * res - 0.5f;

        const float flx = std::floor(fx);
        const float fly = std::floor(fy);
        const float fracX = fx - flx;
        const float fracY = fy - fly;

        const int32_t maxIndex = static_cast<int32_t>(resolution) - 1;
        auto clampIndex = [maxIndex](float v)
        {
            // floor() first: static_cast truncates toward zero, which would fold -0.4 onto texel 0
            // instead of clamping it there from the correct side.
            const int32_t i = static_cast<int32_t>(std::floor(v));
            return static_cast<std::size_t>(std::clamp(i, 0, maxIndex));
        };

        // Clamp-to-edge, exactly like the sampler on the GPU side.
        const std::size_t x0 = clampIndex(flx);
        const std::size_t x1 = clampIndex(flx + 1.0f);
        const std::size_t y0 = clampIndex(fly);
        const std::size_t y1 = clampIndex(fly + 1.0f);

        const std::size_t stride = resolution;
        const float v00 = data[y0 * stride + x0];
        const float v10 = data[y0 * stride + x1];
        const float v01 = data[y1 * stride + x0];
        const float v11 = data[y1 * stride + x1];

        const float v0 = v00 + fracX * (v10 - v00);
        const float v1 = v01 + fracX * (v11 - v01);
        return v0 + fracY * (v1 - v0);
    }

    float ShoreDepthField::sample(const glm::vec2& worldXZ) const
    {
        return sampleShoreDepth(front, fieldResolution, fieldOrigin, fieldWindowSize, worldXZ);
    }

    glm::vec2 ShoreDepthField::gradient(const glm::vec2& worldXZ, float eps) const
    {
        const float e = std::max(eps, 1e-3f);
        const float dx = sample(worldXZ + glm::vec2(e, 0.0f)) - sample(worldXZ - glm::vec2(e, 0.0f));
        const float dz = sample(worldXZ + glm::vec2(0.0f, e)) - sample(worldXZ - glm::vec2(0.0f, e));
        return glm::vec2(dx, dz) / (2.0f * e);
    }

    float ShoreDepthField::edgeFade(const glm::vec2& worldXZ, float fadeStart) const
    {
        // One implementation, shared with the shader twin in ShoalingMath.hpp.
        return shoreWindowFade(worldXZ, fieldOrigin, fieldWindowSize, fadeStart);
    }

    float ShoreDepthField::bakeProgress() const
    {
        if (baking)
            return static_cast<float>(nextRow) / static_cast<float>(fieldResolution);
        return hasBakedOnce() ? 1.0f : 0.0f;
    }
}
